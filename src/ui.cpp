// Gridline F1 — UI core: header, navigation, dialogs, input dispatch, backlight
#include "ui.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "settings.h"
#include "touch.h"
#include "ui_internal.h"

using namespace gl;

namespace ui {

Layout L;

void layoutApply(uint8_t rotation) {
  L.portrait = (rotation & 1) == 0;
  if (!L.portrait) {
    L.W = 320; L.H = 240;
    L.headerH = 26; L.contentY = 26; L.navY = 206; L.navH = 34;
    L.m = 14;
    L.listW = 286; L.listHdrY = 26; L.listY0 = 42; L.rowH = 20; L.rows = 8;
    L.pagerBelow = false; L.pagerX = 286; L.pagerW = 34; L.pagerY = 0; L.pagerH = 0;
    L.setRows = 6;
  } else {
    L.W = 240; L.H = 320;
    L.headerH = 44; L.contentY = 44; L.navY = 284; L.navH = 36;
    L.m = 12;
    L.listW = 240; L.listHdrY = 44; L.listY0 = 60; L.rowH = 20; L.rows = 10;
    L.pagerBelow = true; L.pagerX = 0; L.pagerW = 240; L.pagerY = 260; L.pagerH = 24;
    L.setRows = 7;
  }
  L.contentH = L.navY - L.contentY;
}

Screen g_screen = S_NEXT;
uint8_t g_page = 0;
uint8_t g_raceIndex = 0;
bool g_weekendView = false;
bool g_useNextSeason = false;

static Button s_buttons[40];
static int s_buttonCount = 0;
static bool s_needFull = true, s_needContent = false, s_needStatus = false, s_needHeader = true;
static uint32_t s_lastVersions[DS_COUNT];
static uint32_t s_lastNetVersion = 0;
static int s_lastMinute = -1;
static uint32_t s_lastSecondTick = 0;
static char s_lastStatus[48] = {0};
static uint16_t s_lastStatusDot = 0;
static int s_titleEnd = 132;

// toast
static char s_toast[40] = {0};
static uint32_t s_toastUntil = 0;
// dialog
static bool s_dialog = false;
static char s_dlgTitle[32], s_dlgMsg[200], s_dlgOk[16];
static uint8_t s_dlgAction = 0;
// tabs
static const char* const kTabLabels[5] = {"NEXT", "DRIVERS", "TEAMS", "RESULTS", "CALENDAR"};
static int16_t s_tabX[6], s_tabW[6];
static bool s_tabsLaidOut = false;
static int16_t s_indicatorX = -1, s_indicatorW = 0;

void addButton(int x, int y, int w, int h, uint8_t id) {
  if (s_buttonCount < (int)(sizeof(s_buttons) / sizeof(s_buttons[0]))) s_buttons[s_buttonCount++] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, id};
}
void clearButtons() { s_buttonCount = 0; }
void requestContent() { s_needContent = true; }
void requestFull() { s_needFull = true; }

static bool isTab(Screen s) { return s <= S_CALENDAR; }

static int tabOf(Screen s) {
  if (isTab(s)) return (int)s;
  if (s == S_RACE) return S_CALENDAR;
  return 5;  // settings group
}

void go(Screen s) {
  s_dialog = false;  // a screen change always dismisses an open dialog
  if (g_screen == S_NEXT && s != S_NEXT) leaveNext();
  bool tabChange = tabOf(s) != tabOf(g_screen);
  g_screen = s;
  g_page = 0;
  g_weekendView = false;
  if (s == S_CALENDAR) {
    // open on the page containing the next race
    DataLock l;
    int64_t now = nowUtc();
    const Calendar* cal = &g_data.calendar;
    g_useNextSeason = false;
    if (now && seasonComplete(now, cal) && g_data.calendarNext.count && g_data.calendarNext.season == cal->season + 1) {
      g_useNextSeason = true;
      cal = &g_data.calendarNext;
    }
    int idx = now ? nextRaceIndex(now, cal) : -1;
    if (idx >= 0) g_page = (uint8_t)(idx / L.rows);
  }
  if (s == S_NEXT) enterNext();
  s_needContent = true;
  s_needHeader = true;
  s_lastStatus[0] = 0;
  if (tabChange) s_needFull = true;
  touchFlush();
}

