"""Compile and execute production UpdateReceiver code with a host C++ compiler.

Examples: python scripts/run_device_update_host_tests.py --compiler g++
          python scripts/run_device_update_host_tests.py --zig C:/tools/zig.exe
"""
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
with tempfile.TemporaryDirectory(prefix="exlink-receiver-") as temporary:
    binary = Path(temporary) / "receiver_test.exe"
    subprocess.run(compiler + ["-std=c++11", "-I", str(root / "include"),
                              str(root / "tests/device_update_host.cpp"),
                              str(root / "src/device_update.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    transport_binary = Path(temporary) / "ble_update_test.exe"
    subprocess.run(compiler + ["-std=c++11", "-I", str(root / "tests/ble_update_host/stubs"),
                              "-I", str(root / "include"),
                              str(root / "tests/ble_update_host/test.cpp"),
                              str(root / "src/ble_update_service.cpp"),
                              str(root / "src/device_update.cpp"), "-o", str(transport_binary)], check=True)
    subprocess.run([str(transport_binary)], check=True)
