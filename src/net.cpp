#include "net.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sntp.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include "esp_crt_bundle.h"  // project-local patched copy (lib/WiFiClientSecure)
#include "app_state.h"
#include "certs/ca_bundle.h"
#include "config.h"
#include "gl_http.h"
#include "gl_logic.h"
#include "gl_parse.h"
#include "log.h"
#include "settings.h"
#include "storage.h"

using namespace gl;

static TaskHandle_t s_task = nullptr;
static volatile bool s_refreshRequested = false;
static volatile bool s_selfTestRequested = false;
static uint32_t s_lastManualRefreshMs = 0;

// ------------------------------------------------------------------ status helpers
static void setActivity(uint8_t a) {
  DataLock l;
  if (g_data.net.activity != a) {
    g_data.net.activity = a;
    g_data.netVersion++;
  }
}

static void setWifiState(uint8_t w) {
  DataLock l;
  if (g_data.net.wifi != w) {
    g_data.net.wifi = w;
    g_data.netVersion++;
  }
}

// ------------------------------------------------------------------ TLS client
// Exposes the verification result of the underlying mbedTLS context.
class VerifiedClient : public WiFiClientSecure {
 public:
  uint32_t verifyResult() {
    if (!sslclient) return 0xFFFFFFFFu;
    return mbedtls_ssl_get_verify_result(&sslclient->ssl_ctx);
  }
};

class TlsSource : public ByteSource {
 public:
  explicit TlsSource(WiFiClientSecure& c) : c_(c) {}
  int readSome(uint8_t* buf, size_t n) override {
    int a = c_.available();
    if (a > 0) {
      int r = c_.read(buf, n < (size_t)a ? n : (size_t)a);
      return r > 0 ? r : 0;
    }
    if (!c_.connected()) return -1;
    return 0;
  }
  uint32_t nowMs() override { return millis(); }
  void idle() override { vTaskDelay(pdMS_TO_TICKS(5)); }

 private:
  WiFiClientSecure& c_;
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

static int64_t x509TimeToUtc(const mbedtls_x509_time& t) {
  return makeUtc(t.year, t.mon, t.day, t.hour, t.min, t.sec);
}

// The precompiled mbedTLS in arduino-esp32 2.0.17 is built without
// MBEDTLS_HAVE_TIME_DATE, so it never checks validity periods. We check
// every certificate presented by the server against the NTP clock.
static bool checkChainValidity(const mbedtls_x509_crt* crt, int64_t now, char* detail, size_t n, bool log) {
  int depth = 0;
  bool ok = true;
  for (const mbedtls_x509_crt* c = crt; c && depth < 8; c = c->next, depth++) {
    // Certificates above a trusted root (old cross-signing paths) are not part
    // of the trust decision, exactly as in browsers: stop at the anchor.
    if (depth > 0 && gl_crt_bundle_is_anchor(c)) {
      if (log) {
        char subj[96];
        mbedtls_x509_dn_gets(subj, sizeof(subj), &c->subject);
        LOGI("TLS", " cert[%d] trusted root reached: %s", depth, subj);
      }
      break;
    }
    char subj[96], iss[96];
    mbedtls_x509_dn_gets(subj, sizeof(subj), &c->subject);
    mbedtls_x509_dn_gets(iss, sizeof(iss), &c->issuer);
    int64_t from = x509TimeToUtc(c->valid_from);
    int64_t to = x509TimeToUtc(c->valid_to);
    if (log) {
      LOGI("TLS", " cert[%d] subject: %s", depth, subj);
      LOGI("TLS", " cert[%d] issuer : %s", depth, iss);
      LOGI("TLS", " cert[%d] valid  : %04d-%02d-%02d .. %04d-%02d-%02d", depth, c->valid_from.year, c->valid_from.mon,
           c->valid_from.day, c->valid_to.year, c->valid_to.mon, c->valid_to.day);
    }
    if (now + 300 < from) {
      snprintf(detail, n, "cert[%d] not yet valid", depth);
      ok = false;
    } else if (now > to) {
      snprintf(detail, n, "cert[%d] expired", depth);
      ok = false;
    }
  }
  return ok;
}

struct HttpResult {
  FetchError err = FE_OK;
  int status = 0;
  int32_t retryAfter = -1;
  size_t bytes = 0;
  size_t jsonPeak = 0;
  uint32_t ms = 0;
  char detail[48] = {0};
};

// Opens a verified TLS connection. Returns FE_OK and leaves the client
// connected, or an error with the client stopped.
static FetchError tlsConnect(VerifiedClient& client, const char* host, char* detail, size_t dn, bool logChain) {
  if (WiFi.status() != WL_CONNECTED) return FE_NO_WIFI;
  int64_t now = nowUtc();
  if (now == 0) return FE_CLOCK;  // never start TLS without a valid clock
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) {
    snprintf(detail, dn, "DNS lookup failed");
    return FE_DNS;
  }
  client.setCACertBundle(GL_CA_BUNDLE);
  client.setHandshakeTimeout(GL_TLS_HANDSHAKE_TIMEOUT_S);
  if (!client.connect(host, GL_API_PORT, GL_TCP_CONNECT_TIMEOUT_MS)) {
    char err[96] = {0};
    int code = client.lastError(err, sizeof(err));
    client.stop();
    if (code == -1 || code == 0) {
      snprintf(detail, dn, "connect/handshake timeout");
      return FE_CONNECT;
    }
    snprintf(detail, dn, "-0x%04X %.36s", -code, err);
    return FE_TLS;
  }
  uint32_t vr = client.verifyResult();
  if (vr != 0) {
    char info[128];
    mbedtls_x509_crt_verify_info(info, sizeof(info), "", vr);
    LOGI("TLS", "verification flags 0x%08x: %s", (unsigned)vr, info);
    snprintf(detail, dn, "verify flags 0x%x", (unsigned)vr);
    client.stop();
    return FE_TLS;
  }
  const mbedtls_x509_crt* peer = client.getPeerCertificate();
  if (!peer) {
    snprintf(detail, dn, "no peer certificate");
    client.stop();
    return FE_TLS;
  }
  if (!checkChainValidity(peer, now, detail, dn, logChain)) {
    LOGI("TLS", "rejected: %s", detail);
    client.stop();
    return FE_TLS;
  }
  return FE_OK;
}

