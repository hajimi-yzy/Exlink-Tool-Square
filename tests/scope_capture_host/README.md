# Capture worker host regression

Run `python tests/scope_capture_host/run.py` (or pass `--compiler /path/to/zig`).
The runner compiles the unchanged `src/scope_capture.cpp` and real `scope_math.h`.
Arduino critical sections use mutexes, the FreeRTOS worker uses a C++ thread and
notifications, and the ADC/calibration APIs are explicit mocks.

Checks cover STOP returning while a read is held, ADC ownership through teardown,
rapid stop/start/reconfiguration, frame sequence and contents, quiesce timeout and
resume, transient read errors, persistent read/init/configure/start failures, and
an older configuration failure racing a newer START. Resource counts must balance
across 25 restart cycles. Deterministic read/configuration/teardown gates exercise
the generation races without depending on thread scheduling. The ADC mock rejects
overlapping or out-of-order initialization/start/stop/deinitialization.

Four isolated process cases inject stop/deinitialize errors (including cleanup
after configuration/start failure). They verify retained ADC ownership, ERROR,
failed quiesce, blocked START/resume, and exactly one teardown attempt. The mock
keeps its allocation on failed deinitialize; these cases intentionally end with
the simulated controller reserved, matching production's reboot requirement.

This is not a real DMA, ESP32 scheduling, analogue accuracy, or physical hardware
test. The mock calibration maps synthetic raw values directly to millivolts.
