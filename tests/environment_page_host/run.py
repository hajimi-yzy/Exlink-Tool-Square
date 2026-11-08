"""Run production environment_page.c on real LVGL and save its 480px display.

Requires Python and Zig; Pillow is optional for PNG output. Hardware and the
cache bridge are simulated. Page widgets, refresh and DELETE handlers are the
actual production source, with no copied geometry or recorded baseline.
Example: python tests/environment_page_host/run.py --compiler C:/path/to/zig.exe
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    default_zig = (os.environ.get("ZIG_EXE") or shutil.which("zig") or
                   str(Path.home() / ".codex/tmp/exlink-host-zig-20261029/ziglang/zig.exe"))
    parser.add_argument("--compiler", type=Path, default=Path(default_zig))
    parser.add_argument("--output", type=Path,
                        default=repo / "deliverables/environment-page-preview.png")
    args = parser.parse_args()
    if not args.compiler.is_file():
        parser.error(f"Zig unavailable: {args.compiler}; pass --compiler or set ZIG_EXE")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    ppm = output.with_suffix(".ppm")
    sources = sorted((repo / "lib/lvgl/src").rglob("*.c"))
    sources += [repo / "src/environment_page.c", Path(__file__).with_name("test.c")]
    with tempfile.TemporaryDirectory(prefix="exlink-environment-host-") as temporary:
        build = Path(temporary)
        executable = build / ("environment_page_host.exe" if os.name == "nt" else "environment_page_host")
        flags = [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-UNDEBUG",
            "-DLV_CONF_SKIP=1", "-DLV_KCONFIG_IGNORE=1", "-DLV_COLOR_DEPTH=16",
            "-DLV_COLOR_16_SWAP=0", "-DLV_MEM_SIZE=262144",
            "-DLV_MEM_CUSTOM=0", "-DLV_TICK_CUSTOM=0",
            "-DLV_USE_ASSERT_OBJ=1", "-DLV_USE_ASSERT_MEM_INTEGRITY=1",
            "-DLV_ASSERT_HANDLER_INCLUDE=<stdlib.h>", "-DLV_ASSERT_HANDLER=abort();",
            "-DLV_THEME_DEFAULT_DARK=1",
            *[f"-DLV_FONT_MONTSERRAT_{size}=1" for size in (14, 16, 20, 24, 28, 48)],
            "-I", str(repo / "include"), "-I", str(repo / "lib/lvgl"),
            *map(str, sources), "-o", str(executable), "-lm",
        ]
        # Response files keep all LVGL translation units below Windows' shell
        # command limit. Paths use forward slashes for Clang response parsing.
        response = build / "compile.rsp"
        response.write_text("\n".join(
            '"' + flag.replace("\\", "/").replace('"', '\\"') + '"' for flag in flags),
            encoding="utf-8")
        print(f"Compiling production Environment page + real LVGL ({len(sources)} C files)", flush=True)
        subprocess.run([str(args.compiler), "cc", "@" + str(response)], cwd=repo, check=True)
        subprocess.run([str(executable), str(ppm)], cwd=repo, check=True, timeout=30)
    try:
        from PIL import Image, PngImagePlugin
    except ImportError:
        print(f"Pillow unavailable; real LVGL PPM retained at {ppm}")
    else:
        metadata = PngImagePlugin.PngInfo()
        metadata.add_text("Description", "Actual production environment_page.c rendered by LVGL at 480x480")
        metadata.add_text("Sensor", "SIMULATED AHT20: temperature 25.6 C, humidity 58.2 percent RH")
        with Image.open(ppm) as image:
            image.save(output, pnginfo=metadata)
        print(f"Actual LVGL preview: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
