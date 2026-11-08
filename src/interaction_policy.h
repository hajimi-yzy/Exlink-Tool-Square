#pragma once

#include <stdint.h>

#if __cplusplus >= 201402L
#define INTERACTION_CONSTEXPR constexpr
#else
#define INTERACTION_CONSTEXPR
#endif

// Pure input policy: an edge gesture owns the entire contact, including its
// release. It never becomes a button press halfway through a swipe.
class EdgeBackGesture
{
public:
    enum class Result { Forward, Consume, Back };
    constexpr EdgeBackGesture() = default;

    INTERACTION_CONSTEXPR Result update(bool pressed, int x, int y, bool allowed)
    {
        if (!pressed)
        {
            const bool consumed = _captured;
            _down = _captured = _sent = false;
            return consumed ? Result::Consume : Result::Forward;
        }
        if (!_down)
        {
            _down = true;
            _x = x;
            _y = y;
            _captured = allowed && x >= 448; // rightmost 32 native pixels
        }
        if (!_captured) return Result::Forward;
        const int dx = _x - x;
        const int dy = y > _y ? y - _y : _y - y;
        if (!_sent && dx >= 40 && dx > 2 * dy)
        {
            _sent = true;
            return Result::Back;
        }
        return Result::Consume;
    }

private:
    bool _down = false, _captured = false, _sent = false;
    int _x = 0, _y = 0;
};

constexpr bool idle_timeout_elapsed(uint32_t now, uint32_t last, uint32_t seconds)
{
    // Unsigned subtraction also handles the millis() wrap at ~49 days.
    return seconds != 0 && (uint32_t)(now - last) >= seconds * 1000U;
}

// These describe running functions, never the page that happens to be open.
// Background touch/battery polling is deliberately absent.
struct RuntimeActivity
{
    bool power_output;
    bool measurement;
    bool waveform_output;
    bool digital_capture;
    bool wired_bridge;
    bool bluetooth_link;
    bool update;
};

constexpr bool blocks_automatic_sleep(const RuntimeActivity &activity)
{
    return activity.power_output || activity.measurement || activity.waveform_output ||
           activity.digital_capture || activity.wired_bridge || activity.bluetooth_link ||
           activity.update;
}

constexpr uint32_t sanitize_screen_idle_seconds(uint32_t seconds)
{
    return seconds == 0 || seconds == 15 || seconds == 30 || seconds == 60 ||
           seconds == 120 || seconds == 300 ? seconds : 30;
}

constexpr uint32_t sanitize_screen_photo_seconds(uint32_t seconds)
{
    return seconds == 5 || seconds == 10 || seconds == 15 || seconds == 30 ||
           seconds == 60 ? seconds : 5;
}

constexpr uint32_t screensaver_duration_ms(uint16_t frame_count,
                                         uint32_t media_ms, uint32_t photo_seconds)
{
    return frame_count == 1 ? sanitize_screen_photo_seconds(photo_seconds) * 1000U : media_ms;
}

#undef INTERACTION_CONSTEXPR