template <typename ParseFn>
static void httpsGetJson(const char* path, ParseFn parse, HttpResult& res) {
  uint32_t t0 = millis();
  static bool s_chainLogged = false;
  VerifiedClient client;
  LOGI("HTTP", "GET https://%s%s", GL_API_HOST, path);
  res.err = tlsConnect(client, GL_API_HOST, res.detail, sizeof(res.detail), !s_chainLogged);
  if (res.err != FE_OK) {
    res.ms = millis() - t0;
    return;
  }
  if (!s_chainLogged) {
    s_chainLogged = true;
    LOGI("TLS", "chain verified for %s (bundle: %d roots, %s)", GL_API_HOST, GL_CA_BUNDLE_COUNT, GL_CA_BUNDLE_SOURCE);
  }
  char req[320];
  int len = buildGetRequest(req, sizeof(req), GL_API_HOST, path, GL_USER_AGENT);
  if (len < 0 || client.write((const uint8_t*)req, len) != (size_t)len) {
    res.err = FE_CONNECT;
    snprintf(res.detail, sizeof(res.detail), "request write failed");
    client.stop();
    res.ms = millis() - t0;
    return;
  }
  TlsSource src(client);
  BufferedSource in(src, GL_HTTP_IDLE_TIMEOUT_MS, GL_HTTP_TOTAL_TIMEOUT_MS);
  HttpHead head;
  res.err = readHttpHead(in, head);
  if (res.err == FE_OK) {
    res.status = head.status;
    res.retryAfter = head.retryAfterSec;
    res.err = classifyHttpStatus(head.status);
    LOGI("HTTP", "status %d, length %lld, chunked %d, type %s", head.status, (long long)head.contentLength,
         head.chunked ? 1 : 0, head.contentType);
  }
  if (res.err == FE_OK) {
    BodyReader body(in, head, GL_HTTP_MAX_BODY_BYTES);
    if (body.error() != FE_OK) {
      res.err = body.error();
    } else {
      BodyJson js(body);
      ParseStats st;
      res.err = parse(js, st);
      FetchError te = body.finish();
      if (res.err == FE_OK && te != FE_OK) res.err = te;
      res.jsonPeak = st.peakJsonBytes;
      if (st.detail[0]) strncpy(res.detail, st.detail, sizeof(res.detail) - 1);
      if (st.entriesSkipped || st.truncated)
        LOGI("JSON", "entries %d, skipped %d, truncated %d", st.entriesSeen, st.entriesSkipped, st.truncated ? 1 : 0);
    }
    res.bytes = body.bytesDelivered();
  }
  client.stop();
  res.ms = millis() - t0;
}

