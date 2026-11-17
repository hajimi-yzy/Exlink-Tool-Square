#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LOW 0
#define HIGH 1
#define constrain(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
uint32_t millis(void);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t value);
static inline uint32_t analogReadMilliVolts(uint8_t pin) { (void)pin; return 1950; }
