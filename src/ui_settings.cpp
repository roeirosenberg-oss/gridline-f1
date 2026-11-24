// Gridline F1 — settings, night dimming, favourite picker, diagnostics,
// and the button handler for all screens.
#include <WiFi.h>
#include <esp_system.h>
#include "certs/ca_bundle.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "settings.h"
#include "storage.h"
#include "touch.h"
#include "ui.h"
#include "ui_internal.h"

using namespace gl;

namespace ui {

static const int kSetRowH = 29;

static int setRowY(int i) { return L.contentY + 4 + i * kSetRowH; }
static int ctrlX() { return L.listW - 14 - 112; }   // x of a [-] value [+] control

static void settingsRowFrame(int i, const char* label, bool last) {
  int y = setRowY(i);
  tft.fillRect(0, y, L.listW, kSetRowH, col::BG);
  drawText(label, 12, y + 14, F_M, col::TEXT, col::BG, ML_DATUM);
  if (!last) tft.drawFastHLine(12, y + kSetRowH - 1, L.listW - 20, col::LINE);
}

static void valueWithChevron(int i, const char* value, uint8_t id) {
  int y = setRowY(i);
  if (value && *value) drawTextFit(value, L.listW - 20, y + 14, L.portrait ? 96 : 120, F_S, col::TEXT2, col::BG, MR_DATUM);
  drawChevron(L.listW - 14, y + 14, col::TEXT3);
  addButton(0, y, L.listW, kSetRowH, id);
}

// ---- orientation: one segmented switch [ Wide | Tall ] + an "Upside down" toggle
static bool isFlipped(uint8_t rot) { return rot >= 2; }
static uint8_t rotationFor(bool tall, bool flipped) { return tall ? (flipped ? 2 : 0) : (flipped ? 3 : 1); }

static void drawScreenIcon(int cx, int cy, bool tall, uint16_t c) {
  int w = tall ? 10 : 16, h = tall ? 16 : 10;
  tft.drawRoundRect(cx - w / 2, cy - h / 2, w, h, 2, c);
  tft.drawRoundRect(cx - w / 2 + 1, cy - h / 2 + 1, w - 2, h - 2, 1, c);
}

static void drawOrientationSwitch(int x, int y) {
  // two 56 px segments, same footprint as a [-] value [+] control
  const bool tall = (settingsRotation() & 1) == 0;
  for (int k = 0; k < 2; k++) {
    bool seg = (k == 1);
    bool on = seg == tall;
    int sx = x + k * 57;
    uint16_t bg = on ? accent() : col::PANEL2;
    uint16_t fg = on ? col::BG : col::TEXT2;
    tft.fillRoundRect(sx, y, 55, 23, 6, bg);
    drawScreenIcon(sx + 12, y + 11, seg, fg);
    drawText(seg ? "Tall" : "Wide", sx + 36, y + 12, F_S, fg, bg, MC_DATUM);
    addButton(sx - 2, y - 3, 59, 29, seg ? B_ORIENT_TALL : B_ORIENT_WIDE);
  }
}

void drawSettings() {
  clearContent();
  const int items = 11;
  uint8_t pages = pageCount(items, L.setRows);
  if (g_page >= pages) g_page = pages - 1;
  char buf[40];
  for (int i = 0; i < L.setRows; i++) {
    int item = g_page * L.setRows + i;
    if (item >= items) break;
    bool last = (i == L.setRows - 1) || item == items - 1;
    int y = setRowY(i);
    switch (item) {
      case 0:
        settingsRowFrame(i, "Brightness", last);
        snprintf(buf, sizeof(buf), "%u%%", g_settings.brightness);
        drawMinusPlus(ctrlX(), y + 3, B_BRIGHT_DOWN, B_BRIGHT_UP, buf);
        break;
      case 1:
        settingsRowFrame(i, "Orientation", last);
        drawOrientationSwitch(ctrlX(), y + 3);
        break;
      case 2:
        settingsRowFrame(i, "Upside down", last);
        drawToggle(L.listW - 14 - 42, y + 4, isFlipped(settingsRotation()));
        addButton(0, y, L.listW, kSetRowH, B_FLIP);
        break;
      case 3:
        settingsRowFrame(i, "Night dimming", last);
        if (g_settings.nightEnabled) snprintf(buf, sizeof(buf), "%02u:00-%02u:00", g_settings.nightStart, g_settings.nightEnd);
        else snprintf(buf, sizeof(buf), "Off");
        valueWithChevron(i, buf, B_NIGHT);
        break;
      case 4: {
        settingsRowFrame(i, "Accent", last);
        int x0 = L.listW - 14 - 6 * 26 + 4;
        for (int a = 0; a < accentCount() && a < 6; a++) {
          int x = x0 + a * 26;
          uint16_t c = rgb565(accentRgb(a));
          if (a == g_settings.accent) tft.drawRoundRect(x - 2, y + 3, 24, 24, 7, col::TEXT);
          tft.fillRoundRect(x + 1, y + 6, 18, 18, 5, c);
          addButton(x - 3, y, 26, kSetRowH, B_ACCENT0 + a);
        }
        break;
      }
      case 5:
        settingsRowFrame(i, L.portrait ? "Favorite" : "Favorite driver", last);
        valueWithChevron(i, g_settings.favoriteId[0] ? g_settings.favoriteCode : "None", B_FAVORITE);
        break;
      case 6: {
        settingsRowFrame(i, "Wi-Fi", last);
        {
          DataLock l;
          snprintf(buf, sizeof(buf), "%s", g_data.net.ssid[0] ? g_data.net.ssid : "Not set");
        }
        valueWithChevron(i, buf, B_WIFI);
        break;
      }
      case 7:
        settingsRowFrame(i, "Touch calibration", last);
        valueWithChevron(i, "", B_CALIBRATE);
        break;
      case 8:
        settingsRowFrame(i, "Refresh data now", last);
        valueWithChevron(i, "", B_REFRESH);
        break;
      case 9:
        settingsRowFrame(i, L.portrait ? "About" : "About & diagnostics", last);
        valueWithChevron(i, "v" GL_FW_VERSION, B_ABOUT);
        break;
      case 10:
        settingsRowFrame(i, "Restart display", last);
        valueWithChevron(i, "", B_RESTART);
        break;
    }
  }
  drawPager(g_page, pages);
}

void drawNight() {
  clearContent();
  char buf[16];
  settingsRowFrame(0, "Automatic dimming", false);
  drawToggle(L.listW - 14 - 42, setRowY(0) + 4, g_settings.nightEnabled);
  addButton(0, setRowY(0), L.W, kSetRowH, B_NIGHT_TOGGLE);
  settingsRowFrame(1, "Dim from", false);
  snprintf(buf, sizeof(buf), "%02u:00", g_settings.nightStart);
  drawMinusPlus(ctrlX(), setRowY(1) + 3, B_NSTART_DOWN, B_NSTART_UP, buf);
  settingsRowFrame(2, "Until", false);
  snprintf(buf, sizeof(buf), "%02u:00", g_settings.nightEnd);
  drawMinusPlus(ctrlX(), setRowY(2) + 3, B_NEND_DOWN, B_NEND_UP, buf);
  settingsRowFrame(3, L.portrait ? "Dim level" : "Night brightness", true);
  snprintf(buf, sizeof(buf), "%u%%", g_settings.nightBrightness);
  drawMinusPlus(ctrlX(), setRowY(3) + 3, B_NLEVEL_DOWN, B_NLEVEL_UP, buf);
  drawWrapped("Times are Israel local time. The display dims within half a minute of the start time.", 12,
              setRowY(4) + 6, L.W - 24, 16, 3, F_S, col::TEXT3, col::BG);
}

// ---- favourite driver picker
void drawFavorite() {
  clearContent();
  DataLock l;
  const DriverStandings& d = g_data.drivers;
  int items = 1 + d.count;
  uint8_t pages = pageCount(items, L.rows);
  if (g_page >= pages) g_page = pages - 1;
  drawTextFit(d.count ? "Tap a driver to highlight them" : "Driver list appears after the first update", 6,
              L.listHdrY + 8, L.listW - 12, F_S, col::TEXT3, col::BG, ML_DATUM);
  for (int i = 0; i < L.rows; i++) {
    int item = g_page * L.rows + i;
    int y = rowY(i), cy = y + L.rowH / 2;
    if (item >= items) { tft.fillRect(0, y, L.listW, L.rowH, col::BG); continue; }
    bool selected;
    if (item == 0) selected = g_settings.favoriteId[0] == 0;
    else selected = strcmp(d.rows[item - 1].driverId, g_settings.favoriteId) == 0;
    drawRowBackground(i, selected);
    uint16_t bg = selected ? accentDim() : ((i & 1) ? col::ZEBRA : col::BG);
    if (item == 0) {
      drawText("None", 37, cy, F_M, col::TEXT, bg, ML_DATUM);
    } else {
      const DriverRow& r = d.rows[item - 1];
      drawTeamBar(29, y + 3, r.teamId);
      drawText(r.code, 37, cy, F_M, selected ? accent() : col::TEXT, bg, ML_DATUM);
      char name[48];
      snprintf(name, sizeof(name), "%s %s", r.given, r.family);
      drawTextFit(name, 78, cy, L.listW - 90, F_S, col::TEXT2, bg, ML_DATUM);
    }
    addButton(0, y, L.listW, L.rowH, B_ROW0 + i);
  }
  drawPager(g_page, pages);
}

// ---- about / diagnostics
static void aboutLine(int& y, const char* label, const char* value, uint16_t c = 0) {
  const int vx = L.portrait ? 84 : 96;
  drawText(label, L.portrait ? 8 : 12, y + 8, F_S, col::TEXT3, col::BG, ML_DATUM);
  drawTextFit(value, vx, y + 8, L.listW - vx - 6, F_S, c ? c : col::TEXT, col::BG, ML_DATUM);
  y += 17;
}

static const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brown-out (power supply)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_EXT: return "external reset";
    default: return "other";
  }
}