void showToast(const char* msg) {
  strncpy(s_toast, msg, sizeof(s_toast) - 1);
  s_toast[sizeof(s_toast) - 1] = 0;
  s_toastUntil = millis() + 1800;
  useFont(F_S);
  int w = tft.textWidth(s_toast) + 32;
  int x = (L.W - w) / 2;
  int y = L.navY - 30;
  tft.fillRoundRect(x, y, w, 24, 12, col::PANEL2);
  tft.drawRoundRect(x, y, w, 24, 12, accent());
  drawText(s_toast, L.W / 2, y + 12, F_S, col::TEXT, col::PANEL2, MC_DATUM);
}

void openDialog(const char* title, const char* msg, const char* okLabel, uint8_t action) {
  strncpy(s_dlgTitle, title, sizeof(s_dlgTitle) - 1);
  s_dlgTitle[sizeof(s_dlgTitle) - 1] = 0;
  strncpy(s_dlgMsg, msg, sizeof(s_dlgMsg) - 1);
  s_dlgMsg[sizeof(s_dlgMsg) - 1] = 0;
  strncpy(s_dlgOk, okLabel, sizeof(s_dlgOk) - 1);
  s_dlgOk[sizeof(s_dlgOk) - 1] = 0;
  s_dlgAction = action;
  s_dialog = true;
  // dim the content behind the dialog
  tft.fillRect(0, L.contentY, L.W, L.contentH, col::BG);
  const int bw = L.W - 48 > 272 ? 272 : L.W - 24;       // dialog box
  const int lines = L.portrait ? 4 : 3;
  const int bh = 86 + lines * 16;
  const int bx = (L.W - bw) / 2, by = L.contentY + (L.contentH - bh) / 2;
  tft.fillRoundRect(bx, by, bw, bh, 10, col::PANEL2);
  tft.drawRoundRect(bx, by, bw, bh, 10, col::LINE);
  tft.fillRect(bx, by + 12, 4, 20, accent());
  drawText(s_dlgTitle, bx + 16, by + 12, F_M, col::TEXT, col::PANEL2, TL_DATUM);
  drawWrapped(s_dlgMsg, bx + 16, by + 38, bw - 32, 16, lines, F_S, col::TEXT2, col::PANEL2);
  const int btnW = (bw - 48) / 2, btnY = by + bh - 44;
  tft.fillRoundRect(bx + 16, btnY, btnW, 32, 8, col::PANEL);
  tft.drawRoundRect(bx + 16, btnY, btnW, 32, 8, col::LINE);
  drawText("Cancel", bx + 16 + btnW / 2, btnY + 16, F_M, col::TEXT, col::PANEL, MC_DATUM);
  tft.fillRoundRect(bx + bw - 16 - btnW, btnY, btnW, 32, 8, accent());
  drawText(s_dlgOk, bx + bw - 16 - btnW / 2, btnY + 16, F_M, col::BG, accent(), MC_DATUM);
  clearButtons();
  addButton(bx + 12, btnY - 6, btnW + 8, 44, B_DLG_CANCEL);
  addButton(bx + bw - 20 - btnW, btnY - 6, btnW + 8, 44, B_DLG_OK);
  touchFlush();
}

// ------------------------------------------------------------------ helpers
void clearContent() { tft.fillRect(0, L.contentY, L.W, L.contentH, col::BG); }

int rowY(int row) { return L.listY0 + row * L.rowH; }

void drawRowBackground(int row, bool highlight) {
  int y = rowY(row);
  uint16_t bg = highlight ? accentDim() : ((row & 1) ? col::ZEBRA : col::BG);
  tft.fillRect(0, y, L.listW, L.rowH, bg);
  if (highlight) tft.fillRect(0, y, 3, L.rowH, accent());
}

