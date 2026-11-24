// Gridline F1 — XPT2046 resistive touch on its own SPI bus, with filtering,
// debouncing and calibrated tap events.
#pragma once
#include <Arduino.h>
#include "gl_logic.h"

struct RawTouch {
  int16_t x, y;   // 12-bit raw ADC values (median filtered)
  int16_t z;      // pressure estimate
  bool down;
};

void touchBegin();
bool touchReadRaw(RawTouch& r);
bool touchIrqActive();

void touchSetRotation(uint8_t rotation);   // screen rotation used for tap coordinates
void touchSetCal(const gl::TouchCal& cal);  // calibration in the canonical (rotation 1) frame
bool touchHasCal();
void touchClearCal();

void touchPoll();                              // call every loop iteration
bool touchTakeTap(int16_t& x, int16_t& y);     // one tap per physical press
bool touchIsDown();
void touchFlush();                             // drop pending events (after screen changes)
