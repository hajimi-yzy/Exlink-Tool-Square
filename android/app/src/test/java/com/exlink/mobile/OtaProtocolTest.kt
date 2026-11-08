package com.exlink.mobile

import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

class OtaProtocolTest {
    private val data = OtaExpectation(42, 1, 1000, 480, 960, 1, 1)

    private fun status(state: Int = 1, error: Int = 0, kind: Int = 1,
        session: Int = 42, accepted: Int = 960, total: Int = 1000) =
        OtaStatus(state, error, kind, session, accepted, total)

    private fun rejected(block: () -> Unit) {
        try { block(); fail("Unsafe status must be rejected") }
        catch (_: OtaRejectedException) { }
    }

    @Test fun lostNotificationIsResolvedByPersistedOffset() {
        assertTrue(data.recover(status()))
        assertFalse(data.recover(status(accepted = 480)))
    }

    @Test fun recoveryRejectsAnotherSessionKindOrTotal() {
        rejected { data.recover(status(session = 43)) }
        rejected { data.recover(status(kind = 2)) }
        rejected { data.recover(status(total = 1001)) }
    }

    @Test fun recoveryRejectsPartialChunkAndSpeculativeAdvance() {
        rejected { data.recover(status(accepted = 720)) }
        rejected { data.recover(status(accepted = 1000)) }
        rejected { data.recover(status(accepted = 0)) }
    }

    @Test fun lastNonfatalErrorDoesNotDiscardPersistedProgress() {
        // Firmware retains the previous command's result on reconnect. A WrongOffset
        // result must not cause a second flash write for already persisted DATA.
        assertTrue(data.recover(status(error = 5)))
        assertFalse(data.confirmed(status(error = 5)))
        rejected { data.recover(status(state = 3, error = 7)) }
        rejected { data.recover(status(error = 8)) }
    }

    @Test fun endNeedsReadyAndActivationNeedsIdleFromTheSameSession() {
        val end = OtaExpectation(42, 1, 1000, 1000, 1000, 1, 2)
        assertFalse(end.recover(status(accepted = 1000)))
        assertTrue(end.recover(status(state = 2, accepted = 1000)))
        val activation = end.copy(beforeState = 2, afterState = 0)
        assertFalse(activation.confirmed(status(state = 2, accepted = 1000)))
        assertTrue(activation.confirmed(status(state = 0, accepted = 1000)))
        assertFalse(activation.confirmed(status(state = 0, session = 0, accepted = 0, total = 0)))
    }

    @Test fun parserRejectsTruncatedUnsupportedAndImpossibleStatus() {
        fun frame(version: Byte = 1, accepted: Int = 960) = ByteBuffer.allocate(16)
            .order(ByteOrder.LITTLE_ENDIAN).put(version).put(1).put(0).put(1)
            .putInt(42).putInt(accepted).putInt(1000).array()
        assertEquals(status(), OtaStatus.parse(frame()))
        assertNull(OtaStatus.parse(frame().copyOf(15)))
        assertNull(OtaStatus.parse(frame(version = 2)))
        assertNull(OtaStatus.parse(frame(accepted = 1001)))
        assertNull(OtaStatus.parse(frame(accepted = -1)))
    }

    @Test fun signedSessionRemainsValidOnWire() {
        val session = Int.MIN_VALUE
        val frame = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN)
            .put(1).put(1).put(0).put(1).putInt(session).putInt(480).putInt(1000).array()
        assertEquals(session, OtaStatus.parse(frame)?.session)
    }

    @Test fun callbackMustMatchConnectionOperationAndCharacteristic() = runBlocking {
        val gate = GattOperationGate<Int>()
        val current = gate.begin(2, "READ:telemetry")
        assertFalse(gate.complete(1, "READ:telemetry", 111))
        assertFalse(gate.complete(2, "WRITE:telemetry", 222))
        assertFalse(gate.complete(2, "READ:hardware", 333))
        assertFalse(current.result.isCompleted)
        assertTrue(gate.complete(2, "READ:telemetry", 0))
        assertEquals(0, current.result.await())
        assertFalse(gate.complete(2, "READ:telemetry", 444))
        gate.finish(current)
    }

    @Test fun disconnectImmediatelyFailsPendingReadAndOldCallbackCannotFinishReconnect() = runBlocking {
        val gate = GattOperationGate<Int>()
        val old = gate.begin(1, "READ:status")
        val waiter = async {
            try { old.result.await(); false } catch (_: IOException) { true }
        }
        gate.fail(1, IOException("disconnected"))
        assertTrue(waiter.await())
        val fresh = gate.begin(2, "READ:status")
        gate.finish(old)
        assertFalse(gate.complete(1, "READ:status", 1))
        assertFalse(fresh.result.isCompleted)
        assertTrue(gate.complete(2, "READ:status", 0))
        assertEquals(0, fresh.result.await())
    }

    @Test fun parallelNativeOperationsAreRejected() {
        val gate = GattOperationGate<Int>()
        val read = gate.begin(1, "READ:status")
        try { gate.begin(1, "WRITE:data"); fail("Only one native request may be outstanding") }
        catch (_: IllegalStateException) { }
        gate.finish(read)
        assertNotNull(gate.begin(1, "WRITE:data"))
    }

    @Test fun reconnectBudgetIsFinite() {
        val budget = OtaRetryBudget(2)
        assertTrue(budget.take())
        assertTrue(budget.take())
        repeat(10) { assertFalse(budget.take()) }
        assertEquals(2, budget.used)
    }
}