// ------------------------------------------------------------------ scheduling state
static ScheduleState s_sched;
static bool s_forced[DS_COUNT];
static uint32_t s_lastRequestMs = 0;
static uint8_t* s_staging = nullptr;  // large enough for any dataset

static size_t datasetSize(uint8_t ds) {
  switch (ds) {
    case DS_CALENDAR:
    case DS_CALENDAR_NEXT: return sizeof(Calendar);
    case DS_DRIVERS: return sizeof(DriverStandings);
    case DS_TEAMS: return sizeof(ConstructorStandings);
    case DS_RESULTS: return sizeof(RaceResults);
  }
  return 0;
}

static void* datasetPtr(uint8_t ds) {
  switch (ds) {
    case DS_CALENDAR: return &g_data.calendar;
    case DS_CALENDAR_NEXT: return &g_data.calendarNext;
    case DS_DRIVERS: return &g_data.drivers;
    case DS_TEAMS: return &g_data.teams;
    case DS_RESULTS: return &g_data.results;
  }
  return nullptr;
}

static void updateSeasonRound(uint8_t ds, const void* p) {
  switch (ds) {
    case DS_CALENDAR:
    case DS_CALENDAR_NEXT:
      s_sched.seasonOf[ds] = ((const Calendar*)p)->season;
      break;
    case DS_DRIVERS:
      s_sched.seasonOf[ds] = ((const DriverStandings*)p)->season;
      s_sched.roundOf[ds] = ((const DriverStandings*)p)->round;
      break;
    case DS_TEAMS:
      s_sched.seasonOf[ds] = ((const ConstructorStandings*)p)->season;
      s_sched.roundOf[ds] = ((const ConstructorStandings*)p)->round;
      break;
    case DS_RESULTS:
      s_sched.seasonOf[ds] = ((const RaceResults*)p)->season;
      s_sched.roundOf[ds] = ((const RaceResults*)p)->round;
      break;
  }
}

void netRestoreCache() {
  memset(&s_sched, 0, sizeof(s_sched));
  for (uint8_t ds = 0; ds < DS_COUNT; ds++) {
    int64_t fetched = 0;
    void* dst = datasetPtr(ds);
    DataLock l;
    if (cacheLoad(ds, dst, datasetSize(ds), fetched)) {
      g_data.meta[ds].fetchedUtc = fetched;
      g_data.meta[ds].origin = ORIGIN_CACHE;
      g_data.version[ds]++;
      s_sched.meta[ds] = g_data.meta[ds];
      updateSeasonRound(ds, dst);
    } else {
      memset(dst, 0, datasetSize(ds));
    }
  }
}

// Fetch one dataset for one season into the staging buffer.
static FetchError fetchInto(uint8_t ds, uint16_t season, HttpResult& res) {
  char path[96];
  switch (ds) {
    case DS_CALENDAR:
    case DS_CALENDAR_NEXT:
      snprintf(path, sizeof(path), GL_API_BASE "/%u/races.json?limit=100", season);
      httpsGetJson(path, [&](JsonSource& s, ParseStats& st) {
        return parseCalendar(s, season, *(Calendar*)s_staging, &st);
      }, res);
      break;
    case DS_DRIVERS:
      snprintf(path, sizeof(path), GL_API_BASE "/%u/driverstandings.json?limit=100", season);
      httpsGetJson(path, [&](JsonSource& s, ParseStats& st) {
        return parseDriverStandings(s, season, *(DriverStandings*)s_staging, &st);
      }, res);
      break;
    case DS_TEAMS:
      snprintf(path, sizeof(path), GL_API_BASE "/%u/constructorstandings.json?limit=100", season);
      httpsGetJson(path, [&](JsonSource& s, ParseStats& st) {
        return parseConstructorStandings(s, season, *(ConstructorStandings*)s_staging, &st);
      }, res);
      break;
    case DS_RESULTS:
      snprintf(path, sizeof(path), GL_API_BASE "/%u/last/results.json?limit=100", season);
      httpsGetJson(path, [&](JsonSource& s, ParseStats& st) {
        return parseRaceResults(s, season, *(RaceResults*)s_staging, &st);
      }, res);
      break;
  }
  s_lastRequestMs = millis();
  {
    DataLock l;
    g_data.net.requests++;
  }
  return res.err;
}

