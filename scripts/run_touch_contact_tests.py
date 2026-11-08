"""Run the production touch-contact policy and read schedule without hardware."""
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
with tempfile.TemporaryDirectory(prefix="exlink-touch-contact-") as temporary:
    for name in ("touch_contact_policy", "touch_read_schedule"):
        binary = Path(temporary) / (name + "_test.exe")
        subprocess.run(compiler + ["-std=c++11", "-Wall", "-Wextra", "-Werror",
                                  "-UNDEBUG", "-I", str(root / "include"),
                                  str(root / "tests" / (name + "_test.cpp")),
                                  "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
