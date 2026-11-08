# Complete-source native geometry regression

Run `python tests/ui_native_host/run.py`. It compiles complete production
`src/ui.c`, `src/scope_page.c`, assets and real LVGL C sources. No function
extraction or copied page geometry is used for UI construction. The generated
event stubs make hardware actions inert; the three boot animation callbacks
are taken unchanged from `src/event.c`. Arduino is limited to two inline stubs
(`analogReadMilliVolts` and `digitalWrite`).

Dependencies: Python 3, Zig (`--compiler` or `ZIG_EXE`), optional Pillow for PNG
conversion. The `-fcommon` flag is needed because production `ui.h` defines
tentative globals. Enabled Montserrat sizes match every font referenced by
the complete production translation units. ESP32/Arduino SDKs are unnecessary.

The checked-in `baseline-font20` was recorded from the complete pre-migration `ui.c`
on 2026-10-26 (SHA256
`2a93669b078339cfc31b4da04fdc6d2d66f4fdfe98478193197e24ab5bd99453`).
The preserved source was
`C:/Users/USER/AppData/Local/Temp/exlink-native-baseline-622b289a3803405abe368fe3444aee14/ui.c`.
The first four pages and the fourteen later page/mode snapshots were recorded
from that same source. The initial host used the LVGL internal default font;
that configuration was corrected to the production `LV_FONT_DEFAULT` (20),
read directly from `lib/lvgl/lv_conf.h`, and every baseline was regenerated
from the saved old source. This corrects a test configuration mismatch; it
does not accept new production geometry. The incorrectly configured snapshots
are not used or retained.
It covers all fourteen pages plus four BLE modes, 619 objects in total. The
main menu includes the generated tile preview, and the BLE mode snapshots
exercise UART, I2C Bridge, I2C Listen and SPI pin visibility. Each object
records its tree path, class, absolute pixel rectangle, font line height and
hidden state. Dropdowns also record SELECTED/ITEMS and popup list font heights.
Geometry is compared exactly after native-coordinate migration, and the
actual LVGL display driver is checked against the shared display dimensions.
The two new sensor cards are checked explicitly for order and focus count.
For comparison with the original fourteen-item menu only, their subtrees are
excluded and the three following original cards are shifted back by exactly
340 pixels (two 160-pixel cards plus their 10-pixel gaps). The original JSON
baseline is not changed. All other page objects still compare exactly.
The known-bad pre-migration boot is excluded; its final geometry is asserted
against the approved native layout. The separate `boot_page_host` tests retain
the approved RGB565 pixel oracle and exercise all animation phases.

`--record-baseline --ui-source <saved-ui.c>` exists for deliberate baseline
creation and refuses to replace different existing baseline files. Do not use it to
accept an unexplained regression. Simulated ADC/state do not validate physical
display, power control, touch hardware or electrical measurements.
