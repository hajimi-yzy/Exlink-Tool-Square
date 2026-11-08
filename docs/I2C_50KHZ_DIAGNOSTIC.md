# Internal I2C 50 kHz diagnostic

Production remains at 100 kHz. This separate build changes only the internal
Wire clock to 50 kHz and adds the configured clock plus cumulative CST9220
read/error/recovery counters to the existing 10-second health log.

The internal bus is shared by CST9220, MCP4017, INA226, MPU6050, and AHT20 on
GPIO18/21. A single FreeRTOS worker owns runtime transactions, prioritizes
touch, and retains the existing error backoff and controller-only recovery.
The external I2C bridge uses Wire1 on GPIO6/7 and is unchanged.

## Why test 50 kHz

Slower edges reduce switching frequency and increase the time available for
rise/fall and sampling margins. This is a reversible diagnostic, not proof that
nearby routing is the cause. It cannot suppress a coupled glitch that crosses
the input threshold, repair ringing or ground bounce, or identify which PCB
trace is responsible. The ESP-IDF 4.4.6 driver already enables its hardware
glitch filter at the maximum documented setting of seven APB cycles, so this
test does not alter undocumented filter registers. It also does not add retries
to touch mailbox operations.

## A/B procedure

1. Use the current 100 kHz 1.0.22 firmware as baseline. Record the `health`
   lines for five minutes, including idle periods and ordinary touch use. Note
   starting and ending `up`, `touch=errors/reads`, `recoveries`, `io_max`, and
   `i2c_hz`.
2. Build and flash the diagnostic image with the commands below. Repeat the
   same five-minute sequence and compare error/read deltas and longest I2C
   service time. Check touch latency, MCP4017 output adjustment, INA readings,
   and the sensor pages as functional regressions.
3. Keep 50 kHz only if repeated runs show a clear reduction in communication
   errors without unacceptable touch or feature latency. Otherwise return to
   the default 100 kHz image.

Run from the project root and replace COM5 with the actual port:

```powershell
python -m platformio run -e esp32s3_i2c_50k_diag
python -m platformio run -e esp32s3_i2c_50k_diag -t upload --upload-port COM5
python -m platformio device monitor -p COM5 -b 115200 --dtr 0 --rts 0
```

The boot log should report `clock=50000Hz`; health logs report `i2c_hz=50000`.
`touch=errors/reads` and `recoveries` are cumulative since boot. Compare their
deltas over equal-duration runs; do not compare raw totals from runs of
different lengths.

## Limits

Reducing the I2C clock does not change the GPIO drive strength or edge rate, and
the neighboring bus remains an independent Wire1 peripheral. If errors do not
fall at 50 kHz, further software retries or a stronger digital filter would
mask evidence and could worsen touch latency. The next useful evidence would be
capturing SDA/SCL on the device side with a scope or logic analyzer while the
other I2C bus and QSPI are active, then checking pull-up resistance, rise time,
ringing, ground reference, and bus idle levels.
