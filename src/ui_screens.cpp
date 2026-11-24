// Gridline F1 — information screens: next race, standings, results, calendar
#include <math.h>
#include "config.h"
#include "fonts/font_xl.h"
#include "settings.h"
#include "ui_internal.h"

using namespace gl;

namespace ui {

// ------------------------------------------------------------------ shared bits
static void drawStar(int cx, int cy, int r, uint16_t c) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = -1.5708f + i * 0.6283f;
    float rr = (i & 1) ? r * 0.45f : r;
    px[i] = cx + rr * cosf(a);
    py[i] = cy + rr * sinf(a);
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    tft.fillTriangle(cx, cy, (int)px[i], (int)py[i], (int)px[j], (int)py[j], c);
  }
}

static void drawEmptyState(const char* title, int ds) {
  clearContent();
  uint8_t wifi, clock, lastErr = 0;
  bool hasCreds;
  {
    DataLock l;
    wifi = g_data.net.wifi;
    clock = g_data.net.clockValid;
    if (ds >= 0) lastErr = g_data.meta[ds].lastError;
    hasCreds = wifi != WS_NO_CREDS;
  }
  const char* msg;
  char buf[64];
  if (!hasCreds) msg = "Wi-Fi is not set up yet.";
  else if (wifi != WS_CONNECTED) msg = "Waiting for Wi-Fi...";
  else if (!clock) msg = "Synchronising the clock...";
  else if (lastErr == FE_EMPTY) msg = "Not published yet for this season.";
  else if (lastErr) { snprintf(buf, sizeof(buf), "%s - retrying automatically.", fetchErrorUi(lastErr)); msg = buf; }
  else msg = "Downloading...";
  // a small "data waiting" glyph
  int cx = L.W / 2, cy = L.contentY + L.contentH / 2 - 38;
  tft.drawCircle(cx, cy, 16, col::LINE);
  tft.drawCircle(cx, cy, 15, col::LINE);
  tft.fillRect(cx - 1, cy - 9, 3, 10, accent());
  tft.fillRect(cx - 1, cy, 8, 3, accent());
  drawTextFit(title, cx, cy + 34, L.W - 2 * L.m, F_M, col::TEXT, col::BG, MC_DATUM);
  drawTextFit(msg, cx, cy + 56, L.W - 2 * L.m, F_S, col::TEXT2, col::BG, MC_DATUM);
  if (!hasCreds) {
    tft.fillRoundRect(cx - 70, cy + 76, 140, 32, 8, accent());
    drawText("Open Wi-Fi setup", cx, cy + 92, F_S, col::BG, accent(), MC_DATUM);
    addButton(cx - 76, cy + 70, 152, 44, B_OPEN_SETUP);
  }
}

static void israelDate(int64_t utc, bool hasTime, char* out, size_t n, bool withWeekday) {
  CivilTime c = hasTime ? toIsrael(utc) : toCivilUtc(utc);
  if (withWeekday) formatDayDate(c, out, n); else formatShortDate(c, out, n);
}

static void israelTime(int64_t utc, bool hasTime, char* out, size_t n) {
  if (!hasTime) { snprintf(out, n, "TBC"); return; }
  formatHm(toIsrael(utc), out, n);
}

static const Calendar* activeCalendar() {
  if (g_data.meta[DS_CALENDAR].origin == ORIGIN_NONE && g_data.calendar.count == 0) return nullptr;
  return &g_data.calendar;
}

static const Calendar* nextSeasonCalendar(const Calendar* cur) {
  if (!cur || !g_data.calendarNext.count) return nullptr;
  return g_data.calendarNext.season == cur->season + 1 ? &g_data.calendarNext : nullptr;
}

// ------------------------------------------------------------------ NEXT RACE
static TFT_eSprite s_digit(&tft);
static bool s_digitReady = false;
static int64_t s_target = 0;
static bool s_targetHasTime = false, s_targetLive = false, s_countdownShown = false;
static int64_t s_liveUntil = 0;
static char s_lastDigits[12];
static int s_groupCount = 0;
static const int kCellW = FONT_XL_DIGIT_ADV + 2;
static const int kGroupW = 2 * kCellW;
static const int kSepW = 16;

// Countdown geometry.
//  landscape: one row of up to 4 groups at y 112.
//  portrait : 4 groups as a 2x2 grid (DAYS HRS / MIN SEC); 1..3 groups in one centred row.
static int countdownTop() { return L.portrait ? 128 : 108; }
static int countdownBottom() { return L.portrait ? 236 : 160; }

