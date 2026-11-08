package com.exlink.mobile

/** Bounds Movie's eager native GIF raster allocations without decoding LZW pixels. */
internal object GifPreflight {
    const val MAX_SOURCE_BYTES = 16 * 1024 * 1024
    const val MAX_EDGE = 2048
    const val MAX_SOURCE_FRAMES = 2500
    // One indexed byte per source raster pixel; allows 250 full 480x480 frames.
    const val MAX_RASTER_PIXELS = 64L * 1024 * 1024

    data class Info(val width: Int, val height: Int, val frames: Int, val rasterPixels: Long)

    fun validate(bytes: ByteArray, checkCancelled: () -> Unit = {}): Info {
        require(bytes.size <= MAX_SOURCE_BYTES) { "导入文件超过 16 MiB" }
        var cursor = 0
        fun skip(count: Int) {
            require(count <= bytes.size - cursor) { "GIF 数据不完整" }
            cursor += count
        }
        fun byte(): Int {
            require(cursor < bytes.size) { "GIF 数据不完整" }
            return bytes[cursor++].toInt() and 255
        }
        fun word() = byte() or (byte() shl 8)
        fun palette(flags: Int) {
            if (flags and 128 != 0) skip(3 * (2 shl (flags and 7)))
        }
        fun subBlocks(): Boolean {
            var hasData = false
            while (true) {
                checkCancelled()
                val length = byte()
                if (length == 0) return hasData
                skip(length)
                hasData = true
            }
        }

        skip(6)
        val signature = String(bytes, 0, 6, Charsets.US_ASCII)
        require(signature == "GIF87a" || signature == "GIF89a") { "GIF 文件头无效" }
        val width = word()
        val height = word()
        require(width in 1..MAX_EDGE && height in 1..MAX_EDGE) { "GIF 尺寸超过 2048 或尺寸无效，请先缩小" }
        val screenFlags = byte()
        skip(2) // Background index and pixel aspect ratio.
        palette(screenFlags)
        var frames = 0
        var rasterPixels = 0L
        while (true) {
            checkCancelled()
            when (byte()) {
                0x2c -> {
                    val left = word()
                    val top = word()
                    val frameWidth = word()
                    val frameHeight = word()
                    require(frameWidth > 0 && frameHeight > 0 &&
                        left + frameWidth <= width && top + frameHeight <= height) { "GIF 帧范围无效" }
                    frames++
                    rasterPixels += frameWidth.toLong() * frameHeight
                    require(frames <= MAX_SOURCE_FRAMES && rasterPixels <= MAX_RASTER_PIXELS) {
                        "GIF 解码量过大（最多 2500 源帧、64 MiB 像素），请缩短或缩小 GIF"
                    }
                    val flags = byte()
                    require((flags or screenFlags) and 128 != 0) { "GIF 缺少调色板" }
                    palette(flags)
                    require(byte() in 2..8) { "GIF LZW 编码无效" }
                    require(subBlocks()) { "GIF 图像数据为空" }
                }
                0x21 -> when (byte()) {
                    0xf9 -> {
                        require(byte() == 4) { "GIF 控制块无效" }
                        skip(4)
                        require(byte() == 0) { "GIF 控制块无效" }
                    }
                    0xff -> {
                        require(byte() == 11) { "GIF 应用扩展块无效" }
                        skip(11)
                        subBlocks()
                    }
                    0x01 -> {
                        require(byte() == 12) { "GIF 文本扩展块无效" }
                        skip(12)
                        subBlocks()
                    }
                    else -> subBlocks() // Comments and extensions unknown to this version.
                }
                0x3b -> {
                    require(frames > 0) { "GIF 没有图像帧" }
                    return Info(width, height, frames, rasterPixels)
                }
                else -> error("GIF 数据块无效")
            }
        }
    }
}
