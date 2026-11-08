package com.exlink.mobile

import kotlinx.coroutines.CompletableDeferred
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** One native GATT request may be outstanding. A timed-out request retires its connection. */
internal class GattOperationGate<T> {
    internal class Pending<T>(val generation: Long, val key: String) {
        val result = CompletableDeferred<T>()
    }
    private var pending: Pending<T>? = null

    @Synchronized fun begin(generation: Long, key: String): Pending<T> {
        check(pending == null) { "已有蓝牙操作正在进行" }
        return Pending<T>(generation, key).also { pending = it }
    }

    @Synchronized fun complete(generation: Long, key: String, value: T): Boolean {
        val request = pending ?: return false
        if (request.generation != generation || request.key != key) return false
        return request.result.complete(value)
    }

    @Synchronized fun finish(request: Pending<T>) {
        if (pending === request) pending = null
    }

    @Synchronized fun fail(generation: Long, cause: Throwable) {
        val request = pending ?: return
        if (request.generation != generation) return
        pending = null
        request.result.completeExceptionally(cause)
    }
}

internal class OtaRejectedException(message: String) : IOException(message)

internal data class OtaStatus(
    val state: Int, val error: Int, val kind: Int,
    val session: Int, val accepted: Int, val total: Int
) {
    fun belongsTo(session: Int, kind: Int, total: Int) =
        this.session == session && this.kind == kind && this.total == total

    companion object {
        fun parse(bytes: ByteArray): OtaStatus? {
            if (bytes.size != 16 || bytes[0].toInt() != 1) return null
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            val value = OtaStatus(bytes[1].toInt() and 255, bytes[2].toInt() and 255,
                bytes[3].toInt() and 255, b.getInt(4), b.getInt(8), b.getInt(12))
            return value.takeIf { it.state in 0..3 && it.error in 0..9 && it.kind in 0..2 &&
                it.accepted >= 0 && it.total >= 0 && it.accepted <= it.total }
        }
    }
}

/** The only safe positions are before or after the single packet currently in flight. */
internal data class OtaExpectation(
    val session: Int, val kind: Int, val total: Int,
    val from: Int, val to: Int, val beforeState: Int, val afterState: Int
) {
    fun confirmed(value: OtaStatus) = value.belongsTo(session, kind, total) &&
        value.error == 0 && value.accepted == to && value.state == afterState

    fun recover(value: OtaStatus): Boolean {
        if (!value.belongsTo(session, kind, total))
            throw OtaRejectedException("设备更新会话已改变，无法安全续传；请重新授权后开始")
        if (value.state == 3 || value.error in setOf(3, 7, 8))
            throw OtaRejectedException("设备更新失败，错误 ${value.error}")
        // Status.error can describe an earlier rejected command. State/offset are the
        // persisted truth; a valid retry clears the earlier nonfatal error on firmware.
        if (value.accepted == to && value.state == afterState) return true
        if (value.accepted == from && value.state == beforeState) return false
        throw OtaRejectedException("设备更新进度不一致（${value.accepted}/$total），停止续传")
    }
}

internal class OtaRetryBudget(private val limit: Int = 2) {
    var used: Int = 0
        private set
    fun take(): Boolean {
        if (used >= limit) return false
        used++
        return true
    }
}