static void groupPos(int g, int groups, int& x, int& y) {
  if (!L.portrait) {
    x = 14 + g * (kGroupW + kSepW);
    y = 112;
    return;
  }
  if (groups == 4) {
    x = L.W / 2 - kSepW / 2 - kGroupW + (g % 2) * (kGroupW + kSepW);
    y = (g < 2) ? 134 : 186;
    return;
  }
  int total = groups * kGroupW + (groups - 1) * kSepW;
  x = (L.W - total) / 2 + g * (kGroupW + kSepW);
  y = groups == 1 ? 146 : 160;
}

void enterNext() {
  if (!s_digitReady) {
    s_digit.setColorDepth(16);
    if (s_digit.createSprite(kCellW, FONT_XL_DIGIT_H + 2)) {
      s_digit.setTextWrap(false, false);
      s_digit.loadFont(FONT_XL);
      s_digitReady = true;
    }
  }
  memset(s_lastDigits, 0, sizeof(s_lastDigits));
}

void leaveNext() {
  if (s_digitReady) {
    s_digit.unloadFont();
    s_digit.deleteSprite();
    s_digitReady = false;
  }
  s_countdownShown = false;
}

static void drawDigit(int x, int y, char d) {
  char s[2] = {d, 0};
  if (!s_digitReady) {
    tft.fillRect(x, y, kCellW, FONT_XL_DIGIT_H + 2, col::BG);
    useFont(F_XL);
    tft.setTextColor(col::TEXT, col::BG, false);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(s, x + kCellW / 2, y - FONT_XL_DIGIT_TOP);
    return;
  }
  s_digit.fillSprite(col::BG);
  s_digit.setTextColor(col::TEXT, col::BG, false);
  s_digit.setTextDatum(TC_DATUM);
  s_digit.drawString(s, kCellW / 2, -FONT_XL_DIGIT_TOP + 1);
  s_digit.pushSprite(x, y - 1);
}

static void drawCountdownFrame(int groups, const char* const* labels) {
  tft.fillRect(0, countdownTop(), L.W, countdownBottom() - countdownTop(), col::BG);
  for (int g = 0; g < groups; g++) {
    int x, y;
    groupPos(g, groups, x, y);
    drawText(labels[g], x + kCellW, y + FONT_XL_DIGIT_H + 9, F_S, col::TEXT3, col::BG, MC_DATUM);
    if (g < groups - 1) {
      int nx, ny;
      groupPos(g + 1, groups, nx, ny);
      if (ny == y) {  // separator only between groups of the same row
        int sx = x + kGroupW + kSepW / 2 - 2;
        tft.fillRect(sx, y + 8, 4, 4, col::TEXT3);
        tft.fillRect(sx, y + 19, 4, 4, col::TEXT3);
      }
    }
  }
  if (groups == 1) {
    int x, y;
    groupPos(0, 1, x, y);
    if (L.portrait) {
      drawText("Start time not yet confirmed", L.W / 2, y + FONT_XL_DIGIT_H + 32, F_S, col::AMBER, col::BG, MC_DATUM);
    } else {
      drawText("Start time", 84, y + 6, F_S, col::AMBER, col::BG, TL_DATUM);
      drawText("not yet confirmed", 84, y + 22, F_S, col::AMBER, col::BG, TL_DATUM);
    }
  }
  memset(s_lastDigits, 0, sizeof(s_lastDigits));
}

void tickNext(bool force) {
  if (!s_countdownShown || g_weekendView) return;
  int64_t now = nowUtc();
  if (!now) return;
  if (s_targetLive) {
    if (now > s_liveUntil) requestContent();
    return;
  }
  if (now >= s_target && s_targetHasTime) { requestContent(); return; }
  Countdown cd = splitCountdown(now, s_target);
  char digits[12];
  int groups;
  if (!s_targetHasTime) {
    // only days are meaningful without a start time
    int64_t today = (now + israelOffsetSeconds(now)) / 86400;
    int64_t raceDay = s_target / 86400;
    int64_t days = raceDay - today;
    if (days < 0) days = 0;
    if (days > 99) days = 99;
    snprintf(digits, sizeof(digits), "%02d", (int)days);
    groups = 1;
  } else if (cd.days > 99) {
    snprintf(digits, sizeof(digits), "99%02d%02d", cd.hours, cd.minutes);
    groups = 3;
  } else if (cd.days > 0) {
    snprintf(digits, sizeof(digits), "%02d%02d%02d%02d", (int)cd.days, cd.hours, cd.minutes, cd.seconds);
    groups = 4;
  } else {
    snprintf(digits, sizeof(digits), "%02d%02d%02d", cd.hours, cd.minutes, cd.seconds);
    groups = 3;
  }
  if (groups != s_groupCount || force) {
    s_groupCount = groups;
    static const char* const L4[] = {"DAYS", "HRS", "MIN", "SEC"};
    static const char* const L3d[] = {"DAYS", "HRS", "MIN"};
    static const char* const L3[] = {"HRS", "MIN", "SEC"};
    static const char* const L1[] = {"DAYS"};
    const char* const* labels = groups == 4 ? L4 : groups == 1 ? L1 : (cd.days > 99 ? L3d : L3);
    drawCountdownFrame(groups, labels);
  }
  for (int i = 0; i < groups * 2; i++) {
    if (s_lastDigits[i] != digits[i]) {
      s_lastDigits[i] = digits[i];
      int x, y;
      groupPos(i / 2, groups, x, y);
      drawDigit(x + (i % 2) * kCellW, y, digits[i]);
    }
  }
}

