// Gridline F1 — internal UI declarations shared by the ui_*.cpp files
#pragma once
#include <Arduino.h>
#include "app_state.h"
#include "display.h"
#include "gl_logic.h"
#include "gl_text.h"
#include "gl_time.h"

namespace ui {

// ---- layout: computed for the current rotation (see layoutApply) ----
//                  landscape 320x240         portrait 240x320
//  header          0..25 (one row)           0..43 (title row + status row)
//  content         26..205                   44..283
//  list rows       8 x 20 px, pager right    10 x 20 px, pager below
//  nav bar         206..239 (5 tabs + gear)  284..319 (5 tabs; gear in header)
struct Layout {
  bool portrait;
  int16_t W, H;
  int16_t headerH;
  int16_t contentY, contentH;
  int16_t navY, navH;
  int16_t m;                 // side margin
  int16_t listW;             // width of list rows (excludes a side pager)
  int16_t listHdrY, listY0, rowH, rows;
  bool pagerBelow;           // portrait: pager strip under the list
  int16_t pagerX, pagerW;    // landscape: vertical pager at the right
  int16_t pagerY, pagerH;    // portrait: horizontal pager strip
  int16_t setRows;           // rows per settings page
};
extern Layout L;
void layoutApply(uint8_t rotation);

enum Screen : uint8_t {
  S_NEXT = 0, S_DRIVERS, S_TEAMS, S_RESULTS, S_CALENDAR,  // tabs
  S_SETTINGS, S_NIGHT, S_FAVORITE, S_ABOUT, S_RACE,       // sub screens
};

// Button identifiers (per screen meaning, 0 = none)
enum : uint8_t {
  B_NONE = 0, B_PAGE_UP, B_PAGE_DOWN, B_BACK, B_TOGGLE_VIEW,
  B_TAB0, B_TAB1, B_TAB2, B_TAB3, B_TAB4, B_TAB_SETTINGS,
  B_ROW0 = 32,  // B_ROW0 + n = list row n on the current page
  B_BRIGHT_DOWN = 64, B_BRIGHT_UP, B_NIGHT, B_ACCENT0, B_ACCENT_LAST = B_ACCENT0 + 7,
  B_FAVORITE, B_WIFI, B_CALIBRATE, B_REFRESH, B_ABOUT, B_RESTART, B_SELFTEST,
  B_NIGHT_TOGGLE, B_NSTART_DOWN, B_NSTART_UP, B_NEND_DOWN, B_NEND_UP, B_NLEVEL_DOWN, B_NLEVEL_UP,
  B_DLG_CANCEL, B_DLG_OK, B_OPEN_SETUP, B_ORIENT_WIDE, B_ORIENT_TALL, B_FLIP,
};

struct Button { int16_t x, y, w, h; uint8_t id; };

void addButton(int x, int y, int w, int h, uint8_t id);
void clearButtons();

// state shared between files
extern Screen g_screen;
extern uint8_t g_page;          // page of the current list screen
extern uint8_t g_raceIndex;     // calendar index for S_RACE
extern bool g_weekendView;      // S_NEXT: show session list instead of countdown
extern bool g_useNextSeason;    // calendar screen shows the S+1 calendar

void requestContent();          // redraw the content area
void requestFull();             // redraw everything
void go(Screen s);
void showToast(const char* msg);
void openDialog(const char* title, const char* msg, const char* okLabel, uint8_t action);

// helpers
void clearContent();
void drawPager(uint8_t page, uint8_t pages);
void drawListHeader(const char* const* labels, const int* xs, const uint8_t* datums, int n);
void drawRowBackground(int row, bool highlight);
int rowY(int row);
void drawTeamBar(int x, int y, const char* teamId);
void drawChevron(int x, int cy, uint16_t c);
void drawMinusPlus(int x, int y, uint8_t idMinus, uint8_t idPlus, const char* value);
void drawToggle(int x, int y, bool on);
void formatWhen(int64_t utc, char* out, size_t n);   // "16:05" today, else "23 Oct"
uint8_t pageCount(int items, int perPage);
const char* headerSubtitle(char* buf, size_t n);

// screens (drawContent fills the content area and registers buttons)
void drawNext();
void tickNext(bool force);      // per-second countdown update
void drawDrivers();
void drawTeams();
void drawResults();
void drawCalendar();
void drawRace();
void drawSettings();
void drawNight();
void drawFavorite();
void drawAbout();
void enterNext();
void leaveNext();

bool handleScreenButton(uint8_t id);   // returns true when handled
void dialogAction(uint8_t action);
void applyOrientation(uint8_t rotation);   // rotate screen + touch, relayout, redraw

}  // namespace ui
