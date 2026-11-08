#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENVIRONMENT_ERROR_NONE = 0,
    ENVIRONMENT_ERROR_MISSING = 1,
    ENVIRONMENT_ERROR_INITIALIZING = 2,
    ENVIRONMENT_ERROR_READ_FAILED = 3
} environment_error_t;

typedef struct {
    bool available;
    bool calibrated;
    float temperature_c;
    float humidity_rh;
    uint8_t error;
} environment_reading_t;

/* Cache-only bridge. False means the cached sample is stale. Missing,
 * initializing and failed readings return their status with available=false.
 * UI callbacks never perform I2C; activation is serviced by the sensor task. */
bool environment_sensor_read(environment_reading_t *reading);
void environment_sensor_set_active(bool active);

void environment_init(void);

#ifdef __cplusplus
}
#endif
