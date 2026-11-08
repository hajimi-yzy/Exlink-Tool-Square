#pragma once
#include <stddef.h>
#include <stdint.h>
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
