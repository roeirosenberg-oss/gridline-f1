#include "display.h"
#include <math.h>
#include "config.h"
#include "fonts/font_l.h"
#include "fonts/font_m.h"
#include "fonts/font_s.h"
#include "fonts/font_xl.h"
#include "fonts/font_xs.h"
#include "settings.h"

TFT_eSPI tft;
static FontId s_font = F_NONE;
static uint8_t s_backlight = 0;
static const int kBlChannel = 7;

uint16_t rgb565(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
  // red+blue with a 6-bit alpha (no carry between the packed channels), green with 8 bits
  uint32_t rb = bg & 0xF81F;
  rb += ((fg & 0xF81F) - rb) * (a >> 2) >> 6;
  uint32_t g = bg & 0x07E0;
  g += ((fg & 0x07E0) - g) * a >> 8;
  return (uint16_t)((rb & 0xF81F) | (g & 0x07E0));
}

namespace col {
const uint16_t BG = rgb565(0x07090D);
const uint16_t ZEBRA = rgb565(0x0C0F14);
const uint16_t PANEL = rgb565(0x10141B);
const uint16_t PANEL2 = rgb565(0x1A1F29);
const uint16_t LINE = rgb565(0x262C38);
const uint16_t TEXT = rgb565(0xF2F4F7);
const uint16_t TEXT2 = rgb565(0xA3ACBA);
const uint16_t TEXT3 = rgb565(0x606A7B);
const uint16_t AMBER = rgb565(0xFFB224);
const uint16_t RED = rgb565(0xFF5050);
const uint16_t GREEN = rgb565(0x3DDC97);
}  // namespace col

static const struct { const char* name; uint32_t rgb; } kAccents[] = {
    {"Ember", 0xFF3B30}, {"Papaya", 0xFF8A00}, {"Cyan", 0x18C6FF},
    {"Volt", 0xB8F02A},  {"Violet", 0xA27BFF}, {"Silver", 0xCDD3DC},
};

int accentCount() { return sizeof(kAccents) / sizeof(kAccents[0]); }
uint32_t accentRgb(int i) { return kAccents[(i >= 0 && i < accentCount()) ? i : 0].rgb; }
const char* accentName(int i) { return kAccents[(i >= 0 && i < accentCount()) ? i : 0].name; }
uint16_t accent() { return rgb565(accentRgb(g_settings.accent)); }
uint16_t accentDim() { return blend565(accent(), col::BG, 46); }

void displayBegin() {
  tft.init();
  tft.setRotation(settingsRotation());
  tft.setTextWrap(false, false);  // never wrap: all layouts clip/shorten explicitly
  tft.fillScreen(col::BG);
  ledcSetup(kBlChannel, 5000, 10);
  ledcAttachPin(GL_PIN_BACKLIGHT, kBlChannel);
  backlightSet(0);
}

void backlightSet(uint8_t percent) {
  if (percent > 100) percent = 100;
  s_backlight = percent;
  // perceptual curve so low settings remain usable
  float f = powf(percent / 100.0f, 2.0f);
  uint32_t duty = (uint32_t)(f * 1023.0f + 0.5f);
  if (percent > 0 && duty < 4) duty = 4;
  ledcWrite(kBlChannel, duty);
}

uint8_t backlightGet() { return s_backlight; }

void useFont(FontId f) {
  if (f == s_font) return;
  if (s_font != F_NONE) tft.unloadFont();
  switch (f) {
    case F_S: tft.loadFont(FONT_S); break;
    case F_M: tft.loadFont(FONT_M); break;
    case F_L: tft.loadFont(FONT_L); break;
    case F_XL: tft.loadFont(FONT_XL); break;
    case F_XS: tft.loadFont(FONT_XS); break;
    default: break;
  }
  s_font = f;
}

FontId currentFont() { return s_font; }

int textWidth(const char* s, FontId f) {
  useFont(f);
  return tft.textWidth(s);
}

int fontHeight(FontId f) {
  useFont(f);
  return tft.fontHeight();
}

void drawText(const char* s, int x, int y, FontId f, uint16_t fg, uint16_t bg, uint8_t datum) {
  if (!s || !*s) return;
  useFont(f);
  tft.setTextColor(fg, bg, false);
  tft.setTextDatum(datum);
  tft.drawString(s, x, y);
}

static size_t prevUtf8(const char* s, size_t len) {
  if (len == 0) return 0;
  size_t i = len - 1;
  while (i > 0 && (((unsigned char)s[i]) & 0xC0) == 0x80) i--;
  return i;
}

void drawTextFit(const char* s, int x, int y, int maxW, FontId f, uint16_t fg, uint16_t bg, uint8_t datum) {
  if (!s || !*s) return;
  useFont(f);
  if (tft.textWidth(s) <= maxW) { drawText(s, x, y, f, fg, bg, datum); return; }
  char buf[96];
  size_t len = strlen(s);
  if (len > sizeof(buf) - 4) len = sizeof(buf) - 4;
  while (len > 0) {
    len = prevUtf8(s, len);
    while (len > 0 && s[len - 1] == ' ') len--;
    memcpy(buf, s, len);
    memcpy(buf + len, "...", 4);
    if (tft.textWidth(buf) <= maxW) break;
  }
  drawText(buf, x, y, f, fg, bg, datum);
}

int drawWrapped(const char* s, int x, int y, int maxW, int lineH, int maxLines, FontId f, uint16_t fg, uint16_t bg) {
  useFont(f);
  char line[96];
  int lines = 0;
  const char* p = s;
  while (*p && lines < maxLines) {
    size_t best = 0, i = 0;
    while (p[i] && i < sizeof(line) - 1) {
      if (p[i] == ' ' || p[i + 1] == 0) {
        size_t cand = (p[i] == ' ') ? i : i + 1;
        memcpy(line, p, cand);
        line[cand] = 0;
        if (tft.textWidth(line) <= maxW) best = cand; else break;
      }
      i++;
    }
    if (best == 0) {  // single long word
      best = i;
    }
    memcpy(line, p, best);
    line[best] = 0;
    if (lines == maxLines - 1 && p[best]) {
      drawTextFit(p, x, y + lines * lineH, maxW, f, fg, bg, TL_DATUM);
    } else {
      drawText(line, x, y + lines * lineH, f, fg, bg, TL_DATUM);
    }
    lines++;
    p += best;
    while (*p == ' ') p++;
  }
  return lines;
}
