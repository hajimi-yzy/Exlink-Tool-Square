"""Compile REAL scope_capture.cpp with threaded FreeRTOS and explicit ADC mocks.

This tests worker/control/resource behavior, not physical ESP32 ADC/DMA hardware.
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
    args = parser.parse_args()
    if not args.compiler.is_file():
        parser.error(f"Zig is unavailable: {args.compiler}")
    test_dir = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="exlink-scope-capture-host-") as temporary:
        binary = Path(temporary) / ("capture.exe" if os.name == "nt" else "capture")
        command = [str(args.compiler), "c++", "-std=c++17", "-O1", "-g",
                   "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-pthread",
                   "-I", str(test_dir), "-I", str(repo / "include"),
                   "-I", str(repo / "src"), str(repo / "src/scope_capture.cpp"),
                   str(test_dir / "test.cpp"), "-o", str(binary)]
        print("Compiling unchanged src/scope_capture.cpp against threaded ADC/RTOS mocks", flush=True)
        subprocess.run(command, cwd=repo, check=True, timeout=120)
        subprocess.run([str(binary)], cwd=repo, check=True, timeout=20)
        for failure in ("stop", "deinit", "configure-deinit", "start-deinit"):
            subprocess.run([str(binary), failure], cwd=repo, check=True, timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