static void waitSpacing() {
  while (millis() - s_lastRequestMs < GL_MIN_REQUEST_SPACING_MS) vTaskDelay(pdMS_TO_TICKS(50));
}

static float jitter() { return (esp_random() % 1000) / 1000.0f; }

static void runFetch(uint8_t ds, uint16_t currentSeason) {
  setActivity(ACT_UPDATING);
  uint16_t season = (ds == DS_CALENDAR_NEXT) ? currentSeason + 1 : currentSeason;
  HttpResult res;
  waitSpacing();
  FetchError e = fetchInto(ds, season, res);
  // Standings and results fall back to the previous season while the new
  // season has no published data yet (January..first race).
  if ((e == FE_EMPTY || e == FE_HTTP_404) && (ds == DS_DRIVERS || ds == DS_TEAMS || ds == DS_RESULTS)) {
    LOGI("DATA", "%s: no %u data yet, trying %u", datasetName(ds), season, season - 1);
    season = currentSeason - 1;
    res = HttpResult();
    waitSpacing();
    e = fetchInto(ds, season, res);
  }
  int64_t now = nowUtc();
  DatasetMeta& m = s_sched.meta[ds];
  m.lastAttemptUtc = now;
  if (e == FE_OK) {
    size_t sz = datasetSize(ds);
    {
      DataLock l;
      memcpy(datasetPtr(ds), s_staging, sz);
      m.fetchedUtc = now;
      m.origin = ORIGIN_NETWORK;
      m.lastError = FE_OK;
      m.consecutiveFailures = 0;
      g_data.meta[ds] = m;
      g_data.version[ds]++;
      g_data.net.lastError = FE_OK;
      g_data.net.lastErrorDetail[0] = 0;
      g_data.net.lastSuccessUtc = now;
      g_data.netVersion++;
    }
    updateSeasonRound(ds, s_staging);
    s_sched.nextAttemptUtc[ds] = 0;
    s_sched.networkFailures = 0;
    s_sched.networkRetryUtc = 0;
    LOGI("DATA", "%s %u updated: %u bytes in %u ms, JSON peak %u B", datasetName(ds), season, (unsigned)res.bytes,
         (unsigned)res.ms, (unsigned)res.jsonPeak);
    // Persist the new data (a storage failure never discards the update).
    if (!cacheSave(ds, s_staging, sz, now)) LOGI("FS", "cache write failed for %s", datasetName(ds));
  } else if (e == FE_EMPTY) {
    uint32_t wait = (ds == DS_CALENDAR_NEXT) ? 6 * 3600 : retryDelaySeconds(1, FE_EMPTY, -1, jitter());
    s_sched.nextAttemptUtc[ds] = now + wait;
    m.lastError = FE_EMPTY;
    {
      DataLock l;
      g_data.meta[ds] = m;
      g_data.version[ds]++;
    }
    LOGI("DATA", "%s %u: no data published yet; next check in %u s", datasetName(ds), season, (unsigned)wait);
  } else {
    if (m.consecutiveFailures < 250) m.consecutiveFailures++;
    m.lastError = e;
    uint32_t wait = retryDelaySeconds(m.consecutiveFailures, e, res.retryAfter, jitter());
    s_sched.nextAttemptUtc[ds] = now + wait;
    if (isNetworkLevelError(e)) {
      if (s_sched.networkFailures < 250) s_sched.networkFailures++;
      uint32_t gate = retryDelaySeconds(s_sched.networkFailures, e, res.retryAfter, jitter());
      s_sched.networkRetryUtc = now + gate;
      if (gate > wait) wait = gate;
    }
    {
      DataLock l;
      g_data.meta[ds] = m;
      g_data.version[ds]++;
      g_data.net.failures++;
      g_data.net.lastError = e;
      strncpy(g_data.net.lastErrorDetail, res.detail, sizeof(g_data.net.lastErrorDetail) - 1);
      g_data.netVersion++;
    }
    LOGI("RETRY", "%s failed: %s (%s), HTTP %d, %u ms; retry in %u s (failure #%u)", datasetName(ds),
         fetchErrorName(e), res.detail, res.status, (unsigned)res.ms, (unsigned)wait, m.consecutiveFailures);
  }
}

