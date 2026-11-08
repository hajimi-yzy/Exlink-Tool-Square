#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Convert Espressif's five official EAF expressions to a tiny ROM package.

The source files use 8-bit palettes and byte-pair RLE blocks.  This converter
strictly validates that format, converts each palette to the RGB565 values used
by esp_emote_gfx, joins the blocks, and re-RLEs each complete frame.  Generated
C++ decodes directly from flash into a caller-owned scanline; no frame buffer or
heap allocation is required.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path


ASSETS = (
    ("Neutral", "neutral.eaf"),
    ("Happy", "Happy.eaf"),
    ("Shocked", "shocked.eaf"),
    ("Winking", "winking.eaf"),
    ("Sleepy", "sleep.eaf"),
)
FPS = 20


@dataclass(frozen=True)
class Converted:
    enum_name: str
    source_name: str
    width: int
    height: int
    frames: tuple[bytes, ...]
    source_size: int
    source_sha256: str
    pixels_sha256: str


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def rgb565(entry: bytes) -> int:
    """Match gfx_eaf_dec.c: palette bytes are B, G, R, A."""
    b, g, r, _a = entry
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def decode_eaf(path: Path, enum_name: str) -> Converted:
    data = path.read_bytes()
    if len(data) < 16 or data[:4] != b"\x89EAF":
        raise ValueError(f"{path}: invalid EAF header")
    frame_total, stored_checksum, stored_len = struct.unpack_from("<III", data, 4)
    if frame_total == 0 or 16 + stored_len > len(data):
        raise ValueError(f"{path}: invalid table payload length")
    if (sum(data[16 : 16 + stored_len]) & 0xFFFFFFFF) != stored_checksum:
        raise ValueError(f"{path}: checksum mismatch")
    table_end = 16 + frame_total * 8
    if table_end > len(data):
        raise ValueError(f"{path}: truncated frame table")

    frames: list[bytes] = []
    dimensions: tuple[int, int] | None = None
    pixel_hash = hashlib.sha256()
    control_frames = 0

    for frame_no in range(frame_total):
        frame_size, frame_offset = struct.unpack_from("<II", data, 16 + frame_no * 8)
        start = table_end + frame_offset
        end = start + frame_size
        if frame_size < 4 or end > len(data) or data[start : start + 2] != b"ZZ":
            raise ValueError(f"{path}: invalid frame {frame_no} bounds/magic")
        frame = data[start + 2 : end]
        if frame[:2] == b"_C":
            control_frames += 1
            continue
        if frame[:2] != b"_S" or len(frame) < 18:
            raise ValueError(f"{path}: unsupported frame {frame_no} type")
        if frame[9] != 8:
            raise ValueError(f"{path}: frame {frame_no} is not 8-bit paletted")

        width, height, blocks, block_height = struct.unpack_from("<HHHH", frame, 10)
        if not width or not height or not blocks or not block_height:
            raise ValueError(f"{path}: frame {frame_no} has invalid dimensions")
        if blocks * block_height < height:
            raise ValueError(f"{path}: frame {frame_no} does not cover its height")
        if dimensions is None:
            dimensions = (width, height)
        elif dimensions != (width, height):
            raise ValueError(f"{path}: inconsistent dimensions in frame {frame_no}")

        lengths_end = 18 + blocks * 4
        palette_end = lengths_end + 256 * 4
        if palette_end > len(frame):
            raise ValueError(f"{path}: frame {frame_no} header is truncated")
        lengths = struct.unpack_from(f"<{blocks}I", frame, 18)
        palette = tuple(frame[lengths_end + i * 4 : lengths_end + (i + 1) * 4]
                        for i in range(256))
        cursor = palette_end
        indexes = bytearray()
        for block_no, block_len in enumerate(lengths):
            block_end = cursor + block_len
            if block_len < 1 or block_end > len(frame):
                raise ValueError(f"{path}: frame {frame_no} block {block_no} is truncated")
            block = frame[cursor:block_end]
            cursor = block_end
            if block[0] != 0:  # EAF_DEC_ENCODING_RLE
                raise ValueError(
                    f"{path}: frame {frame_no} block {block_no} encoding {block[0]} unsupported"
                )
            payload = block[1:]
            if len(payload) & 1:
                raise ValueError(f"{path}: frame {frame_no} block {block_no} odd RLE size")
            decoded = bytearray()
            for pos in range(0, len(payload), 2):
                count, value = payload[pos], payload[pos + 1]
                if count == 0:
                    raise ValueError(f"{path}: zero RLE count in frame {frame_no}")
                decoded.extend(bytes((value,)) * count)
            expected = width * block_height
            if len(decoded) != expected:
                raise ValueError(
                    f"{path}: frame {frame_no} block {block_no}: "
                    f"decoded {len(decoded)}, expected {expected}"
                )
            indexes.extend(decoded)
        indexes = indexes[: width * height]
        if len(indexes) != width * height:
            raise ValueError(f"{path}: frame {frame_no} pixel count mismatch")

        used = set(indexes)
        non_opaque = [i for i in used if palette[i][3] != 255]
        if non_opaque:
            raise ValueError(
                f"{path}: frame {frame_no} uses non-opaque palette indexes {non_opaque[:8]}"
            )
        palette565 = [rgb565(entry) for entry in palette]
        rendered = b"".join(struct.pack("<H", palette565[index]) for index in indexes)
        pixel_hash.update(rendered)

        record = bytearray(struct.pack("<256H", *palette565))
        pos = 0
        while pos < len(indexes):
            value = indexes[pos]
            count = 1
            while pos + count < len(indexes) and indexes[pos + count] == value and count < 255:
                count += 1
            record.extend((count, value))
            pos += count

        # Decode our record again and prove pixel-for-pixel RGB565 equality.
        round_trip = bytearray()
        payload = record[512:]
        for pos in range(0, len(payload), 2):
            count, value = payload[pos], payload[pos + 1]
            round_trip.extend(struct.pack("<H", palette565[value]) * count)
        if bytes(round_trip) != rendered:
            raise AssertionError(f"{path}: internal round-trip failure in frame {frame_no}")
        frames.append(bytes(record))

    if dimensions is None or not frames:
        raise ValueError(f"{path}: no drawable frames")
    if control_frames != 1:
        raise ValueError(f"{path}: expected one control frame, found {control_frames}")
    return Converted(
        enum_name=enum_name,
        source_name=path.name,
        width=dimensions[0],
        height=dimensions[1],
        frames=tuple(frames),
        source_size=len(data),
        source_sha256=hashlib.sha256(data).hexdigest(),
        pixels_sha256=pixel_hash.hexdigest(),
    )


