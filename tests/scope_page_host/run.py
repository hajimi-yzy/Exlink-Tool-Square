"""Build real LVGL + scope_page.c; exercise pointer input and export its display.

The ADC is a timed cache mock. These tests do not validate ESP32 DMA hardware,
physical touch sampling, or analogue voltage accuracy.
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
    parser.add_argument("--compiler", type=Path, default=Path(
        os.environ.get("ZIG_EXE") or shutil.which("zig") or
        "C:/Users/USER/AppData/Local/Temp/exlink-level-host-tools/ziglang/zig.exe"))
    parser.add_argument("--output", type=Path, default=repo / "deliverables/scope-page-preview.png")
    args = parser.parse_args()
    if not args.compiler.is_file():
        parser.error(f"Zig is unavailable: {args.compiler}")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    ppm = output.with_suffix(".ppm")
    sources = sorted((repo / "lib/lvgl/src").rglob("*.c"))
    sources += [repo / "src/scope_page.c", repo / "tests/scope_page_host/test.c"]
    with tempfile.TemporaryDirectory(prefix="exlink-scope-host-") as temporary:
        build = Path(temporary)
        executable = build / ("scope_page_host.exe" if os.name == "nt" else "scope_page_host")
        route = build / "route.o"
        subprocess.run([str(args.compiler), "c++", "-std=c++14", "-O1", "-c",
                        str(repo / "tests/scope_page_host/route.cpp"), "-o", str(route)],
                       cwd=repo, check=True)
        flags = [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
            "-DLV_CONF_SKIP=1", "-DLV_KCONFIG_IGNORE=1", "-DLV_COLOR_DEPTH=16",
            "-DLV_COLOR_16_SWAP=0", "-DLV_MEM_SIZE=131072",
            "-DLV_MEM_CUSTOM=0", "-DLV_TICK_CUSTOM=0",
            "-DLV_USE_ASSERT_OBJ=1", "-DLV_USE_ASSERT_MEM_INTEGRITY=1",
            "-DLV_ASSERT_HANDLER_INCLUDE=<stdlib.h>", "-DLV_ASSERT_HANDLER=abort();",
            "-DLV_FONT_MONTSERRAT_12=1", "-DLV_FONT_MONTSERRAT_14=1",
            "-DLV_FONT_MONTSERRAT_16=1", "-DLV_FONT_MONTSERRAT_18=1",
            "-DLV_FONT_MONTSERRAT_24=1", "-DLV_THEME_DEFAULT_DARK=1",
            "-I", str(repo / "include"), "-I", str(repo / "lib/lvgl"),
            *map(str, sources), str(route), "-o", str(executable), "-lm",
        ]
        response = build / "compile.rsp"
        response.write_text("\n".join(
            '"' + flag.replace("\\", "/").replace('"', '\\"') + '"' for flag in flags),
            encoding="utf-8")
        print(f"Compiling {len(sources)} actual C translation units", flush=True)
        subprocess.run([str(args.compiler), "cc", "@" + str(response)], cwd=repo, check=True)
        subprocess.run([str(executable), str(ppm)], cwd=repo, check=True, timeout=60)
    try:
        from PIL import Image, PngImagePlugin
    except ImportError:
        print(f"Pillow unavailable; preview retained: {ppm}")
        return 0
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text("Description", "Actual src/scope_page.c rendered by LVGL 8.2 at 480x480")
    metadata.add_text("Input", "SIMULATED 250Hz sine at ADC pin; not a hardware measurement")
    with Image.open(ppm) as image:
        image.save(output, pnginfo=metadata)
    print(f"Actual LVGL scope preview: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
