"""Compile complete production ui.c/scope_page.c against real LVGL.

Hardware reads and event actions are stubbed; construction, translation-unit
macros, static helpers, headers, assets and geometry are production code.
Requires Python, Zig and (for PNG previews only) Pillow. No ESP32 SDK needed.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path(
        os.environ.get("ZIG_EXE") or shutil.which("zig") or
        "C:/Users/USER/AppData/Local/Temp/exlink-level-host-tools/ziglang/zig.exe"))
    parser.add_argument("--output", type=Path, default=repo / "deliverables/ui-native-host")
    parser.add_argument("--ui-source", type=Path, default=repo / "src/ui.c")
    parser.add_argument("--record-baseline", action="store_true",
                        help="Record pre-migration geometry, excluding the known-bad boot page")
    parser.add_argument("--baseline", type=Path,
                        default=Path(__file__).with_name("baseline-font20"))
    args = parser.parse_args()
    if not args.compiler.is_file():
        parser.error(f"Zig unavailable: {args.compiler}")
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(repo / "tests/boot_page_host"))
    from run import assert_native_geometry, assert_production_context, function
    ui_source = args.ui_source.resolve()
    if not args.record_baseline:
        assert_native_geometry(ui_source.read_text(encoding="utf-8-sig"))
        assert_production_context(repo)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    default_font = re.search(r"^#define LV_FONT_DEFAULT\s+([^\r\n]+)",
        (repo / "lib/lvgl/lv_conf.h").read_text(encoding="utf-8-sig"), re.MULTILINE)
    assert default_font, "Production LV_FONT_DEFAULT missing"
    sources = sorted((repo / "lib/lvgl/src").rglob("*.c"))
    sources += [ui_source, repo / "src/scope_page.c", repo / "src/level_page.c",
                repo / "src/environment_page.c", repo / "src/main_png.c",
                repo / "src/boot_artwork.c", Path(__file__).with_name("stubs.c"),
                Path(__file__).with_name("test.c")]
    with tempfile.TemporaryDirectory(prefix="exlink-ui-native-host-") as temporary:
        build = Path(temporary)
        # Only non-UI event actions are inert. Boot animation callbacks are exact
        # production code, with the production ui.h/event.h declarations in scope.
        events = (repo / "src/event.c").read_text(encoding="utf-8-sig")
        if not args.record_baseline:
            assert_native_geometry(events, "src/event.c")
        declarations = (repo / "src/event.h").read_text(encoding="utf-8-sig")
        callbacks = re.findall(r"^void (\w+)\(lv_event_t \*\w+\);", declarations, re.MULTILINE)
        stubs = [f"void {name}(lv_event_t *event) {{ (void)event; }}" for name in callbacks]
        stubs += [function(events, name) for name in ("anim_cb1", "anim_cb2", "anim_end_callback")]
        (build / "event_stubs.inc").write_text("\n\n".join(stubs), encoding="utf-8")
        executable = build / ("ui_native_host.exe" if os.name == "nt" else "ui_native_host")
        flags = ["-std=c11", "-O1", "-g", "-fcommon", "-UNDEBUG",
            "-DLV_CONF_SKIP=1", "-DLV_KCONFIG_IGNORE=1", "-DLV_COLOR_DEPTH=16",
            "-DLV_COLOR_16_SWAP=0", "-DLV_MEM_SIZE=524288",
            "-DLV_MEM_CUSTOM=0", "-DLV_TICK_CUSTOM=0", "-DLV_THEME_DEFAULT_DARK=1",
            "-DLV_FONT_DEFAULT=" + default_font[1].strip(),
            "-DLV_USE_ASSERT_OBJ=1", "-DLV_USE_ASSERT_MEM_INTEGRITY=1",
            "-DLV_ASSERT_HANDLER_INCLUDE=<stdlib.h>", "-DLV_ASSERT_HANDLER=abort();",
            *[f"-DLV_FONT_MONTSERRAT_{size}=1" for size in
              (10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 36, 42, 44, 48)],
            "-I", str(Path(__file__).parent), "-I", str(repo / "src"),
            "-I", str(repo / "include"), "-I", str(repo / "lib/lvgl"), "-I", str(build),
            *map(str, sources), "-o", str(executable), "-lm"]
        response = build / "compile.rsp"
        response.write_text("\n".join(
            '"' + flag.replace("\\", "/").replace('"', '\\"') + '"' for flag in flags),
            encoding="utf-8")
        print(f"Compiling complete ui.c + scope_page.c + real LVGL ({len(sources)} C files)", flush=True)
        subprocess.run([str(args.compiler), "cc", "@" + str(response)], cwd=repo, check=True)
        subprocess.run([str(executable), str(output),
                        "baseline" if args.record_baseline else "native"],
                       cwd=repo, check=True, timeout=60)
    failures = {}
    for name in ("menu", "power", "settings", "scope", "pinmap", "pwm", "servo",
                 "uart", "i2c", "spi", "voltmeter", "wireless", "frequency", "readme",
                 "wireless-uart", "wireless-i2c", "wireless-listen", "wireless-spi"):
        actual = json.loads((output / f"{name}.json").read_text())
        baseline = args.baseline / f"{name}.json"
        if args.record_baseline:
            args.baseline.mkdir(parents=True, exist_ok=True)
            if baseline.exists():
                assert json.loads(baseline.read_text(encoding="utf-8")) == actual, (
                    f"Refusing to replace different baseline: {baseline}")
            else:
                baseline.write_text("[\n" + ",\n".join(
                    json.dumps(obj, separators=(",", ":")) for obj in actual) + "\n]\n",
                    encoding="utf-8")
        else:
            expected = json.loads(baseline.read_text(encoding="utf-8"))
            if name == "menu":
                # Intentional insertion: Level and Environment precede 2048.
                # Keep the original baseline immutable. Compare every old
                # object after undoing exactly two 160px cards + 10px gaps.
                original_objects = []
                for obj in actual:
                    match = re.fullmatch(r"screen/0/(\d+)(/.*)?", obj["path"])
                    if match and 11 <= int(match[1]) <= 12:
                        continue
                    obj = dict(obj)
                    if match and int(match[1]) >= 13:
                        obj["path"] = f"screen/0/{int(match[1]) - 2}{match[2] or ''}"
                        obj["y"] -= 340
                    original_objects.append(obj)
                actual = original_objects
            differences = [(old, new) for old, new in zip(expected, actual) if old != new]
            if len(actual) != len(expected) or differences:
                failures[name] = {"expected_count": len(expected), "actual_count": len(actual),
                                  "differences": differences}
                print(f"FAIL: {name} geometry changed: count {len(expected)} -> {len(actual)}; "
                      f"first differences={differences[:4]}")
            else:
                print(f"PASS: {name} geometry matches pre-migration complete-source baseline ({len(actual)} objects)")
    (output / "geometry-differences.json").write_text(
        json.dumps(failures, indent=2) + "\n", encoding="utf-8")
    try:
        from PIL import Image
    except ImportError:
        print(f"Pillow unavailable; real LVGL PPM previews retained: {output}")
    else:
        for ppm in output.glob("*.ppm"):
            with Image.open(ppm) as image:
                image.save(ppm.with_suffix(".png"))
    print(f"Complete-source geometry and previews: {output}")
    assert not failures, f"Native geometry differs; see {output / 'geometry-differences.json'}"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