void drawPager(uint8_t page, uint8_t pages) {
  bool canUp = page > 0, canDown = page + 1 < pages;
  char buf[12];
  snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(page + 1), (unsigned)(pages ? pages : 1));
  if (L.pagerBelow) {
    // horizontal strip under the list:  [ ^ ]   1/3   [ v ]
    int y0 = rowY(L.rows);
    if (L.pagerY > y0) tft.fillRect(0, y0, L.W, L.pagerY - y0, col::BG);
    tft.fillRect(0, L.pagerY, L.W, L.pagerH, col::BG);
    tft.drawFastHLine(L.m, L.pagerY + 1, L.W - 2 * L.m, col::LINE);
    int cy = L.pagerY + L.pagerH / 2 + 1;
    int ux = L.W / 6, dx = L.W - L.W / 6;
    tft.fillTriangle(ux - 8, cy + 5, ux + 8, cy + 5, ux, cy - 6, canUp ? col::TEXT2 : col::LINE);
    tft.fillTriangle(dx - 8, cy - 5, dx + 8, cy - 5, dx, cy + 6, canDown ? col::TEXT2 : col::LINE);
    drawText(buf, L.W / 2, cy, F_S, col::TEXT3, col::BG, MC_DATUM);
    if (canUp) addButton(0, L.pagerY - 2, L.W / 3, L.pagerH + 2, B_PAGE_UP);
    if (canDown) addButton(L.W - L.W / 3, L.pagerY - 2, L.W / 3, L.pagerH + 2, B_PAGE_DOWN);
    return;
  }
  // vertical pager at the right edge
  tft.fillRect(0, rowY(L.rows), L.listW, L.navY - rowY(L.rows), col::BG);  // strip under the last row
  tft.fillRect(L.pagerX, L.contentY, L.pagerW, L.contentH, col::BG);
  tft.drawFastVLine(L.pagerX, L.contentY + 6, L.contentH - 12, col::LINE);
  int cx = L.pagerX + L.pagerW / 2 + 1;
  int top = L.contentY + 32, bottom = L.navY - 32, mid = (L.contentY + L.navY) / 2;
  tft.fillTriangle(cx - 8, top + 12, cx + 8, top + 12, cx, top, canUp ? col::TEXT2 : col::LINE);
  tft.fillTriangle(cx - 8, bottom - 12, cx + 8, bottom - 12, cx, bottom, canDown ? col::TEXT2 : col::LINE);
  drawText(buf, cx, mid, F_S, col::TEXT3, col::BG, MC_DATUM);
  if (canUp) addButton(L.pagerX, L.contentY, L.pagerW, L.contentH / 2 - 2, B_PAGE_UP);
  if (canDown) addButton(L.pagerX, mid + 2, L.pagerW, L.contentH / 2 - 2, B_PAGE_DOWN);
}

void drawListHeader(const char* const* labels, const int* xs, const uint8_t* datums, int n) {
  tft.fillRect(0, L.listHdrY, L.listW, L.listY0 - L.listHdrY, col::BG);
  for (int i = 0; i < n; i++) drawText(labels[i], xs[i], L.listHdrY + 8, F_S, col::TEXT3, col::BG, datums[i]);
}

void drawTeamBar(int x, int y, const char* teamId) {
  TeamStyle t = teamStyle(teamId, nullptr);
  tft.fillRect(x, y, 3, 14, rgb565(t.rgb));
}

void drawChevron(int x, int cy, uint16_t c) {
  tft.drawLine(x, cy - 4, x + 4, cy, c);
  tft.drawLine(x + 4, cy, x, cy + 4, c);
  tft.drawLine(x + 1, cy - 4, x + 5, cy, c);
  tft.drawLine(x + 5, cy, x + 1, cy + 4, c);
}

void drawMinusPlus(int x, int y, uint8_t idMinus, uint8_t idPlus, const char* value) {
  // [ - ]  value  [ + ]  : total width 112
  tft.fillRoundRect(x, y, 30, 23, 6, col::PANEL2);
  tft.fillRect(x + 9, y + 11, 12, 2, col::TEXT);
  tft.fillRoundRect(x + 82, y, 30, 23, 6, col::PANEL2);
  tft.fillRect(x + 91, y + 11, 12, 2, col::TEXT);
  tft.fillRect(x + 96, y + 6, 2, 12, col::TEXT);
  tft.fillRect(x + 31, y, 50, 23, col::BG);
  drawText(value, x + 56, y + 12, F_M, col::TEXT, col::BG, MC_DATUM);
  addButton(x - 6, y - 3, 40, 29, idMinus);
  addButton(x + 78, y - 3, 40, 29, idPlus);
}

void drawToggle(int x, int y, bool on) {
  tft.fillRoundRect(x, y, 42, 22, 11, on ? accent() : col::PANEL2);
  tft.fillCircle(on ? x + 31 : x + 11, y + 11, 8, on ? col::BG : col::TEXT3);
}

void formatWhen(int64_t utc, char* out, size_t n) {
  if (utc <= 0) { snprintf(out, n, "-"); return; }
  int64_t now = nowUtc();
  CivilTime c = toIsrael(utc);
  if (now) {
    CivilTime t = toIsrael(now);
    if (t.year == c.year && t.month == c.month && t.day == c.day) { formatHm(c, out, n); return; }
  }
  formatShortDate(c, out, n);
}

