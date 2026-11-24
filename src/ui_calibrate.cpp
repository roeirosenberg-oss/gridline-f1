// Gridline F1 — guided touch calibration (5 targets + verification)
#include <algorithm>
#include "config.h"
#include "display.h"
#include "log.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"

using namespace gl;

static void drawTarget(int x, int y, uint16_t c) {
  tft.drawCircle(x, y, 10, c);
  tft.drawCircle(x, y, 9, c);
  tft.drawFastHLine(x - 16, y, 33, c);
  tft.drawFastVLine(x, y - 16, 33, c);
  tft.fillCircle(x, y, 2, c);
}

static void calScreen(const char* title, const char* line1, const char* line2) {
  const int W = tft.width(), H = tft.height();
  tft.fillScreen(col::BG);
  drawText(title, W / 2, H / 2 - 32, F_M, col::TEXT, col::BG, MC_DATUM);
  drawTextFit(line1, W / 2, H / 2 - 8, W - 16, F_S, col::TEXT2, col::BG, MC_DATUM);
  if (line2) drawTextFit(line2, W / 2, H / 2 + 10, W - 16, F_S, col::TEXT3, col::BG, MC_DATUM);
}

// Waits for a full press/release and returns the median raw position.
// Returns false when cancelled with the BOOT button.
static bool captureRaw(int32_t& rx, int32_t& ry, bool allowCancel) {
  RawTouch r;
  // wait for release of any previous touch
  uint32_t upSince = millis();
  while (millis() - upSince < 200) {
    if (touchReadRaw(r)) upSince = millis();
    delay(10);
  }
  int16_t xs[40], ys[40];
  int n = 0, ups = 0;
  bool pressing = false;
  for (;;) {
    if (allowCancel && digitalRead(GL_PIN_BOOT_BUTTON) == LOW) return false;
    bool down = touchReadRaw(r);
    if (down) {
      ups = 0;
      pressing = true;
      if (n < 40) { xs[n] = r.x; ys[n] = r.y; n++; }
    } else if (pressing) {
      if (++ups >= 4) {
        if (n >= 6) break;
        pressing = false;  // too short: ignore and wait again
        n = 0;
      }
    }
    delay(12);
  }
  // drop the first and last samples (finger landing / lifting), take the median
  int a = n > 10 ? 2 : 0, b = n > 10 ? n - 2 : n;
  std::sort(xs + a, xs + b);
  std::sort(ys + a, ys + b);
  rx = xs[(a + b) / 2];
  ry = ys[(a + b) / 2];
  return true;
}

bool uiRunCalibration(bool allowCancel) {
  // targets in the current screen rotation; the fit is done in the canonical
  // (rotation 1) frame so the calibration stays valid in every orientation
  const uint8_t rot = settingsRotation();
  const int W = tft.width(), H = tft.height();
  const int32_t sx[5] = {28, W - 28, W - 28, 28, W / 2};
  const int32_t sy[5] = {28, 28, H - 28, H - 28, H / 2};
  int32_t cx[5], cy[5];
  for (int i = 0; i < 5; i++) rotToCanonical(rot, sx[i], sy[i], cx[i], cy[i]);
  backlightSet(g_settings.brightness < 40 ? 40 : g_settings.brightness);
  LOGI("TOUCH", "calibration started");
  for (int attempt = 0; attempt < 10; attempt++) {
    int32_t rx[5], ry[5];
    for (int i = 0; i < 5; i++) {
      char step[48];
      snprintf(step, sizeof(step), "Tap the center of the target  (%d/5)", i + 1);
      calScreen("TOUCH CALIBRATION", step, allowCancel ? "Use a stylus or fingernail. BOOT = cancel" : "Use a stylus or fingernail.");
      drawTarget(sx[i], sy[i], accent());
      if (!captureRaw(rx[i], ry[i], allowCancel)) {
        LOGI("TOUCH", "calibration cancelled");
        return false;
      }
      drawTarget(sx[i], sy[i], col::GREEN);
      LOGI("TOUCH", "point %d screen(%d,%d) raw(%d,%d)", i + 1, (int)sx[i], (int)sy[i], (int)rx[i], (int)ry[i]);
      delay(250);
    }
    TouchCal cal;
    float rms = 99;
    bool ok = fitTouchCal(rx, ry, cx, cy, 5, cal, &rms);
    LOGI("TOUCH", "fit %s, rms error %.1f px", ok ? "ok" : "FAILED", rms);
    if (!ok || rms > 10.0f) {
      calScreen("NOT ACCURATE ENOUGH", "Let's try again - tap each target precisely.", nullptr);
      delay(1600);
      continue;
    }
    // verification point not used for fitting
    const int vx = W * 74 / 100, vy = H * 68 / 100;
    calScreen("CHECK", "Tap the last target to confirm", nullptr);
    drawTarget(vx, vy, accent());
    int32_t qx, qy;
    if (!captureRaw(qx, qy, allowCancel)) return false;
    int32_t lx, ly, mx, my;
    applyTouchCal(cal, qx, qy, lx, ly);
    rotFromCanonical(rot, lx, ly, mx, my);
    int err = (int)sqrtf((float)((mx - vx) * (mx - vx) + (my - vy) * (my - vy)));
    LOGI("TOUCH", "verification: expected (%d,%d) got (%d,%d), error %d px", vx, vy, (int)mx, (int)my, err);
    if (err > 16) {
      calScreen("CHECK FAILED", "The touch point was off. Starting again.", nullptr);
      delay(1600);
      continue;
    }
    touchCalSave(cal);
    touchSetCal(cal);
    calScreen("CALIBRATION SAVED", "Touch is ready.", nullptr);
    delay(900);
    touchFlush();
    tft.fillScreen(col::BG);
    return true;
  }
  return false;
}
