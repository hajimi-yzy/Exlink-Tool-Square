"""Exercise production I2C-result dispatch and measurement consumption.

Only the asynchronous queue/clock and power-result logging are mocked. The
consumer, invalidation and integration functions are extracted from main.cpp.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"^(?:static\s+)?void\s+" + re.escape(name) +
                      r"\([^)]*\)[^\n]*\n\{", source, re.MULTILINE)
    if not match:
        raise ValueError("Production function missing: " + name)
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("ZIG_EXE") or shutil.which("zig"))
    parser.add_argument("--expect-legacy", action="store_true",
                        help="Prove the test catches the old stale-value branch")
    args = parser.parse_args()
    if not args.compiler or not shutil.which(args.compiler):
        parser.error("Zig unavailable; set ZIG_EXE or pass --compiler /path/to/zig")
    source = (repo / "src/main.cpp").read_text(encoding="utf-8-sig")
    production = "\n\n".join(function(source, name) for name in (
        "ina266_reset_measurement", "internal_io_results_task", "ina266_task"))
    if args.expect_legacy:
        production, count = re.subn(r"else ina266_reset_measurement\(\);", "", production)
        if count != 1:
            raise ValueError("Legacy mutant requires the production cancellation branch")
    header = (repo / "include/internal_i2c_service.h").read_text(encoding="utf-8-sig")
    structures = []
    for definition in (r"enum class IoRequestKind\s*:\s*uint8_t", r"struct IoResult"):
        match = re.search(definition + r"\s*\{.*?\n\};", header, re.DOTALL)
        if not match:
            raise ValueError("Production I2C result definition missing: " + definition)
        structures.append(match.group())
    with tempfile.TemporaryDirectory(prefix="exlink-io-results-") as temporary:
        build = Path(temporary)
        (build / "production_io_types.h").write_text(
            "namespace exlink {\n" + "\n".join(structures) + "\n}\n", encoding="utf-8")
        (build / "production_io_results.inc").write_text(production, encoding="utf-8")
        binary = build / "io_results.exe"
        subprocess.run([str(args.compiler), "c++", "-std=c++11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                        "-I", str(build), "-I", str(repo / "include"),
                        str(repo / "tests/io_results_host/test.cpp"),
                        "-o", str(binary)], check=True, timeout=120)
        subprocess.run([str(binary), *(["--legacy"] if args.expect_legacy else [])],
                       check=True, timeout=20)


if __name__ == "__main__":
    main()
