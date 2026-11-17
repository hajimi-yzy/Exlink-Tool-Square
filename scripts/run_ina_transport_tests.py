"""Exercise the actual INA library, including ESP32 EEPROM branches."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", default="g++")
parser.add_argument("--zig", default=os.environ.get("ZIG_EXE") or shutil.which("zig"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
compiler = [args.zig, "c++"] if args.zig else [args.compiler]
with tempfile.TemporaryDirectory(prefix="exlink-ina-transport-") as temporary:
    binary = Path(temporary) / "ina_test.exe"
    subprocess.run(compiler + ["-std=c++11", "-DARDUINO=100", "-DESP32", "-UNDEBUG",
                              "-I", str(root / "tests/ina_transport_host"),
                              "-I", str(root / "lib/INA-master/src"),
                              str(root / "tests/ina_transport_host/test.cpp"),
                              str(root / "lib/INA-master/src/INA.cpp"),
                              "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
