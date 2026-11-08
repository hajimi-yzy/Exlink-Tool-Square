"""Run actual MPU6050 driver/filter tests without sensor hardware."""
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
with tempfile.TemporaryDirectory(prefix="exlink-level-driver-") as temporary:
    binary = Path(temporary) / "level_test.exe"
    subprocess.run(compiler + ["-std=c++11", "-Wall", "-Wextra", "-Werror",
                              "-UNDEBUG", "-I", str(root / "include"),
                              str(root / "tests/mpu6050_level_test.cpp"),
                              "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
