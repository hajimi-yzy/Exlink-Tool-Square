"""Fault-inject the real startup/sleep functions extracted from main.cpp.

GPIO, panel, I2C and ESP sleep calls are mocks. This checks control flow and
cleanup, not ESP32 electrical behavior or real light-sleep power consumption.
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
    match = re.search(r"^(?:static\s+)?(?:void|uint8_t)\s+" + re.escape(name) +
                      r"\([^)]*\)\s*\{", source, re.MULTILINE)
    if not match:
        raise ValueError("Production function missing: " + name)
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main() -> None:
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("ZIG_EXE") or shutil.which("zig"))
    args = parser.parse_args()
    if not args.compiler or not shutil.which(args.compiler):
        parser.error("Zig unavailable; set ZIG_EXE or pass --compiler /path/to/zig")
    source = (repo / "src/main.cpp").read_text(encoding="utf-8-sig")
    production = "\n\n".join(function(source, name) for name in (
        "co5300_power_sequence", "restore_active_display",
        "enter_display_sleep", "initialize_ina_at_boot"))
    with tempfile.TemporaryDirectory(prefix="exlink-sleep-host-") as temporary:
        build = Path(temporary)
        (build / "production_sleep.inc").write_text(production, encoding="utf-8")
        binary = build / ("sleep_test.exe" if os.name == "nt" else "sleep_test")
        subprocess.run([str(args.compiler), "c++", "-std=c++17", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                        "-I", str(build), "-I", str(repo / "include"),
                        str(repo / "tests/sleep_wake_host/test.cpp"),
                        "-o", str(binary)], check=True, timeout=120)
        subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    main()
