// Host-simulator framebuffer implementation of the subset of the TFT_eSPI
// API used by Gridline F1. Smooth-font (VLW) placement follows TFT_eSPI
// 2.5.43: maxAscent = header ascent, yAdvance = ascent + max glyph descent,
// glyph top = cursor_y + maxAscent - dY, anti-aliasing blends with the text
// background colour, fully transparent pixels are not drawn.
#pragma once
#include <stdint.h>
#include <vector>

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

class MockGfx {
 public:
  virtual ~MockGfx() {}
  virtual int W() const = 0;
  virtual int H() const = 0;
  virtual void px(int x, int y, uint16_t c) = 0;

  void fillScreen(uint16_t c) { fillRect(0, 0, W(), H(), c); }
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawPixel(int x, int y, uint16_t c) { px(x, y, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c);
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
  void drawCircle(int x0, int y0, int r, uint16_t c);
  void fillCircle(int x0, int y0, int r, uint16_t c);
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c);
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c);

  void loadFont(const uint8_t* vlw);
  void unloadFont() { font_ = nullptr; }
  int16_t textWidth(const char* s);
  int16_t fontHeight() { return font_ ? yAdvance_ : 8; }
  void setTextColor(uint16_t fg, uint16_t bg, bool fill = false) { fg_ = fg; bg_ = bg; fill_ = fill; }
  void setTextDatum(uint8_t d) { datum_ = d; }
  void setTextWrap(bool, bool = false) {}
  int16_t drawString(const char* s, int x, int y);

 protected:
  struct Glyph { uint32_t code; int h, w, adv, dy, dx; uint32_t bitmap; };
  const uint8_t* font_ = nullptr;
  std::vector<Glyph> glyphs_;
  int ascent_ = 0, descent_ = 0, maxDescent_ = 0, yAdvance_ = 0, spaceWidth_ = 0;
  uint16_t fg_ = 0xFFFF, bg_ = 0;
  bool fill_ = false;
  uint8_t datum_ = 0;
  const Glyph* find(uint32_t code) const;
  void drawGlyph(const Glyph& g, int& cursorX, int cursorY);
  void fillCircleHelper(int x0, int y0, int r, uint8_t corners, int delta, uint16_t c);
  void drawCircleHelper(int x0, int y0, int r, uint8_t corners, uint16_t c);
};

class TFT_eSPI : public MockGfx {
 public:
  static const int kW = 320, kH = 240;   // landscape; portrait swaps them
  uint16_t fb[kW * kH];
  int rot_ = 1;
  void init() {}
  void setRotation(int r) { rot_ = r & 3; }
  int16_t width() const { return (int16_t)W(); }
  int16_t height() const { return (int16_t)H(); }
  int W() const override { return (rot_ & 1) ? kW : kH; }
  int H() const override { return (rot_ & 1) ? kH : kW; }
  void px(int x, int y, uint16_t c) override {
    if (x >= 0 && y >= 0 && x < W() && y < H()) fb[y * W() + x] = c;
  }
  bool writePPM(const char* path) const;
};

class TFT_eSprite : public MockGfx {
 public:
  explicit TFT_eSprite(TFT_eSPI* parent) : parent_(parent) {}
  void setColorDepth(int) {}
  void* createSprite(int w, int h) { w_ = w; h_ = h; buf_.assign((size_t)w * h, 0); return buf_.data(); }
  void deleteSprite() { buf_.clear(); w_ = h_ = 0; }
  void fillSprite(uint16_t c) { fillScreen(c); }
  void pushSprite(int x, int y) {
    for (int j = 0; j < h_; j++)
      for (int i = 0; i < w_; i++) parent_->px(x + i, y + j, buf_[j * w_ + i]);
  }
  int W() const override { return w_; }
  int H() const override { return h_; }
  void px(int x, int y, uint16_t c) override {
    if (x >= 0 && y >= 0 && x < w_ && y < h_) buf_[y * w_ + x] = c;
  }

 private:
  TFT_eSPI* parent_;
  int w_ = 0, h_ = 0;
  std::vector<uint16_t> buf_;
};
