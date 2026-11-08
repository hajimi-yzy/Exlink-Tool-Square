# Internal I2C worker host integration tests

`scripts/run_internal_i2c_tests.py` compiles the production
`src/internal_i2c_service.cpp`, its real MPU/AHT state machines, and the real
MCP4017 controller. Only Arduino/Wire, CST9220, INA, and FreeRTOS are replaced.
The fake task creation captures the real task callback; fake notification waits
advance a deterministic clock and execute test actions. An exception terminates
the otherwise infinite production worker at each scenario's deadline.

Scenarios cover continuous touch with both sensors, legacy MCP/INA request
completion, absent sensor timeout/backoff, page deactivation, sleep and OTA
pause/resume, a page closing/reopening while actual Wire data is in flight,
freshness/stability checks for zeroing, and corrupt AHT CRC handling. No private
service state is exposed and no copy of its scheduling code is tested.

Run with a desktop C++ compiler:

```text
python scripts/run_internal_i2c_tests.py --compiler g++
python scripts/run_internal_i2c_tests.py --zig C:/path/to/zig.exe
```

These tests verify the worker's deterministic logic and bounded driver calls.
The fake Wire does not reproduce ESP-IDF recovery timing, actual electrical
faults, interrupt latency, sensor orientation, or physical calibration quality.
The existing real CST9220 host suite covers its packet handling separately.
