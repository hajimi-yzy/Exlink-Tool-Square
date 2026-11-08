#include "../../src/interaction_policy.h"

/* Use the exact firmware edge policy before the actual LVGL pointer driver. */
extern "C" int scope_test_route(bool pressed, int x, int y)
{
    static EdgeBackGesture gesture;
    return static_cast<int>(gesture.update(pressed, x, y, true));
}