def hex_array(data: bytes, indent: str = "    ") -> str:
    lines = []
    for pos in range(0, len(data), 16):
        lines.append(indent + ", ".join(f"0x{x:02X}" for x in data[pos : pos + 16]) + ",")
    return "\n".join(lines)


def generate_cpp(items: tuple[Converted, ...]) -> str:
    chunks = [
        "/*",
        " * SPDX-License-Identifier: Apache-2.0",
        " *",
        " * Generated from Espressif esp_emote_assets EAF files. The installed",
        " * component's idf_component.yml declares Apache-2.0; it contains no",
        " * separate LICENSE file. Do not edit this generated file by hand.",
        " */",
        '#include "emote_assets.h"',
        "",
        "#if defined(__has_include)",
        "#  if __has_include(<pgmspace.h>)",
        "#    include <pgmspace.h>",
        "#  endif",
        "#endif",
        "#ifndef PROGMEM",
        "#  define PROGMEM",
        "#endif",
        "#ifndef pgm_read_byte",
        "#  define pgm_read_byte(addr) (*(const uint8_t *)(addr))",
        "#endif",
        "",
        "namespace idle_emote {",
        "namespace {",
        "struct Asset {",
        "    uint16_t width, height, frame_count, fps;",
        "    const uint32_t *offsets;",
        "    const uint8_t *data;",
        "};",
        "",
        "static uint16_t read16(const uint8_t *p) {",
        "    return static_cast<uint16_t>(pgm_read_byte(p)) |",
        "           static_cast<uint16_t>(pgm_read_byte(p + 1)) << 8;",
        "}",
        "static uint32_t read32(const uint32_t *p) {",
        "    const uint8_t *b = reinterpret_cast<const uint8_t *>(p);",
        "    return static_cast<uint32_t>(pgm_read_byte(b)) |",
        "           static_cast<uint32_t>(pgm_read_byte(b + 1)) << 8 |",
        "           static_cast<uint32_t>(pgm_read_byte(b + 2)) << 16 |",
        "           static_cast<uint32_t>(pgm_read_byte(b + 3)) << 24;",
        "}",
        "",
    ]
    for item in items:
        ident = item.enum_name.lower()
        offsets = [0]
        blob = bytearray()
        for frame in item.frames:
            blob.extend(frame)
            offsets.append(len(blob))
        chunks.extend(
            [
                f"// {item.source_name}; source sha256 {item.source_sha256}",
                f"// rendered RGB565 sha256 {item.pixels_sha256}",
                f"static const uint32_t {ident}_offsets[] PROGMEM = {{",
                "    " + ", ".join(f"{x}u" for x in offsets) + ",",
                "};",
                f"static const uint8_t {ident}_data[] PROGMEM = {{",
                hex_array(bytes(blob)),
                "};",
                "",
            ]
        )
    chunks.extend(["static const Asset assets[] = {"])
    for item in items:
        ident = item.enum_name.lower()
        chunks.append(
            f"    {{{item.width}, {item.height}, {len(item.frames)}, {FPS}, "
            f"{ident}_offsets, {ident}_data}},"
        )
    chunks.extend(
        [
            "};",
            "static_assert(sizeof(assets) / sizeof(assets[0]) ==",
            "              static_cast<size_t>(Emote::Count), \"asset table mismatch\");",
            "",
            "static const Asset *findAsset(Emote emote) {",
            "    const size_t index = static_cast<size_t>(emote);",
            "    return index < static_cast<size_t>(Emote::Count) ? &assets[index] : nullptr;",
            "}",
            "}  // namespace",
            "",
            "bool getInfo(Emote emote, AnimationInfo *out) {",
            "    const Asset *asset = findAsset(emote);",
            "    if (!asset || !out) return false;",
            "    *out = {asset->width, asset->height, asset->frame_count, asset->fps};",
            "    return true;",
            "}",
            "",
            "uint16_t frameForElapsed(Emote emote, uint32_t elapsed_ms) {",
            "    const Asset *asset = findAsset(emote);",
            "    if (!asset || !asset->frame_count || !asset->fps) return 0;",
            "    return static_cast<uint16_t>(((elapsed_ms * asset->fps) / 1000u) %",
            "                                 asset->frame_count);",
            "}",
            "",
            "bool decodeFrameRows(Emote emote, uint16_t frame_index,",
            "                     uint16_t *row_buffer, size_t row_buffer_pixels,",
            "                     RowCallback callback, void *user, bool byte_swap) {",
            "    const Asset *asset = findAsset(emote);",
            "    if (!asset || !row_buffer || !callback || frame_index >= asset->frame_count ||",
            "        row_buffer_pixels < asset->width) return false;",
            "    const uint32_t begin = read32(asset->offsets + frame_index);",
            "    const uint32_t end = read32(asset->offsets + frame_index + 1);",
            "    if (end < begin + 512u) return false;",
            "    const uint8_t *record = asset->data + begin;",
            "    const uint8_t *cursor = record + 512;",
            "    const uint8_t *limit = asset->data + end;",
            "    uint16_t x = 0, y = 0;",
            "    while (cursor + 1 < limit && y < asset->height) {",
            "        uint16_t count = pgm_read_byte(cursor++);",
            "        const uint8_t index = pgm_read_byte(cursor++);",
            "        if (!count) return false;",
            "        uint16_t color = read16(record + static_cast<uint16_t>(index) * 2u);",
            "        if (byte_swap) color = static_cast<uint16_t>((color << 8) | (color >> 8));",
            "        while (count--) {",
            "            row_buffer[x++] = color;",
            "            if (x == asset->width) {",
            "                if (!callback(user, y, row_buffer, asset->width)) return false;",
            "                x = 0;",
            "                ++y;",
            "                if (y == asset->height && (count || cursor != limit)) return false;",
            "            }",
            "        }",
            "    }",
            "    return y == asset->height && x == 0 && cursor == limit;",
            "}",
            "",
            "}  // namespace idle_emote",
            "",
        ]
    )
    return "\n".join(chunks)


