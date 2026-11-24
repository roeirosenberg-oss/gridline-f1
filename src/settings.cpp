#include "settings.h"
#include <Preferences.h>
#include <esp_attr.h>
#include "config.h"
#include "log.h"

Settings g_settings;
static Settings s_saved;
static uint32_t s_dirtySince = 0;
static bool s_dirty = false;

static const char* kNs = "gridline";
static const uint8_t kSettingsVersion = 1;

static void defaults(Settings& s) {
  memset(&s, 0, sizeof(s));
  s.version = kSettingsVersion;
  s.brightness = 80;
  s.accent = 0;
  s.nightEnabled = 0;
  s.nightStart = 23;
  s.nightEnd = 7;
  s.nightBrightness = 12;
}

static void sanitize(Settings& s) {
  if (s.brightness < 5 || s.brightness > 100) s.brightness = 80;
  if (s.accent > 5) s.accent = 0;
  if (s.nightStart > 23) s.nightStart = 23;
  if (s.nightEnd > 23) s.nightEnd = 7;
  if (s.nightBrightness < 2 || s.nightBrightness > 60) s.nightBrightness = 12;
  s.nightEnabled = s.nightEnabled ? 1 : 0;
  s.favoriteId[sizeof(s.favoriteId) - 1] = 0;
  s.favoriteCode[sizeof(s.favoriteCode) - 1] = 0;
  if (s.rotation > 4) s.rotation = 0;
}

void settingsBegin() {
  defaults(g_settings);
  Preferences p;
  if (p.begin(kNs, false)) {
    Settings tmp;
    size_t n = p.getBytesLength("cfg");
    if (n == sizeof(Settings) && p.getBytes("cfg", &tmp, sizeof(tmp)) == sizeof(tmp) && tmp.version == kSettingsVersion) {
      g_settings = tmp;
      LOGI("CFG", "settings loaded");
    } else {
      LOGI("CFG", "no stored settings (or incompatible), using defaults");
    }
    p.end();
  }
  sanitize(g_settings);
  s_saved = g_settings;
}

void settingsChanged() {
  sanitize(g_settings);
  s_dirty = true;
  s_dirtySince = millis();
}

static void writeNow() {
  if (memcmp(&s_saved, &g_settings, sizeof(Settings)) == 0) { s_dirty = false; return; }
  Preferences p;
  if (p.begin(kNs, false)) {
    size_t w = p.putBytes("cfg", &g_settings, sizeof(Settings));
    p.end();
    if (w == sizeof(Settings)) {
      s_saved = g_settings;
      LOGI("CFG", "settings saved");
    } else {
      LOGI("CFG", "settings save FAILED");
    }
  }
  s_dirty = false;
}

void settingsTick() {
  if (s_dirty && millis() - s_dirtySince > GL_SETTINGS_SAVE_DELAY_MS) writeNow();
}

void settingsFlush() {
  if (s_dirty) writeNow();
}

bool wifiCredsLoad(char* ssid, size_t ssidCap, char* pass, size_t passCap) {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.isKey("wssid");
  if (ok) {
    p.getString("wssid", ssid, ssidCap);
    if (p.isKey("wpass")) p.getString("wpass", pass, passCap);
    else pass[0] = 0;
  }
  p.end();
  return ok && ssid[0];
}

bool wifiCredsSave(const char* ssid, const char* pass) {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putString("wssid", ssid) > 0;
  p.putString("wpass", pass);
  p.end();
  LOGI("CFG", "Wi-Fi credentials saved for SSID length %u (password not logged)", (unsigned)strlen(ssid));
  return ok;
}

bool wifiCredsExist() {
  char s[33], pw[65];
  return wifiCredsLoad(s, sizeof(s), pw, sizeof(pw));
}

struct CalBlob {
  uint32_t magic;
  gl::TouchCal cal;
  uint32_t crc;
};
static const uint32_t kCalMagic = 0x4C414347;  // "GCAL"

bool touchCalLoad(gl::TouchCal& cal) {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  CalBlob b;
  bool ok = p.getBytesLength("cal") == sizeof(b) && p.getBytes("cal", &b, sizeof(b)) == sizeof(b);
  p.end();
  if (!ok || b.magic != kCalMagic || b.crc != gl::crc32(&b.cal, sizeof(b.cal))) return false;
  if (!gl::touchCalPlausible(b.cal)) return false;
  cal = b.cal;
  return true;
}

bool touchCalSave(const gl::TouchCal& cal) {
  CalBlob b;
  b.magic = kCalMagic;
  b.cal = cal;
  b.crc = gl::crc32(&b.cal, sizeof(b.cal));
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putBytes("cal", &b, sizeof(b)) == sizeof(b);
  p.end();
  LOGI("TOUCH", "calibration %s", ok ? "saved" : "save FAILED");
  return ok;
}

RTC_NOINIT_ATTR static uint32_t s_bootMagic;
RTC_NOINIT_ATTR static uint32_t s_bootRequest;
static const uint32_t kBootMagic = 0x47524944;

void bootRequestSet(BootRequest r) {
  s_bootMagic = kBootMagic;
  s_bootRequest = (uint32_t)r;
}

BootRequest bootRequestTake() {
  BootRequest r = BOOT_NORMAL;
  if (s_bootMagic == kBootMagic && s_bootRequest <= BOOT_CALIBRATE) r = (BootRequest)s_bootRequest;
  s_bootMagic = 0;
  s_bootRequest = 0;
  return r;
}
