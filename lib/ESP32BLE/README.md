# Local Arduino ESP32 BLE runtime

Source copied without examples from Arduino-ESP32 2.0.14, BLE library 2.0.0
(`framework-arduinoespressif32` 3.20014.231204). Upstream:
https://github.com/espressif/arduino-esp32/tree/2.0.14/libraries/BLE
Original attribution/license headers are retained. Arduino-ESP32 license:
https://github.com/espressif/arduino-esp32/blob/2.0.14/LICENSE.md

The project-local copy is used instead of modifying the globally installed SDK.
`BLEServer::restartGattDatabase()` re-registers the app and recreates its GATT
database after Bluedroid disable/enable. It resets service, characteristic and
descriptor handles and handle lookup maps while retaining UUIDs, values,
callbacks and allocated objects. CCCDs reset per connection. This avoids stale
handles and repeated allocation during manual BLE Off/On and light-sleep.
It must run in loop after all connections close and the host is enabled.

The rebuild uses the order of each service's first successful GATT handle,
not its C++ object's heap address. This order survives a partially failed
rebuild. Characteristic and descriptor objects retain their original ordering.
GAP and GATTS callbacks are reinstalled before registering the application.

Registration, service creation, characteristic/descriptor creation and service
start each use bounded completion waits (5 seconds per semaphore acquisition or
event). Immediate API failures, non-OK completion events and missing events
return failure and release completion semaphores; expired completions are
ignored. The existing public `void start()` / `executeCreate()` entry points
remain available; restoration uses checked internal methods and fails on the
first unsuccessful step. A caller must stop the host/controller after a failed
restore before retrying, so a partial database and old events cannot mix with a
new registration. No new services or attributes are allocated on a rebuild.

This behavior was checked against the installed Arduino-ESP32 2.0.14 / ESP-IDF
4.4.6 headers and the matching IDF sources, rather than inferred from IDF 5.x:

- `bta_gatts_api_disable()` deregisters applications and clears the GATTS control
  block: https://github.com/espressif/esp-idf/blob/v4.4.6/components/bt/host/bluedroid/bta/gatt/bta_gatts_act.c
- GATTS callback registration writes a separate BTC callback table:
  https://github.com/espressif/esp-idf/blob/v4.4.6/components/bt/host/bluedroid/api/esp_gatts_api.c
  and https://github.com/espressif/esp-idf/blob/v4.4.6/components/bt/common/btc/core/btc_manage.c

Compile verification does not validate radio timing or a phone's GATT cache.
Exercise repeated Off/On and sleep/wake on hardware, and verify discovery,
encrypted reads, CCCD subscription and OTA after each transition. If attribute
definitions change in a future firmware, the phone must rediscover the database.

The disabled host/controller still provide the original sleep/power behavior;
the repair does not keep advertising or connections alive while BLE is Off.