def main() -> int:
    script = Path(__file__).resolve()
    project = script.parents[1]
    default_source = (
        project.parent
        / "esp32s3_co5300_480_qspi_31pin_touch"
        / "managed_components"
        / "espressif2022__esp_emote_assets"
        / "emoji_large"
    )
    default_output = project / "lib" / "IdleEmote" / "src" / "emote_assets.cpp"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=default_source)
    parser.add_argument("--output", type=Path, default=default_output)
    parser.add_argument("--check", action="store_true", help="verify output is current; do not write")
    args = parser.parse_args()

    items = tuple(decode_eaf(args.source_dir / filename, enum_name)
                  for enum_name, filename in ASSETS)
    generated = generate_cpp(items)
    generated_bytes = generated.encode("utf-8")
    package_bytes = sum(sum(len(frame) for frame in item.frames) +
                        4 * (len(item.frames) + 1) for item in items)

    for item in items:
        encoded = sum(len(frame) for frame in item.frames) + 4 * (len(item.frames) + 1)
        print(
            f"{item.enum_name:8s} {item.width}x{item.height} "
            f"frames={len(item.frames):3d} source={item.source_size:6d} "
            f"package={encoded:6d} rgb565_sha256={item.pixels_sha256}"
        )
    print(f"total package bytes: {package_bytes}")
    print(f"generated C++ sha256: {hashlib.sha256(generated_bytes).hexdigest()}")
    if package_bytes >= 2 * 1024 * 1024:
        raise ValueError("package exceeds the 2 MiB requirement")

    if args.check:
        current = args.output.read_bytes() if args.output.exists() else b""
        if current != generated_bytes:
            raise SystemExit(f"OUTDATED: {args.output}")
        print(f"verified current output: {args.output}")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(generated_bytes)
        print(f"wrote: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
