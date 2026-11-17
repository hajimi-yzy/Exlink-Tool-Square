#pragma once
#include "fake_host.h"

#define IRAM_ATTR
#define FALLING 2
#define LOW 0
#define HIGH 1
inline uint32_t millis() { return host::runtime.now; }
inline int digitalRead(int) { return host::runtime.irq_low ? LOW : HIGH; }
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterruptArg(int, void (*)(void *), void *, int type) {
  host::runtime.irq_enabled = true; host::runtime.irq_type = type;
}
