// Gridline F1 — display, fonts, colours, backlight
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

enum FontId : uint8_t { F_NONE = 0, F_S, F_M, F_L, F_XL, F_XS };

void displayBegin();
void useFont(FontId f);
FontId currentFont();
void backlightSet(uint8_t percent);  // 0..100, perceptual curve
uint8_t backlightGet();

uint16_t rgb565(uint32_t rgb888);
uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha);

namespace col {
extern const uint16_t BG, PANEL, PANEL2, LINE, TEXT, TEXT2, TEXT3, AMBER, RED, GREEN, ZEBRA;
}
int accentCount();
uint32_t accentRgb(int i);
const char* accentName(int i);
uint16_t accent();          // current accent (from settings)
uint16_t accentDim();       // accent blended into the background

int textWidth(const char* s, FontId f);
int fontHeight(FontId f);
// Draws text with the given datum (TL_DATUM, MR_DATUM, ...). bg is used for
// anti-aliasing only; callers clear the area themselves.
void drawText(const char* s, int x, int y, FontId f, uint16_t fg, uint16_t bg, uint8_t datum = TL_DATUM);
// Same, but shortens the text with "..." so it fits into maxW pixels.
void drawTextFit(const char* s, int x, int y, int maxW, FontId f, uint16_t fg, uint16_t bg, uint8_t datum = TL_DATUM);
// Splits text into at most maxLines lines of maxW pixels (word wrap).
int drawWrapped(const char* s, int x, int y, int maxW, int lineH, int maxLines, FontId f, uint16_t fg, uint16_t bg);
