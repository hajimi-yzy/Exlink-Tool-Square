#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool available;
    bool stable;
    float roll_deg;
    float pitch_deg;
    uint8_t address;
    uint8_t who_am_i;
    bool configured;
    bool has_sample;
} level_reading_t;

/* Cache-only bridge: false means stale; available=false means initializing,
 * missing sensor, or no valid gravity. Angles use the current zero.
 * Activating the page and setting zero must not perform I2C in the UI task. */
bool level_sensor_read(level_reading_t *reading);
void level_sensor_set_active(bool active);
bool level_sensor_zero(void);

void level_init(void);

#ifdef __cplusplus
}
#endif