void drawAbout() {
  clearContent();
  uint8_t pages = 2;
  if (g_page >= pages) g_page = pages - 1;
  char v[80], w[16];
  int y = L.contentY + 4;
  DataLock l;
  const NetStatus& n = g_data.net;
  if (g_page == 0) {
    snprintf(v, sizeof(v), "%s %s · %s", GL_FW_NAME, GL_FW_VERSION, GL_DISPLAY_PROFILE);
    aboutLine(y, "Firmware", v);
    aboutLine(y, "Data", "Jolpica-F1 API · CC BY-NC-SA 4.0");
    if (n.wifi == WS_CONNECTED) snprintf(v, sizeof(v), "%s · %d dBm · %s", n.ssid, n.rssi, n.ip);
    else snprintf(v, sizeof(v), "%s (%s)", n.ssid[0] ? n.ssid : "-", n.wifi == WS_NO_CREDS ? "not set up" : "not connected");
    aboutLine(y, "Wi-Fi", v, n.wifi == WS_CONNECTED ? 0 : col::AMBER);
    int64_t now = nowUtc();
    if (now) {
      CivilTime c = toIsrael(now);
      snprintf(v, sizeof(v), "synced · %02d:%02d Israel (UTC+%d)", c.hour, c.minute, israelOffsetSeconds(now) / 3600);
    } else {
      snprintf(v, sizeof(v), "not synchronised");
    }
    aboutLine(y, "Clock", v, now ? 0 : col::AMBER);
    static const uint8_t order[] = {DS_CALENDAR, DS_DRIVERS, DS_TEAMS, DS_RESULTS, DS_CALENDAR_NEXT};
    static const char* const labels[] = {"Calendar", "Drivers", "Teams", "Results", "Next season"};
    for (int k = 0; k < 5; k++) {
      uint8_t ds = order[k];
      const DatasetMeta& m = g_data.meta[ds];
      const char* origin = m.origin == ORIGIN_NETWORK ? "online" : m.origin == ORIGIN_CACHE ? "cached" : "none";
      formatWhen(m.fetchedUtc, w, sizeof(w));
      uint16_t season = 0;
      switch (ds) {
        case DS_CALENDAR: season = g_data.calendar.season; break;
        case DS_CALENDAR_NEXT: season = g_data.calendarNext.season; break;
        case DS_DRIVERS: season = g_data.drivers.season; break;
        case DS_TEAMS: season = g_data.teams.season; break;
        case DS_RESULTS: season = g_data.results.season; break;
      }
      if (m.origin == ORIGIN_NONE) snprintf(v, sizeof(v), "none%s%s", m.lastError ? " · " : "", m.lastError ? fetchErrorName(m.lastError) : "");
      else snprintf(v, sizeof(v), "%u · %s %s%s%s", season, origin, w, m.lastError ? " · " : "", m.lastError ? fetchErrorName(m.lastError) : "");
      uint16_t c = (m.origin == ORIGIN_NETWORK && !m.lastError) ? 0 : col::AMBER;
      if (ds == DS_CALENDAR_NEXT && m.origin == ORIGIN_NONE && (!m.lastError || m.lastError == FE_EMPTY)) c = col::TEXT3;
      aboutLine(y, labels[k], v, c);
    }
    snprintf(v, sizeof(v), "%s %s", n.lastError ? fetchErrorName(n.lastError) : "none", n.lastErrorDetail);
    aboutLine(y, "Last error", v, n.lastError ? col::AMBER : 0);
  } else {
    snprintf(v, sizeof(v), "%u requests · %u failed", (unsigned)n.requests, (unsigned)n.failures);
    aboutLine(y, "API", v);
    snprintf(v, sizeof(v), "%u KB free · min %u KB · block %u KB", (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getMinFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
    aboutLine(y, "Memory", v);
    snprintf(v, sizeof(v), "net %u B · ui %u B free", (unsigned)netTaskStackHighWater(),
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    aboutLine(y, "Stacks", v);
    uint32_t up = millis() / 1000;
    snprintf(v, sizeof(v), "%lu h %02lu min", (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60));
    aboutLine(y, "Uptime", v);
    size_t used, total;
    storageInfo(used, total);
    snprintf(v, sizeof(v), "%u / %u KB", (unsigned)(used / 1024), (unsigned)(total / 1024));
    aboutLine(y, "Cache", v);
    snprintf(v, sizeof(v), "%d roots · %s", GL_CA_BUNDLE_COUNT, GL_CA_BUNDLE_SOURCE);
    aboutLine(y, "TLS trust", v);
    aboutLine(y, "TLS test", n.selfTest[0] ? n.selfTest : "not run", 0);
    aboutLine(y, "Reset", resetReasonName());
    int by = y + 6;
    tft.fillRoundRect(12, by, 150, 28, 8, col::PANEL2);
    drawText("Run TLS self-test", 87, by + 14, F_S, col::TEXT, col::PANEL2, MC_DATUM);
    addButton(6, by - 6, 162, 40, B_SELFTEST);
  }
  drawPager(g_page, pages);
}

// ------------------------------------------------------------------ actions
static void adjust(uint8_t& v, int delta, int lo, int hi, bool wrap) {
  int nv = (int)v + delta;
  if (wrap) {
    if (nv > hi) nv = lo;
    if (nv < lo) nv = hi;
  } else {
    if (nv > hi) nv = hi;
    if (nv < lo) nv = lo;
  }
  v = (uint8_t)nv;
}

static void applyBrightnessNow() {
  backlightSet(g_settings.brightness);
}

void dialogAction(uint8_t action) {
  switch (action) {
    case B_WIFI:
    case B_OPEN_SETUP:
      LOGI("UI", "restarting into Wi-Fi setup");
      settingsFlush();
      bootRequestSet(BOOT_SETUP);
      delay(150);
      ESP.restart();
      break;
    case B_CALIBRATE:
      if (uiRunCalibration(true)) showToast("Touch calibrated");
      requestFull();
      break;
    case B_RESTART:
      settingsFlush();
      delay(100);
      ESP.restart();
      break;
    default:
      if (action >= B_ROW0 && action < B_ROW0 + L.rows) {
        // favourite from the drivers list
        DataLock l;
        int idx = g_page * L.rows + (action - B_ROW0);
        if (idx < g_data.drivers.count) {
          const DriverRow& r = g_data.drivers.rows[idx];
          strncpy(g_settings.favoriteId, r.driverId, sizeof(g_settings.favoriteId) - 1);
          strncpy(g_settings.favoriteCode, r.code, sizeof(g_settings.favoriteCode) - 1);
          settingsChanged();
        }
      }
      break;
  }
}

bool handleScreenButton(uint8_t id) {
  char msg[96];
  switch (g_screen) {
    case S_NEXT:
      if (id == B_TOGGLE_VIEW) { g_weekendView = !g_weekendView; requestContent(); return true; }
      if (id == B_OPEN_SETUP) {
        openDialog("Wi-Fi setup", "The display restarts and creates its own Wi-Fi network for setup.", "Open", B_OPEN_SETUP);
        return true;
      }
      return false;
    case S_DRIVERS:
      if (id >= B_ROW0 && id < B_ROW0 + L.rows) {
        DataLock l;
        int idx = g_page * L.rows + (id - B_ROW0);
        if (idx >= g_data.drivers.count) return true;
        const DriverRow& r = g_data.drivers.rows[idx];
        if (strcmp(r.driverId, g_settings.favoriteId) == 0) return true;
        snprintf(msg, sizeof(msg), "Highlight %s %s on all screens?", r.given, r.family);
        openDialog("Favorite driver", msg, "Set", id);
        return true;
      }
      if (id == B_OPEN_SETUP) { dialogAction(B_OPEN_SETUP); return true; }
      return false;
    case S_CALENDAR:
      if (id >= B_ROW0 && id < B_ROW0 + L.rows) {
        g_raceIndex = (uint8_t)(g_page * L.rows + (id - B_ROW0));
        bool nextSeason = g_useNextSeason;
        go(S_RACE);
        g_useNextSeason = nextSeason;
        return true;
      }
      return false;
    case S_SETTINGS:
      switch (id) {
        case B_BRIGHT_DOWN: adjust(g_settings.brightness, -10, 10, 100, false); applyBrightnessNow(); settingsChanged(); requestContent(); return true;
        case B_BRIGHT_UP: adjust(g_settings.brightness, 10, 10, 100, false); applyBrightnessNow(); settingsChanged(); requestContent(); return true;
        case B_NIGHT: go(S_NIGHT); return true;
        case B_ORIENT_WIDE:
        case B_ORIENT_TALL:
        case B_FLIP: {
          uint8_t cur = settingsRotation();
          bool tall = (cur & 1) == 0, flipped = isFlipped(cur);
          if (id == B_ORIENT_WIDE) tall = false;
          if (id == B_ORIENT_TALL) tall = true;
          if (id == B_FLIP) flipped = !flipped;
          uint8_t next = rotationFor(tall, flipped);
          if (next == cur) return true;
          g_settings.rotation = next + 1;
          settingsChanged();
          applyOrientation(next);   // immediate; stays on this settings page
          return true;
        }
        case B_FAVORITE: go(S_FAVORITE); return true;
        case B_ABOUT: go(S_ABOUT); return true;
        case B_WIFI:
          openDialog("Wi-Fi setup", "Restart into setup mode? The saved network is kept until you choose a new one.",
                     "Open", B_WIFI);
          return true;
        case B_CALIBRATE:
          openDialog("Touch calibration", "You will tap 5 targets, then one check point. Use a stylus or fingernail.", "Start", B_CALIBRATE);
          return true;
        case B_REFRESH:
          netRequestRefresh();
          showToast("Refresh requested");
          return true;
        case B_RESTART:
          openDialog("Restart", "Restart the display now? Cached data is kept.", "Restart", B_RESTART);
          return true;
        default:
          if (id >= B_ACCENT0 && id <= B_ACCENT_LAST) {
            g_settings.accent = id - B_ACCENT0;
            settingsChanged();
            requestFull();
            return true;
          }
      }
      return false;
    case S_NIGHT:
      switch (id) {
        case B_NIGHT_TOGGLE: g_settings.nightEnabled = !g_settings.nightEnabled; break;
        case B_NSTART_DOWN: adjust(g_settings.nightStart, -1, 0, 23, true); break;
        case B_NSTART_UP: adjust(g_settings.nightStart, 1, 0, 23, true); break;
        case B_NEND_DOWN: adjust(g_settings.nightEnd, -1, 0, 23, true); break;
        case B_NEND_UP: adjust(g_settings.nightEnd, 1, 0, 23, true); break;
        case B_NLEVEL_DOWN: adjust(g_settings.nightBrightness, -2, 2, 60, false); backlightSet(g_settings.nightBrightness); break;
        case B_NLEVEL_UP: adjust(g_settings.nightBrightness, 2, 2, 60, false); backlightSet(g_settings.nightBrightness); break;
        default: return false;
      }
      settingsChanged();
      requestContent();
      return true;
    case S_FAVORITE:
      if (id >= B_ROW0 && id < B_ROW0 + L.rows) {
        int item = g_page * L.rows + (id - B_ROW0);
        DataLock l;
        if (item == 0) {
          g_settings.favoriteId[0] = 0;
          g_settings.favoriteCode[0] = 0;
        } else if (item - 1 < g_data.drivers.count) {
          const DriverRow& r = g_data.drivers.rows[item - 1];
          strncpy(g_settings.favoriteId, r.driverId, sizeof(g_settings.favoriteId) - 1);
          strncpy(g_settings.favoriteCode, r.code, sizeof(g_settings.favoriteCode) - 1);
        }
        settingsChanged();
        go(S_SETTINGS);
        return true;
      }
      return false;
    case S_ABOUT:
      if (id == B_SELFTEST) {
        netRequestSelfTest();
        showToast("TLS self-test started");
        return true;
      }
      return false;
    default:
      return false;
  }
}

}  // namespace ui