uint8_t pageCount(int items, int perPage) {
  if (items <= 0) return 1;
  return (uint8_t)((items + perPage - 1) / perPage);
}

// ------------------------------------------------------------------ header
static void drawMark(int x) {
  uint16_t a = accent();
  tft.fillTriangle(x, 19, x + 4, 6, x + 8, 6, a);
  tft.fillTriangle(x, 19, x + 8, 6, x + 4, 19, a);
  uint16_t b = blend565(a, col::BG, 110);
  tft.fillTriangle(x + 6, 19, x + 10, 6, x + 13, 6, b);
  tft.fillTriangle(x + 6, 19, x + 13, 6, x + 9, 19, b);
}

static const char* screenTitle() {
  switch (g_screen) {
    case S_NEXT: return "NEXT RACE";
    case S_DRIVERS: return "DRIVERS";
    case S_TEAMS: return "CONSTRUCTORS";
    case S_RESULTS: return "LAST RACE";
    case S_CALENDAR: return "CALENDAR";
    case S_SETTINGS: return "SETTINGS";
    case S_NIGHT: return "NIGHT DIMMING";
    case S_FAVORITE: return "FAVORITE DRIVER";
    case S_ABOUT: return "ABOUT";
    case S_RACE: return "RACE WEEKEND";
  }
  return "";
}

static bool hasBack() { return g_screen == S_NIGHT || g_screen == S_FAVORITE || g_screen == S_ABOUT || g_screen == S_RACE; }

static int relevantDataset() {
  switch (g_screen) {
    case S_NEXT: case S_CALENDAR: case S_RACE: return DS_CALENDAR;
    case S_DRIVERS: case S_FAVORITE: return DS_DRIVERS;
    case S_TEAMS: return DS_TEAMS;
    case S_RESULTS: return DS_RESULTS;
    default: return -1;
  }
}

static void computeStatus(char* text, size_t n, uint16_t& dot, uint16_t& fg) {
  DataLock l;
  const NetStatus& ns = g_data.net;
  fg = col::TEXT2;
  int ds = relevantDataset();
  char when[16] = {0};
  const DatasetMeta* m = ds >= 0 ? &g_data.meta[ds] : nullptr;
  if (m && m->origin != ORIGIN_NONE) formatWhen(m->fetchedUtc, when, sizeof(when));
  if (ns.activity == ACT_UPDATING) { snprintf(text, n, "Updating"); dot = accent(); return; }
  if (ns.activity == ACT_SELFTEST) { snprintf(text, n, "TLS test"); dot = accent(); return; }
  if (ns.wifi == WS_NO_CREDS) { snprintf(text, n, "Wi-Fi not set up"); dot = col::RED; return; }
  if (ns.wifi == WS_CONNECTING) { snprintf(text, n, "Connecting"); dot = accent(); return; }
  if (ns.wifi == WS_LOST) {
    if (when[0]) snprintf(text, n, "Offline · %s", when); else snprintf(text, n, "Offline");
    dot = col::RED;
    return;
  }
  if (!ns.clockValid) { snprintf(text, n, "Syncing clock"); dot = accent(); return; }
  if (!m) {
    if (ns.lastError && ns.lastError != FE_EMPTY) { snprintf(text, n, "%s", fetchErrorUi(ns.lastError)); dot = col::AMBER; }
    else { snprintf(text, n, "Online"); dot = col::GREEN; }
    return;
  }
  if (m->origin == ORIGIN_NONE) {
    if (m->lastError == FE_EMPTY) { snprintf(text, n, "No data yet"); dot = col::TEXT3; }
    else if (m->lastError) { snprintf(text, n, "%s", fetchErrorUi(m->lastError)); dot = col::RED; }
    else { snprintf(text, n, "Waiting"); dot = col::TEXT3; }
    return;
  }
  int64_t now = nowUtc();
  bool stale = now && now - m->fetchedUtc > 24 * 3600;
  if (m->lastError && m->lastError != FE_EMPTY) {
    snprintf(text, n, "%s · %s", fetchErrorUi(m->lastError), when);
    dot = col::AMBER;
  } else if (m->origin == ORIGIN_CACHE) {
    snprintf(text, n, "Cached · %s", when);
    dot = col::AMBER;
  } else if (stale) {
    snprintf(text, n, "Stale · %s", when);
    dot = col::AMBER;
  } else {
    snprintf(text, n, "Updated %s", when);
    dot = col::GREEN;
    fg = col::TEXT3;
  }
}

