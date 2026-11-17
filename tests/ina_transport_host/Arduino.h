#pragma once
#include <stddef.h>
#include <stdint.h>
#define B00000111 7
#define B111 7
#define bitRead(value, bit) (((value) >> (bit)) & 1U)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
inline void delayMicroseconds(unsigned) {}
inline void delay(unsigned) {}
