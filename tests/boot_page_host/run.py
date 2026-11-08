"""Render the actual production boot function with the repository's LVGL.

Arduino hardware and the main-menu constructor are stubbed. Boot construction,
animation callbacks and timeline cleanup are extracted from production files
unchanged. The production A8 artwork is compiled alongside real LVGL.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def assert_native_geometry(source: str, filename: str = "src/ui.c") -> None:
    """Do not silently drop translation-unit geometry aliases during extraction.

    Both object-like and function-like macros alter production calls, including
    parenthesized names for object-like macros. The extracted renderer cannot
    reproduce arbitrary translation-unit state, so reject geometry interception.
    The complete ui_native_host test compiles the production file itself.
    """
    aliases = re.findall(
        r"^\s*#\s*define\s+(lv_obj_(?:set_\w+|align(?:_to)?|clean))\b",
        source, re.MULTILINE)
    if aliases:
        raise ValueError(f"{filename} redefines LVGL geometry setters: {', '.join(aliases)}; "
                         "the extracted boot test would omit production context")
    if re.search(r"\bui_(?:scale_\w+|obj_(?:set_|align)\w*|clean_object)\b", source):
        raise ValueError(f"{filename} still contains runtime UI geometry scaling")


def assert_production_context(repo: Path) -> None:
    for directory in (repo / "src", repo / "include"):
        for path in sorted(directory.rglob("*")):
            if path.suffix in (".c", ".cpp", ".h", ".hpp"):
                assert_native_geometry(path.read_text(encoding="utf-8-sig"),
                                       str(path.relative_to(repo)))


def function(source: str, name: str) -> str:
    match = re.search(r"^(?:static\s+)?void\s+" + re.escape(name) +
                      r"\([^)]*\)\s*\{", source, re.MULTILINE)
    if not match:
        raise ValueError(f"Production function missing: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def asset(source: str, name: str) -> str:
    """Extract only the original named array and its LVGL descriptor."""
    pixels = re.search(r"^const[^\n]*\buint8_t\s+" + re.escape(name) +
                       r"_data\[\]\s*=\s*[{].*?^[}];", source, re.MULTILINE | re.DOTALL)
    descriptor = re.search(r"^const lv_img_dsc_t " + re.escape(name) +
                           r"\s*=\s*[{].*?^[}];", source, re.MULTILINE | re.DOTALL)
    if not pixels or not descriptor:
        raise ValueError(f"Production image missing: {name}")
    return pixels[0] + "\n\n" + descriptor[0]


def alpha_rgb565(color: int, alpha: int) -> tuple[int, int, int]:
    """Independent oracle for LVGL 8.2 RGB565-over-black blending.

    lv_color_mix reduces alpha to five bits before its packed integer blend.
    This is separate from rendering the production code under test.
    """
    mix = (alpha + 4) >> 3
    foreground = (color | (color << 16)) & 0x07E0F81F
    packed = ((foreground * mix) >> 5) & 0x07E0F81F
    color = ((packed >> 16) | packed) & 0xFFFF
    return (((color >> 11) * 263 + 7) >> 5,
            (((color >> 5) & 63) * 259 + 3) >> 6,
            ((color & 31) * 263 + 7) >> 5)


def white_alpha_rgb565(alpha: int) -> tuple[int, int, int]:
    return alpha_rgb565(0xFFFF, alpha)


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path(
        os.environ.get("ZIG_EXE") or shutil.which("zig") or
        "C:/Users/USER/AppData/Local/Temp/exlink-level-host-tools/ziglang/zig.exe"))
    parser.add_argument("--output", type=Path, default=repo / "deliverables/boot-reveal-lvgl")
    args = parser.parse_args()
    if not args.compiler.is_file():
        parser.error(f"Zig unavailable: {args.compiler}")
    from PIL import Image, ImageChops, PngImagePlugin
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    ui = (repo / "src/ui.c").read_text(encoding="utf-8-sig")
    events = (repo / "src/event.c").read_text(encoding="utf-8-sig")
    assert_native_geometry(ui)
    assert_native_geometry(events, "src/event.c")
    assert_production_context(repo)
    wordmark = asset((repo / "src/main_png.c").read_text(encoding="utf-8-sig"), "Exlink_png")
    production = "\n\n".join(['#include "boot_artwork.h"', '#include "display_geometry.h"',
        wordmark,
        *[function(events, name) for name in ("anim_cb1", "anim_cb2", "anim_end_callback")],
        function(ui, "boot_caption_progress"),
        function(ui, "boot_animation_owner_deleted"), function(ui, "create_boot_animation")])
    sources = sorted((repo / "lib/lvgl/src").rglob("*.c"))
    sources.extend([repo / "src/boot_artwork.c", repo / "tests/boot_page_host/test.c"])
    with tempfile.TemporaryDirectory(prefix="exlink-boot-preview-") as temporary:
        build = Path(temporary)
        (build / "production_boot.inc").write_text(production, encoding="utf-8")
        executable = build / ("boot_preview.exe" if os.name == "nt" else "boot_preview")
        flags = ["-std=c11", "-O1", "-g", "-UNDEBUG",
            "-DLV_CONF_SKIP=1", "-DLV_KCONFIG_IGNORE=1", "-DLV_COLOR_DEPTH=16",
            "-DLV_COLOR_16_SWAP=0", "-DLV_MEM_SIZE=131072",
            "-DLV_MEM_CUSTOM=0", "-DLV_TICK_CUSTOM=0",
            "-DLV_DISP_DEF_REFR_PERIOD=16", "-DLV_THEME_DEFAULT_DARK=1",
            "-DLV_USE_ASSERT_OBJ=1", "-DLV_USE_ASSERT_MEM_INTEGRITY=1",
            "-DLV_ASSERT_HANDLER_INCLUDE=<stdlib.h>", "-DLV_ASSERT_HANDLER=abort();",
            "-I", str(repo / "lib/lvgl"), "-I", str(repo / "include"), "-I", str(build),
            *map(str, sources), "-o", str(executable), "-lm"]
        response = build / "compile.rsp"
        response.write_text("\n".join(
            '"' + flag.replace("\\", "/").replace('"', '\"') + '"' for flag in flags),
            encoding="utf-8")
        print(f"Compiling real LVGL + extracted boot page ({len(sources)} C files)", flush=True)
        subprocess.run([str(args.compiler), "cc", "@" + str(response)], cwd=repo, check=True)
        subprocess.run([str(executable), str(build)], cwd=repo, check=True, timeout=60)
        frames = []
        for ppm in sorted(build.glob("frame-*.ppm")):
            with Image.open(ppm) as image:
                frames.append(image.convert("RGB"))
        assert len(frames) == 90, f"Expected 0..3560ms at 40ms intervals, got {len(frames)} frames"
        metadata = PngImagePlugin.PngInfo()
        metadata.add_text("Description", "Production boot reveal and A8 assets rendered by LVGL 8.2 in RGB565, 480x480")
        frames[-1].save(output / "boot-final.png", pnginfo=metadata)
        frames[0].save(output / "boot-animation.gif", save_all=True,
            append_images=frames[1:], duration=40, disposal=2, optimize=False)
        # Omitting the Netscape loop extension makes the preview play once.
        with Image.open(output / "boot-animation.gif") as gif:
            assert "loop" not in gif.info
            duration = 0
            for index in range(gif.n_frames):
                gif.seek(index)
                duration += gif.info["duration"]
            assert duration == 3600
        for name, index in (("square", 22), ("stretched", 35), ("revealing", 45)):
            frames[index].save(output / (name + ".png"))
        approved = repo / "deliverables/boot-reveal-python/boot-final.png"
        if approved.is_file():
            with Image.open(approved) as image:
                reference = image.convert("RGB")
            assert frames[-1].size == reference.size == (480, 480)
            background = reference.copy()
            for box in ((17, 12, 217, 99), (12, 105, 222, 138), (12, 150, 166, 184)):
                background.paste((0, 0, 0), box)
            assert not background.getbbox(), "Python preview contains pixels outside the approved layout"
            difference = ImageChops.difference(frames[-1], reference)
            channel_errors = tuple(high for low, high in difference.getextrema())
            oracle = Image.new("RGB", reference.size, 0)
            for box in ((17, 12, 217, 99), (12, 150, 166, 184)):
                artwork = reference.crop(box)
                pixels = list(getattr(artwork, "get_flattened_data", artwork.getdata)())
                assert all(r == g == b for r, g, b in pixels), "White artwork must be grayscale"
                quantized = Image.new("RGB", artwork.size)
                quantized.putdata([white_alpha_rgb565(r) for r, g, b in pixels])
                oracle.paste(quantized, box[:2])
            # Decode the original RGB565+alpha bytes independently of LVGL;
            # the wordmark must preserve Exlink red and TOOL white exactly.
            wordmark_bytes = bytes(int(value, 16) for value in re.findall(
                r"0x([0-9a-fA-F]{2})", wordmark.split("};", 1)[0]))
            assert len(wordmark_bytes) == 210 * 33 * 3
            wordmark_oracle = Image.new("RGB", (210, 33))
            wordmark_oracle.putdata([alpha_rgb565(
                wordmark_bytes[i] | wordmark_bytes[i + 1] << 8, wordmark_bytes[i + 2])
                for i in range(0, len(wordmark_bytes), 3)])
            assert not ImageChops.difference(reference.crop((12, 105, 222, 138)), wordmark_oracle).getbbox(), (
                "Python preview changed the original wordmark colour or glyphs")
            oracle.paste(wordmark_oracle, (12, 105))
            quantized_difference = ImageChops.difference(frames[-1], oracle)
            assert not quantized_difference.getbbox(), (
                f"Final LVGL frame differs from approved RGB565 oracle: {quantized_difference.getextrema()}")
            print(f"Approved final-frame comparison PASS: exact RGB565 oracle; max 8-bit RGB error={channel_errors}")
        else:
            print(f"Approved final-frame comparison skipped: {approved} is unavailable")
    print(f"Final preview: {output / 'boot-final.png'}")
    print(f"Animated preview: {output / 'boot-animation.gif'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