// ------------------------------------------------------------------ TLS self-test
static void runSelfTest() {
  setActivity(ACT_SELFTEST);
  struct T { const char* host; bool expectOk; const char* label; };
  static const T tests[] = {
      {GL_API_HOST, true, "API"},
      {"wrong.host.badssl.com", false, "wrong host"},
      {"self-signed.badssl.com", false, "self-signed"},
      {"untrusted-root.badssl.com", false, "untrusted root"},
      {"expired.badssl.com", false, "expired"},
  };
  char out[96] = {0};
  int pass = 0, total = 0;
  for (const T& t : tests) {
    VerifiedClient c;
    char detail[48] = {0};
    waitSpacing();
    FetchError e = tlsConnect(c, t.host, detail, sizeof(detail), false);
    s_lastRequestMs = millis();
    c.stop();
    bool ok = (e == FE_OK);
    const char* verdict;
    if (e == FE_DNS || e == FE_CONNECT || e == FE_NO_WIFI || e == FE_CLOCK) {
      verdict = "n/a";
    } else {
      total++;
      if (ok == t.expectOk) { pass++; verdict = "PASS"; }
      else verdict = "FAIL";
    }
    LOGI("TLS", "self-test %-26s expect %-8s got %-12s %s %s", t.host, t.expectOk ? "accept" : "reject",
         fetchErrorName(e), detail, verdict);
  }
  snprintf(out, sizeof(out), "%d/%d checks passed", pass, total);
  LOGI("TLS", "self-test result: %s", out);
  DataLock l;
  strncpy(g_data.net.selfTest, out, sizeof(g_data.net.selfTest) - 1);
  g_data.netVersion++;
}

// ------------------------------------------------------------------ task
static void onTimeSync(struct timeval* tv) {
  LOGI("NTP", "time synchronised: %ld", (long)tv->tv_sec);
}

