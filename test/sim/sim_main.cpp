// Gridline F1 — host simulator: renders the real UI code into PNG screenshots.
//
// The screens are drawn by the unmodified firmware files ui.cpp,
// ui_screens.cpp, ui_settings.cpp, ui_setup.cpp and display.cpp against a
// framebuffer mock of TFT_eSPI. Data: real 2023 Ergast/Jolpica fixture
// responses parsed with the firmware parsers. The simulated clock is set to
// dates in 2023 so the data is consistent with "now".
#include <stdio.h>
#include <string>
#include <fstream>
#include <sstream>
#include "app_state.h"
#include "display.h"
#include "gl_parse.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"
#include "ui_internal.h"
#include "ui_setup.h"

using namespace gl;

// ------------------------------------------------------------ stubs
EspClass ESP;
SerialStub Serial;
static uint32_t s_ms = 10000;
static int64_t s_now = 0;
uint32_t millis() { return s_ms; }
void delay(uint32_t ms) { s_ms += ms; }
int digitalRead(int) { return HIGH; }

AppData g_data;
Settings g_settings;
void appStateInit() {}
void dataLock() {}
void dataUnlock() {}
int64_t nowUtc() { return s_now; }
bool clockIsValid() { return s_now != 0; }
int64_t buildUtc() { return 0; }
void settingsBegin() {}
void settingsChanged() {}
void settingsTick() {}
void settingsFlush() {}
bool wifiCredsLoad(char*, size_t, char*, size_t) { return true; }
bool wifiCredsSave(const char*, const char*) { return true; }
bool wifiCredsExist() { return true; }
bool touchCalLoad(TouchCal&) { return true; }
bool touchCalSave(const TouchCal&) { return true; }
void bootRequestSet(BootRequest) {}
BootRequest bootRequestTake() { return BOOT_NORMAL; }
void touchBegin() {}
bool touchReadRaw(RawTouch& r) { r.down = false; return false; }
bool touchIrqActive() { return false; }
void touchSetCal(const TouchCal&) {}
void touchSetRotation(uint8_t) {}
bool touchHasCal() { return true; }
void touchClearCal() {}
void touchPoll() {}
static bool g_tapPending = false;
static int16_t g_tapX = 0, g_tapY = 0;
bool touchTakeTap(int16_t& x, int16_t& y) {
  if (!g_tapPending) return false;
  g_tapPending = false;
  x = g_tapX;
  y = g_tapY;
  return true;
}
static void tap(int x, int y) {
  g_tapX = (int16_t)x;
  g_tapY = (int16_t)y;
  g_tapPending = true;
  uiLoop();
  s_ms += 300;
  uiLoop();
}
static int g_tapFails = 0;
static void expectScreen(const char* what, bool ok) {
  printf("  tap test: %-48s %s\n", what, ok ? "PASS" : "FAIL");
  if (!ok) g_tapFails++;
}
bool touchIsDown() { return false; }
void touchFlush() {}
void netRestoreCache() {}
void netStart() {}
void netRequestRefresh() {}
void netRequestSelfTest() {}
uint32_t netTaskStackHighWater() { return 5200; }
bool storageBegin() { return true; }
void storageInfo(size_t& used, size_t& total) { used = 40960; total = 917504; }
void logInit() {}
void logf(const char* tag, const char* fmt, ...) { (void)tag; (void)fmt; }
void logMemory(const char*) {}

// ------------------------------------------------------------ helpers
static std::string g_fix, g_out;
static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static void loadData() {
  memset(&g_data, 0, sizeof(g_data));
  std::string c = readFile(g_fix + "/2023.json");
  StringJsonSource s1(c.data(), c.size());
  if (parseCalendar(s1, 2023, g_data.calendar) != FE_OK) { fprintf(stderr, "calendar parse failed\n"); exit(1); }
  std::string d = readFile(g_fix + "/2023_22_driverstandings.json");
  StringJsonSource s2(d.data(), d.size());
  parseDriverStandings(s2, 2023, g_data.drivers);
  std::string t = readFile(g_fix + "/2023_22_constructorstandings.json");
  StringJsonSource s3(t.data(), t.size());
  parseConstructorStandings(s3, 2023, g_data.teams);
  std::string r = readFile(g_fix + "/2023_9_results.json");
  StringJsonSource s4(r.data(), r.size());
  parseRaceResults(s4, 2023, g_data.results);
  for (int ds = 0; ds < DS_COUNT; ds++) {
    if (ds == DS_CALENDAR_NEXT) continue;
    g_data.meta[ds].origin = ORIGIN_NETWORK;
    g_data.version[ds] = 1;
  }
  g_data.net.wifi = WS_CONNECTED;
  g_data.net.clockValid = 1;
  g_data.net.rssi = -58;
  strcpy(g_data.net.ssid, "HomeNetwork");
  strcpy(g_data.net.ip, "192.168.1.42");
  g_data.net.requests = 14;
  strcpy(g_data.net.selfTest, "4/4 checks passed (example)");
}

