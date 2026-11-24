// Host-simulator stand-in for the Arduino core (only what the UI code uses).
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define PROGMEM
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

uint32_t millis();
void delay(uint32_t ms);
int digitalRead(int pin);
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(int, int) {}
inline void ledcWrite(int, uint32_t) {}
inline uint32_t uxTaskGetStackHighWaterMark(void*) { return 4096; }

struct EspClass {
  void restart() { printf("[sim] ESP.restart() requested\n"); }
  uint32_t getFreeHeap() { return 141000; }
  uint32_t getMinFreeHeap() { return 98000; }
  uint32_t getMaxAllocHeap() { return 69000; }
};
extern EspClass ESP;

struct SerialStub {
  template <typename... A> void printf(const char* f, A... a) { ::printf(f, a...); }
  void begin(int) {}
};
extern SerialStub Serial;
