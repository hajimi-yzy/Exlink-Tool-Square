package com.exlink.mobile

import android.graphics.*
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import java.io.File

/** EXA1 little-endian RGB565 row RLE, shared contract with ESP32 firmware. */
object AssetEncoder {
    data class Result(val file: File, val frames: Int, val durationMs: Int,
                      val samples: Int, val samplingReduced: Boolean, val trimmed: Boolean)

    fun crop(source: Bitmap, horizontal: Float, vertical: Float): Bitmap {
        val edge = minOf(source.width, source.height)
        val left = ((source.width - edge) * horizontal).toInt()
        val top = ((source.height - edge) * vertical).toInt()
        return Bitmap.createBitmap(480, 480, Bitmap.Config.ARGB_8888).also { output ->
            Canvas(output).apply { drawColor(Color.BLACK); drawBitmap(source, Rect(left, top, left + edge, top + edge), Rect(0, 0, 480, 480), Paint(Paint.FILTER_BITMAP_FLAG)) }
        }
    }
    suspend fun create(image: Bitmap?, gif: ByteArray?, horizontal: Float, vertical: Float, target: File): Result {
        val coroutine = currentCoroutineContext()
        val checkCancelled = { coroutine.ensureActive() }
        fun encode(bitmap: Bitmap) = AssetFormat.encodeRows({ y, row ->
            bitmap.getPixels(row, 0, 480, 0, y, 480, 1)
        }, checkCancelled)
        var trimmed = false
        val animation = if (gif != null) {
            // Movie eagerly expands every source raster, before output downsampling.
            val checked = GifPreflight.validate(gif, checkCancelled)
            checkCancelled()
            val movie = Movie.decodeByteArray(gif, 0, gif.size) ?: error("GIF 解码失败")
            require(movie.width() == checked.width && movie.height() == checked.height) { "GIF 解码失败" }
            val originalDuration = movie.duration().takeIf { it > 0 } ?: 100
            val duration = originalDuration.coerceAtMost(AssetFormat.MAX_DURATION_MS)
            trimmed = originalDuration > duration
            val canvasBitmap = Bitmap.createBitmap(movie.width(), movie.height(), Bitmap.Config.ARGB_8888)
            val canvas = Canvas(canvasBitmap)
            try {
                AssetFormat.fitAnimation(duration, { time ->
                    // Movie draws its already-composited bitmap (including GIF disposal).
                    // Clear only our destination so transparent pixels have a stable black background.
                    canvas.drawColor(Color.BLACK)
                    movie.setTime(AssetFormat.movieSampleTime(time, duration)); movie.draw(canvas, 0f, 0f)
                    val cropped = crop(canvasBitmap, horizontal, vertical)
                    try { encode(cropped) } finally { cropped.recycle() }
                }, checkCancelled)
            } finally { canvasBitmap.recycle() }
        } else {
            val cropped = crop(image ?: error("请先导入图片"), horizontal, vertical)
            val bytes = try { encode(cropped) } finally { cropped.recycle() }
            AssetFormat.Animation(listOf(AssetFormat.Frame(bytes, 5000)), 5000, 1)
        }
        checkCancelled()
        target.writeBytes(AssetFormat.pack(animation))
        return Result(target, animation.frames.size, animation.durationMs, animation.samples,
            gif != null && animation.samples < (animation.durationMs / 100).coerceAtLeast(1), trimmed)
    }
}
