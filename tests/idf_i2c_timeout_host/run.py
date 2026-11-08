"""Run fault tests against the actual vendored IDF command-wait function.

Usage: python tests/idf_i2c_timeout_host/run.py [--cc clang]
       python tests/idf_i2c_timeout_host/run.py --zig /path/to/zig
Only a temporary directory receives generated sources and binaries.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="C compiler executable (default: CC or clang/gcc)")
    parser.add_argument("--zig", help="Zig executable; uses its cc subcommand")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    source = here.parents[1] / "vendor/esp-idf-v4.4.8/i2c.c"
    text = source.read_text(encoding="utf-8")
    # IDF's top-level function closing brace is at column zero. Extract the
    # complete function, including validation, mutex and cleanup, unmodified.
    match = re.search(
        r"^esp_err_t i2c_master_cmd_begin\([^\n]+\)\n\{\n.*?^\}",
        text, re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise SystemExit(f"Cannot extract i2c_master_cmd_begin from {source}")
    if args.zig:
        compiler = [args.zig, "cc"]
    else:
        cc = args.cc or os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not cc:
            raise SystemExit("No C compiler found; pass --cc or --zig")
        compiler = [cc]
    with tempfile.TemporaryDirectory(prefix="exlink-idf-i2c-host-") as tmp:
        build = Path(tmp)
        (build / "driver_function.inc").write_text(match.group(0) + "\n", encoding="utf-8")
        executable = build / ("test.exe" if os.name == "nt" else "test")
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(build), str(here / "test.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