static void drawSeasonComplete(const Calendar* cal) {
  char buf[64];
  const int m = L.m, w = L.W - 2 * L.m, y0 = L.contentY;
  snprintf(buf, sizeof(buf), "%u SEASON COMPLETE", cal->season);
  drawText(buf, m, y0 + 8, F_S, accent(), col::BG, TL_DATUM);
  const DriverStandings& d = g_data.drivers;
  uint8_t lastRound = cal->count ? cal->races[cal->count - 1].round : 0;
  bool final = d.count && d.season == cal->season && d.round >= lastRound && lastRound;
  if (final) {
    const DriverRow& wr = d.rows[0];
    drawText("CHAMPION", m, y0 + 36, F_S, col::TEXT3, col::BG, TL_DATUM);
    drawTeamBar(m, y0 + 58, wr.teamId);
    snprintf(buf, sizeof(buf), "%s %s", wr.given, wr.family);
    drawTextFit(buf, m + 10, y0 + 52, w - 10, F_L, col::TEXT, col::BG, TL_DATUM);
    TeamStyle t = teamStyle(wr.teamId, wr.team);
    snprintf(buf, sizeof(buf), "%s · %s pts · %u wins", t.shortName, wr.points, wr.wins);
    drawTextFit(buf, m + 10, y0 + 80, w - 10, F_S, col::TEXT2, col::BG, TL_DATUM);
  } else {
    drawWrapped("Final standings will appear once they are published.", m, y0 + 36, w, 20, 2, F_M, col::TEXT,
                col::BG);
  }
  int y = y0 + (L.portrait ? 128 : 112);
  tft.drawFastHLine(m, y, w, col::LINE);
  drawText("NEXT SEASON", m, y + 10, F_S, col::TEXT3, col::BG, TL_DATUM);
  snprintf(buf, sizeof(buf), "The %u calendar has not been published yet.", cal->season + 1);
  int n = drawWrapped(buf, m, y + 28, w, 17, 2, F_S, col::TEXT2, col::BG);
  drawWrapped("Standings stay available in the other tabs.", m, y + 28 + n * 17 + 1, w, 17, 2, F_S, col::TEXT3,
              col::BG);
}

static void drawWeekend(const Race& r, int64_t now, const NextRace& nr) {
  char buf[48], d[20], t[12];
  const int right = L.W - L.m;
  const int dateX = L.portrait ? 102 : 150;
  const int pitch = L.portrait ? 26 : 21;
  shortenGrandPrix(buf, sizeof(buf), r.name);
  drawTextFit(buf, L.m, L.contentY + 6, L.W - 2 * L.m - 90, F_M, col::TEXT, col::BG, TL_DATUM);
  drawText("ISRAEL TIME", right, L.contentY + 14, F_S, col::TEXT3, col::BG, MR_DATUM);
  int y = L.contentY + (L.portrait ? 40 : 30);
  int n = r.sessionCount;
  for (int i = 0; i <= n && i < kMaxSessions + 1; i++) {
    bool isRace = (i == n);
    int64_t start = isRace ? r.raceUtc : r.sessions[i].startUtc;
    bool hasTime = isRace ? r.hasTime : r.sessions[i].hasTime;
    const char* name = isRace ? "RACE" : sessionShortName(r.sessions[i].kind);
    if (L.portrait && !isRace && r.sessions[i].kind == SK_SPRINT_QUALI) name = "SPRINT Q";
    bool past = now > start + (hasTime ? 2 * 3600 : 86400);
    bool isNext = isRace ? (nr.nextSession == nullptr) : (nr.nextSession == &r.sessions[i]);
    uint16_t bg = isNext ? accentDim() : col::BG;
    if (isNext) {
      tft.fillRect(0, y - 2, L.W, 20, bg);
      tft.fillRect(0, y - 2, 3, 20, accent());
    }
    uint16_t c = past ? col::TEXT3 : col::TEXT;
    drawText(name, L.m, y + 8, F_S, isNext ? accent() : (past ? col::TEXT3 : col::TEXT2), bg, ML_DATUM);
    israelDate(start, hasTime, d, sizeof(d), true);
    drawText(d, dateX, y + 8, F_S, c, bg, ML_DATUM);
    israelTime(start, hasTime, t, sizeof(t));
    drawText(t, right, y + 8, F_M, c, bg, MR_DATUM);
    y += pitch;
  }
  addButton(0, L.contentY, L.W, L.contentH, B_TOGGLE_VIEW);
}

