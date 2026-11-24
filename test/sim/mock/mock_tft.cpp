#include "TFT_eSPI.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void MockGfx::fillRect(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) px(i, j, c);
}

void MockGfx::drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void MockGfx::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  // Adafruit-GFX style scanline fill
  auto swap = [](int& a, int& b) { int t = a; a = b; b = t; };
  if (y0 > y1) { swap(y0, y1); swap(x0, x1); }
  if (y1 > y2) { swap(y2, y1); swap(x2, x1); }
  if (y0 > y1) { swap(y0, y1); swap(x0, x1); }
  if (y0 == y2) {
    int a = x0, b = x0;
    if (x1 < a) a = x1; else if (x1 > b) b = x1;
    if (x2 < a) a = x2; else if (x2 > b) b = x2;
    drawFastHLine(a, y0, b - a + 1, c);
    return;
  }
  int dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
  int sa = 0, sb = 0, last = (y1 == y2) ? y1 : y1 - 1, y;
  for (y = y0; y <= last; y++) {
    int a = x0 + sa / dy01, b = x0 + sb / dy02;
    sa += dx01; sb += dx02;
    if (a > b) swap(a, b);
    drawFastHLine(a, y, b - a + 1, c);
  }
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for (; y <= y2; y++) {
    int a = x1 + sa / dy12, b = x0 + sb / dy02;
    sa += dx12; sb += dx02;
    if (a > b) swap(a, b);
    drawFastHLine(a, y, b - a + 1, c);
  }
}

void MockGfx::drawCircle(int x0, int y0, int r, uint16_t c) {
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  px(x0, y0 + r, c); px(x0, y0 - r, c); px(x0 + r, y0, c); px(x0 - r, y0, c);
  while (x < y) {
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    px(x0 + x, y0 + y, c); px(x0 - x, y0 + y, c); px(x0 + x, y0 - y, c); px(x0 - x, y0 - y, c);
    px(x0 + y, y0 + x, c); px(x0 - y, y0 + x, c); px(x0 + y, y0 - x, c); px(x0 - y, y0 - x, c);
  }
}

void MockGfx::fillCircleHelper(int x0, int y0, int r, uint8_t corners, int delta, uint16_t c) {
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r, px_ = x, py_ = y;
  delta++;
  while (x < y) {
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    if (x < (y + 1)) {
      if (corners & 1) drawFastVLine(x0 + x, y0 - y, 2 * y + delta, c);
      if (corners & 2) drawFastVLine(x0 - x, y0 - y, 2 * y + delta, c);
    }
    if (y != py_) {
      if (corners & 1) drawFastVLine(x0 + py_, y0 - px_, 2 * px_ + delta, c);
      if (corners & 2) drawFastVLine(x0 - py_, y0 - px_, 2 * px_ + delta, c);
      py_ = y;
    }
    px_ = x;
  }
}

void MockGfx::fillCircle(int x0, int y0, int r, uint16_t c) {
  drawFastVLine(x0, y0 - r, 2 * r + 1, c);
  fillCircleHelper(x0, y0, r, 3, 0, c);
}

void MockGfx::drawCircleHelper(int x0, int y0, int r, uint8_t corners, uint16_t c) {
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  while (x < y) {
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    if (corners & 4) { px(x0 + x, y0 + y, c); px(x0 + y, y0 + x, c); }
    if (corners & 2) { px(x0 + x, y0 - y, c); px(x0 + y, y0 - x, c); }
    if (corners & 8) { px(x0 - y, y0 + x, c); px(x0 - x, y0 + y, c); }
    if (corners & 1) { px(x0 - y, y0 - x, c); px(x0 - x, y0 - y, c); }
  }
}

void MockGfx::drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  int m = (w < h ? w : h) / 2;
  if (r > m) r = m;
  drawFastHLine(x + r, y, w - 2 * r, c);
  drawFastHLine(x + r, y + h - 1, w - 2 * r, c);
  drawFastVLine(x, y + r, h - 2 * r, c);
  drawFastVLine(x + w - 1, y + r, h - 2 * r, c);
  drawCircleHelper(x + r, y + r, r, 1, c);
  drawCircleHelper(x + w - r - 1, y + r, r, 2, c);
  drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, c);
  drawCircleHelper(x + r, y + h - r - 1, r, 8, c);
}

void MockGfx::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  int m = (w < h ? w : h) / 2;
  if (r > m) r = m;
  fillRect(x + r, y, w - 2 * r, h, c);
  fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c);
  fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, c);
}

