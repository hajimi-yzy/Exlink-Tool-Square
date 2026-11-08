"""Run unchanged production PWM/servo task bodies against a small LEDC mock."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", default="g++")
parser.add_argument("--zig")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
compiler = [args.zig, "c++"] if args.zig else [args.compiler]
source = (root / "src/main.cpp").read_text(encoding="utf-8")
functions = []
for signature in ("static bool configure_pwm_channel", "void pwm_task", "void servo_task"):
    match = re.search(r"(?m)^" + re.escape(signature) + r"\([^\n]*\n\{.*?^\}",
                      source, re.DOTALL)
    if not match:
        raise RuntimeError("Cannot extract production function: " + signature)
    functions.append(match.group(0))

with tempfile.TemporaryDirectory(prefix="exlink-ledc-handoff-") as temporary:
    directory = Path(temporary)
    (directory / "production_tasks.inc").write_text("\n\n".join(functions), encoding="utf-8")
    binary = directory / "ledc_handoff_test.exe"
    subprocess.run(compiler + ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                              "-I", str(directory), str(root / "tests/ledc_handoff_test.cpp"),
                              "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