void drawNext() {
  clearContent();
  s_countdownShown = false;
  s_groupCount = 0;
  DataLock l;
  const Calendar* cur = activeCalendar();
  int64_t now = nowUtc();
  if (!cur || !now) {
    drawEmptyState(now ? "Waiting for the calendar" : "Waiting for the clock", DS_CALENDAR);
    return;
  }
  const Calendar* nxt = nextSeasonCalendar(cur);
  NextRace nr = findNextRace(now, cur, nxt);
  if (!nr.race) {
    drawSeasonComplete(cur);
    return;
  }
  const Race& r = *nr.race;
  if (g_weekendView) {
    drawWeekend(r, now, nr);
    return;
  }
  char buf[64], d[24], t[12];
  const int m = L.m, right = L.W - L.m, w = L.W - 2 * L.m;
  const bool P = L.portrait;
  const int yRound = P ? 48 : 30, yName = P ? 64 : 46, yCircuit = P ? 92 : 74, yLoc = P ? 108 : 90;
  // round line
  snprintf(buf, sizeof(buf), "ROUND %u", r.round);
  drawText(buf, m, yRound, F_S, accent(), col::BG, TL_DATUM);
  int rw = textWidth(buf, F_S);
  const Calendar* src = nr.fromNextSeason ? nxt : cur;
  snprintf(buf, sizeof(buf), "OF %u · %u", src->count, r.season);
  drawText(buf, m + rw + 6, yRound, F_S, col::TEXT3, col::BG, TL_DATUM);
  if (r.hasSprint) {
    tft.drawRoundRect(right - 52, yRound, 52, 16, 8, accent());
    drawText("SPRINT", right - 26, yRound + 8, F_S, accent(), col::BG, MC_DATUM);
  }
  // race name (shortened when necessary)
  if (textWidth(r.name, F_L) <= w) {
    drawText(r.name, m, yName, F_L, col::TEXT, col::BG, TL_DATUM);
  } else {
    shortenGrandPrix(buf, sizeof(buf), r.name);
    drawTextFit(buf, m, yName, w, F_L, col::TEXT, col::BG, TL_DATUM);
  }
  drawTextFit(r.circuit, m, yCircuit, w, F_S, col::TEXT2, col::BG, TL_DATUM);
  if (r.locality[0] && r.country[0]) snprintf(buf, sizeof(buf), "%s, %s", r.locality, r.country);
  else snprintf(buf, sizeof(buf), "%s", r.country[0] ? r.country : r.locality);
  // favourite driver summary on the right of the location line
  int favW = 0;
  if (g_settings.favoriteId[0]) {
    const DriverStandings& ds = g_data.drivers;
    for (int i = 0; i < ds.count; i++) {
      if (strcmp(ds.rows[i].driverId, g_settings.favoriteId) == 0) {
        char fav[32];
        snprintf(fav, sizeof(fav), "%s  P%s  %s PTS", ds.rows[i].code, ds.rows[i].posText, ds.rows[i].points);
        favW = textWidth(fav, F_S) + 14;
        drawText(fav, right, yLoc + 8, F_S, col::TEXT2, col::BG, MR_DATUM);
        drawStar(right - favW + 5, yLoc + 7, 5, accent());
        break;
      }
    }
  }
  drawTextFit(buf, m, yLoc, w - favW - 8, F_S, col::TEXT3, col::BG, TL_DATUM);

  // countdown / live block
  if (nr.inProgress) {
    s_targetLive = true;
    s_liveUntil = raceFinishedAt(r);
    useFont(F_XL);
    int lw = tft.textWidth("LIVE");
    int lx = P ? (L.W - lw - 22) / 2 + 22 : 40;
    int ly = P ? 150 : 112;
    tft.fillCircle(lx - 16, ly + 16, 7, col::RED);
    tft.setTextColor(col::TEXT, col::BG, false);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("LIVE", lx, ly - FONT_XL_DIGIT_TOP);
    israelTime(r.raceUtc, true, t, sizeof(t));
    snprintf(buf, sizeof(buf), "Lights out was at %s", t);
    if (P) drawText(buf, L.W / 2, ly + 52, F_S, col::TEXT2, col::BG, MC_DATUM);
    else drawText(buf, 128, 128, F_S, col::TEXT2, col::BG, ML_DATUM);
  } else {
    s_targetLive = false;
    s_target = r.raceUtc;
    s_targetHasTime = r.hasTime;
    s_countdownShown = true;
  }
  const int yLine = P ? 240 : 164, yLbl = yLine + 4, yVal = yLine + 20;
  const int col2 = P ? 132 : 186;
  tft.drawFastHLine(m, yLine, w, col::LINE);
  // info row
  drawText(P ? "RACE · ISRAEL" : "RACE · ISRAEL TIME", m, yLbl, F_S, col::TEXT3, col::BG, TL_DATUM);
  israelDate(r.raceUtc, r.hasTime, d, sizeof(d), true);
  israelTime(r.raceUtc, r.hasTime, t, sizeof(t));
  snprintf(buf, sizeof(buf), "%s  %s", d, t);
  drawTextFit(buf, m, yVal, col2 - m - 6, F_M, col::TEXT, col::BG, TL_DATUM);
  if (nr.nextSession) {
    const Session& s = *nr.nextSession;
    snprintf(buf, sizeof(buf), "NEXT · %s", sessionShortName(s.kind));
    drawTextFit(buf, col2, yLbl, right - col2, F_S, col::TEXT3, col::BG, TL_DATUM);
    CivilTime c = s.hasTime ? toIsrael(s.startUtc) : toCivilUtc(s.startUtc);
    israelTime(s.startUtc, s.hasTime, t, sizeof(t));
    snprintf(buf, sizeof(buf), "%s  %s", weekdayShort(c.weekday), t);
    drawText(buf, col2, yVal, F_M, col::TEXT, col::BG, TL_DATUM);
  } else {
    drawText("WEEKEND", col2, yLbl, F_S, col::TEXT3, col::BG, TL_DATUM);
    drawText("Race day", col2, yVal, F_M, col::TEXT, col::BG, TL_DATUM);
  }
  addButton(0, countdownTop() - 4, L.W, L.navY - countdownTop() + 4, B_TOGGLE_VIEW);
  tickNext(true);
}

