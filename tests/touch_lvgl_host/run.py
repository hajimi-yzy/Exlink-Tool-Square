"""Run actual LVGL with touch callbacks extracted from production main.cpp.

The I2C queue and clock are mocked; no controller, USB or GPIO is accessed.
--expect-legacy restores timeout-as-release in the temporary extracted callback
to reproduce its unwanted CLICKED event.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def extract_function(source, name):
    match = re.search(r"^(?:static\s+)?(?:void|bool)\s+" + re.escape(name) +
                      r"\([^)]*\)\s*\{", source, re.MULTILINE)
    if not match:
        raise ValueError(f"Production function not found: {name}")
    opening = source.index("{", match.start())
    depth, end = 1, opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[match.start():end]


repo = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", type=Path, default=Path(
    os.environ.get("ZIG_EXE", "C:/Users/USER/AppData/Local/Temp/exlink-level-host-tools/ziglang/zig.exe")))
parser.add_argument("--expect-legacy", action="store_true")
args = parser.parse_args()
source = (repo / "src/main.cpp").read_text(encoding="utf-8-sig")
functions = "\n\n".join(extract_function(source, name) for name in
                          ("clear_touch_contact", "consume_touch_sample", "my_touchpad_read"))
if args.expect_legacy:
    # Deliberate regression mutant: change only timeout cancellation back
    # to the former ordinary release. All other callbacks remain extracted.
    timeout = re.compile(
        r"if \(touch_contact_down && millis\(\) - touch_contact_sample_ms >= 75U\) \{"
        r"\s*touch_contact_cancelled = true;\s*touch_contact_down = false;\s*\}")
    functions, count = timeout.subn(
        "if (millis() - touch_contact_sample_ms >= 75U) touch_contact_down = false;",
        functions)
    if count != 1:
        raise ValueError("Legacy mutant requires the production 75ms timeout branch")
sample_header = (repo / "include/internal_i2c_service.h").read_text(encoding="utf-8-sig")
sample = re.search(r"struct TouchSample\s*\{.*?\n\};", sample_header, re.DOTALL)
if not sample:
    raise ValueError("Production TouchSample structure not found")
sources = sorted((repo / "lib/lvgl/src").rglob("*.c"))
with tempfile.TemporaryDirectory(prefix="exlink-touch-lvgl-") as temporary:
    build = Path(temporary)
    (build / "production_touch_sample.h").write_text(
        "namespace exlink {\n" + sample.group() + "\n}\n", encoding="utf-8")
    (build / "production_touch_functions.inc").write_text(functions, encoding="utf-8")
    binary = build / "touch_lvgl_test.exe"
    test_object = build / "touch_lvgl_test.obj"
    common_flags = ["-O1", "-g", "-UNDEBUG", "-DLV_CONF_SKIP=1", "-DLV_KCONFIG_IGNORE=1",
             "-DLV_COLOR_DEPTH=16", "-DLV_MEM_SIZE=131072", "-DLV_MEM_CUSTOM=0",
             "-DLV_TICK_CUSTOM=0", "-I", str(build), "-I", str(repo / "include"),
             "-I", str(repo / "src"),
             "-I", str(repo / "lib/lvgl")]
    # Compile C++ separately: Zig's Windows driver does not reliably retain
    # per-file language changes when C and C++ are mixed in one invocation.
    subprocess.run([str(args.compiler), "c++", "-std=c++11", *common_flags,
                    "-c", str(repo / "tests/touch_lvgl_host/test.cpp"),
                    "-o", str(test_object)], cwd=repo, check=True)
    flags = ["-std=c11", *common_flags, *map(str, sources), str(test_object),
             "-o", str(binary), "-lm"]
    response = build / "compile.rsp"
    response.write_text("\n".join('"' + value.replace("\\", "/").replace('"', '\\"') + '"'
                                  for value in flags), encoding="utf-8")
    print(f"Compiling actual LVGL ({len(sources)} C sources) + extracted production callbacks", flush=True)
    subprocess.run([str(args.compiler), "cc", "@" + str(response)], cwd=repo, check=True)
    subprocess.run([str(binary), *(["--legacy"] if args.expect_legacy else [])],
                   cwd=repo, check=True, timeout=30)
