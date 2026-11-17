"""Run host reliability regressions; no board, USB or ESP32 SDK is required.

Install Zig 0.13.0, then use --compiler /path/to/zig, ZIG_EXE, or PATH.
Hardware timing, charging, electrical interference and real sleep need a board.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("ZIG_EXE") or shutil.which("zig"),
                        help="Zig executable (default: ZIG_EXE or PATH)")
    parser.add_argument("--output", type=Path, default=Path("deliverables/reliability-tests"),
                        help="Logs, summary and LVGL previews directory")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler) if args.compiler else None
    if not compiler:
        parser.error("Zig unavailable; set ZIG_EXE or pass --compiler /path/to/zig")
    compiler = str(Path(compiler).resolve())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = [
        ("touch_driver_and_power", "scripts/run_touch_host_tests.py", "--zig", compiler),
        ("touch_contact_and_schedule", "scripts/run_touch_contact_tests.py", "--zig", compiler),
        ("mpu_driver", "scripts/run_level_tests.py", "--zig", compiler),
        ("aht_driver", "scripts/run_aht20_tests.py", "--zig", compiler),
        ("pwm_servo_handoff", "scripts/run_ledc_handoff_tests.py", "--zig", compiler),
        ("scope_analysis", "scripts/run_scope_math_tests.py", "--zig", compiler),
        ("ble_update_transactions", "scripts/run_device_update_host_tests.py", "--zig", compiler),
        ("scope_capture", "tests/scope_capture_host/run.py", "--compiler", compiler),
        ("scope_ui", "tests/scope_page_host/run.py", "--compiler", compiler,
         "--output", str(output / "scope-preview.png")),
        ("touch_lvgl", "tests/touch_lvgl_host/run.py", "--compiler", compiler),
        ("native_ui", "tests/ui_native_host/run.py", "--compiler", compiler,
         "--output", str(output / "ui-preview")),
        ("navigation_stress", "tests/ui_navigation_host/run.py", "--compiler", compiler),
        ("sleep_wake", "tests/sleep_wake_host/run.py", "--compiler", compiler),
        ("internal_i2c", "scripts/run_internal_i2c_tests.py", "--zig", compiler),
        ("ina_transport", "scripts/run_ina_transport_tests.py", "--zig", compiler),
        ("display_contracts", "scripts/check_display_contracts.py"),
        ("measurement_results", "tests/io_results_host/run.py", "--compiler", compiler),
        ("legacy_measurement_detection", "tests/io_results_host/run.py", "--compiler", compiler,
         "--expect-legacy"),
    ]
    environment = dict(os.environ, PYTHONUNBUFFERED="1", PYTHONUTF8="1")

    def run(case):
        name, *command = case
        started = time.monotonic()
        try:
            process = subprocess.run([sys.executable, *command], cwd=root,
                                     env=environment, capture_output=True, timeout=600)
            status, log = process.returncode, process.stdout + process.stderr
        except subprocess.TimeoutExpired as error:
            status = 124
            log = (error.stdout or b"") + (error.stderr or b"") + b"\nTIMEOUT after 600s\n"
        except OSError as error:
            status, log = 1, str(error).encode("utf-8")
        (output / (name + ".log")).write_bytes(log)
        result = {"name": name, "exit_code": status,
                  "seconds": round(time.monotonic() - started, 2), "log": name + ".log"}
        print(f"{'PASS' if status == 0 else 'FAIL'} {name} ({result['seconds']}s)", flush=True)
        if status:
            print(log.decode("utf-8", "replace")[-2400:], flush=True)
        return result

    print("Running 17 host regression groups and one legacy-regression detection check", flush=True)
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = {pool.submit(run, case): index for index, case in enumerate(cases)}
        results = [None] * len(cases)
        for future in as_completed(futures):
            results[futures[future]] = future.result()
    summary = {"groups": 17, "checks": len(results),
               "passed": sum(case["exit_code"] == 0 for case in results),
               "results": results}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"{summary['passed']}/{summary['checks']} checks passed; logs: {output}", flush=True)
    return int(summary["passed"] != summary["checks"])


if __name__ == "__main__":
    raise SystemExit(main())