// ------------------------------------------------------------------ DRIVERS
void drawDrivers() {
  DataLock l;
  const DriverStandings& d = g_data.drivers;
  if (g_data.meta[DS_DRIVERS].origin == ORIGIN_NONE || d.count == 0) {
    drawEmptyState("No driver standings yet", DS_DRIVERS);
    return;
  }
  uint8_t pages = pageCount(d.count, L.rows);
  if (g_page >= pages) g_page = pages - 1;
  const int R = L.listW - 5;                 // right edge of the points column
  const bool showTeam = !L.portrait;          // portrait: team shown by the colour bar only
  static const char* const labels[] = {"POS", "DRIVER", "PTS", "TEAM"};
  const int xs[] = {24, 37, R, 176};
  static const uint8_t dt[] = {MR_DATUM, ML_DATUM, MR_DATUM, ML_DATUM};
  drawListHeader(labels, xs, dt, showTeam ? 4 : 3);
  for (int i = 0; i < L.rows; i++) {
    int idx = g_page * L.rows + i;
    int y = rowY(i), cy = y + L.rowH / 2;
    if (idx >= d.count) { tft.fillRect(0, y, L.listW, L.rowH, col::BG); continue; }
    const DriverRow& r = d.rows[idx];
    bool fav = g_settings.favoriteId[0] && strcmp(r.driverId, g_settings.favoriteId) == 0;
    drawRowBackground(i, fav);
    uint16_t bg = fav ? accentDim() : ((i & 1) ? col::ZEBRA : col::BG);
    drawText(r.posText, 24, cy, F_M, r.pos && r.pos <= 3 ? col::TEXT : col::TEXT2, bg, MR_DATUM);
    drawTeamBar(29, y + 3, r.teamId);
    drawText(r.code, 37, cy, F_M, fav ? accent() : col::TEXT, bg, ML_DATUM);
    drawTextFit(r.family, 74, cy, showTeam ? 98 : R - 34 - 74, F_S, col::TEXT2, bg, ML_DATUM);
    if (showTeam) {
      TeamStyle t = teamStyle(r.teamId, r.team);
      drawTextFit(t.shortName, 176, cy, 74, F_S, col::TEXT3, bg, ML_DATUM);
    }
    drawText(r.points, R, cy, F_M, col::TEXT, bg, MR_DATUM);
    addButton(0, y, L.listW, L.rowH, B_ROW0 + i);
  }
  drawPager(g_page, pages);
}

