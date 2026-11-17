#pragma once
#include <stdint.h>
static inline uint32_t esp_random(void)
{
    static uint32_t state = 1;
    state = state * 1664525U + 1013904223U;
    return state;
}
