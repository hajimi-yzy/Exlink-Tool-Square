#pragma once
#include "RTOS.h"
#include <esp_gatt_defs.h>
#include "esp32-hal-log.h"

// Lifecycle operations run serially in setup/loop. Both acquiring a completion
// semaphore and waiting for its asynchronous IDF event must be bounded. Use
// take(ms), because this Arduino release's timedWait argument is actually ticks.
static constexpr uint32_t BLE_GATT_OPERATION_TIMEOUT_MS = 5000;
inline bool bleWaitGattEvent(FreeRTOS::Semaphore &semaphore, const char *operation) {
    const bool signalled = semaphore.take(BLE_GATT_OPERATION_TIMEOUT_MS, operation);
    const bool succeeded = signalled && semaphore.value() == ESP_GATT_OK;
    semaphore.give(); // Also recover a missing-event timeout for the next radio cycle.
    if (!signalled) log_e("GATT %s: completion timed out", operation);
    else if (!succeeded) log_e("GATT %s: completion status 0x%02x", operation, unsigned(semaphore.value()));
    return succeeded;
}
