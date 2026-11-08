#pragma once
#include "scope_capture.h"
#include <math.h>
#include <string.h>

/* These rates divide the IDF 4.4 S3 ADC clock (5 MHz / 2) exactly. */
static inline uint32_t scope_sample_hz(uint16_t time_div_ms)
{
    switch (time_div_ms) {
    case 1: case 2: return 20000;
    case 5: return 10000;
    case 10: return 5000;
    case 20: return 2500;
    case 50: return 1000;
    default: return 0;
    }
}

static inline uint16_t scope_point_count(uint16_t time_div_ms)
{
    const uint32_t hz = scope_sample_hz(time_div_ms);
    return hz ? (uint16_t)(hz * time_div_ms / 100 + 1) : 0;
}

/* Capture two screen widths, find a hysteretic edge with 25% pretrigger,
 * then measure only the displayed window. Inputs are calibrated pin mV. */
static inline void scope_analyze(const uint16_t *input, uint16_t count,
                                 uint32_t hz, const scope_config_t *config,
                                 scope_frame_t *frame)
{
    if (!frame) return;
    frame->count = frame->min_mv = frame->max_mv = 0;
    frame->sample_hz = 0;
    frame->mean_mv = frame->rms_mv = frame->frequency_hz = 0;
    frame->triggered = frame->clipped = false;
    /* Invalid windows must not divide by zero or write past mv[]. Keep the
     * worker-owned sequence/drop/calibration metadata intact. */
    if (!input || !config || !hz || !count || count > SCOPE_MAX_POINTS) return;
    uint16_t start = 0;
    if (config->trigger != SCOPE_FREE) {
        const int threshold = config->trigger_mv;
        bool armed = false;
        for (unsigned i = count / 4; i < count + count / 4; ++i) {
            const int value = input[i];
            if (config->trigger == SCOPE_RISING) {
                if (value < threshold - 20) armed = true;
                if (!armed || value < threshold + 20) continue;
            } else {
                if (value > threshold + 20) armed = true;
                if (!armed || value > threshold - 20) continue;
            }
            start = i - count / 4;
            frame->triggered = true;
            break;
        }
    }
    frame->count = count;
    frame->sample_hz = hz;
    frame->min_mv = UINT16_MAX;
    frame->max_mv = 0;
    uint64_t sum = 0, squares = 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint16_t value = frame->mv[i] = input[start + i];
        if (value < frame->min_mv) frame->min_mv = value;
        if (value > frame->max_mv) frame->max_mv = value;
        sum += value;
        squares += (uint64_t)value * value;
        /* S3 ADC 11 dB characterized range is approx. 150..3100 mV.
         * A range warning is not a claim that 0 V is an invalid input. */
        if (value < 150 || value > 3100) frame->clipped = true;
    }
    frame->mean_mv = (float)sum / count;
    frame->rms_mv = sqrtf((float)squares / count);
    if (frame->max_mv - frame->min_mv < 100) return;
    const float mid = (frame->min_mv + frame->max_mv) * 0.5f;
    const float hysteresis = (frame->max_mv - frame->min_mv) * 0.1f;
    bool armed = false;
    float first = 0, last = 0, previous = 0, min_period = 1e9f, max_period = 0;
    unsigned edges = 0;
    for (unsigned i = 1; i < count; ++i) {
        if (frame->mv[i] < mid - hysteresis) armed = true;
        if (!armed || frame->mv[i] < mid + hysteresis) continue;
        /* Interpolate at the same hysteresis threshold each cycle. */
        const float delta = (float)frame->mv[i] - frame->mv[i - 1];
        const float edge = i - 1 + (mid + hysteresis - frame->mv[i - 1]) / delta;
        if (!edges) first = edge;
        else {
            const float period = edge - previous;
            if (period < min_period) min_period = period;
            if (period > max_period) max_period = period;
        }
        last = previous = edge;
        ++edges;
        armed = false;
    }
    /* Require two complete, consistent cycles and >=20 samples/period.
     * Reject DC, noise, undersampled signals and unstable frequency guesses. */
    if (edges >= 3 && min_period >= 20 && max_period <= min_period * 1.2f)
        frame->frequency_hz = hz * (edges - 1) / (last - first);
}
