#pragma once
#include <stdint.h>
#define LOW 0
static inline uint32_t analogReadMilliVolts(uint8_t pin) { (void)pin; return 1950; }
static inline void digitalWrite(uint8_t pin, uint8_t value) { (void)pin; (void)value; }