static void netTask(void*) {
  char ssid[33] = {0}, pass[65] = {0};
  if (!wifiCredsLoad(ssid, sizeof(ssid), pass, sizeof(pass))) {
    setWifiState(WS_NO_CREDS);
    LOGI("WIFI", "no credentials stored - waiting for setup");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
  }
  {
    DataLock l;
    strncpy(g_data.net.ssid, ssid, sizeof(g_data.net.ssid) - 1);
  }
  WiFi.persistent(false);
  WiFi.setHostname(GL_HOSTNAME);  // must precede mode()/begin() on arduino-esp32 2.0.x
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(ssid, pass);
  LOGI("WIFI", "connecting to \"%s\"", ssid);
  setWifiState(WS_CONNECTING);
  setActivity(ACT_CONNECTING);
  uint32_t lastBegin = millis();
  const uint32_t firstBegin = lastBegin;
  uint32_t reconnectDelay = 15000;
  bool wasConnected = false, sntpStarted = false, clockLogged = false;
  uint32_t lastRssi = 0, lastMemLog = 0;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    wl_status_t wl = WiFi.status();
    if (wl != WL_CONNECTED) {
      if (wasConnected) {
        wasConnected = false;
        LOGI("WIFI", "connection lost (status %d)", (int)wl);
        setWifiState(WS_LOST);
        lastBegin = millis();
        reconnectDelay = 5000;
      }
      setActivity(ACT_CONNECTING);
      // never connected since boot: after 45 s report "Offline" instead of "Connecting"
      if (!sntpStarted && millis() - firstBegin > 45000) setWifiState(WS_LOST);
      if (millis() - lastBegin > reconnectDelay) {
        LOGI("WIFI", "reconnecting (status %d, next wait %u ms)", (int)wl, (unsigned)reconnectDelay);
        WiFi.disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        WiFi.begin(ssid, pass);
        lastBegin = millis();
        reconnectDelay = reconnectDelay < 60000 ? reconnectDelay * 2 : 60000;
      }
      continue;
    }
    if (!wasConnected) {
      wasConnected = true;
      reconnectDelay = 15000;
      DataLock l;
      strncpy(g_data.net.ip, WiFi.localIP().toString().c_str(), sizeof(g_data.net.ip) - 1);
      g_data.net.rssi = (int8_t)WiFi.RSSI();
      g_data.net.wifi = WS_CONNECTED;
      g_data.netVersion++;
      LOGI("WIFI", "connected: IP %s, RSSI %d dBm, channel %d", g_data.net.ip, g_data.net.rssi, WiFi.channel());
      if (!sntpStarted) {
        sntp_set_time_sync_notification_cb(onTimeSync);
        configTime(0, 0, GL_NTP_1, GL_NTP_2, GL_NTP_3);  // the device keeps UTC
        sntpStarted = true;
        LOGI("NTP", "SNTP started (%s, %s, %s)", GL_NTP_1, GL_NTP_2, GL_NTP_3);
      }
    }
    if (millis() - lastRssi > 15000) {
      lastRssi = millis();
      DataLock l;
      int8_t r = (int8_t)WiFi.RSSI();
      if (r != g_data.net.rssi) { g_data.net.rssi = r; g_data.netVersion++; }
    }
    if (!clockIsValid()) {
      setActivity(ACT_SYNCING);
      continue;
    }
    if (!clockLogged) {
      clockLogged = true;
      int64_t t = nowUtc();
      CivilTime u = toCivilUtc(t), il = toIsrael(t);
      LOGI("NTP", "clock valid: %04d-%02d-%02d %02d:%02d:%02d UTC = %02d:%02d Israel (UTC+%d)", u.year, u.month, u.day,
           u.hour, u.minute, u.second, il.hour, il.minute, israelOffsetSeconds(t) / 3600);
      DataLock l;
      g_data.net.clockValid = 1;
      g_data.netVersion++;
    }
    if (millis() - lastMemLog > 600000 || lastMemLog == 0) {
      lastMemLog = millis();
      logMemory("net");
    }
    if (s_selfTestRequested) {
      s_selfTestRequested = false;
      runSelfTest();
      continue;
    }
    if (s_refreshRequested) {
      s_refreshRequested = false;
      for (uint8_t ds = 0; ds < DS_COUNT; ds++) s_forced[ds] = true;
      s_sched.networkRetryUtc = 0;
      LOGI("DATA", "manual refresh requested");
    }

    int64_t now = nowUtc();
    uint16_t season = (uint16_t)toCivilUtc(now).year;
    ScheduleContext ctx;
    ctx.now = now;
    ctx.currentSeason = season;
    int pick = -1;
    {
      DataLock l;
      bool calOk = g_data.meta[DS_CALENDAR].origin != ORIGIN_NONE && g_data.calendar.season == season;
      ctx.calendar = calOk ? &g_data.calendar : nullptr;
      ctx.needNextSeasonCalendar = calOk && findNextRace(now, &g_data.calendar, nullptr).race == nullptr;
      if (now >= s_sched.networkRetryUtc) {
        static const uint8_t order[] = {DS_CALENDAR, DS_DRIVERS, DS_RESULTS, DS_TEAMS, DS_CALENDAR_NEXT};
        for (uint8_t ds : order) {
          if (!s_forced[ds]) continue;
          s_forced[ds] = false;
          if (ds == DS_CALENDAR_NEXT && !ctx.needNextSeasonCalendar) continue;
          pick = ds;
          break;
        }
      }
      if (pick < 0) pick = pickDueDataset(ctx, s_sched);
    }
    if (pick < 0) {
      setActivity(ACT_IDLE);
      continue;
    }
    runFetch((uint8_t)pick, season);
    setActivity(ACT_IDLE);
  }
}

#ifdef GL_EMULATOR_SELFTEST
void emulatorSelfTestStart();
#endif

void netStart() {
#ifdef GL_EMULATOR_SELFTEST
  LOGI("BOOT", "EMULATOR TEST BUILD: radio disabled, running on-target self-test instead of the network task");
  emulatorSelfTestStart();
  return;
#endif
  size_t need = sizeof(Calendar);
  if (sizeof(DriverStandings) > need) need = sizeof(DriverStandings);
  if (sizeof(ConstructorStandings) > need) need = sizeof(ConstructorStandings);
  if (sizeof(RaceResults) > need) need = sizeof(RaceResults);
  s_staging = (uint8_t*)malloc(need);
  LOGI("BOOT", "staging buffer %u bytes", (unsigned)need);
  xTaskCreatePinnedToCore(netTask, "net", 14 * 1024, nullptr, 1, &s_task, 0);
}

void netRequestRefresh() {
  if (s_lastManualRefreshMs && millis() - s_lastManualRefreshMs < GL_MANUAL_REFRESH_COOLDOWN_S * 1000UL) {
    LOGI("DATA", "manual refresh ignored (cool-down)");
    return;
  }
  s_lastManualRefreshMs = millis();
  s_refreshRequested = true;
}

void netRequestSelfTest() { s_selfTestRequested = true; }

uint32_t netTaskStackHighWater() { return s_task ? uxTaskGetStackHighWaterMark(s_task) : 0; }
