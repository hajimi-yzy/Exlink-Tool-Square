"""Exercise actual scope analysis as C11, without ADC or display hardware."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", default="cc")
parser.add_argument("--zig")
args = parser.parse_args()
zig = args.zig or shutil.which("zig")
if not zig and not shutil.which(args.compiler):
    candidate = Path(os.environ.get("LOCALAPPDATA", "C:/Users/USER/AppData/Local")) / "Temp/exlink-level-host-tools/ziglang/zig.exe"
    if candidate.is_file():
        zig = str(candidate)
compiler = [zig, "cc"] if zig else [args.compiler]
with tempfile.TemporaryDirectory(prefix="exlink-scope-math-") as temporary:
    binary = Path(temporary) / ("scope_math_test.exe" if os.name == "nt" else "scope_math_test")
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                              "-I", str(root / "include"),
                              str(root / "tests/scope_math_test.c"), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