// Header geometry. Landscape: one 26 px row [mark TITLE sub ... status clock].
// Portrait: row 1 (0..25) [mark TITLE sub ... gear], row 2 (26..43) [status ... clock].
static const int kRow2Y = 35;  // centre of the portrait status row

static void drawClock() {
  int64_t now = nowUtc();
  char buf[8];
  if (now) {
    CivilTime c = toIsrael(now);
    formatHm(c, buf, sizeof(buf));
    s_lastMinute = c.minute;
  } else {
    snprintf(buf, sizeof(buf), "--:--");
  }
  if (L.portrait) {
    tft.fillRect(L.W - 48, 27, 48, 16, col::BG);
    drawText(buf, L.W - 8, kRow2Y, F_S, col::TEXT, col::BG, MR_DATUM);
  } else {
    tft.fillRect(272, 0, 48, L.headerH - 1, col::BG);
    drawText(buf, 314, 13, F_S, col::TEXT, col::BG, MR_DATUM);
  }
}

static void drawStatus(bool force) {
  char text[48];
  uint16_t dot, fg;
  computeStatus(text, sizeof(text), dot, fg);
  if (!force && strcmp(text, s_lastStatus) == 0 && dot == s_lastStatusDot) return;
  strncpy(s_lastStatus, text, sizeof(s_lastStatus) - 1);
  s_lastStatusDot = dot;
  if (L.portrait) {
    // left aligned in the second header row
    int maxW = L.W - 56 - 22;
    tft.fillRect(0, 27, L.W - 50, 16, col::BG);
    tft.fillCircle(L.m, kRow2Y, 3, dot);
    drawTextFit(text, L.m + 9, kRow2Y, maxW, F_S, fg, col::BG, ML_DATUM);
    return;
  }
  int right = 270;
  int left = s_titleEnd + 6;
  if (left > right - 60) left = right - 60;
  tft.fillRect(left, 0, right - left + 1, L.headerH - 1, col::BG);
  drawTextFit(text, right - 2, 13, right - left - 14, F_S, fg, col::BG, MR_DATUM);
  useFont(F_S);
  int w = tft.textWidth(text);
  if (w > right - left - 14) w = right - left - 14;
  tft.fillCircle(right - 2 - w - 8, 13, 3, dot);
}

const char* headerSubtitle(char* buf, size_t n) {
  buf[0] = 0;
  DataLock l;
  switch (g_screen) {
    case S_DRIVERS:
    case S_TEAMS: {
      uint16_t season = g_screen == S_DRIVERS ? g_data.drivers.season : g_data.teams.season;
      uint8_t round = g_screen == S_DRIVERS ? g_data.drivers.round : g_data.teams.round;
      if (season) snprintf(buf, n, "%u · R%u", season, round);
      break;
    }
    case S_RESULTS:
      if (g_data.results.season) snprintf(buf, n, "%u · R%u", g_data.results.season, g_data.results.round);
      break;
    case S_CALENDAR: {
      const Calendar& c = g_useNextSeason ? g_data.calendarNext : g_data.calendar;
      if (c.season) snprintf(buf, n, "%u", c.season);
      break;
    }
    default: break;
  }
  return buf;
}

static void drawSettingsIcon(int cx, int cy, uint16_t c) {
  for (int i = 0; i < 3; i++) {
    int y = cy - 6 + i * 6;
    tft.drawFastHLine(cx - 9, y, 18, c);
    int kx = cx + (i == 0 ? -4 : i == 1 ? 4 : -1);
    tft.fillCircle(kx, y, 2, c);
  }
}

static void drawHeader() {
  tft.fillRect(0, 0, L.W, L.headerH - 1, col::BG);
  tft.drawFastHLine(0, L.headerH - 1, L.W, col::LINE);
  int x;
  if (hasBack()) {
    // back affordance
    tft.drawLine(13, 7, 7, 13, col::TEXT);
    tft.drawLine(7, 13, 13, 19, col::TEXT);
    tft.drawLine(14, 7, 8, 13, col::TEXT);
    tft.drawLine(8, 13, 14, 19, col::TEXT);
    x = 22;
  } else {
    drawMark(6);
    x = 26;
  }
  const char* title = screenTitle();
  drawText(title, x, 13, F_S, col::TEXT, col::BG, ML_DATUM);
  int tw = textWidth(title, F_S);
  s_titleEnd = x + tw;
  char sub[24];
  headerSubtitle(sub, sizeof(sub));
  int subLimit = L.portrait ? L.W - 40 : 160;  // portrait: keep clear of the settings button
  if (sub[0]) {
    int sw = textWidth(sub, F_S);
    if (x + tw + 8 + sw <= subLimit) {
      drawText(sub, x + tw + 8, 13, F_S, col::TEXT3, col::BG, ML_DATUM);
      s_titleEnd = x + tw + 8 + sw;
    }
  }
  if (L.portrait) {
    bool active = tabOf(g_screen) == 5;
    if (active) tft.fillRoundRect(L.W - 38, 2, 34, 22, 6, col::PANEL2);
    drawSettingsIcon(L.W - 21, 13, active ? col::TEXT : col::TEXT2);
  }
  drawClock();
  drawStatus(true);
}

