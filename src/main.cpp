// Gridline F1 — standalone Formula 1 desk display for the ESP32-2432S028
//
// Boot sequence:
//   1. serial log, settings (NVS), cache file system
//   2. display + splash, touch controller
//   3. touch calibration (first boot, on request, or when the screen is
//      held during start-up)
//   4. Wi-Fi setup mode (first boot, on request from Settings / BOOT button)
//   5. restore cached data, start the network task, run the UI loop
#include <Arduino.h>
#include <esp_chip_info.h>
#include "app_state.h"
#include "certs/ca_bundle.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "net.h"
#include "provisioning.h"
#include "settings.h"
#include "storage.h"
#include "touch.h"
#include "ui.h"

static void ledsOff() {
  // the on-board RGB LED is active low
  const int pins[] = {GL_PIN_LED_R, GL_PIN_LED_G, GL_PIN_LED_B};
  for (int p : pins) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
  }
}

static void logBanner() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  LOGI("BOOT", "%s %s (%s profile), built %s %s", GL_FW_NAME, GL_FW_VERSION, GL_DISPLAY_PROFILE, __DATE__, __TIME__);
  LOGI("BOOT", "chip ESP32 rev %d, %d cores, flash %u KB, PSRAM %s, CPU %u MHz", chip.revision, chip.cores,
       (unsigned)(ESP.getFlashChipSize() / 1024), psramFound() ? "found" : "none", (unsigned)getCpuFrequencyMhz());
  LOGI("BOOT", "reset reason %d, TLS trust store: %d roots (%s)", (int)esp_reset_reason(), GL_CA_BUNDLE_COUNT,
       GL_CA_BUNDLE_SOURCE);
  logMemory("boot");
}

void setup() {
  ledsOff();
  pinMode(GL_PIN_BOOT_BUTTON, INPUT_PULLUP);
  logInit();
  delay(50);
  logBanner();
  appStateInit();
  settingsBegin();
  storageBegin();

  displayBegin();
  uiShowSplash("Starting");
  for (int b = 0; b <= g_settings.brightness; b += 4) {  // gentle fade-in
    backlightSet(b);
    delay(8);
  }
  backlightSet(g_settings.brightness);

  touchBegin();
  touchSetRotation(settingsRotation());
  LOGI("BOOT", "screen rotation %u (%s)", settingsRotation(), (settingsRotation() & 1) ? "landscape" : "portrait");
  gl::TouchCal cal;
  bool haveCal = touchCalLoad(cal);
  if (haveCal) touchSetCal(cal);
  LOGI("TOUCH", "calibration %s", haveCal ? "loaded" : "missing");

  BootRequest req = bootRequestTake();
  LOGI("BOOT", "boot request: %d", (int)req);
  bool recal = req == BOOT_CALIBRATE || !haveCal || uiTouchHeldAtBoot(3000);
  if (recal) {
    while (!uiRunCalibration(haveCal)) {
      if (haveCal) break;  // cancelled: keep the previous calibration
    }
  }

  bool creds = wifiCredsExist();
  LOGI("WIFI", "stored credentials: %s", creds ? "yes" : "no");
  if (req == BOOT_SETUP || !creds) {
    provisioningRun(creds);  // never returns
  }

  uiShowSplash("Loading saved data");
  netRestoreCache();
  netStart();
  uiBegin();
  logMemory("setup done");
}

void loop() {
  uiLoop();
  settingsTick();
  delay(2);
}
