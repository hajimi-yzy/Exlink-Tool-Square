"""Compile the real internal I2C worker and sensor drivers with fake peripherals."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", default="g++")
parser.add_argument("--zig")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
compiler = [args.zig, "c++"] if args.zig else [args.compiler]
with tempfile.TemporaryDirectory(prefix="exlink-internal-i2c-") as temporary:
    binary = Path(temporary) / "internal_i2c_test.exe"
    subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                              "-UNDEBUG", "-I", str(root / "tests/internal_i2c_host"),
                              "-I", str(root / "include"),
                              str(root / "tests/internal_i2c_host/test.cpp"),
                              str(root / "src/internal_i2c_service.cpp"),
                              "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