// ------------------------------------------------------------------ navigation bar
static FontId tabFont() { return L.portrait ? F_XS : F_S; }

static void layoutTabs() {
  FontId f = tabFont();
  int total = 0, w[5];
  for (int i = 0; i < 5; i++) { w[i] = textWidth(kTabLabels[i], f); total += w[i]; }
  int avail = L.portrait ? L.W : 278;   // portrait: the settings button lives in the header
  int pad = (avail - total) / 5;
  int extra = (avail - total) - pad * 5;
  int x = 0;
  for (int i = 0; i < 5; i++) {
    s_tabX[i] = x;
    s_tabW[i] = w[i] + pad + (i == 4 ? extra : 0);
    x += s_tabW[i];
  }
  if (L.portrait) {
    s_tabX[5] = L.W - 44;   // header settings button (hit test only)
    s_tabW[5] = 44;
  } else {
    s_tabX[5] = 280;
    s_tabW[5] = 40;
  }
  s_tabsLaidOut = true;
}

static void drawIndicator(int x, int w) {
  tft.fillRect(0, L.navY + 1, L.W, 3, col::PANEL);
  if (w > 0) tft.fillRect(x + 6, L.navY + 1, w - 12, 3, accent());
}

static void drawNav(bool animate) {
  if (!s_tabsLaidOut) layoutTabs();
  int active = tabOf(g_screen);
  tft.fillRect(0, L.navY, L.W, L.navH, col::PANEL);
  tft.drawFastHLine(0, L.navY, L.W, col::LINE);
  for (int i = 0; i < 5; i++) {
    uint16_t c = (i == active) ? col::TEXT : col::TEXT3;
    drawText(kTabLabels[i], s_tabX[i] + s_tabW[i] / 2, L.navY + L.navH / 2 + 2, tabFont(), c, col::PANEL, MC_DATUM);
  }
  if (!L.portrait) drawSettingsIcon(300, L.navY + 19, active == 5 ? col::TEXT : col::TEXT3);
  int tx = s_tabX[active], tw = s_tabW[active];
  if (L.portrait && active == 5) tw = 0;   // no tab underline while in the settings group
  if (animate && s_indicatorX >= 0 && s_indicatorX != tx && tw > 0 && s_indicatorW > 0) {
    // short slide of the active-tab indicator
    int fx = s_indicatorX, fw = s_indicatorW;
    for (int k = 1; k <= 5; k++) {
      int ix = fx + (tx - fx) * k / 5, iw = fw + (tw - fw) * k / 5;
      drawIndicator(ix, iw);
      delay(14);
    }
  }
  drawIndicator(tx, tw);
  s_indicatorX = tx;
  s_indicatorW = tw;
}

static int navHit(int x, int y) {
  if (L.portrait && y < L.headerH && x >= s_tabX[5]) return 5;  // header settings button
  if (y < L.navY - 4) return -1;
  int n = L.portrait ? 5 : 6;
  for (int i = 0; i < n; i++)
    if (x >= s_tabX[i] && x < s_tabX[i] + s_tabW[i]) return i;
  return -1;
}

// ------------------------------------------------------------------ backlight / night mode
static uint32_t s_lastBacklightEval = 0;
static uint8_t s_effectiveBrightness = 0;

static bool nightActive() {
  if (!g_settings.nightEnabled) return false;
  int64_t now = nowUtc();
  if (!now) return false;
  int h = toIsrael(now).hour;
  int s = g_settings.nightStart, e = g_settings.nightEnd;
  if (s == e) return false;
  return s < e ? (h >= s && h < e) : (h >= s || h < e);
}

static void applyBacklight(bool force) {
  uint8_t target = g_settings.brightness;
  if (nightActive() && g_settings.nightBrightness < target) target = g_settings.nightBrightness;
  if (force || target != s_effectiveBrightness) {
    s_effectiveBrightness = target;
    backlightSet(target);
  }
}