// ------------------------------------------------------------------ CONSTRUCTORS
void drawTeams() {
  DataLock l;
  const ConstructorStandings& c = g_data.teams;
  if (g_data.meta[DS_TEAMS].origin == ORIGIN_NONE || c.count == 0) {
    drawEmptyState("No constructor standings yet", DS_TEAMS);
    return;
  }
  uint8_t pages = pageCount(c.count, L.rows);
  if (g_page >= pages) g_page = pages - 1;
  const int R = L.listW - 5, winsR = R - 45, nameW = winsR - 30 - 37;
  static const char* const labels[] = {"POS", "TEAM", "WINS", "PTS"};
  const int xs[] = {24, 37, winsR, R};
  static const uint8_t dt[] = {MR_DATUM, ML_DATUM, MR_DATUM, MR_DATUM};
  drawListHeader(labels, xs, dt, 4);
  int32_t leader = 1;
  for (int i = 0; i < c.count; i++) {
    int32_t p = pointsToTenths(c.rows[i].points);
    if (p > leader) leader = p;
  }
  for (int i = 0; i < L.rows; i++) {
    int idx = g_page * L.rows + i;
    int y = rowY(i), cy = y + L.rowH / 2 - 1;
    if (idx >= c.count) { tft.fillRect(0, y, L.listW, L.rowH, col::BG); continue; }
    const TeamRow& r = c.rows[idx];
    drawRowBackground(i, false);
    uint16_t bg = (i & 1) ? col::ZEBRA : col::BG;
    TeamStyle t = teamStyle(r.teamId, r.name);
    drawText(r.posText, 24, cy, F_M, r.pos && r.pos <= 3 ? col::TEXT : col::TEXT2, bg, MR_DATUM);
    drawTeamBar(29, y + 2, r.teamId);
    drawTextFit(r.name, 37, cy, nameW, F_M, col::TEXT, bg, ML_DATUM);
    int32_t p = pointsToTenths(r.points);
    int barW = p > 0 ? (int)((int64_t)nameW * p / leader) : 0;
    if (barW > 0) tft.fillRect(37, y + 17, barW, 2, blend565(rgb565(t.rgb), bg, 150));
    char wins[6];
    snprintf(wins, sizeof(wins), "%u", r.wins);
    drawText(wins, winsR, cy, F_S, col::TEXT3, bg, MR_DATUM);
    drawText(r.points, R, cy, F_M, col::TEXT, bg, MR_DATUM);
  }
  drawPager(g_page, pages);
}