static void setNow(int64_t t) {
  s_now = t;
  for (int ds = 0; ds < DS_COUNT; ds++)
    if (g_data.meta[ds].origin != ORIGIN_NONE) g_data.meta[ds].fetchedUtc = t - 17 * 60;
}

static const char* g_prefix = "";
static void snap(const char* name) {
  std::string p = g_out + "/" + g_prefix + name + ".ppm";
  tft.writePPM(p.c_str());
  printf("  %s\n", name);
}

static void show(ui::Screen s, int page = -1) {
  ui::go(s);
  if (page >= 0) ui::g_page = (uint8_t)page;
  ui::requestFull();
  uiLoop();
  s_ms += 1000;
  uiLoop();
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: sim <fixtures> <outdir>\n"); return 2; }
  g_fix = argv[1];
  g_out = argv[2];
  memset(&g_settings, 0, sizeof(g_settings));
  g_settings.version = 1;
  g_settings.brightness = 80;
  g_settings.nightStart = 23;
  g_settings.nightEnd = 7;
  g_settings.nightBrightness = 12;
  strcpy(g_settings.favoriteId, "leclerc");
  strcpy(g_settings.favoriteCode, "LEC");

  for (int pass = 0; pass < 2; pass++) {
    // pass 0: landscape (rotation 1); pass 1: portrait (rotation 0)
    g_prefix = pass ? "p" : "";
    g_settings.rotation = pass ? 1 : 2;   // stored as rotation + 1
    g_settings.accent = 0;
    g_settings.nightEnabled = 0;
    tft.setRotation(settingsRotation());
    tft.fillScreen(0);
    extern void uiResetSplashForSim();
    uiResetSplashForSim();
    uiShowSplash("Starting");
    snap("00_splash");
    loadData();
    // Thursday before the 2023 British GP (race 9 Jul 14:00 UTC)
    setNow(makeUtc(2023, 7, 6, 9, 12, 26));
    uiBegin();
    show(ui::S_NEXT);
    snap("01_next_race");
    ui::g_weekendView = true;
    ui::requestContent();
    uiLoop();
    snap("02_next_weekend_schedule");
    ui::g_weekendView = false;
    show(ui::S_DRIVERS);
    snap("03_drivers_p1");
    show(ui::S_DRIVERS, 1);
    snap("04_drivers_p2");
    show(ui::S_TEAMS);
    snap("05_constructors");
    show(ui::S_RESULTS);
    snap("06_last_race_results_p1");
    show(ui::S_RESULTS, 2);
    snap("07_last_race_results_p3");
    show(ui::S_CALENDAR);
    snap("08_calendar");
    ui::g_raceIndex = 9;
    show(ui::S_RACE);
    snap("09_race_detail");
    show(ui::S_SETTINGS);
    snap("10_settings_p1");
    show(ui::S_SETTINGS, 1);
    snap("11_settings_p2");
    g_settings.nightEnabled = 1;
    show(ui::S_NIGHT);
    snap("12_night_dimming");
    show(ui::S_FAVORITE);
    snap("13_favorite_picker");
    show(ui::S_ABOUT);
    snap("14_about_p1");
    show(ui::S_ABOUT, 1);
    snap("15_about_p2");
    show(ui::S_SETTINGS);
    ui::openDialog("Wi-Fi setup", "Restart into setup mode? The saved network is kept until you choose a new one.", "Open", ui::B_WIFI);
    snap("16_dialog");

    // race in progress
    setNow(makeUtc(2023, 7, 9, 14, 40, 0));
    show(ui::S_NEXT);
    snap("17_next_live");
    // race day, less than a day to go
    setNow(makeUtc(2023, 7, 9, 6, 59, 30));
    show(ui::S_NEXT);
    snap("18_next_same_day");
    // after the season: champion + no next calendar; results older than the last race => pending notice
    setNow(makeUtc(2023, 12, 10, 12, 0, 0));
    show(ui::S_NEXT);
    snap("19_season_complete");
    show(ui::S_RESULTS);
    snap("20_results_pending_notice");
    // cached + offline state
    for (int ds = 0; ds < DS_COUNT; ds++) g_data.meta[ds].origin = ds == DS_CALENDAR_NEXT ? ORIGIN_NONE : ORIGIN_CACHE;
    g_data.net.wifi = WS_LOST;
    setNow(makeUtc(2023, 7, 6, 9, 12, 26));
    for (int ds = 0; ds < DS_COUNT; ds++) g_data.meta[ds].fetchedUtc = makeUtc(2023, 7, 5, 18, 40, 0);
    show(ui::S_DRIVERS);
    snap("21_offline_cached");
    // no data yet
    memset(&g_data, 0, sizeof(g_data));
    g_data.net.wifi = WS_CONNECTED;
    g_data.net.clockValid = 1;
    show(ui::S_NEXT);
    snap("22_waiting_for_data");
    g_data.net.wifi = WS_NO_CREDS;
    show(ui::S_DRIVERS);
    snap("23_wifi_not_set_up");
    // accent colour variant
    loadData();
    setNow(makeUtc(2023, 7, 6, 9, 12, 26));
    g_settings.accent = 2;
    show(ui::S_NEXT);
    snap("24_next_cyan_accent");
    // Wi-Fi setup screen (example credentials; the device generates its own)
    g_settings.accent = 0;
    SetupView v{"GRIDLINE-7F3A", "mx7k-p4ra", "Phone connected - open the page", SETUP_IDLE, true};
    setupDrawScreen(v);
    snap("25_wifi_setup");

  }
  // ---- interaction tests through the real tap handler (portrait, then landscape)
  loadData();
  setNow(makeUtc(2023, 7, 6, 9, 12, 26));
  g_settings.rotation = 1;  // portrait
  tft.setRotation(settingsRotation());
  uiBegin();
  show(ui::S_NEXT);
  tap(60, 302);   expectScreen("portrait: tab DRIVERS", ui::g_screen == ui::S_DRIVERS);
  tap(200, 272);  expectScreen("portrait: pager down -> page 2", ui::g_page == 1);
  tap(40, 272);   expectScreen("portrait: pager up -> page 1", ui::g_page == 0);
  tap(220, 13);   expectScreen("portrait: header settings button", ui::g_screen == ui::S_SETTINGS);
  // Settings row 1 = [Wide|Tall] switch (x = listW-126, segments 57 px), row 2 = Upside down
  const int ROW1_P = 48 + 4 + 29 + 14, ROW2_P = 48 + 4 + 58 + 14;   // portrait row centres
  const int ROW1_L = 26 + 4 + 29 + 14, ROW2_L = 26 + 4 + 58 + 14;   // landscape row centres
  tap(114 + 84, ROW1_P);
  expectScreen("portrait: tap 'Tall' while tall -> no change", settingsRotation() == 0 && tft.width() == 240);
  snap("x_settings_portrait");
  tap(114 + 28, ROW1_P);
  expectScreen("portrait: tap 'Wide' -> landscape", settingsRotation() == 1 && tft.width() == 320);
  expectScreen("still on the settings screen after rotating", ui::g_screen == ui::S_SETTINGS);
  snap("x_settings_landscape");
  tap(160 + 84, ROW1_L);
  expectScreen("landscape: tap 'Tall' -> portrait", settingsRotation() == 0 && tft.width() == 240);
  tap(200, ROW2_P);
  expectScreen("portrait: Upside down on -> portrait 180", settingsRotation() == 2 && tft.width() == 240);
  tap(114 + 28, ROW1_P);
  expectScreen("upside down kept: 'Wide' -> landscape 180", settingsRotation() == 3 && tft.width() == 320);
  tap(200, ROW2_L);
  expectScreen("landscape: Upside down off -> landscape", settingsRotation() == 1);
  tap(300, 222);  expectScreen("landscape: settings icon keeps settings", ui::g_screen == ui::S_SETTINGS);
  tap(40, 222);   expectScreen("landscape: tab NEXT", ui::g_screen == ui::S_NEXT);
  tap(160, 140);  // countdown area
  expectScreen("landscape: tap on countdown toggles weekend view", ui::g_weekendView);
  tap(160, 140);
  expectScreen("landscape: tap again returns to countdown", !ui::g_weekendView);
  printf("tap tests: %d failure(s)\n", g_tapFails);
  return g_tapFails ? 1 : 0;
}
