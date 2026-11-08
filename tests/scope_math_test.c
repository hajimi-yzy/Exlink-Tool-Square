#include "scope_math.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

static uint16_t samples[2 * SCOPE_MAX_POINTS];
static scope_frame_t frame;
static scope_config_t config = {2, 1500, SCOPE_FREE};

static void near(float actual, float expected, float tolerance)
{
    assert(isfinite(actual));
    assert(fabsf(actual - expected) <= tolerance);
}

static void time_axis_tests(void)
{
    const uint16_t divisions[] = {1, 2, 5, 10, 20, 50};
    const uint32_t rates[] = {20000, 20000, 10000, 5000, 2500, 1000};
    for (unsigned i = 0; i < sizeof(divisions) / sizeof(divisions[0]); ++i) {
        const uint16_t count = scope_point_count(divisions[i]);
        assert(scope_sample_hz(divisions[i]) == rates[i]);
        assert(count > 1 && count <= SCOPE_MAX_POINTS);
        /* N samples have N-1 intervals: chart endpoints cover 10 divisions. */
        near((count - 1) * 1000.0f / rates[i], 10.0f * divisions[i], 0.001f);
    }
    assert(scope_sample_hz(0) == 0 && scope_point_count(0) == 0);
    assert(scope_sample_hz(3) == 0 && scope_point_count(3) == 0);
    assert(scope_sample_hz(UINT16_MAX) == 0 && scope_point_count(UINT16_MAX) == 0);
}

static void dc_and_range_tests(void)
{
    config.trigger = SCOPE_FREE;
    const uint16_t values[] = {0, 149, 150, 1234, 3100, 3101, UINT16_MAX};
    for (unsigned j = 0; j < sizeof(values) / sizeof(values[0]); ++j) {
        const uint16_t value = values[j];
        for (unsigned i = 0; i < 2 * SCOPE_MAX_POINTS; ++i) samples[i] = value;
        scope_analyze(samples, SCOPE_MAX_POINTS, 20000, &config, &frame);
        assert(frame.count == SCOPE_MAX_POINTS && frame.sample_hz == 20000);
        assert(frame.min_mv == value && frame.max_mv == value);
        near(frame.mean_mv, value, 0.01f);
        near(frame.rms_mv, value, 0.01f);
        assert(frame.frequency_hz == 0 && !frame.triggered);
        assert(frame.clipped == (value < 150 || value > 3100));
    }
}

static void sine_samples(unsigned period)
{
    for (unsigned i = 0; i < 2 * SCOPE_MAX_POINTS; ++i)
        samples[i] = (uint16_t)lroundf(1700.0f + 1000.0f *
            sinf(6.283185307179586f * i / period));
}

static void sine_measurement_tests(void)
{
    config.trigger = SCOPE_FREE;
    sine_samples(100);
    scope_analyze(samples, 501, 20000, &config, &frame);
    assert(frame.count == 501 && frame.min_mv == 700 && frame.max_mv == 2700);
    assert(!frame.clipped && !frame.triggered);
    near(frame.mean_mv, 1700, 0.1f);
    /* RMS includes DC. Five complete periods plus the repeated endpoint. */
    near(frame.rms_mv, sqrtf(1700.0f * 1700 + 1000000.0f * 500 / (2 * 501)), 0.5f);
    near(frame.frequency_hz, 200, 0.1f);
    for (unsigned i = 0; i < frame.count; ++i) assert(frame.mv[i] == samples[i]);

    /* The same sampled period has a different real frequency at another rate. */
    scope_analyze(samples, 501, 5000, &config, &frame);
    near(frame.frequency_hz, 50, 0.03f);

    /* Less than two complete periods must not turn a partial slope into Hz. */
    sine_samples(400);
    scope_analyze(samples, 501, 20000, &config, &frame);
    assert(frame.frequency_hz == 0);

    /* Too few samples per period: waveform is visible, frequency is withheld. */
    sine_samples(10);
    scope_analyze(samples, 501, 20000, &config, &frame);
    assert(frame.frequency_hz == 0);

    /* A low-amplitude input has no robust frequency measurement. */
    for (unsigned i = 0; i < 2 * SCOPE_MAX_POINTS; ++i)
        samples[i] = (uint16_t)(1500 + (i % 100 < 50 ? 30 : -30));
    scope_analyze(samples, 501, 20000, &config, &frame);
    assert(frame.frequency_hz == 0);

    /* Deliberately varying pulse periods are not one stable frequency. */
    for (unsigned i = 0; i < 2 * SCOPE_MAX_POINTS; ++i) samples[i] = 1000;
    const unsigned edges[] = {50, 100, 220, 260, 410};
    for (unsigned j = 0; j < sizeof(edges) / sizeof(edges[0]); ++j)
        for (unsigned i = edges[j]; i < edges[j] + 10; ++i) samples[i] = 2000;
    scope_analyze(samples, 501, 20000, &config, &frame);
    assert(frame.frequency_hz == 0);
}

