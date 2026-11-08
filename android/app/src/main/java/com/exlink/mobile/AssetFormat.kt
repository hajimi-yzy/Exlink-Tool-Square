package com.exlink.mobile

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** The existing EXA1 device format: complete RGB565 frames, not GIF compression. */
internal object AssetFormat {
    const val EDGE = 480
    const val MAX_BYTES = 0x380000
    const val MAX_FRAMES = 250
    const val MAX_DURATION_MS = 25_000
    private const val HEADER_BYTES = 32
    private const val ENTRY_BYTES = 12

    data class Frame(val bytes: ByteArray, var durationMs: Int)
    data class Animation(val frames: List<Frame>, val durationMs: Int, val samples: Int) {
        val size: Int get() = HEADER_BYTES + frames.size * ENTRY_BYTES + frames.sumOf { it.bytes.size }
    }

    // Android Movie treats an exact GIF frame boundary as part of the preceding frame.
    // Probe inside the interval while retaining its original EXA1 start/duration.
    fun movieSampleTime(intervalStartMs: Int, durationMs: Int): Int {
        require(durationMs > 0 && intervalStartMs in 0 until durationMs)
        return if (intervalStartMs == 0) 0 else intervalStartMs + 1
    }

    // At most one budget-sized candidate plus the current (<= 921600-byte) frame is retained.
    // Reduce time samples, never the playback duration; callers show the result before upload.
    fun fitAnimation(durationMs: Int, render: (Int) -> ByteArray, checkCancelled: () -> Unit = {}): Animation {
        require(durationMs in 1..MAX_DURATION_MS)
        var samples = (durationMs / 100).coerceIn(1, MAX_FRAMES)
        while (true) {
            val frames = ArrayList<Frame>()
            var size = HEADER_BYTES
            for (index in 0 until samples) {
                checkCancelled()
                val time = index * durationMs / samples
                val next = (index + 1) * durationMs / samples
                val bytes = render(time)
                val previous = frames.lastOrNull()
                if (previous != null && previous.bytes.contentEquals(bytes)) {
                    previous.durationMs += next - time
                } else {
                    size += ENTRY_BYTES + bytes.size
                    if (size > MAX_BYTES) break
                    frames.add(Frame(bytes, next - time))
                }
                if (index == samples - 1) return Animation(frames, durationMs, samples)
            }
            check(samples > 1) { "单帧转换后仍超过设备 3.5 MiB 限制" }
            samples = (samples / 2).coerceAtLeast(1)
        }
    }

    fun encodeRows(readRow: (Int, IntArray) -> Unit, checkCancelled: () -> Unit = {}): ByteArray {
        val output = ByteArrayOutputStream()
        val row = IntArray(EDGE)
        fun word(value: Int) { output.write(value and 255); output.write(value ushr 8 and 255) }
        fun rgb565(color: Int) = ((color ushr 19 and 31) shl 11) or
            ((color ushr 10 and 63) shl 5) or (color ushr 3 and 31)
        for (y in 0 until EDGE) {
            checkCancelled()
            readRow(y, row)
            var x = 0
            while (x < EDGE) {
                val color = rgb565(row[x])
                var count = 1
                while (x + count < EDGE && rgb565(row[x + count]) == color) count++
                word(count); word(color); x += count
            }
        }
        return output.toByteArray()
    }

    fun pack(animation: Animation): ByteArray {
        require(animation.size <= MAX_BYTES && animation.frames.size in 1..MAX_FRAMES)
        require(animation.durationMs in 1..MAX_DURATION_MS &&
            animation.frames.all { it.durationMs > 0 } &&
            animation.frames.sumOf { it.durationMs } == animation.durationMs)
        val output = ByteBuffer.allocate(animation.size).order(ByteOrder.LITTLE_ENDIAN)
        output.put("EXA1".toByteArray(Charsets.US_ASCII)).putShort(1).putShort(32)
            .putShort(EDGE.toShort()).putShort(EDGE.toShort())
            .putShort(animation.frames.size.toShort()).putShort(1)
            .putInt(animation.size).putInt(animation.durationMs).putInt(32).putInt(0)
        var offset = HEADER_BYTES + animation.frames.size * ENTRY_BYTES
        animation.frames.forEach { frame ->
            output.putInt(offset).putInt(frame.bytes.size).putInt(frame.durationMs)
            offset += frame.bytes.size
        }
        animation.frames.forEach { output.put(it.bytes) }
        return output.array()
    }
}
