// Compile with -std=gnu++14: these assertions execute the real input policy at
// compile time, needing neither a host C++ runtime nor a connected target.
#include "../src/interaction_policy.h"
using R = EdgeBackGesture::Result;

constexpr bool one_contact_one_back()
{
    EdgeBackGesture g;
    return g.update(true, 474, 220, true) == R::Consume &&
           g.update(true, 458, 222, true) == R::Consume &&
           g.update(true, 428, 225, true) == R::Back &&
           g.update(true, 400, 225, false) == R::Consume &&
           g.update(false, 400, 225, false) == R::Consume &&
           g.update(true, 300, 100, true) == R::Forward;
}

constexpr bool vertical_edge_is_not_back()
{
    EdgeBackGesture g;
    return g.update(true, 460, 200, true) == R::Consume &&
           g.update(true, 452, 280, true) == R::Consume &&
           g.update(true, 410, 320, true) == R::Consume;
}

constexpr bool center_buttons_are_unaffected()
{
    EdgeBackGesture g;
    return g.update(true, 430, 200, true) == R::Forward &&
           g.update(true, 350, 200, true) == R::Forward &&
           g.update(false, 350, 200, true) == R::Forward;
}

constexpr bool home_does_not_capture()
{
    EdgeBackGesture g;
    return g.update(true, 470, 200, false) == R::Forward &&
           g.update(true, 400, 200, false) == R::Forward;
}

constexpr bool rightward_swipe_is_not_back()
{
    EdgeBackGesture g;
    return g.update(true, 450, 200, true) == R::Consume &&
           g.update(true, 479, 200, true) == R::Consume;
}

static_assert(one_contact_one_back(), "edge owns one contact including release");
static_assert(vertical_edge_is_not_back(), "reject vertical/diagonal motion");
static_assert(center_buttons_are_unaffected(), "center buttons stay usable");
static_assert(home_does_not_capture(), "main menu has no back");
static_assert(rightward_swipe_is_not_back(), "left direction only");
static_assert(!idle_timeout_elapsed(29999, 0, 30), "30s not reached");
static_assert(idle_timeout_elapsed(30000, 0, 30), "30s reached");
static_assert(idle_timeout_elapsed(10000, 0xFFFFB1E0U, 30), "millis wraps safely");
static_assert(!idle_timeout_elapsed(0, 0, 0), "Never does not trigger immediately");
static_assert(!idle_timeout_elapsed(0xFFFFFFFFU, 0, 0), "Never stays awake across full uptime range");
static_assert(!blocks_automatic_sleep({false, false, false, false, false, false, false}),
              "main menu or unopened function page may sleep");
static_assert(blocks_automatic_sleep({true, false, false, false, false, false, false}),
              "power output must not be interrupted");
static_assert(blocks_automatic_sleep({false, true, false, false, false, false, false}),
              "started voltage/current measurement must stay awake");
static_assert(blocks_automatic_sleep({false, false, true, false, false, false, false}),
              "PWM and servo output must stay awake");
static_assert(blocks_automatic_sleep({false, false, false, true, false, false, false}),
              "scope and frequency capture must stay awake");
static_assert(blocks_automatic_sleep({false, false, false, false, true, false, false}),
              "manual UART I2C SPI bridges must stay awake");
static_assert(blocks_automatic_sleep({false, false, false, false, false, true, false}),
              "connected phone and BLE bridge must stay awake");
static_assert(blocks_automatic_sleep({false, false, false, false, false, false, true}),
              "update transfer must stay awake");
static_assert(sanitize_screen_idle_seconds(0) == 0, "Never survives NVS reload");
static_assert(sanitize_screen_idle_seconds(300) == 300, "longest idle choice survives reload");
static_assert(sanitize_screen_idle_seconds(1) == 30, "invalid idle value uses default");
static_assert(screensaver_duration_ms(1, 5000, 60) == 60000, "single photo uses user duration");
static_assert(screensaver_duration_ms(1, 5000, 0) == 5000, "invalid photo duration uses default");
static_assert(screensaver_duration_ms(10, 1000, 60) == 1000, "GIF retains original duration");
