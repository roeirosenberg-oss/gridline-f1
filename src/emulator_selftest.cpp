// Gridline F1 — on-target self-test for the ESP32 emulator (QEMU).
// Compiled only into the "qemu_selftest" PlatformIO environment, which is a
// test build that is NOT part of the release. QEMU has no Wi-Fi radio, so
// this task replaces the network task: it runs the real parsers and HTTP
// reader on the Xtensa CPU with recorded API fixtures, commits the data,
// writes the LittleFS cache and reports heap usage.
#ifdef GL_EMULATOR_SELFTEST
#include <Arduino.h>
#include <sys/time.h>
#include "app_state.h"
#include "fixtures_embedded.h"
#include "gl_http.h"
#include "gl_logic.h"
#include "gl_parse.h"
#include "log.h"
#include "storage.h"

using namespace gl;

namespace {

class MemSource : public ByteSource {
 public:
  MemSource(const char* d, size_t n) : d_(d), n_(n) {}
  int readSome(uint8_t* b, size_t n) override {
    if (pos_ >= n_) return -1;
    size_t k = n < 97 ? n : 97;  // odd piece size to exercise boundaries
    if (k > n_ - pos_) k = n_ - pos_;
    memcpy(b, d_ + pos_, k);
    pos_ += k;
    return (int)k;
  }
  uint32_t nowMs() override { return millis(); }

 private:
  const char* d_;
  size_t n_, pos_ = 0;
};

class BodyJson : public JsonSource {
 public:
  explicit BodyJson(BodyReader& b) : b_(b) {}
  int read() override { return b_.read(); }
  size_t readBytes(char* buf, size_t n) override { return b_.readBytes(buf, n); }
  FetchError transportError() const override { return b_.error(); }

