"""Run the real CST9220 driver against a deterministic fake I2C controller."""
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
with tempfile.TemporaryDirectory(prefix="exlink-touch-host-") as temporary:
    binary = Path(temporary) / "touch_test.exe"
    subprocess.run(compiler + ["-std=c++11", "-Wall", "-Wextra",
                              "-I", str(root / "tests/cst9220_host"),
                              "-I", str(root / "lib/CST9220/src"),
                              str(root / "tests/cst9220_host/test.cpp"),
                              str(root / "lib/CST9220/src/CST9220.cpp"),
                              "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

    power_binary = Path(temporary) / "power_test.exe"
    subprocess.run(compiler + ["-std=c++11", "-I", str(root / "include"),
                              str(root / "tests/mcp4017_control_test.cpp"),
                              "-o", str(power_binary)], check=True)
    subprocess.run([str(power_binary)], check=True)
