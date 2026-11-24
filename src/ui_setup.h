// Gridline F1 — drawing of the Wi-Fi setup screen (shared with the host simulator)
#pragma once
#include <stdint.h>

enum SetupState : uint8_t { SETUP_IDLE = 0, SETUP_CONNECTING, SETUP_CONNECTED, SETUP_FAILED };

struct SetupView {
  const char* apSsid;
  const char* apPass;
  const char* message;
  uint8_t state;      // SetupState
  bool canCancel;
};

void setupDrawScreen(const SetupView& v);
void setupDrawStatus(const SetupView& v);
bool setupCancelHit(int16_t x, int16_t y);   // tap on the Cancel button?
