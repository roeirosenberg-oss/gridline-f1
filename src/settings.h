// Gridline F1 — persistent settings (NVS), Wi-Fi credentials, touch calibration
#pragma once
#include <Arduino.h>
#include "config.h"
#include "gl_logic.h"

struct Settings {
  uint8_t version;
  uint8_t brightness;       // 5..100 %
  uint8_t accent;           // index into the accent palette
  uint8_t nightEnabled;     // automatic night dimming
  uint8_t nightStart;       // hour 0..23 (Israel local time)
  uint8_t nightEnd;         // hour 0..23
  uint8_t nightBrightness;  // 2..60 %
  uint8_t rotation;         // 0 = build default, otherwise TFT rotation + 1
  char favoriteId[24];      // driverId, "" = none
  char favoriteCode[4];
};

extern Settings g_settings;

// Effective TFT_eSPI rotation (0..3).
inline uint8_t settingsRotation() {
  return g_settings.rotation ? (uint8_t)((g_settings.rotation - 1) & 3) : (uint8_t)GL_DEFAULT_ROTATION;
}

void settingsBegin();                 // load (or defaults)
void settingsChanged();               // schedule a deferred save
void settingsTick();                  // call from loop; writes when due
void settingsFlush();                 // write now if changed (before restart)

// Wi-Fi credentials (separate keys, never logged)
bool wifiCredsLoad(char* ssid, size_t ssidCap, char* pass, size_t passCap);
bool wifiCredsSave(const char* ssid, const char* pass);
bool wifiCredsExist();

// Touch calibration
bool touchCalLoad(gl::TouchCal& cal);
bool touchCalSave(const gl::TouchCal& cal);

// One-shot boot request kept in RTC memory across a software restart.
enum BootRequest : uint32_t { BOOT_NORMAL = 0, BOOT_SETUP = 1, BOOT_CALIBRATE = 2 };
void bootRequestSet(BootRequest r);
BootRequest bootRequestTake();