// ------------------------------------------------------------------ input
static void drawContent() {
  clearButtons();
  switch (g_screen) {
    case S_NEXT: drawNext(); break;
    case S_DRIVERS: drawDrivers(); break;
    case S_TEAMS: drawTeams(); break;
    case S_RESULTS: drawResults(); break;
    case S_CALENDAR: drawCalendar(); break;
    case S_RACE: drawRace(); break;
    case S_SETTINGS: drawSettings(); break;
    case S_NIGHT: drawNight(); break;
    case S_FAVORITE: drawFavorite(); break;
    case S_ABOUT: drawAbout(); break;
  }
}

static void flashButton(const Button& b) {
  tft.drawRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 4, accent());
}

static void handleTap(int16_t x, int16_t y) {
  if (s_dialog) {
    for (int i = 0; i < s_buttonCount; i++) {
      const Button& b = s_buttons[i];
      if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
        s_dialog = false;
        s_needContent = true;
        if (b.id == B_DLG_OK) dialogAction(s_dlgAction);
        return;
      }
    }
    return;  // taps outside the dialog are ignored
  }
  int tab = navHit(x, y);
  if (tab >= 0) {
    Screen target = tab < 5 ? (Screen)tab : S_SETTINGS;
    if (target == g_screen && g_page != 0) {  // tapping the active tab returns to page 1
      g_page = 0;
      s_needContent = true;
      return;
    }
    if (target != g_screen) {
      go(target);
      drawNav(true);
      if (L.portrait) s_needHeader = true;
      s_needFull = false;
      s_needContent = true;
      s_needHeader = true;
    }
    return;
  }
  if (hasBack() && y < 26 + 6 && x < 140) {
    go(g_screen == S_RACE ? S_CALENDAR : S_SETTINGS);
    return;
  }
  for (int i = 0; i < s_buttonCount; i++) {
    const Button& b = s_buttons[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
      flashButton(b);
      if (b.id == B_PAGE_UP) { if (g_page > 0) g_page--; s_needContent = true; return; }
      if (b.id == B_PAGE_DOWN) { g_page++; s_needContent = true; return; }
      if (!handleScreenButton(b.id)) LOGI("UI", "unhandled button %u", b.id);
      return;
    }
  }
}

// BOOT button: hold for 3 s after start-up to reopen Wi-Fi setup
static uint32_t s_bootDownSince = 0;
static bool s_bootArmed = false;

static void checkBootButton() {
  bool down = digitalRead(GL_PIN_BOOT_BUTTON) == LOW;
  if (millis() < 3000) { s_bootDownSince = 0; return; }  // ignore strap/boot phase
  if (down) {
    if (!s_bootDownSince) s_bootDownSince = millis();
    if (!s_bootArmed && millis() - s_bootDownSince > GL_BOOT_HOLD_MS) {
      s_bootArmed = true;
      int bx = (L.W - 220) / 2, by = (L.H - 70) / 2;
      tft.fillRoundRect(bx, by, 220, 70, 10, col::PANEL2);
      tft.drawRoundRect(bx, by, 220, 70, 10, accent());
      drawText("Release to open", L.W / 2, by + 24, F_M, col::TEXT, col::PANEL2, MC_DATUM);
      drawText("Wi-Fi setup", L.W / 2, by + 46, F_M, accent(), col::PANEL2, MC_DATUM);
    }
  } else {
    if (s_bootArmed) {
      LOGI("UI", "BOOT held - restarting into Wi-Fi setup");
      settingsFlush();
      bootRequestSet(BOOT_SETUP);
      delay(200);
      ESP.restart();
    }
    s_bootDownSince = 0;
  }
}

