#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { SCOPE_MAX_POINTS = 512 };
typedef enum { SCOPE_FREE, SCOPE_RISING, SCOPE_FALLING } scope_trigger_t;
typedef struct {
    uint16_t time_div_ms;
    uint16_t trigger_mv;
    scope_trigger_t trigger;
} scope_config_t;
typedef struct {
    uint16_t mv[SCOPE_MAX_POINTS];
    uint16_t count, min_mv, max_mv;
    float mean_mv, rms_mv, frequency_hz;
    uint32_t sample_hz, sequence, dropped;
    bool triggered, clipped, calibrated;
} scope_frame_t;
typedef enum { SCOPE_STOPPED, SCOPE_STARTING, SCOPE_RUNNING, SCOPE_ERROR } scope_state_t;

/* No LVGL or I2C calls in the acquisition worker. Control calls never wait
 * for a frame. ADC1 remains owned until stop/deinitialize has completed. */
bool scope_acq_start(const scope_config_t *config);
void scope_acq_stop(void);
bool scope_acq_quiesce(void);
bool scope_acq_resume(void);            // resume previous settings after OTA
bool scope_adc_owned(void);
scope_state_t scope_acq_state(void);
bool scope_acq_read(scope_frame_t *frame, uint32_t after_sequence);
void scope_page_init(void);

#ifdef __cplusplus
}
#endif