// ------------------------------------------------------------------ LAST RACE RESULTS
void drawResults() {
  DataLock l;
  const RaceResults& rr = g_data.results;
  if (g_data.meta[DS_RESULTS].origin == ORIGIN_NONE || rr.count == 0) {
    drawEmptyState("No race results yet", DS_RESULTS);
    return;
  }
  uint8_t pages = pageCount(rr.count, L.rows);
  if (g_page >= pages) g_page = pages - 1;
  // header row: race name and date, or a pending notice
  const int R = L.listW - 5;
  const int famW = L.portrait ? 66 : 86, infoX = L.portrait ? 144 : 166, infoW = L.portrait ? R - 26 - 144 : 86;
  tft.fillRect(0, L.listHdrY, L.listW, L.listY0 - L.listHdrY, col::BG);
  char buf[64], name[40], d[16];
  shortenGrandPrix(name, sizeof(name), rr.name);
  israelDate(rr.raceUtc, rr.hasTime, d, sizeof(d), false);
  snprintf(buf, sizeof(buf), "%s · %s", name, d);
  int64_t now = nowUtc();
  const Race* last = now ? latestCompletedRace(now, activeCalendar()) : nullptr;
  bool pending = last && ((last->season == rr.season && last->round > rr.round) || last->season > rr.season);
  if (pending) {
    char p[24];
    snprintf(p, sizeof(p), "R%u pending", last->round);
    drawText(p, R, L.listHdrY + 8, F_S, col::AMBER, col::BG, MR_DATUM);
    drawTextFit(buf, 6, L.listHdrY + 8, R - textWidth(p, F_S) - 14, F_S, col::TEXT2, col::BG, ML_DATUM);
  } else {
    drawText("PTS", R, L.listHdrY + 8, F_S, col::TEXT3, col::BG, MR_DATUM);
    drawTextFit(buf, 6, L.listHdrY + 8, R - 40, F_S, col::TEXT2, col::BG, ML_DATUM);
  }
  for (int i = 0; i < L.rows; i++) {
    int idx = g_page * L.rows + i;
    int y = rowY(i), cy = y + L.rowH / 2;
    if (idx >= rr.count) { tft.fillRect(0, y, L.listW, L.rowH, col::BG); continue; }
    const ResultRow& r = rr.rows[idx];
    bool fav = g_settings.favoriteId[0] && strcmp(r.driverId, g_settings.favoriteId) == 0;
    drawRowBackground(i, fav);
    uint16_t bg = fav ? accentDim() : ((i & 1) ? col::ZEBRA : col::BG);
    bool classified = r.finishClass == FC_CLASSIFIED;
    uint16_t bad = (r.finishClass == FC_WITHDRAWN || r.finishClass == FC_FAILED_QUALIFY) ? col::TEXT3 : col::RED;
    if (classified) drawText(r.posText, 24, cy, F_M, r.pos <= 3 ? col::TEXT : col::TEXT2, bg, MR_DATUM);
    else drawText(finishClassShort(r.finishClass), 25, cy, F_S, bad, bg, MR_DATUM);
    drawTeamBar(29, y + 3, r.teamId);
    drawText(r.code, 37, cy, F_M, fav ? accent() : (classified ? col::TEXT : col::TEXT2), bg, ML_DATUM);
    drawTextFit(r.family, 74, cy, famW, F_S, col::TEXT2, bg, ML_DATUM);
    const char* info = classified ? (r.time[0] ? r.time : r.status) : r.status;
    drawTextFit(info, infoX, cy, infoW, F_S, classified ? col::TEXT2 : bad, bg, ML_DATUM);
    bool zero = !r.points[0] || strcmp(r.points, "0") == 0;
    drawText(r.points[0] ? r.points : "0", R, cy, F_M, zero ? col::TEXT3 : col::TEXT, bg, MR_DATUM);
  }
  drawPager(g_page, pages);
}

// ------------------------------------------------------------------ CALENDAR
static const Calendar* calendarForList() {
  const Calendar* cur = activeCalendar();
  if (g_useNextSeason) {
    const Calendar* n = nextSeasonCalendar(cur);
    if (n) return n;
  }
  return cur;
}

void drawCalendar() {
  DataLock l;
  const Calendar* cal = calendarForList();
  if (!cal || cal->count == 0) {
    drawEmptyState("No calendar yet", DS_CALENDAR);
    return;
  }
  uint8_t pages = pageCount(cal->count, L.rows);
  if (g_page >= pages) g_page = pages - 1;
  static const char* const labels[] = {"#", "DATE", "GRAND PRIX"};
  static const int xs[] = {22, 30, 78};
  static const uint8_t dt[] = {MR_DATUM, ML_DATUM, ML_DATUM};
  drawListHeader(labels, xs, dt, 3);
  int64_t now = nowUtc();
  int nextIdx = now ? nextRaceIndex(now, cal) : -1;
  for (int i = 0; i < L.rows; i++) {
    int idx = g_page * L.rows + i;
    int y = rowY(i), cy = y + L.rowH / 2;
    if (idx >= cal->count) { tft.fillRect(0, y, L.listW, L.rowH, col::BG); continue; }
    const Race& r = cal->races[idx];
    bool isNext = idx == nextIdx;
    bool past = now && raceFinishedAt(r) <= now;
    drawRowBackground(i, isNext);
    uint16_t bg = isNext ? accentDim() : ((i & 1) ? col::ZEBRA : col::BG);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u", r.round);
    drawText(buf, 22, cy, F_S, col::TEXT3, bg, MR_DATUM);
    israelDate(r.raceUtc, r.hasTime, buf, sizeof(buf), false);
    drawText(buf, 30, cy, F_S, past ? col::TEXT3 : col::TEXT2, bg, ML_DATUM);
    shortenGrandPrix(buf, sizeof(buf), r.name);
    const int LW = L.listW;
    drawTextFit(buf, 78, cy, isNext || r.hasSprint ? LW - 138 : LW - 90, F_M, past ? col::TEXT3 : col::TEXT, bg,
                ML_DATUM);
    if (isNext) {
      tft.fillRoundRect(LW - 48, y + 3, 44, 15, 7, accent());
      drawText("NEXT", LW - 26, cy + 1, F_S, col::BG, accent(), MC_DATUM);
    } else if (past) {
      uint16_t c = col::TEXT3;
      int x = LW - 20;
      tft.drawLine(x, cy, x + 4, cy + 4, c);
      tft.drawLine(x + 4, cy + 4, x + 12, cy - 4, c);
      tft.drawLine(x, cy + 1, x + 4, cy + 5, c);
      tft.drawLine(x + 4, cy + 5, x + 12, cy - 3, c);
    } else if (r.hasSprint) {
      drawText("SPRINT", LW - 5, cy, F_S, col::TEXT3, bg, MR_DATUM);
    }
    addButton(0, y, L.listW, L.rowH, B_ROW0 + i);
  }
  drawPager(g_page, pages);
}