 private:
  BodyReader& b_;
};

int s_fail = 0;

void expect(bool ok, const char* what) {
  LOGI("EMU", "%s %s", ok ? "PASS" : "FAIL", what);
  if (!ok) s_fail++;
}

template <typename T, typename F>
FetchError viaHttp(const char* json, T& out, F parse, bool chunked) {
  // build an HTTP response around the fixture and run it through the real reader
  size_t len = strlen(json);
  size_t cap = len + 4096;
  char* wire = (char*)malloc(cap);
  if (!wire) return FE_JSON_NOMEM;
  size_t w = 0;
  if (chunked) {
    w += snprintf(wire + w, cap - w, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n");
    size_t pos = 0;
    while (pos < len) {
      size_t k = len - pos < 1000 ? len - pos : 1000;
      w += snprintf(wire + w, cap - w, "%x\r\n", (unsigned)k);
      memcpy(wire + w, json + pos, k);
      w += k;
      pos += k;
      w += snprintf(wire + w, cap - w, "\r\n");
    }
    w += snprintf(wire + w, cap - w, "0\r\n\r\n");
  } else {
    w += snprintf(wire + w, cap - w, "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", (unsigned)len);
    memcpy(wire + w, json, len);
    w += len;
  }
  MemSource src(wire, w);
  BufferedSource in(src, 1000, 10000);
  HttpHead head;
  FetchError e = readHttpHead(in, head);
  if (e == FE_OK) {
    BodyReader body(in, head, 96 * 1024);
    BodyJson js(body);
    e = parse(js, out);
    FetchError te = body.finish();
    if (e == FE_OK && te != FE_OK) e = te;
  }
  free(wire);
  return e;
}

void commit(uint8_t ds, const void* src, size_t sz, void* dst) {
  int64_t now = nowUtc();
  {
    DataLock l;
    memcpy(dst, src, sz);
    g_data.meta[ds].fetchedUtc = now;
    g_data.meta[ds].origin = ORIGIN_NETWORK;
    g_data.version[ds]++;
  }
  expect(cacheSave(ds, src, sz, now), "cache written atomically");
}

void selfTestTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  LOGI("EMU", "emulator self-test start (no radio in QEMU; fixtures = recorded 2023 API responses)");
  bool restored;
  {
    DataLock l;
    restored = g_data.meta[DS_CALENDAR].origin == ORIGIN_CACHE && g_data.calendar.count == 22;
  }
  LOGI("EMU", "cache restored from previous boot: %s", restored ? "yes" : "no");
  struct timeval tv = {(time_t)makeUtc(2023, 7, 6, 9, 12, 26), 0};
  settimeofday(&tv, nullptr);
  {
    DataLock l;
    g_data.net.wifi = WS_CONNECTED;
    g_data.net.clockValid = 1;
    strcpy(g_data.net.ssid, "emulator");
    g_data.netVersion++;
  }
  logMemory("emu before parsing");
  void* staging = malloc(sizeof(Calendar));
  uint32_t t0 = millis();
  FetchError e = viaHttp(EMU_FIX_CALENDAR, *(Calendar*)staging,
                         [](JsonSource& s, Calendar& o) { return parseCalendar(s, 2023, o); }, true);
  Calendar* cal = (Calendar*)staging;
  LOGI("EMU", "calendar: %s, %d races, %u ms", fetchErrorName(e), e == FE_OK ? cal->count : 0, (unsigned)(millis() - t0));
  expect(e == FE_OK && cal->count == 22 && cal->races[5].raceUtc == makeUtc(2023, 5, 28, 13, 0, 0), "calendar parsed on target");
  if (e == FE_OK) commit(DS_CALENDAR, staging, sizeof(Calendar), &g_data.calendar);
  logMemory("emu after calendar");
  t0 = millis();
  DriverStandings* ds = (DriverStandings*)staging;
  e = viaHttp(EMU_FIX_DRIVERS, *ds, [](JsonSource& s, DriverStandings& o) { return parseDriverStandings(s, 2023, o); }, false);
  LOGI("EMU", "drivers: %s, %u ms", fetchErrorName(e), (unsigned)(millis() - t0));
  expect(e == FE_OK && strcmp(ds->rows[0].code, "VER") == 0 && strcmp(ds->rows[0].points, "575") == 0, "driver standings parsed on target");
  if (e == FE_OK) commit(DS_DRIVERS, staging, sizeof(DriverStandings), &g_data.drivers);
  ConstructorStandings* cs = (ConstructorStandings*)staging;
  e = viaHttp(EMU_FIX_TEAMS, *cs, [](JsonSource& s, ConstructorStandings& o) { return parseConstructorStandings(s, 2023, o); }, true);
  expect(e == FE_OK && cs->count == 10, "constructor standings parsed on target");
  if (e == FE_OK) commit(DS_TEAMS, staging, sizeof(ConstructorStandings), &g_data.teams);
  RaceResults* rr = (RaceResults*)staging;
  e = viaHttp(EMU_FIX_RESULTS, *rr, [](JsonSource& s, RaceResults& o) { return parseRaceResults(s, 2023, o); }, true);
  expect(e == FE_OK && rr->count == 20 && rr->rows[19].finishClass == FC_RETIRED, "race results parsed on target (incl. DNF)");
  if (e == FE_OK) commit(DS_RESULTS, staging, sizeof(RaceResults), &g_data.results);
  // negative cases must never be committed
  static const char trunc[] = "{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":\"2023\",\"round\":\"1\"";
  StringJsonSource ts(trunc, sizeof(trunc) - 1);
  expect(parseCalendar(ts, 2023, *(Calendar*)staging) == FE_JSON_INVALID, "truncated JSON rejected");
  static const char html[] = "<html>502 Bad Gateway</html>";
  StringJsonSource hs(html, sizeof(html) - 1);
  expect(parseCalendar(hs, 2023, *(Calendar*)staging) != FE_OK, "HTML error page rejected");
  {
    DataLock l;
    expect(g_data.calendar.count == 22, "committed calendar untouched by failed parses");
  }
  free(staging);
  // time conversion on target
  expect(israelOffsetSeconds(makeUtc(2026, 10, 24, 22, 59, 59)) == 10800 &&
             israelOffsetSeconds(makeUtc(2026, 10, 24, 23, 0, 0)) == 7200,
         "Israel DST end 2026 on target");
  logMemory("emu after parsing");
  LOGI("EMU", "self-test finished: %d failure(s)", s_fail);
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    logMemory("emu idle");
  }
}

}  // namespace

void emulatorSelfTestStart() { xTaskCreatePinnedToCore(selfTestTask, "emu", 12 * 1024, nullptr, 1, nullptr, 0); }
#endif
