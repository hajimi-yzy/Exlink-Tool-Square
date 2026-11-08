# CST9220 transport/recovery regression tests

`python scripts/run_touch_host_tests.py --compiler g++`

Alternatively pass `--zig C:/path/to/zig.exe`. The tests compile the production
driver, substituting only Arduino GPIO/delay and the Wire hardware interface.
They cover empty and malformed frames vs transport errors, explicit release,
partial enqueue/read, ACK failure/retry, address changes across recovery,
unknown/legacy firmware, and recovery without reinitializing the shared bus.
The runner also exercises the production MCP4017 controller.

These do not emulate the ESP-IDF driver or physical I2C signalling; firmware
builds and hardware validation remain necessary.
