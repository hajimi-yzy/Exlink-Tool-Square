// The runner extracts the production functions verbatim; only their hardware
// and textarea dependencies are mocked here. Channels 2/3 share timer 1 in
// Arduino-ESP32, and attaching either channel replaces GPIO5's prior route.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
using std::max;

template <class T> static T constrain(T value, T low, T high) {
    return value < low ? low : value > high ? high : value;
}
static long map(long value, long in_low, long in_high, long out_low, long out_high) {
    return (value - in_low) * (out_high - out_low) / (in_high - in_low) + out_low;
}
static const char *lv_textarea_get_text(const char *object) { return object; }
enum { LEDC_TIMER_1_BIT = 1, LEDC_TIMER_14_BIT = 14 };
static const char *fre = "1000", *duty = "25";
static int pwm_flag, servo_flag, pwm_Freq, pwm_Duty;
static int new_pwm_Freq, new_pwm_Duty, servo_angle = 90, new_servo_angle = -1;
static bool pwm_ledc_ready, servo_ledc_ready;
static uint8_t servo_resolution_bits;
static uint32_t timer_frequency[4], channel_duty[8];
static unsigned writes[8], setups[8], attaches[8];
static int gpio5_channel = -1;
static bool fail_setup;

static uint32_t ledcSetup(uint8_t channel, uint32_t frequency, uint8_t) {
    ++setups[channel];
    if (fail_setup) return 0;
    timer_frequency[(channel / 2) % 4] = frequency;
    return frequency;
}
static void ledcAttachPin(uint8_t pin, uint8_t channel) {
    assert(pin == 5);
    ++attaches[channel];
    gpio5_channel = channel;
}
static void ledcWrite(uint8_t channel, uint32_t value) {
    ++writes[channel];
    channel_duty[channel] = value;
}

#include "production_tasks.inc"

// Match the production loop's ordering, including the inactive owner's task.
static void tick() { pwm_task(); servo_task(); }
static void expect_servo() {
    assert(servo_ledc_ready && gpio5_channel == 3);
    assert(timer_frequency[1] == 50);
    // 90 degrees is a 1500 us pulse in a 20000 us period at 14-bit resolution.
    assert(channel_duty[3] == 1229);
}
static void stop_all() {
    pwm_flag = servo_flag = 0;
    tick();
    assert(!pwm_ledc_ready && !servo_ledc_ready);
    assert(channel_duty[2] == 0 && channel_duty[3] == 0);
    const unsigned pwm_writes = writes[2], servo_writes = writes[3];
    tick(); tick();
    assert(writes[2] == pwm_writes && writes[3] == servo_writes);
}

int main() {
    servo_flag = 1;
    tick();
    expect_servo();
    stop_all();

    pwm_flag = 1;
    const unsigned stopped_servo_writes = writes[3];
    tick();
    assert(pwm_ledc_ready && gpio5_channel == 2);
    assert(timer_frequency[1] == 1000 && channel_duty[2] == 4095);
    tick();
    assert(writes[3] == stopped_servo_writes); // old owner remains silent
    stop_all();

    const unsigned stopped_pwm_writes = writes[2];
    servo_flag = 1;
    tick();
    expect_servo();
    tick();
    assert(writes[2] == stopped_pwm_writes);
    assert(attaches[3] == 2 && setups[3] == 2);
    stop_all();

    gpio5_channel = -1; // frequency counter configured GPIO5 as an input
    tick();
    assert(gpio5_channel == -1); // STOP must not reclaim another tool's pin
    servo_flag = 1;
    tick();
    expect_servo();
    assert(attaches[3] == 3 && setups[3] == 3);
    stop_all();

    // If setup fails, do not attach/write; a later servo tick may retry.
    fail_setup = true;
    servo_flag = 1;
    const unsigned prior_attaches = attaches[3], prior_writes = writes[3];
    tick();
    assert(!servo_ledc_ready && attaches[3] == prior_attaches && writes[3] == prior_writes);
    fail_setup = false;
    tick();
    expect_servo();
    stop_all();

    puts("production LEDC handoff tests passed");
}
