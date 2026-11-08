# ESP-IDF I2C deadline fix

`i2c.c` is the unmodified Apache-2.0 source from:

https://github.com/espressif/esp-idf/blob/v4.4.8/components/driver/i2c.c

Raw SHA-256:
`3d01c6bbe547758a817454095b18575f7bbb78db5065900643e7f648e5b381e9`

Compared with the pinned Arduino SDK's ESP-IDF v4.4.6 source, the only changes
are the copyright year, `const` on `ticks_start`, and the event-wait deadline
fix in `i2c_master_cmd_begin`. It clamps waits to the remaining caller budget
instead of extending short timeouts to at least 1000ms. When the budget is
exhausted, a zero-time wait still consumes any already queued DONE event.
The upstream driver retains its mutex, ISR shutdown, FSM reset and cleanup.

`src/idf_i2c_timeout_backport.c` builds this driver as a direct project object.
Its public symbols satisfy the SDK I2C references, so the old `libdriver.a`
I2C object must not be pulled in. Check the firmware map: all I2C driver
symbols should resolve to `idf_i2c_timeout_backport.c.o`, with no
`libdriver.a(i2c.c.obj)` implementation. The wrapper refuses other targets or
SDK versions, so an upgrade requires reviewing/removing this backport.

This changes both hardware I2C ports because they share the same SDK driver.
It does not alter their GPIO routing, clock rates, peripherals or protocols.
No globally installed PlatformIO/Arduino files are modified.

The clock-stretch setting added in 1.0.11 alone did not eliminate live
1002ms waits. This backport fixes the caller deadline itself. It does not
repair electrical faults or the MPU6050's abnormal stationary data.