void drawRace() {
  clearContent();
  DataLock l;
  const Calendar* cal = calendarForList();
  if (!cal || g_raceIndex >= cal->count) {
    drawEmptyState("Race not available", DS_CALENDAR);
    return;
  }
  const Race& r = cal->races[g_raceIndex];
  char buf[64], d[40], t[12];
  const int m = L.m, right = L.W - L.m, w = L.W - 2 * L.m;
  const bool P = L.portrait;
  const int y0 = L.contentY;
  snprintf(buf, sizeof(buf), "ROUND %u · %u", r.round, r.season);
  drawText(buf, m, y0 + 4, F_S, accent(), col::BG, TL_DATUM);
  if (textWidth(r.name, F_L) <= w) drawText(r.name, m, y0 + 20, F_L, col::TEXT, col::BG, TL_DATUM);
  else {
    shortenGrandPrix(buf, sizeof(buf), r.name);
    drawTextFit(buf, m, y0 + 20, w, F_L, col::TEXT, col::BG, TL_DATUM);
  }
  drawTextFit(r.circuit, m, y0 + 48, w, F_S, col::TEXT2, col::BG, TL_DATUM);
  if (r.locality[0] && r.country[0]) snprintf(buf, sizeof(buf), "%s, %s", r.locality, r.country);
  else snprintf(buf, sizeof(buf), "%s", r.country);
  drawTextFit(buf, m, y0 + 64, w, F_S, col::TEXT3, col::BG, TL_DATUM);
  tft.drawFastHLine(m, y0 + 84, w, col::LINE);
  drawText("ISRAEL TIME", right, y0 + 92, F_S, col::TEXT3, col::BG, MR_DATUM);
  // landscape: two columns of up to 4 rows; portrait: one column
  int n = r.sessionCount;
  int64_t now = nowUtc();
  for (int i = 0; i <= n; i++) {
    bool isRace = i == n;
    int64_t start = isRace ? r.raceUtc : r.sessions[i].startUtc;
    bool hasTime = isRace ? r.hasTime : r.sessions[i].hasTime;
    const char* name = isRace ? "RACE" : sessionShortName(r.sessions[i].kind);
    if (!isRace && r.sessions[i].kind == SK_SPRINT_QUALI) name = "SPRINT Q";
    int x, y, valueRight;
    if (P) {
      x = m;
      y = y0 + 102 + i * 19;
      valueRight = right;
    } else {
      int colIdx = i < 4 ? 0 : 1;
      int row = i < 4 ? i : i - 4;
      x = colIdx ? 166 : 14;
      y = y0 + 104 + row * 19;
      valueRight = x + 140;
    }
    bool past = now && now > start + (hasTime ? 2 * 3600 : 86400);
    uint16_t c = past ? col::TEXT3 : col::TEXT;
    drawText(name, x, y + 8, F_S, isRace ? accent() : col::TEXT3, col::BG, ML_DATUM);
    CivilTime ct = hasTime ? toIsrael(start) : toCivilUtc(start);
    israelTime(start, hasTime, t, sizeof(t));
    if (P) {
      char dd[20];
      israelDate(start, hasTime, dd, sizeof(dd), true);
      snprintf(d, sizeof(d), "%s  %s", dd, t);
    } else {
      snprintf(d, sizeof(d), "%s %s", weekdayShort(ct.weekday), t);
    }
    drawText(d, valueRight, y + 8, F_S, c, col::BG, MR_DATUM);
  }
}

}  // namespace ui
