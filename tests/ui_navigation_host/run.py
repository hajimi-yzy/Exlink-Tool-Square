"""Stress real LVGL using production pages, event.c and extracted keypad_read.

Only GPIO/time, hardware output, sensor and transport services are simulated.
Run with --compiler /path/to/zig.exe. No flashing or source rewriting.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def function(source: str, name: str) -> str:
    match = re.search(r"^(?:static\s+)?(?:bool|void)\s+" + name + r"\([^;]*?\)\s*(?://[^\n]*)?\s*\{", source, re.M)
    if not match:
        raise ValueError(name)
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("ZIG_EXE") or shutil.which("zig"))
    args = parser.parse_args()
    if not args.compiler or not shutil.which(args.compiler):
        parser.error("Zig unavailable; set ZIG_EXE or pass --compiler /path/to/zig")
    with tempfile.TemporaryDirectory(prefix="exlink-nav-host-") as tmp:
        build = Path(tmp)
        main_source = (repo / "src/main.cpp").read_text(encoding="utf-8-sig")
        definitions = []
        for name in ("RIGHT_PIN", "LEFT_PIN", "PUSH_PIN", "LONG_PRESS_THRESHOLD", "SHORT_PRESS_THRESHOLD"):
            match = re.search(r"^#define\s+" + name + r"\s+(\d+)", main_source, re.M)
            assert match, name
            definitions.append("#define " + name + " " + match[1])
        names = ("any_key_down", "is_servo_slider_key_control_target", "is_servo_slider_key_control_active", "keypad_read")
        (build / "production_keypad.inc").write_text("\n".join(definitions + [function(main_source, name) for name in names]), encoding="utf-8")
        sources = sorted((repo / "lib/lvgl/src").rglob("*.c"))
        sources += [repo / "src" / name for name in ("ui.c", "event.c", "game2048.c", "scope_page.c", "level_page.c", "environment_page.c", "main_png.c", "boot_artwork.c")]
        sources += [Path(__file__).with_name("test.c")]
        flags = ["-std=c11", "-O1", "-g", "-fcommon", "-UNDEBUG", "-DLV_CONF_SKIP=1", "-DLV_KCONFIG_IGNORE=1", "-DLV_COLOR_DEPTH=16", "-DLV_COLOR_16_SWAP=0", "-DLV_MEM_SIZE=524288", "-DLV_MEM_CUSTOM=0", "-DLV_TICK_CUSTOM=0", "-DLV_THEME_DEFAULT_DARK=1", "-DLV_FONT_DEFAULT=&lv_font_montserrat_20", "-DLV_USE_ASSERT_OBJ=1", "-DLV_USE_ASSERT_MEM_INTEGRITY=1", "-DLV_ASSERT_HANDLER_INCLUDE=<stdlib.h>", "-DLV_ASSERT_HANDLER=abort();"]
        flags += [f"-DLV_FONT_MONTSERRAT_{size}=1" for size in (10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 36, 42, 44, 48)]
        for path in (Path(__file__).parent, repo / "src", repo / "include", repo / "lib/lvgl", build):
            flags += ["-I", str(path)]
        executable = build / ("navigation.exe" if os.name == "nt" else "navigation")
        flags += [*map(str, sources), "-o", str(executable), "-lm"]
        response = build / "compile.rsp"
        response.write_text("\n".join('"' + flag.replace("\\", "/").replace('"', '\\"') + '"' for flag in flags), encoding="utf-8")
        print("Compiling real LVGL/pages/events + production keypad", flush=True)
        subprocess.run([str(args.compiler), "cc", "@" + str(response)], cwd=repo, check=True)
        subprocess.run([str(executable)], cwd=repo, check=True, timeout=180)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
