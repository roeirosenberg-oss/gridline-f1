#include "ui_setup.h"
#include <stdio.h>
#include "config.h"
#include "display.h"

extern "C" {
// QR encoder: on the device this is the ESP-IDF "qrcode" component
// (Nayuki qrcodegen, MIT); the host simulator links the same library.
enum qrcodegen_Ecc { qrcodegen_Ecc_LOW = 0, qrcodegen_Ecc_MEDIUM, qrcodegen_Ecc_QUARTILE, qrcodegen_Ecc_HIGH };
enum qrcodegen_Mask { qrcodegen_Mask_AUTO = -1 };
bool qrcodegen_encodeText(const char* text, uint8_t tempBuffer[], uint8_t qrcode[], enum qrcodegen_Ecc ecl,
                          int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl);
int qrcodegen_getSize(const uint8_t qrcode[]);
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);
}

static int drawQr(const char* text, int x, int y, int maxSize) {
  static uint8_t qr[220], tmp[220];  // buffers for QR version <= 6
  if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW, 1, 6, qrcodegen_Mask_AUTO, false)) return 0;
  int n = qrcodegen_getSize(qr);
  int scale = maxSize / (n + 4);
  if (scale < 1) scale = 1;
  int size = (n + 4) * scale;
  tft.fillRoundRect(x, y, size, size, 6, TFT_WHITE);
  for (int my = 0; my < n; my++)
    for (int mx = 0; mx < n; mx++)
      if (qrcodegen_getModule(qr, mx, my)) tft.fillRect(x + (mx + 2) * scale, y + (my + 2) * scale, scale, scale, TFT_BLACK);
  return size;
}

// Cancel button: landscape inside the status bar, portrait just above it
// (the narrow status bar needs its full width for the message).
static void cancelRect(int& x, int& y, int& w, int& h) {
  const int W = tft.width(), H = tft.height();
  w = 80; h = 24; x = W - 88;
  y = (W < H) ? H - 34 - 32 : H - 34 + 5;
}

bool setupCancelHit(int16_t tx, int16_t ty) {
  int x, y, w, h;
  cancelRect(x, y, w, h);
  return tx >= x - 10 && tx < x + w + 8 && ty >= y - 8 && ty < y + h + 8;
}

void setupDrawStatus(const SetupView& v) {
  const int W = tft.width(), H = tft.height(), y0 = H - 34, cy = H - 17;
  const bool portrait = W < H;
  tft.fillRect(0, y0, W, 34, col::PANEL);
  tft.drawFastHLine(0, y0, W, col::LINE);
  uint16_t dot = v.state == SETUP_CONNECTED ? col::GREEN
                 : v.state == SETUP_FAILED  ? col::RED
                 : v.state == SETUP_CONNECTING ? accent() : col::TEXT2;
  tft.fillCircle(14, cy, 4, dot);
  bool inBar = v.canCancel && !portrait;
  drawTextFit(v.message, 26, cy, inBar ? W - 124 : W - 34, F_S, col::TEXT, col::PANEL, ML_DATUM);
  if (v.canCancel) {
    int x, y, w, h;
    cancelRect(x, y, w, h);
    uint16_t bg = portrait ? col::BG : col::PANEL;
    if (portrait) tft.fillRect(x - 2, y - 2, w + 4, h + 4, bg);
    tft.fillRoundRect(x, y, w, h, 8, col::PANEL2);
    drawText("Cancel", x + w / 2, y + h / 2, F_S, col::TEXT, col::PANEL2, MC_DATUM);
  }
}

void setupDrawScreen(const SetupView& v) {
  const int W = tft.width();
  const bool portrait = W < tft.height();
  tft.fillScreen(col::BG);
  tft.drawFastHLine(0, 25, W, col::LINE);
  uint16_t a = accent();
  tft.fillTriangle(6, 19, 10, 6, 14, 6, a);
  tft.fillTriangle(6, 19, 14, 6, 10, 19, a);
  drawText("WI-FI SETUP", 22, 13, F_S, col::TEXT, col::BG, ML_DATUM);
  drawText("2.4 GHz only", W - 6, 13, F_S, col::TEXT3, col::BG, MR_DATUM);
  char qr[96];
  snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", v.apSsid, v.apPass);
  if (!portrait) {
    drawText("1  Join this network on your phone", 12, 38, F_S, col::TEXT2, col::BG, ML_DATUM);
    drawText(v.apSsid, 26, 58, F_M, col::TEXT, col::BG, ML_DATUM);
    drawText("Password", 26, 78, F_S, col::TEXT3, col::BG, ML_DATUM);
    drawText(v.apPass, 26, 97, F_L, a, col::BG, ML_DATUM);
    drawText("2  Open in the browser", 12, 124, F_S, col::TEXT2, col::BG, ML_DATUM);
    drawText("http://" GL_AP_IP_STR, 26, 143, F_M, col::TEXT, col::BG, ML_DATUM);
    drawText("3  Pick your Wi-Fi, enter its password", 12, 166, F_S, col::TEXT2, col::BG, ML_DATUM);
    drawText("The page often opens by itself.", 26, 184, F_S, col::TEXT3, col::BG, ML_DATUM);
    int size = drawQr(qr, 210, 32, 104);
    if (size) drawText("Scan to join", 210 + size / 2, 32 + size + 10, F_S, col::TEXT3, col::BG, MC_DATUM);
  } else {
    // portrait 240 x 320: QR code centred, steps below
    drawText("1  Join this network on your phone", 10, 38, F_S, col::TEXT2, col::BG, ML_DATUM);
    drawText(v.apSsid, 24, 58, F_M, col::TEXT, col::BG, ML_DATUM);
    drawText("Password", 24, 78, F_S, col::TEXT3, col::BG, ML_DATUM);
    drawText(v.apPass, 24, 97, F_L, a, col::BG, ML_DATUM);
    int size = drawQr(qr, W - 8 - 80, 48, 80);
    if (size) drawText("Scan", W - 8 - 80 + size / 2, 48 + size + 9, F_S, col::TEXT3, col::BG, MC_DATUM);
    drawText("2  Open in the browser", 10, 150, F_S, col::TEXT2, col::BG, ML_DATUM);
    drawText("http://" GL_AP_IP_STR, 24, 170, F_M, col::TEXT, col::BG, ML_DATUM);
    drawText("3  Pick your Wi-Fi network", 10, 198, F_S, col::TEXT2, col::BG, ML_DATUM);
    drawText("and enter its password.", 24, 216, F_S, col::TEXT2, col::BG, ML_DATUM);
    drawText("The page often opens by itself.", 24, 240, F_S, col::TEXT3, col::BG, ML_DATUM);
  }
  setupDrawStatus(v);
}