// ------------------------------------------------------------------ main loop
void loopImpl() {
  touchPoll();
  checkBootButton();
  uint32_t ms = millis();
  if (ms - s_lastBacklightEval > 20000) {
    s_lastBacklightEval = ms;
    applyBacklight(false);
  }
  int16_t tx, ty;
  if (touchTakeTap(tx, ty)) handleTap(tx, ty);

  // data changes
  {
    DataLock l;
    for (int ds = 0; ds < DS_COUNT; ds++) {
      if (g_data.version[ds] != s_lastVersions[ds]) {
        s_lastVersions[ds] = g_data.version[ds];
        if (!s_dialog && g_screen != S_SETTINGS && g_screen != S_NIGHT) { s_needContent = true; s_needHeader = true; }
        s_needStatus = true;
      }
    }
    if (g_data.netVersion != s_lastNetVersion) {
      s_lastNetVersion = g_data.netVersion;
      s_needStatus = true;
      if (g_screen == S_ABOUT && !s_dialog) s_needContent = true;
    }
  }
  if (s_toastUntil && ms > s_toastUntil) {
    s_toastUntil = 0;
    if (!s_dialog) s_needContent = true;
  }
  if (s_needFull) {
    s_needFull = false;
    s_needHeader = false;
    drawHeader();
    drawNav(false);
    if (!s_dialog) { drawContent(); s_needContent = false; }
    s_needStatus = false;
  }
  if (s_needHeader && !s_dialog) {
    s_needHeader = false;
    drawHeader();
    s_needStatus = false;
  }
  if (s_needContent && !s_dialog) {
    s_needContent = false;
    drawContent();
  }
  if (s_needStatus) {
    s_needStatus = false;
    drawStatus(false);
  }
  if (ms - s_lastSecondTick >= 1000) {
    s_lastSecondTick = ms;
    int64_t now = nowUtc();
    if (now) {
      CivilTime c = toIsrael(now);
      if (c.minute != s_lastMinute) {
        drawClock();
        drawStatus(true);   // "Updated hh:mm" may turn into a date at midnight
        if (g_screen == S_ABOUT && !s_dialog) s_needContent = true;
      }
    } else if (s_lastMinute != -2) {
      s_lastMinute = -2;
      drawClock();
    }
    if (g_screen == S_NEXT && !s_dialog && !s_toastUntil) tickNext(false);
  }
}

void applyOrientation(uint8_t rotation) {
  tft.setRotation(rotation);
  touchSetRotation(rotation);
  layoutApply(rotation);
  layoutTabs();
  s_indicatorX = -1;
  s_indicatorW = 0;
  tft.fillScreen(col::BG);
  LOGI("UI", "orientation: rotation %u (%s)", rotation, L.portrait ? "portrait" : "landscape");
  if (g_screen == S_NEXT) { leaveNext(); enterNext(); }
  s_needFull = true;
  touchFlush();
}

void begin() {
  layoutApply(settingsRotation());
  layoutTabs();
  applyBacklight(true);
  go(S_NEXT);
  s_needFull = true;
}

}  // namespace ui

void uiBegin() { ui::begin(); }
void uiLoop() { ui::loopImpl(); }

static bool s_splashDrawn = false;
void uiResetSplashForSim() { s_splashDrawn = false; }

void uiShowSplash(const char* status) {
  using namespace ui;
  bool& drawn = s_splashDrawn;
  if (!drawn) {
    drawn = true;
    tft.fillScreen(col::BG);
    uint16_t a = accent();
    const int W = tft.width(), H = tft.height();
    const int ox = W / 2 - 160, oy = H / 2 - 120;   // logo block designed around (160,120)
    // speed stripes
    for (int i = 0; i < 3; i++) {
      int x = ox + 92 + i * 12;
      uint16_t c = blend565(a, col::BG, 255 - i * 70);
      tft.fillTriangle(x, oy + 108, x + 7, oy + 84, x + 13, oy + 84, c);
      tft.fillTriangle(x, oy + 108, x + 13, oy + 84, x + 6, oy + 108, c);
    }
    drawText("GRIDLINE", ox + 134, oy + 96, F_L, col::TEXT, col::BG, ML_DATUM);
    drawText("F1 DESK DISPLAY", ox + 134, oy + 118, F_S, col::TEXT3, col::BG, ML_DATUM);
    char v[32];
    snprintf(v, sizeof(v), "v%s · %s", GL_FW_VERSION, GL_DISPLAY_PROFILE);
    drawText(v, W / 2, H - 14, F_S, col::TEXT3, col::BG, MC_DATUM);
  }
  const int W = tft.width(), H = tft.height();
  tft.fillRect(0, H / 2 + 30, W, 40, col::BG);
  drawText(status, W / 2, H / 2 + 46, F_S, col::TEXT2, col::BG, MC_DATUM);
}

bool uiTouchHeldAtBoot(uint32_t ms) {
  RawTouch r;
  if (!touchReadRaw(r)) return false;
  uiShowSplash("Keep touching to recalibrate...");
  uint32_t start = millis();
  while (millis() - start < ms) {
    if (!touchReadRaw(r)) {
      delay(30);
      if (!touchReadRaw(r)) { uiShowSplash("Starting"); return false; }
    }
    delay(20);
  }
  return true;
}