// ---------------------------------------------------------------- smooth fonts
static int32_t be32(const uint8_t* p) { return (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

void MockGfx::loadFont(const uint8_t* vlw) {
  font_ = vlw;
  int count = be32(vlw);
  ascent_ = be32(vlw + 16);
  descent_ = be32(vlw + 20);
  glyphs_.clear();
  uint32_t bitmap = 24 + count * 28;
  maxDescent_ = descent_;
  for (int i = 0; i < count; i++) {
    const uint8_t* g = vlw + 24 + i * 28;
    Glyph gl{(uint32_t)be32(g), be32(g + 4), be32(g + 8), be32(g + 12), be32(g + 16), (int8_t)be32(g + 20), bitmap};
    bitmap += gl.w * gl.h;
    if (gl.h - gl.dy > maxDescent_ && ((gl.code > 0x20 && gl.code < 0xA0 && gl.code != 0x7F) || gl.code > 0xFF))
      maxDescent_ = gl.h - gl.dy;
    glyphs_.push_back(gl);
  }
  yAdvance_ = ascent_ + maxDescent_;
  spaceWidth_ = (ascent_ + descent_) * 2 / 7;
}

const MockGfx::Glyph* MockGfx::find(uint32_t code) const {
  for (const Glyph& g : glyphs_)
    if (g.code == code) return &g;
  return nullptr;
}

static uint32_t nextCode(const char*& s) {
  const unsigned char* p = (const unsigned char*)s;
  uint32_t c = *p++;
  if ((c & 0xE0) == 0xC0 && *p) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
  else if ((c & 0xF0) == 0xE0 && p[0] && p[1]) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
  s = (const char*)p;
  return c;
}

int16_t MockGfx::textWidth(const char* s) {
  if (!font_) return (int16_t)(strlen(s) * 6);
  int w = 0;
  while (*s) {
    uint32_t c = nextCode(s);
    if (c == 0x20) { w += spaceWidth_; continue; }
    const Glyph* g = find(c);
    if (!g) { w += spaceWidth_ + 1; continue; }
    if (w == 0 && g->dx < 0) w -= g->dx;
    if (*s) w += g->adv; else w += g->dx + g->w;
  }
  return (int16_t)w;
}

static uint16_t alphaBlend(uint8_t a, uint16_t fg, uint16_t bg) {
  uint16_t al = (a + 4) >> 3;  // as TFT_eSPI: 5-bit alpha approximation
  uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81F;
  uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81F;
  b += (((f - b) * al) >> 5);
  b &= 0x07E0F81F;
  return (uint16_t)(b | (b >> 16));
}

void MockGfx::drawGlyph(const Glyph& g, int& cursorX, int cursorY) {
  if (cursorX == 0) cursorX -= g.dx;
  int cy = cursorY + ascent_ - g.dy;
  int cx = cursorX + g.dx;
  const uint8_t* bmp = font_ + g.bitmap;
  for (int y = 0; y < g.h; y++)
    for (int x = 0; x < g.w; x++) {
      uint8_t a = bmp[y * g.w + x];
      if (!a) continue;
      px(cx + x, cy + y, a == 0xFF ? fg_ : alphaBlend(a, fg_, bg_));
    }
  cursorX += g.adv;
}

int16_t MockGfx::drawString(const char* s, int x, int y) {
  int w = textWidth(s);
  int h = fontHeight();
  switch (datum_) {
    case TC_DATUM: x -= w / 2; break;
    case TR_DATUM: x -= w; break;
    case ML_DATUM: y -= h / 2; break;
    case MC_DATUM: x -= w / 2; y -= h / 2; break;
    case MR_DATUM: x -= w; y -= h / 2; break;
    case BL_DATUM: y -= h; break;
    case BC_DATUM: x -= w / 2; y -= h; break;
    case BR_DATUM: x -= w; y -= h; break;
  }
  if (!font_) return 0;
  int cursorX = x;
  while (*s) {
    uint32_t c = nextCode(s);
    if (c == 0x20) { cursorX += spaceWidth_; continue; }
    const Glyph* g = find(c);
    if (!g) {  // TFT_eSPI draws a box for missing glyphs
      fprintf(stderr, "[sim] missing glyph U+%04X\n", c);
      for (int i = 0; i < spaceWidth_; i++) { px(cursorX + i, y + 2, 0xF800); px(cursorX + i, y + h - 3, 0xF800); }
      cursorX += spaceWidth_ + 1;
      continue;
    }
    drawGlyph(*g, cursorX, y);
  }
  return (int16_t)w;
}

bool TFT_eSPI::writePPM(const char* path) const {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", W(), H());
  for (int i = 0; i < W() * H(); i++) {
    uint16_t c = fb[i];
    unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 0x1F) * 255 / 31), (unsigned char)(((c >> 5) & 0x3F) * 255 / 63),
                            (unsigned char)((c & 0x1F) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return true;
}
