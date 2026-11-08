package com.exlink.mobile

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CancellationException
import org.junit.Assert.*
import org.junit.Test

class AssetFormatTest {
    private fun solid(color: Int) = AssetFormat.encodeRows({ _, row -> row.fill(color) })
    private fun fixture(name: String, animation: AssetFormat.Animation) {
        File("build/exa-fixture/$name.exa").apply {
            parentFile.mkdirs(); writeBytes(AssetFormat.pack(animation))
        }
    }

    @Test fun movieBoundaryProbeKeepsBothHundredMillisecondFrames() {
        // Models AOSP Movie's inclusive end boundary; this is NOT an Android Movie test.
        fun movieFrame(time: Int) = if (time <= 100) 0 else 1
        val animation = AssetFormat.fitAnimation(200, { start ->
            byteArrayOf(movieFrame(AssetFormat.movieSampleTime(start, 200)).toByte())
        })
        assertEquals(listOf(0, 1), animation.frames.map { it.bytes.single().toInt() })
        assertEquals(listOf(100, 100), animation.frames.map { it.durationMs })
    }

    @Test fun movieProbeAlsoHandlesBoundaryAfterBudgetRetryWithoutChangingTiming() {
        val animation = AssetFormat.fitAnimation(800, { start ->
            val time = AssetFormat.movieSampleTime(start, 800)
            val frame = if (time == 0) 0 else (time - 1) / 100
            ByteArray(480 * 480 * 4).also { it[0] = frame.toByte() }
        })
        assertEquals(2, animation.samples)
        assertEquals(listOf(0, 4), animation.frames.map { it.bytes[0].toInt() })
        assertEquals(listOf(400, 400), animation.frames.map { it.durationMs })
        assertEquals(799, AssetFormat.movieSampleTime(798, 800))
        assertEquals(800, AssetFormat.movieSampleTime(799, 800))
    }

    @Test fun smallSourceMayExpandButBudgetKeepsWholeDuration() {
        // EXA1 data size, not source GIF bytes, determines Flash capacity.
        val animation = AssetFormat.fitAnimation(1000, { time ->
            ByteArray(900_000).also { it[0] = time.toByte() }
        })
        assertTrue(animation.size <= AssetFormat.MAX_BYTES)
        assertTrue(animation.samples < 10)
        assertEquals(1000, animation.frames.sumOf { it.durationMs })
        assertTrue(animation.frames.size > 1)
    }

    @Test fun repeatedSamplesAreStoredOnceAndDoNotForceDownsampling() {
        val frame = solid(0xff123456.toInt())
        val animation = AssetFormat.fitAnimation(25_000, { frame })
        assertEquals(250, animation.samples)
        assertEquals(1, animation.frames.size)
        assertEquals(25_000, animation.frames.single().durationMs)
        assertEquals(32 + 12 + 480 * 4, animation.size)
        fixture("repeated", animation)
    }

    @Test fun fractionalMillisecondsAreNotRoundedUpOrLost() {
        val animation = AssetFormat.fitAnimation(1001, { time -> byteArrayOf(time.toByte()) })
        assertEquals(10, animation.samples)
        assertEquals(1001, animation.frames.sumOf { it.durationMs })
        assertTrue(animation.frames.all { it.durationMs in 100..101 })
    }

    @Test fun maximumDetailRleFrameAndAdaptationStayInsideDeviceSlot() {
        val detailed = AssetFormat.encodeRows({ y, row ->
            row.indices.forEach { x -> row[x] = if ((x + y) % 2 == 0) 0xffff0000.toInt() else 0xff00ff00.toInt() }
        })
        assertEquals(480 * 480 * 4, detailed.size)
        val animation = AssetFormat.fitAnimation(800, { time ->
            detailed.copyOf().also { it[2] = time.toByte() }
        })
        assertTrue(animation.size <= AssetFormat.MAX_BYTES)
        assertEquals(800, animation.frames.sumOf { it.durationMs })
        assertEquals(2, animation.samples)
        assertEquals(2, animation.frames.size)
        fixture("adapted", animation)
    }

    @Test fun cancelledConversionStopsDuringRowEncoding() {
        var rows = 0
        try {
            AssetFormat.encodeRows({ _, row -> row.fill(-1); rows++ }, {
                if (rows == 5) throw CancellationException()
            })
            fail("Cancellation was ignored")
        } catch (_: CancellationException) {
            assertEquals(5, rows)
        }
    }

    @Test fun invalidDurationAndOversizedSingleFrameAreRejected() {
        try {
            AssetFormat.fitAnimation(25_001, { byteArrayOf(1) })
            fail("Oversized duration accepted")
        } catch (_: IllegalArgumentException) { }
        try {
            AssetFormat.fitAnimation(100, { ByteArray(AssetFormat.MAX_BYTES) })
            fail("Oversized single frame accepted")
        } catch (_: IllegalStateException) { }
    }

    @Test fun productionEncoderWritesFirmwareCompatibleRowRunsAndDurations() {
        val red = solid(0xffff0000.toInt())
        val green = solid(0xff00ff00.toInt())
        val blue = solid(0xff0000ff.toInt())
        val animation = AssetFormat.Animation(listOf(
            AssetFormat.Frame(red, 100), AssetFormat.Frame(green, 250), AssetFormat.Frame(blue, 151)
        ), 501, 3)
        val bytes = AssetFormat.pack(animation)
        val view = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals("EXA1", String(bytes, 0, 4, Charsets.US_ASCII))
        assertEquals(3, view.getShort(12).toInt())
        assertEquals(bytes.size, view.getInt(16))
        assertEquals(501, view.getInt(20))
        var next = 68
        val colors = listOf(0xf800, 0x07e0, 0x001f)
        listOf(100, 250, 151).forEachIndexed { index, duration ->
            assertEquals(next, view.getInt(32 + index * 12))
            assertEquals(1920, view.getInt(36 + index * 12))
            assertEquals(duration, view.getInt(40 + index * 12))
            repeat(480) { y ->
                assertEquals(480, view.getShort(next + y * 4).toInt() and 0xffff)
                assertEquals(colors[index], view.getShort(next + y * 4 + 2).toInt() and 0xffff)
            }
            next += 1920
        }
        assertEquals(bytes.size, next)
        // Host firmware parser cross-check fixture; generated only in ignored build/.
        fixture("rgb", animation)
    }
}
