// Failed 1.0.12 experiment retained for explicit opt-in only. Production uses
// the same SDK driver as the hardware-tested 1.0.7 touch baseline.
#if defined(EXLINK_ENABLE_I2C_TIMEOUT_BACKPORT) && EXLINK_ENABLE_I2C_TIMEOUT_BACKPORT
#include "sdkconfig.h"
#include "esp_idf_version.h"

#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(4, 4, 6)
#error "Reassess the I2C timeout backport before changing the target or SDK"
#endif

#include "../vendor/esp-idf-v4.4.8/i2c.c"

// Startup logs identify this object in addition to inspecting the link map.
const char *exlink_i2c_timeout_backport(void) {
    return "idf-4.4.8-deadline";
}
#else
const char *exlink_i2c_timeout_backport(void) {
    return "arduino-idf-4.4.6-stock";
}
#endif
