"""Compile real LVGL + level_page.c, exercise it, and export its 480px display.

Run with Python and Pillow installed. No production lv_conf.h, ESP32 libraries,
or sensor/display hardware are used. The PPM is retained if Pillow is missing.
Example: python tests/level_page_host/run.py --compiler C:/path/to/zig.exe
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def production_function(source: str, name: str) -> str:
    match = re.search(r"(?m)^(?:static )?(?:(?:void|int|float|long|bool|uint16_t) |lv_obj_t \*)" + name + r"\([^;]*?\)\s*\{", source)
    if not match:
        raise ValueError(f"Production function not found: {name}")
    opening = source.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    default_zig = (
        os.environ.get("ZIG_EXE")
        or shutil.which("zig")
        or str(Path(os.environ.get("LOCALAPPDATA", "C:/Users/USER/AppData/Local"))
               / "Temp/exlink-level-host-tools/ziglang/zig.exe")
    )
    parser.add_argument("--compiler", type=Path, default=Path(default_zig))
    parser.add_argument("--output", type=Path,
                        default=repo / "deliverables/level-page-preview.png")
    parser.add_argument("--legacy-readers", action="store_true",
                        help="Prove the wrong-widget regression with original HEAD event readers (expected failure)")
    parser.add_argument("--legacy-power-readings", action="store_true",
                        help="Prove empty Power readings with original HEAD timer callbacks (expected failure)")
    args = parser.parse_args()
    if not args.compiler.is_file():
        parser.error(f"Zig compiler is not available yet: {args.compiler}")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    ppm = output.with_suffix(".ppm")
    sources = sorted((repo / "lib/lvgl/src").rglob("*.c"))
    sources += [repo / "src/level_page.c", repo / "tests/level_page_host/test.c"]
    with tempfile.TemporaryDirectory(prefix="exlink-level-host-") as temporary:
        build = Path(temporary)
        ui = (repo / "src/ui.c").read_text(encoding="utf-8")
        event = (repo / "src/event.c").read_text(encoding="utf-8")
        main_source = (repo / "src/main.cpp").read_text(encoding="utf-8")
        readers = (subprocess.check_output(["git", "show", "HEAD:src/event.c"], cwd=repo).decode("utf-8")
                   if args.legacy_readers else event)
        # Compile the exact shared lifecycle/reader/style functions, not copies
        # maintained by the harness. Other firmware and hardware stay outside it.
        actual = [production_function(ui, name) for name in
                  ("ui_prepare_page", "ensure_main_menu_styles", "clamp_menu_index",
                   "get_menu_button_by_index", "get_menu_index_by_button",
                   "get_slider_value_from_menu_index", "update_slider_timer")]
        actual += [production_function(readers, name) for name in
                   ("read_textarea_int", "read_textarea_float", "read_dropdown_baud",
                    "sync_persistent_inputs_from_ui")]
        actual.append(production_function(event, "slider_event_cb"))
        (build / "production_lifecycle.inc").write_text("\n\n".join(actual), encoding="utf-8")
        tool_helpers = [production_function(ui, name) for name in
                        ("ensure_ui_focus_styles", "apply_ui_focus_emphasis",
                         "ui_refresh_runtime_control", "runtime_toggle_event_cb", "create_runtime_toggle",
                         "suppress_ui_input_focus", "focus_obj_precedes", "sort_focus_chain_by_layout",
                         "find_first_focus_obj", "rebuild_focus_group_internal", "rebuild_focus_group",
                         "rebuild_focus_group_in_order",
                         "get_baud_dropdown_index")]
        tool_pages = [production_function(ui, name) for name in
                      ("power_init", "pwm_init", "servo_init", "uarthelper_init", "i2c_init",
                       "spi_init", "voltmeter_init", "DSO_init", "wirelessuart_init", "FREcount_init")]
        power_behavior = [production_function(main_source, "ina266_reset_measurement")]
        power_source = (subprocess.check_output(["git", "show", "HEAD:src/ui.c"], cwd=repo).decode("utf-8")
                        if args.legacy_power_readings else ui)
        power_behavior += [production_function(power_source, name) for name in
                           ("update_label_timer1", "update_label_timer2", "update_label_timer3",
                            "add_data", "add_data2")]
        power_behavior.append(production_function(ui, "ui_power_refresh_readings"))
        power_behavior.append(production_function(event, "poweronbtn_event_cb"))
        (build / "production_tool_pages.inc").write_text(
            "\n\n".join(tool_helpers + power_behavior + tool_pages), encoding="utf-8")
        executable = build / ("level_page_host.exe" if os.name == "nt" else "level_page_host")
        flags = [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
            "-DLV_CONF_SKIP=1", "-DLV_KCONFIG_IGNORE=1", "-DLV_COLOR_DEPTH=16",
            "-DLV_COLOR_16_SWAP=0", "-DLV_MEM_SIZE=131072",
            "-DLV_MEM_CUSTOM=0", "-DLV_TICK_CUSTOM=0",
            "-DLV_USE_ASSERT_OBJ=1", "-DLV_USE_ASSERT_MEM_INTEGRITY=1",
            "-DLV_ASSERT_HANDLER_INCLUDE=<stdlib.h>", "-DLV_ASSERT_HANDLER=abort();",
            "-DLV_FONT_MONTSERRAT_14=1", "-DLV_FONT_MONTSERRAT_20=1",
            "-DLV_FONT_MONTSERRAT_28=1",
            "-DLV_FONT_MONTSERRAT_12=1", "-DLV_FONT_MONTSERRAT_16=1",
            "-DLV_FONT_MONTSERRAT_18=1", "-DLV_FONT_MONTSERRAT_22=1",
            "-DLV_FONT_MONTSERRAT_24=1", "-DLV_FONT_MONTSERRAT_42=1", "-DLV_FONT_MONTSERRAT_44=1",
            "-DLV_THEME_DEFAULT_DARK=1",
            "-I", str(repo / "include"), "-I", str(repo / "lib/lvgl"), "-I", str(build),
            *map(str, sources), "-o", str(executable), "-lm",
        ]
        # Clang response files avoid Windows' command-length limit when all
        # actual LVGL translation units are included. Forward slashes prevent
        # backslashes in quoted Windows paths from becoming escape sequences.
        response = build / "compile.rsp"
        response.write_text("\n".join(
            '"' + flag.replace("\\", "/").replace('"', '\\"') + '"'
            for flag in flags
        ), encoding="utf-8")
        print(f"Compiling {len(sources)} actual C translation units with {args.compiler}", flush=True)
        subprocess.run([str(args.compiler), "cc", "@" + str(response)], cwd=repo, check=True)
        subprocess.run([str(executable), str(ppm)], cwd=repo, check=True, timeout=30)
    try:
        from PIL import Image, PngImagePlugin
    except ImportError:
        print(f"Pillow unavailable; real LVGL PPM preview retained at {ppm}")
        return 0
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text("Description", "Actual src/level_page.c rendered by LVGL 8.2 at 480x480")
    metadata.add_text("Sensor", "SIMULATED: roll +7.5 deg, pitch -3.2 deg, stable; not hardware data")
    with Image.open(ppm) as image:
        image.save(output, pnginfo=metadata)
    print(f"Actual LVGL preview: {output}")
    print("Sensor values are SIMULATED; this does not validate MPU6050 wiring or physical display output.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
