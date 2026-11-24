// Gridline F1 — user interface (runs on the Arduino loop task, core 1)
#pragma once
#include <Arduino.h>

void uiShowSplash(const char* status);
bool uiTouchHeldAtBoot(uint32_t ms);
// Guided 5-point calibration + verification. allowCancel: BOOT button aborts.
bool uiRunCalibration(bool allowCancel);
void uiBegin();
void uiLoop();
