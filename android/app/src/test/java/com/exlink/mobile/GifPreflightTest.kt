package com.exlink.mobile

import java.io.ByteArrayOutputStream
import java.util.concurrent.CancellationException
import org.junit.Assert.*
import org.junit.Test

class GifPreflightTest {
    // Structural fixtures exercise the scanner, not LZW decoding or Android Movie.
    private class Gif(width: Int = 480, height: Int = 480, globalPalette: Boolean = true) {
        private val output = ByteArrayOutputStream()
        init {
            output.write("GIF89a".toByteArray(Charsets.US_ASCII))
            word(width); word(height)
            raw(if (globalPalette) 0x80 else 0, 0, 0)
            if (globalPalette) raw(0, 0, 0, 255, 255, 255)
        }
        private fun word(value: Int) { raw(value and 255, value ushr 8 and 255) }
        fun raw(vararg values: Int) = apply { values.forEach { output.write(it) } }
        fun frame(width: Int = 480, height: Int = 480, left: Int = 0, top: Int = 0,
                  localPalette: Boolean = false, codeSize: Int = 2) = apply {
            raw(0x2c); word(left); word(top); word(width); word(height)
            raw(if (localPalette) 0x80 else 0)
            if (localPalette) raw(0, 0, 0, 255, 255, 255)
            raw(codeSize, 2, 0x44, 1, 0)
        }
        fun bytes(trailer: Boolean = true): ByteArray = output.toByteArray() +
            if (trailer) byteArrayOf(0x3b) else byteArrayOf()
    }

    private fun rejected(bytes: ByteArray, message: String) {
        try {
            GifPreflight.validate(bytes)
            fail("Invalid or excessive GIF accepted")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message.orEmpty(), e.message.orEmpty().contains(message))
        } catch (e: IllegalStateException) {
            assertTrue(e.message.orEmpty(), e.message.orEmpty().contains(message))
        }
    }

    @Test fun ordinaryTwentyFiveSecondFullFrameGifFitsDecodedBudget() {
        val gif = Gif()
        repeat(250) { gif.raw(0x21, 0xf9, 4, 0, 10, 0, 0, 0).frame() }
        val info = GifPreflight.validate(gif.bytes())
        assertEquals(480, info.width)
        assertEquals(480, info.height)
        assertEquals(250, info.frames)
        assertEquals(57_600_000L, info.rasterPixels)
    }

    @Test fun decodedPixelBudgetIsInclusiveAndRejectsNextFrame() {
        val gif = Gif(2048, 2048)
        repeat(16) { gif.frame(2048, 2048) }
        assertEquals(GifPreflight.MAX_RASTER_PIXELS, GifPreflight.validate(gif.bytes()).rasterPixels)
        rejected(gif.frame(1, 1).bytes(), "解码量过大")
    }

    @Test fun manyTinySourceFramesHaveAnIndependentLimit() {
        val gif = Gif(1, 1)
        repeat(GifPreflight.MAX_SOURCE_FRAMES) { gif.frame(1, 1) }
        assertEquals(2500, GifPreflight.validate(gif.bytes()).frames)
        rejected(gif.frame(1, 1).bytes(), "解码量过大")
    }

    @Test fun validatesLogicalAndFrameBoundsBeforeNativeDecode() {
        rejected(Gif(2049, 1).frame(1, 1).bytes(), "尺寸")
        rejected(Gif(65535, 65535).frame(1, 1).bytes(), "尺寸")
        rejected(Gif(0, 1).frame(1, 1).bytes(), "尺寸")
        rejected(Gif(2, 2).frame(0, 1).bytes(), "范围")
        rejected(Gif(2, 2).frame(2, 2, left = 1).bytes(), "范围")
        rejected(Gif(2, 2).frame(1, 1, top = 65535).bytes(), "范围")
        rejected(Gif(2048, 2048).frame(65535, 65535).bytes(), "范围")
    }

    @Test fun everyTruncatedPrefixIsRejectedIncludingMissingTrailer() {
        val bytes = Gif(1, 1).frame(1, 1).bytes()
        for (length in bytes.indices) rejected(bytes.copyOf(length), "不完整")
        assertEquals(1, GifPreflight.validate(bytes).frames)
        assertEquals(1, GifPreflight.validate(bytes.copyOf().also { it[4] = '7'.code.toByte() }).frames)
        rejected(Gif().bytes(), "没有图像帧")
        rejected(bytes.copyOf().also { it[0] = 0 }, "文件头")
    }

    @Test fun localPaletteExtensionsAndOffsetFramesAreAccepted() {
        val gif = Gif(2, 2, globalPalette = false)
            .raw(0x21, 0xff, 11, 78, 69, 84, 83, 67, 65, 80, 69, 50, 46, 48, 3, 1, 0, 0, 0)
            .raw(0x21, 0xfe, 3, 65, 66, 67, 0)
            .raw(0x21, 0x01, 12, 0, 0, 0, 0, 1, 0, 1, 0, 8, 8, 0, 1, 1, 65, 0)
            .raw(0x21, 0xf9, 4, 1, 10, 0, 0, 0)
            .frame(1, 1, left = 1, top = 1, localPalette = true)
        assertEquals(1L, GifPreflight.validate(gif.bytes()).rasterPixels)
        val bytes = gif.bytes()
        for (length in bytes.indices) rejected(bytes.copyOf(length), "不完整")
    }

    @Test fun malformedBlocksPalettesAndCodeSizesAreRejected() {
        rejected(Gif().raw(0x21, 0xf9, 3, 0, 0, 0, 0).frame().bytes(), "控制块")
        rejected(Gif().raw(0x21, 0xf9, 4, 0, 0, 0, 0, 1).frame().bytes(), "控制块")
        rejected(Gif().raw(0x21, 0xff, 10).bytes(), "应用扩展")
        rejected(Gif().raw(0x21, 0x01, 11).bytes(), "文本扩展")
        rejected(Gif(globalPalette = false).frame().bytes(), "调色板")
        rejected(Gif().frame(codeSize = 9).bytes(), "LZW")
        val emptyData = Gif().frame().bytes().let { it.copyOfRange(0, it.size - 5) + byteArrayOf(0, 0x3b) }
        rejected(emptyData, "图像数据为空")
        rejected(Gif().raw(0x22).bytes(), "数据块")
        rejected(Gif().raw(0x21, 0xfe, 255, 1, 2, 3).bytes(), "不完整")
        rejected(ByteArray(GifPreflight.MAX_SOURCE_BYTES + 1), "16 MiB")
    }

    @Test fun sourceSizeLimitAcceptsExactBoundaryAndIgnoresPaddingAfterTrailer() {
        val bytes = Gif(1, 1).frame(1, 1).bytes().copyOf(GifPreflight.MAX_SOURCE_BYTES)
        assertEquals(1, GifPreflight.validate(bytes).frames)
    }

    @Test fun scanHonorsCancellationBeforeNativeDecode() {
        try {
            GifPreflight.validate(Gif().frame().bytes()) { throw CancellationException() }
            fail("Cancellation ignored")
        } catch (_: CancellationException) { }
    }
}
