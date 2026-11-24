#include "touch.h"
#include <SPI.h>
#include "config.h"

static SPIClass s_spi(VSPI);
static const SPISettings kSpi(2000000, MSBFIRST, SPI_MODE0);
static gl::TouchCal s_cal;
static bool s_hasCal = false;
static uint8_t s_rot = GL_DEFAULT_ROTATION;
static const int16_t kPressThreshold = 400;

void touchBegin() {
  pinMode(GL_TOUCH_CS, OUTPUT);
  digitalWrite(GL_TOUCH_CS, HIGH);
  pinMode(GL_TOUCH_IRQ, INPUT);
  s_spi.begin(GL_TOUCH_SCLK, GL_TOUCH_MISO, GL_TOUCH_MOSI, GL_TOUCH_CS);
}

bool touchIrqActive() { return digitalRead(GL_TOUCH_IRQ) == LOW; }

static uint16_t xfer(uint8_t cmd) {
  s_spi.transfer(cmd);
  uint16_t v = s_spi.transfer16(0);
  return (v >> 3) & 0x0FFF;
}

static void sort5(int16_t* a) {
  for (int i = 1; i < 5; i++) {
    int16_t k = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > k) { a[j + 1] = a[j]; j--; }
    a[j + 1] = k;
  }
}

bool touchReadRaw(RawTouch& r) {
  int16_t xs[5], ys[5];
  s_spi.beginTransaction(kSpi);
  digitalWrite(GL_TOUCH_CS, LOW);
  int16_t z1 = xfer(0xB1);
  int16_t z2 = xfer(0xC1);
  int16_t z = z1 + 4095 - z2;
  xfer(0x91);  // discard first conversion after switching the mux
  for (int i = 0; i < 5; i++) {
    xs[i] = xfer(0xD1);
    ys[i] = xfer(0x91);
  }
  xfer(0xD0);  // power down, re-enable PENIRQ
  digitalWrite(GL_TOUCH_CS, HIGH);
  s_spi.endTransaction();
  sort5(xs);
  sort5(ys);
  r.x = (xs[1] + xs[2] + xs[3]) / 3;
  r.y = (ys[1] + ys[2] + ys[3]) / 3;
  r.z = z;
  // reject readings with excessive spread (finger lifting / sliding)
  bool stable = (xs[3] - xs[1]) < 120 && (ys[3] - ys[1]) < 120;
  r.down = z > kPressThreshold && z1 > 50 && stable && r.x > 50 && r.x < 4050 && r.y > 50 && r.y < 4050;
  return r.down;
}

void touchSetRotation(uint8_t rotation) { s_rot = rotation & 3; }
void touchSetCal(const gl::TouchCal& cal) { s_cal = cal; s_hasCal = true; }
bool touchHasCal() { return s_hasCal; }
void touchClearCal() { s_hasCal = false; }

// ---------------------------------------------------------------- events
static uint8_t s_downCount = 0, s_upCount = 0;
static bool s_pressed = false;
static uint32_t s_pressStart = 0, s_lockoutUntil = 0, s_lastPoll = 0;
static int32_t s_sumX = 0, s_sumY = 0, s_n = 0;
static int16_t s_firstX = 0, s_firstY = 0;
static bool s_moved = false;
static bool s_tapReady = false;
static int16_t s_tapX = 0, s_tapY = 0;

void touchPoll() {
  uint32_t now = millis();
  if (now - s_lastPoll < 12) return;
  s_lastPoll = now;
  RawTouch r;
  bool down = touchReadRaw(r);
  if (down) {
    s_upCount = 0;
    if (!s_pressed) {
      if (now < s_lockoutUntil) return;
      if (++s_downCount >= 2) {
        s_pressed = true;
        s_pressStart = now;
        s_sumX = s_sumY = s_n = 0;
        s_moved = false;
      } else {
        return;
      }
    }
    if (!s_hasCal) return;
    int32_t cx, cy, x, y;
    gl::applyTouchCal(s_cal, r.x, r.y, cx, cy);   // raw -> canonical landscape frame
    gl::rotFromCanonical(s_rot, cx, cy, x, y);    // -> current screen rotation
    if (s_n == 0) { s_firstX = (int16_t)x; s_firstY = (int16_t)y; }
    if (abs(x - s_firstX) > 28 || abs(y - s_firstY) > 28) s_moved = true;
    if (s_n < 6) { s_sumX += x; s_sumY += y; s_n++; }  // average the start of the press
  } else {
    s_downCount = 0;
    if (s_pressed && ++s_upCount >= 3) {
      s_pressed = false;
      uint32_t dur = now - s_pressStart;
      if (s_hasCal && s_n > 0 && !s_moved && dur >= 30 && dur <= 2000) {
        s_tapX = (int16_t)(s_sumX / s_n);
        s_tapY = (int16_t)(s_sumY / s_n);
        s_tapX = constrain(s_tapX, 0, gl::rotWidth(s_rot) - 1);
        s_tapY = constrain(s_tapY, 0, gl::rotHeight(s_rot) - 1);
        s_tapReady = true;
      }
      s_lockoutUntil = now + 180;  // ignore bounce / accidental double taps
    }
  }
}

bool touchTakeTap(int16_t& x, int16_t& y) {
  if (!s_tapReady) return false;
  s_tapReady = false;
  x = s_tapX;
  y = s_tapY;
  return true;
}

bool touchIsDown() { return s_pressed; }

void touchFlush() {
  s_tapReady = false;
  s_pressed = false;
  s_downCount = s_upCount = 0;
  s_lockoutUntil = millis() + 250;
}