static void trigger_tests(void)
{
    const uint16_t count = 201;
    config.trigger_mv = 1500;
    for (unsigned direction = 0; direction < 2; ++direction) {
        config.trigger = direction ? SCOPE_FALLING : SCOPE_RISING;
        for (unsigned i = 0; i < 2 * count; ++i)
            samples[i] = ((i < 200) != (direction != 0)) ? 1200 : 1800;
        scope_analyze(samples, count, 20000, &config, &frame);
        assert(frame.triggered && frame.count == count);
        /* Edge is aligned to 25% pretrigger, without changing time intervals. */
        const unsigned pretrigger = count / 4;
        assert(frame.mv[pretrigger - 1] == (direction ? 1800 : 1200));
        assert(frame.mv[pretrigger] == (direction ? 1200 : 1800));
        for (unsigned i = 0; i < count; ++i) assert(frame.mv[i] == samples[150 + i]);
    }

    /* No valid crossing: show the initial window, never freeze waiting for it. */
    config.trigger = SCOPE_RISING;
    for (unsigned i = 0; i < 2 * count; ++i) samples[i] = 1550 + (i % 5);
    scope_analyze(samples, count, 20000, &config, &frame);
    assert(!frame.triggered);
    for (unsigned i = 0; i < count; ++i) assert(frame.mv[i] == samples[i]);

    /* Noise inside the 40mV hysteresis band cannot trigger. */
    for (unsigned i = 0; i < 2 * count; ++i) samples[i] = i % 2 ? 1490 : 1510;
    scope_analyze(samples, count, 20000, &config, &frame);
    assert(!frame.triggered);

    /* The final permitted crossing still fits entirely in the input buffer. */
    for (unsigned i = 0; i < 2 * SCOPE_MAX_POINTS; ++i)
        samples[i] = i < SCOPE_MAX_POINTS + SCOPE_MAX_POINTS / 4 - 1 ? 1200 : 1800;
    scope_analyze(samples, SCOPE_MAX_POINTS, 20000, &config, &frame);
    assert(frame.triggered && frame.mv[SCOPE_MAX_POINTS - 1] == 1800);
}

static void invalid_input_tests(void)
{
    config.trigger = SCOPE_FREE;
    frame.sequence = 123;
    frame.dropped = 4;
    frame.calibrated = true;
    scope_analyze(samples, 0, 20000, &config, &frame);
    assert(frame.count == 0 && frame.min_mv == 0 && frame.max_mv == 0);
    assert(frame.mean_mv == 0 && frame.rms_mv == 0 && frame.frequency_hz == 0);
    assert(!frame.triggered && !frame.clipped);
    assert(frame.sequence == 123 && frame.dropped == 4 && frame.calibrated);
    scope_analyze(samples, SCOPE_MAX_POINTS + 1, 20000, &config, &frame);
    assert(frame.count == 0);
    scope_analyze(samples, 1, 0, &config, &frame);
    assert(frame.count == 0);
    scope_analyze(NULL, 201, 20000, &config, &frame);
    assert(frame.count == 0);
    scope_analyze(samples, 201, 20000, NULL, &frame);
    assert(frame.count == 0);
    scope_analyze(samples, 201, 20000, &config, NULL);

    /* The smallest supported mathematical window is valid and has DC only. */
    samples[0] = samples[1] = 1200;
    scope_analyze(samples, 1, 20000, &config, &frame);
    assert(frame.count == 1 && frame.mean_mv == 1200 && frame.frequency_hz == 0);
}

int main(void)
{
    time_axis_tests();
    dc_and_range_tests();
    sine_measurement_tests();
    trigger_tests();
    invalid_input_tests();
    puts("PASS: scope math: time axis, DC/sine statistics, hysteretic triggers, frequency rejection and bounds");
    return 0;
}
