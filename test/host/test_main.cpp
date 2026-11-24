// Gridline F1 — host unit tests for the platform-independent core.
// Build & run: tools/run_host_tests.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <random>
#include <functional>

#include "gl_time.h"
#include "gl_text.h"
#include "gl_http.h"
#include "gl_parse.h"
#include "gl_logic.h"

using namespace gl;

static int g_checks = 0, g_fail = 0;
static const char* g_section = "";
#define CHECK(c)                                                                         \
  do {                                                                                   \
    g_checks++;                                                                          \
    if (!(c)) {                                                                          \
      g_fail++;                                                                          \
      fprintf(stderr, "FAIL [%s] %s:%d: %s\n", g_section, __FILE__, __LINE__, #c);      \
    }                                                                                    \
  } while (0)
#define CHECK_EQ(a, b)                                                                   \
  do {                                                                                   \
    g_checks++;                                                                          \
    auto _a = (a); auto _b = (b);                                                        \
    if (!(_a == _b)) {                                                                   \
      g_fail++;                                                                          \
      fprintf(stderr, "FAIL [%s] %s:%d: %s == %s (%lld vs %lld)\n", g_section, __FILE__, \
              __LINE__, #a, #b, (long long)_a, (long long)_b);                            \
    }                                                                                    \
  } while (0)
#define CHECK_STR(a, b)                                                                  \
  do {                                                                                   \
    g_checks++;                                                                          \
    if (strcmp((a), (b)) != 0) {                                                         \
      g_fail++;                                                                          \
      fprintf(stderr, "FAIL [%s] %s:%d: \"%s\" != \"%s\"\n", g_section, __FILE__,        \
              __LINE__, (a), (b));                                                       \
    }                                                                                    \
  } while (0)

static std::string g_fixtures;
static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) { fprintf(stderr, "cannot open %s\n", p.c_str()); exit(2); }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
static std::string fixture(const char* name) { return readFile(g_fixtures + "/" + name); }

// ------------------------------------------------------------------ fake transport
// Delivers a byte string in pieces of configurable size; can stall (timeout)
// or close early.
class FakeSource : public ByteSource {
 public:
  FakeSource(const std::string& data, size_t piece = 64) : data_(data), piece_(piece) {}
  int readSome(uint8_t* buf, size_t n) override {
    if (stallAt_ != std::string::npos && pos_ >= stallAt_) { now_ += 50; return 0; }
    if (pos_ >= data_.size()) return -1;
    size_t k = std::min(std::min(n, piece_), data_.size() - pos_);
    if (stallAt_ != std::string::npos && pos_ + k > stallAt_) k = stallAt_ - pos_;
    memcpy(buf, data_.data() + pos_, k);
    pos_ += k;
    now_ += 1;
    return (int)k;
  }
  uint32_t nowMs() override { return now_; }
  void idle() override { now_ += 5; }
  size_t stallAt_ = std::string::npos;

 private:
  std::string data_;
  size_t piece_;
  size_t pos_ = 0;
  uint32_t now_ = 1000;
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

static std::string chunkify(const std::string& body, std::mt19937& rng, bool ext = false) {
  std::string out;
  size_t pos = 0;
  std::uniform_int_distribution<int> d(1, 700);
  while (pos < body.size()) {
    size_t k = std::min((size_t)d(rng), body.size() - pos);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), ext ? "%zx;foo=bar\r\n" : "%zX\r\n", k);
    out += hdr;
    out += body.substr(pos, k);
    out += "\r\n";
    pos += k;
  }
  out += "0\r\n\r\n";
  return out;
}

static std::string httpResponse(const std::string& body, bool chunked, std::mt19937& rng, int status = 200,
                                const char* extra = "") {
  std::string h = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\n";
  h += extra;
  if (chunked) return h + "Transfer-Encoding: chunked\r\n\r\n" + chunkify(body, rng);
  return h + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

struct FetchOutcome {
  FetchError err;
  FetchError transport;
};

template <typename T, typename F>
static FetchOutcome runPipeline(const std::string& wire, size_t piece, T& out, F parseFn, size_t maxBytes = 65536) {
  FakeSource src(wire, piece);
  BufferedSource in(src, 2000, 30000);
  HttpHead head;
  FetchError e = readHttpHead(in, head);
  if (e != FE_OK) return {e, e};
  FetchError se = classifyHttpStatus(head.status);
  if (se != FE_OK) return {se, se};
  BodyReader body(in, head, maxBytes);
  if (body.error() != FE_OK) return {body.error(), body.error()};
  BodyJson js(body);
  FetchError pe = parseFn(js, out);
  FetchError te = body.finish();
  if (pe == FE_OK && te != FE_OK) pe = te;
  return {pe, te};
}

// ================================================================== tests
static void testCivil() {
  g_section = "civil";
  CHECK_EQ(daysFromCivil(1970, 1, 1), 0);
  CHECK_EQ(makeUtc(2026, 10, 24, 13, 23, 0), 1792848180LL);
  CivilTime c = toCivilUtc(1792848180LL);
  CHECK_EQ(c.year, 2026); CHECK_EQ(c.month, 10); CHECK_EQ(c.day, 24);
  CHECK_EQ(c.hour, 13); CHECK_EQ(c.minute, 23); CHECK_EQ(c.weekday, 6);  // Saturday
  // round trip over a wide range of days
  for (int64_t d = -1000; d < 80000; d += 7) {
    int y, m, dd;
    civilFromDays(d, y, m, dd);
    CHECK_EQ(daysFromCivil(y, m, dd), d);
  }
  CHECK_EQ(toCivilUtc(makeUtc(2038, 1, 19, 3, 14, 8) + 1).year, 2038);  // beyond int32
  CHECK(makeUtc(2038, 1, 19, 3, 14, 8) + 1 > 2147483647LL);
}

static void testIsraelTz(const std::string& csvPath) {
  g_section = "israel_tz";
  std::ifstream f(csvPath);
  CHECK(f.good());
  std::string line;
  int transitions = 0, samples = 0;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == 'T') {
      int y; long long a, b;
      sscanf(line.c_str(), "T,%d,%lld,%lld", &y, &a, &b);
      CHECK_EQ(israelDstStartUtc(y), a);
      CHECK_EQ(israelDstEndUtc(y), b);
      transitions++;
    } else if (line[0] == 'S') {
      long long t; int off;
      sscanf(line.c_str(), "S,%lld,%d", &t, &off);
      if (israelOffsetSeconds(t) != off) {
        fprintf(stderr, "  tz mismatch at %lld: got %d want %d\n", t, israelOffsetSeconds(t), off);
      }
      CHECK_EQ(israelOffsetSeconds(t), off);
      samples++;
    }
  }
  printf("  tz reference: %d years of transitions, %d samples\n", transitions, samples);
  CHECK(transitions >= 30);
  CHECK(samples > 20000);
  // Known instants: today 2026-10-24 is still IDT; DST ends 2026-10-25 02:00 local.
  CHECK_EQ(israelOffsetSeconds(makeUtc(2026, 10, 24, 13, 23, 0)), 10800);
  CHECK_EQ(israelDstEndUtc(2026), makeUtc(2026, 10, 24, 23, 0, 0));
  CHECK_EQ(israelOffsetSeconds(makeUtc(2026, 10, 24, 22, 59, 59)), 10800);
  CHECK_EQ(israelOffsetSeconds(makeUtc(2026, 10, 24, 23, 0, 0)), 7200);
  CHECK_EQ(israelDstStartUtc(2027), makeUtc(2027, 3, 26, 0, 0, 0));  // Friday 26 Mar 2027
  CivilTime l = toIsrael(makeUtc(2026, 10, 24, 22, 30, 0));  // 01:30 IDT on the 25th
  CHECK_EQ(l.day, 25); CHECK_EQ(l.hour, 1); CHECK_EQ(l.minute, 30);
  l = toIsrael(makeUtc(2026, 10, 24, 23, 30, 0));             // 01:30 IST (repeated hour)
  CHECK_EQ(l.day, 25); CHECK_EQ(l.hour, 1); CHECK_EQ(l.minute, 30);
}

static void testIsoParse() {
  g_section = "iso";
  int y, m, d, s;
  CHECK(parseIsoDate("2026-10-25", y, m, d)); CHECK_EQ(y, 2026); CHECK_EQ(m, 10); CHECK_EQ(d, 25);
  CHECK(!parseIsoDate("2026-13-01", y, m, d));
  CHECK(!parseIsoDate("2026-02-30", y, m, d));
  CHECK(!parseIsoDate("2025-02-29", y, m, d));
  CHECK(parseIsoDate("2028-02-29", y, m, d));
  CHECK(!parseIsoDate("2026-1-01", y, m, d));
  CHECK(!parseIsoDate("", y, m, d));
  CHECK(!parseIsoDate(nullptr, y, m, d));
  CHECK(!parseIsoDate("2026-10-25T10:00", y, m, d));
  CHECK(parseIsoTimeUtc("13:00:00Z", s)); CHECK_EQ(s, 46800);
  CHECK(parseIsoTimeUtc("05:30:15", s)); CHECK_EQ(s, 19815);
  CHECK(parseIsoTimeUtc("05:30Z", s)); CHECK_EQ(s, 19800);
  CHECK(parseIsoTimeUtc("05:30:00.000Z", s)); CHECK_EQ(s, 19800);
  CHECK(parseIsoTimeUtc("05:30:00+00:00", s));
  CHECK(!parseIsoTimeUtc("05:30:00+02:00", s));
  CHECK(!parseIsoTimeUtc("25:00:00Z", s));
  CHECK(!parseIsoTimeUtc("", s));
  CHECK(!parseIsoTimeUtc(nullptr, s));
  CHECK(!parseIsoTimeUtc("TBC", s));
  utc_t t; bool ht;
  CHECK(parseApiDateTime("2026-10-25", nullptr, t, ht)); CHECK(!ht); CHECK_EQ(t, makeUtc(2026, 10, 25, 0, 0, 0));
  CHECK(parseApiDateTime("2026-10-25", "20:00:00Z", t, ht)); CHECK(ht); CHECK_EQ(t, makeUtc(2026, 10, 25, 20, 0, 0));
  char buf[24];
  formatDayDate(toCivilUtc(makeUtc(2026, 10, 25, 0, 0, 0)), buf, sizeof(buf));
  CHECK_STR(buf, "Sun 25 Oct");
  Countdown cd = splitCountdown(100, 100 + 2 * 86400 + 3 * 3600 + 4 * 60 + 5);
  CHECK_EQ(cd.days, 2); CHECK_EQ(cd.hours, 3); CHECK_EQ(cd.minutes, 4); CHECK_EQ(cd.seconds, 5); CHECK(!cd.past);
  cd = splitCountdown(200, 100);
  CHECK(cd.past); CHECK_EQ(cd.days, 0);
}

static void testText() {
  g_section = "text";
  char b[8];
  CHECK(!copyUtf8(b, sizeof(b), "P\xC3\xA9rez Mendoza"));
  CHECK_STR(b, "P\xC3\xA9rez ");
  char c[4];
  copyUtf8(c, sizeof(c), "ab\xC3\xA9");  // "abé" needs 5 bytes, must not split
  CHECK_STR(c, "ab");
  char tiny[3];
  copyUtf8(tiny, sizeof(tiny), "\xE2\x82\xAC");  // 3-byte euro sign, does not fit
  CHECK_STR(tiny, "");
  copyUtf8(b, sizeof(b), "a\tb\nc");
  CHECK_STR(b, "a b c");
  copyUtf8(b, sizeof(b), "x\xC3");  // truncated multibyte sequence in source
  CHECK_STR(b, "x");
  CHECK_EQ(utf8Length("H\xC3\xBClkenberg"), 10u);
  char g[40];
  shortenGrandPrix(g, sizeof(g), "Mexico City Grand Prix");
  CHECK_STR(g, "Mexico City GP");
  shortenGrandPrix(g, sizeof(g), "Indianapolis 500");
  CHECK_STR(g, "Indianapolis 500");
  char code[4];
  fallbackDriverCode(code, "de Vries");
  CHECK_STR(code, "DEV");
  fallbackDriverCode(code, "O");
  CHECK_STR(code, "O??");
  CHECK_EQ(pointsToTenths("412"), 4120);
  CHECK_EQ(pointsToTenths("12.5"), 125);
  CHECK_EQ(pointsToTenths("0"), 0);
  CHECK_EQ(pointsToTenths(""), -1);
  CHECK_EQ(pointsToTenths("1a"), -1);
}

// ------------------------------------------------------------------ HTTP
static void testHttp() {
  g_section = "http";
  std::mt19937 rng(1234);
  // content-length
  {
    std::string body = "{\"a\":1}";
    FakeSource src("HTTP/1.1 200 OK\r\nContent-Length: 7\r\nX-Junk:  y \r\n\r\n" + body, 3);
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_OK);
    CHECK_EQ(h.status, 200);
    CHECK_EQ(h.contentLength, 7);
    BodyReader br(in, h, 100);
    char buf[32] = {0};
    CHECK_EQ(br.readBytes(buf, sizeof(buf)), 7u);
    CHECK_STR(buf, body.c_str());
    CHECK_EQ(br.finish(), FE_OK);
  }
  // chunked with extensions & trailers, lower-case header names, LF-only lines
  {
    std::string wire = "HTTP/1.1 200 OK\ntransfer-encoding: Chunked\n\n4;ext=1\r\nWiki\r\n5\r\npedia\r\n0\r\nX-T: 1\r\n\r\n";
    FakeSource src(wire, 2);
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_OK);
    CHECK(h.chunked);
    BodyReader br(in, h, 100);
    char buf[32] = {0};
    CHECK_EQ(br.readBytes(buf, sizeof(buf)), 9u);
    CHECK_STR(buf, "Wikipedia");
    CHECK_EQ(br.finish(), FE_OK);
  }
  // interim 100 Continue is skipped
  {
    FakeSource src("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_OK);
    CHECK_EQ(h.status, 204);
  }
  // 429 with Retry-After
  {
    FakeSource src("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 37\r\nContent-Length: 0\r\n\r\n");
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_OK);
    CHECK_EQ(classifyHttpStatus(h.status), FE_HTTP_429);
    CHECK_EQ(h.retryAfterSec, 37);
  }
  CHECK_EQ(classifyHttpStatus(503), FE_HTTP_5XX);
  CHECK_EQ(classifyHttpStatus(404), FE_HTTP_404);
  CHECK_EQ(classifyHttpStatus(403), FE_HTTP_4XX);
  CHECK_EQ(classifyHttpStatus(301), FE_HTTP_OTHER);
  CHECK_EQ(classifyHttpStatus(200), FE_OK);
  // malformed status lines / headers
  const char* bad[] = {"HTTP/2 200 OK\r\n\r\n", "HTTX/1.1 200 OK\r\n\r\n", "HTTP/1.1 2x0 OK\r\n\r\n",
                       "HTTP/1.1 200 OK\r\nNoColonHere\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 12a\r\n\r\n",
                       "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n",
                       "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n",
                       "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n"};
  for (const char* w : bad) {
    FakeSource src(w);
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_BAD_HEADER);
  }
  // header line longer than the line buffer
  {
    std::string w = "HTTP/1.1 200 OK\r\nX-Long: " + std::string(2000, 'a') + "\r\n\r\n";
    FakeSource src(w);
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_BAD_HEADER);
  }
  // too many headers
  {
    std::string w = "HTTP/1.1 200 OK\r\n";
    for (int i = 0; i < 80; i++) w += "X-H" + std::to_string(i) + ": v\r\n";
    w += "\r\n";
    FakeSource src(w);
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_BAD_HEADER);
  }
  // connection closed before any response / during headers
  {
    FakeSource src("");
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_CONNECT);
    FakeSource src2("HTTP/1.1 200 OK\r\nContent-Le");
    BufferedSource in2(src2, 1000, 10000);
    CHECK_EQ(readHttpHead(in2, h), FE_TRUNCATED);
  }
  // timeout while waiting for headers
  {
    FakeSource src("HTTP/1.1 200 OK\r\n");
    src.stallAt_ = 10;
    BufferedSource in(src, 500, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_TIMEOUT);
  }
  // content-length larger than the limit => rejected before reading
  {
    FakeSource src("HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\nxxxx");
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_OK);
    BodyReader br(in, h, 65536);
    CHECK_EQ(br.error(), FE_TOO_LARGE);
    CHECK_EQ(br.read(), -1);
  }
  // chunked body exceeding the limit
  {
    std::string body(5000, 'x');
    std::string wire = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + chunkify(body, rng);
    FakeSource src(wire);
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    readHttpHead(in, h);
    BodyReader br(in, h, 4096);
    CHECK_EQ(br.finish(), FE_TOO_LARGE);
    CHECK(br.bytesDelivered() <= 4096);
  }
  // read-until-close body (no length, not chunked)
  {
    FakeSource src("HTTP/1.0 200 OK\r\n\r\nhello");
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    readHttpHead(in, h);
    BodyReader br(in, h, 100);
    char buf[16] = {0};
    CHECK_EQ(br.readBytes(buf, 16), 5u);
    CHECK_EQ(br.finish(), FE_OK);
  }
  // truncated content-length body
  {
    FakeSource src("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nshort");
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    readHttpHead(in, h);
    BodyReader br(in, h, 100);
    CHECK_EQ(br.finish(), FE_TRUNCATED);
  }
  // malformed / truncated chunked encodings
  const char* badChunks[] = {"zz\r\nabc\r\n0\r\n\r\n", "3\r\nabcX0\r\n\r\n", "3\r\nab", "5\r\nabcde\r\n",
                             "3\r\nabc\r\n0\r\n", "\r\n", "123456789\r\n"};
  const FetchError expect[] = {FE_BAD_CHUNK, FE_BAD_CHUNK, FE_TRUNCATED, FE_TRUNCATED, FE_TRUNCATED, FE_BAD_CHUNK, FE_BAD_CHUNK};
  for (size_t i = 0; i < sizeof(badChunks) / sizeof(badChunks[0]); i++) {
    std::string wire = std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") + badChunks[i];
    FakeSource src(wire);
    BufferedSource in(src, 1000, 10000);
    HttpHead h;
    CHECK_EQ(readHttpHead(in, h), FE_OK);
    BodyReader br(in, h, 1000);
    FetchError fe = br.finish();
    if (fe != expect[i]) fprintf(stderr, "  chunk case %zu -> %s\n", i, fetchErrorName(fe));
    CHECK_EQ(fe, expect[i]);
  }
  // stall in the middle of the body => timeout
  {
    std::string wire = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n" + std::string(100, 'a');
    FakeSource src(wire);
    src.stallAt_ = 80;
    BufferedSource in(src, 300, 10000);
    HttpHead h;
    readHttpHead(in, h);
    BodyReader br(in, h, 1000);
    CHECK_EQ(br.finish(), FE_TIMEOUT);
  }
  // request builder
  char req[256];
  int n = buildGetRequest(req, sizeof(req), "api.jolpi.ca", "/ergast/f1/2026/races.json?limit=100", "GridlineF1/1.0.0");
  CHECK(n > 0);
  CHECK(strstr(req, "GET /ergast/f1/2026/races.json?limit=100 HTTP/1.1\r\nHost: api.jolpi.ca\r\n") == req);
  CHECK(strstr(req, "User-Agent: GridlineF1/1.0.0\r\n") != nullptr);
  CHECK(strstr(req, "Connection: close\r\n\r\n") != nullptr);
  CHECK_EQ(buildGetRequest(req, 20, "h", "/p", "ua"), -1);
}

// ------------------------------------------------------------------ parsers (real fixtures)
static void testCalendarFixture() {
  g_section = "calendar";
  std::string js = fixture("2023.json");
  Calendar cal;
  ParseStats st;
  StringJsonSource src(js.data(), js.size());
  CHECK_EQ(parseCalendar(src, 2023, cal, &st), FE_OK);
  printf("  calendar 2023: %d races, peak JSON %zu bytes (host, 64-bit)\n", cal.count, st.peakJsonBytes);
  CHECK_EQ(cal.count, 22);
  CHECK_EQ(cal.season, 2023);
  const Race& monaco = cal.races[5];
  CHECK_EQ(monaco.round, 6);
  CHECK_STR(monaco.name, "Monaco Grand Prix");
  CHECK_STR(monaco.circuit, "Circuit de Monaco");
  CHECK_STR(monaco.locality, "Monte-Carlo");
  CHECK_STR(monaco.country, "Monaco");
  CHECK(monaco.hasTime);
  CHECK_EQ(monaco.raceUtc, makeUtc(2023, 5, 28, 13, 0, 0));
  CHECK_EQ(monaco.sessionCount, 4);
  CHECK_EQ(monaco.sessions[0].kind, SK_FP1);
  CHECK_EQ(monaco.sessions[3].kind, SK_QUALI);
  CHECK(!monaco.hasSprint);
  int sprints = 0;
  for (int i = 0; i < cal.count; i++) {
    if (cal.races[i].hasSprint) sprints++;
    CHECK_EQ(cal.races[i].round, i + 1);
    for (int k = 1; k < cal.races[i].sessionCount; k++)
      CHECK(cal.races[i].sessions[k - 1].startUtc <= cal.races[i].sessions[k].startUtc);
  }
  CHECK_EQ(sprints, 6);
  // Baku 2023 (round 4) was a sprint weekend. This fixture is an original
  // Ergast response, which lists the shootout as "SecondPractice".
  const Race& baku = cal.races[3];
  CHECK(baku.hasSprint);
  CHECK_EQ(baku.sessionCount, 4);
  CHECK_EQ(baku.sessions[0].kind, SK_FP1);
  CHECK_EQ(baku.sessions[1].kind, SK_QUALI);
  CHECK_EQ(baku.sessions[3].kind, SK_SPRINT);
  // Current Jolpica naming ("SprintQualifying", 2024+)
  const char* sq =
      "{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":\"2026\",\"round\":\"2\",\"raceName\":\"Chinese Grand Prix\","
      "\"date\":\"2026-03-15\",\"time\":\"07:00:00Z\",\"FirstPractice\":{\"date\":\"2026-03-13\",\"time\":\"03:30:00Z\"},"
      "\"SprintQualifying\":{\"date\":\"2026-03-13\",\"time\":\"07:30:00Z\"},\"Sprint\":{\"date\":\"2026-03-14\",\"time\":\"03:00:00Z\"},"
      "\"Qualifying\":{\"date\":\"2026-03-14\",\"time\":\"07:00:00Z\"}}]}}}";
  Calendar c26;
  StringJsonSource ssq(sq, strlen(sq));
  CHECK_EQ(parseCalendar(ssq, 2026, c26, &st), FE_OK);
  CHECK_EQ(c26.races[0].sessionCount, 4);
  CHECK_EQ(c26.races[0].sessions[1].kind, SK_SPRINT_QUALI);
  CHECK_EQ(c26.races[0].sessions[2].kind, SK_SPRINT);
  CHECK_EQ(c26.races[0].sessions[3].kind, SK_QUALI);
  CHECK(c26.races[0].hasSprint);
  // Israel local time of the Monaco start: 13:00Z => 16:00 IDT
  CivilTime l = toIsrael(monaco.raceUtc);
  CHECK_EQ(l.hour, 16);
  // wrong season is rejected
  StringJsonSource src2(js.data(), js.size());
  CHECK_EQ(parseCalendar(src2, 2024, cal, &st), FE_SCHEMA);
  // empty race table (round 99 does not exist)
  std::string empty = fixture("2023_99_races.json");
  StringJsonSource src3(empty.data(), empty.size());
  CHECK_EQ(parseCalendar(src3, 2023, cal, &st), FE_EMPTY);
}

static void testStandingsFixtures() {
  g_section = "standings";
  std::string js = fixture("2023_22_driverstandings.json");
  DriverStandings ds;
  ParseStats st;
  StringJsonSource s1(js.data(), js.size());
  CHECK_EQ(parseDriverStandings(s1, 2023, ds, &st), FE_OK);
  printf("  drivers 2023: %d rows (API total in fixture limited to 30), peak %zu bytes\n", ds.count, st.peakJsonBytes);
  CHECK(ds.count >= 20);
  CHECK_EQ(ds.round, 22);
  CHECK_EQ(ds.rows[0].pos, 1);
  CHECK_STR(ds.rows[0].code, "VER");
  CHECK_STR(ds.rows[0].driverId, "max_verstappen");
  CHECK_STR(ds.rows[0].points, "575");
  CHECK_STR(ds.rows[0].teamId, "red_bull");
  CHECK_EQ(ds.rows[0].wins, 19);
  for (int i = 1; i < ds.count; i++) {
    if (ds.rows[i].pos && ds.rows[i - 1].pos) CHECK(ds.rows[i].pos > ds.rows[i - 1].pos);
    CHECK(pointsToTenths(ds.rows[i].points) <= pointsToTenths(ds.rows[i - 1].points));
  }
  StringJsonSource s1b(js.data(), js.size());
  CHECK_EQ(parseDriverStandings(s1b, 2022, ds, &st), FE_SCHEMA);

  std::string jc = fixture("2023_22_constructorstandings.json");
  ConstructorStandings cs;
  StringJsonSource s2(jc.data(), jc.size());
  CHECK_EQ(parseConstructorStandings(s2, 2023, cs, &st), FE_OK);
  CHECK_EQ(cs.count, 10);
  CHECK_STR(cs.rows[0].teamId, "red_bull");
  CHECK_STR(cs.rows[0].points, "860");
  CHECK_STR(cs.rows[9].teamId, "haas");
}

static void testResultsFixture() {
  g_section = "results";
  std::string js = fixture("2023_9_results.json");
  RaceResults rr;
  ParseStats st;
  StringJsonSource s(js.data(), js.size());
  CHECK_EQ(parseRaceResults(s, 2023, rr, &st), FE_OK);
  printf("  results 2023 R9: %d rows, peak %zu bytes\n", rr.count, st.peakJsonBytes);
  CHECK_EQ(rr.count, 20);
  CHECK_EQ(rr.round, 9);
  CHECK_STR(rr.name, "Austrian Grand Prix");
  CHECK_STR(rr.rows[0].code, "VER");
  CHECK_STR(rr.rows[0].time, "1:25:33.607");
  CHECK_STR(rr.rows[0].points, "26");
  CHECK_EQ(rr.rows[0].finishClass, FC_CLASSIFIED);
  int classified = 0, notClassified = 0;
  for (int i = 0; i < rr.count; i++) {
    if (rr.rows[i].finishClass == FC_CLASSIFIED) classified++; else notClassified++;
    CHECK(rr.rows[i].status[0] != 0);
  }
  printf("  results 2023 R9: classified %d, not classified %d\n", classified, notClassified);
  CHECK(classified >= 15);
  std::string empty = fixture("2023_99_results.json");
  StringJsonSource s2(empty.data(), empty.size());
  CHECK_EQ(parseRaceResults(s2, 2023, rr, &st), FE_EMPTY);
}

// Hand-written responses in the current (2025+) Jolpica shape: null
// positions, unclassified "R"/"W"/"D" entries, decimal points, missing
// optional fields.
static void testResultsEdgeCases() {
  g_section = "results_edge";
  const char* js =
      "{\"MRData\":{\"RaceTable\":{\"season\":\"2026\",\"round\":\"5\",\"Races\":[{\"season\":\"2026\",\"round\":\"5\","
      "\"raceName\":\"Test Grand Prix\",\"Circuit\":{\"circuitName\":\"Test Ring\",\"Location\":{\"country\":\"Nowhere\"}},"
      "\"date\":\"2026-05-03\",\"Results\":["
      "{\"position\":\"1\",\"positionText\":\"1\",\"points\":\"25\",\"status\":\"Finished\",\"Time\":{\"time\":\"1:30:00.000\"},"
      "\"Driver\":{\"driverId\":\"a\",\"code\":\"AAA\",\"familyName\":\"Alpha\"},\"Constructor\":{\"constructorId\":\"ferrari\"}},"
      "{\"position\":\"2\",\"positionText\":\"2\",\"points\":\"18.5\",\"status\":\"Lapped\","
      "\"Driver\":{\"driverId\":\"b\",\"familyName\":\"P\xC3\xA9rez\"},\"Constructor\":{\"constructorId\":\"cadillac\"}},"
      "{\"position\":\"3\",\"positionText\":\"R\",\"points\":\"0\",\"status\":\"Retired\",\"laps\":\"12\","
      "\"Driver\":{\"driverId\":\"c\",\"code\":\"CCC\",\"familyName\":\"Gamma\"},\"Constructor\":{\"constructorId\":\"x\"}},"
      "{\"position\":null,\"positionText\":\"W\",\"status\":\"Did not start\","
      "\"Driver\":{\"driverId\":\"d\",\"code\":\"DDD\",\"familyName\":\"Delta\"},\"Constructor\":{}},"
      "{\"position\":\"5\",\"positionText\":\"D\",\"points\":\"0\",\"status\":\"Disqualified\","
      "\"Driver\":{\"driverId\":\"e\",\"code\":\"EEE\",\"familyName\":\"Echo\"},\"Constructor\":{\"constructorId\":\"haas\"}},"
      "{\"position\":\"6\",\"positionText\":\"6\",\"points\":1,\"status\":\"Finished\",\"Time\":{\"time\":\"\"},"
      "\"Driver\":{\"driverId\":\"f\",\"code\":\"FFF\",\"familyName\":\"Fox\"},\"Constructor\":{\"constructorId\":\"rb\"}}"
      "]}]}}}";
  RaceResults rr;
  ParseStats st;
  StringJsonSource s(js, strlen(js));
  CHECK_EQ(parseRaceResults(s, 2026, rr, &st), FE_OK);
  CHECK_EQ(rr.count, 6);
  CHECK(!rr.hasTime);
  CHECK_STR(rr.rows[1].code, "PRE");  // fallback from "Pérez": P,R,E (accents skipped)
  CHECK_STR(rr.rows[1].points, "18.5");
  CHECK_EQ(rr.rows[2].finishClass, FC_RETIRED);
  CHECK_STR(finishClassShort(rr.rows[2].finishClass), "DNF");
  CHECK_EQ(rr.rows[3].pos, 0);
  CHECK_EQ(rr.rows[3].finishClass, FC_WITHDRAWN);
  CHECK_STR(rr.rows[3].points, "");
  CHECK_STR(rr.rows[3].teamId, "");
  CHECK_EQ(rr.rows[4].finishClass, FC_DISQUALIFIED);
  CHECK_STR(rr.rows[5].points, "1");
  CHECK_STR(rr.rows[5].time, "");
  // Results present but empty => not yet published
  const char* pending =
      "{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":\"2026\",\"round\":\"6\",\"raceName\":\"X\","
      "\"date\":\"2026-05-10\",\"Results\":[]}]}}}";
  StringJsonSource s2(pending, strlen(pending));
  CHECK_EQ(parseRaceResults(s2, 2026, rr, &st), FE_EMPTY);
  // standings with null position and "-" text, numeric points
  const char* stand =
      "{\"MRData\":{\"StandingsTable\":{\"season\":\"2026\",\"StandingsLists\":[{\"season\":\"2026\",\"round\":\"3\","
      "\"DriverStandings\":[{\"position\":\"1\",\"positionText\":\"1\",\"points\":\"43\",\"wins\":\"2\","
      "\"Driver\":{\"driverId\":\"x\",\"code\":\"XXX\",\"familyName\":\"X\"},\"Constructors\":[{\"constructorId\":\"a\",\"name\":\"A\"},"
      "{\"constructorId\":\"b\",\"name\":\"B\"}]},"
      "{\"position\":null,\"positionText\":\"-\",\"points\":0,\"wins\":\"0\","
      "\"Driver\":{\"driverId\":\"y\",\"familyName\":\"Young\"},\"Constructors\":[]}]}]}}}";
  DriverStandings ds;
  StringJsonSource s3(stand, strlen(stand));
  CHECK_EQ(parseDriverStandings(s3, 2026, ds, &st), FE_OK);
  CHECK_EQ(ds.count, 2);
  CHECK_STR(ds.rows[0].teamId, "b");  // most recent constructor
  CHECK_EQ(ds.rows[1].pos, 0);
  CHECK_STR(ds.rows[1].posText, "-");
  CHECK_STR(ds.rows[1].points, "0");
  CHECK_STR(ds.rows[1].code, "YOU");
  // empty standings list (pre-season)
  const char* none = "{\"MRData\":{\"StandingsTable\":{\"season\":\"2027\",\"StandingsLists\":[]}}}";
  StringJsonSource s4(none, strlen(none));
  CHECK_EQ(parseDriverStandings(s4, 2027, ds, &st), FE_EMPTY);
}

static void testMalformed() {
  g_section = "malformed";
  Calendar cal;
  DriverStandings ds;
  RaceResults rr;
  ParseStats st;
  struct Case { const char* body; FetchError expect; };
  const Case cases[] = {
      {"", FE_JSON_INVALID},
      {"<html><body>502 Bad Gateway</body></html>", FE_SCHEMA},  // rejected either way
      {"{\"MRData\":", FE_JSON_INVALID},
      {"[1,2,3]", FE_SCHEMA},
      {"{\"foo\":1}", FE_SCHEMA},
      {"{\"MRData\":{}}", FE_SCHEMA},
      {"{\"MRData\":{\"RaceTable\":{\"Races\":{}}}}", FE_SCHEMA},
      {"{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":\"2026\",\"round\":\"1\"}]}}}", FE_SCHEMA},
      {"{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":\"2026\",\"round\":\"1\",\"raceName\":\"A\",\"date\":\"bad\"}]}}}", FE_SCHEMA},
      {"{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":\"2026\",\"round\":\"1\",\"raceName\":\"A\",\"date\":\"2026-03-01\"},"
       "{\"season\":\"2026\",\"round\":\"1\",\"raceName\":\"B\",\"date\":\"2026-03-08\"}]}}}", FE_SCHEMA},
  };
  for (const Case& c : cases) {
    StringJsonSource s(c.body, strlen(c.body));
    FetchError e = parseCalendar(s, 2026, cal, &st);
    if (e != c.expect) fprintf(stderr, "  case '%s' -> %s (%s)\n", c.body, fetchErrorName(e), st.detail);
    CHECK_EQ(e, c.expect);
  }
  // valid calendar race with missing optional fields: no time, no circuit
  const char* minimal =
      "{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":\"2026\",\"round\":\"1\",\"raceName\":\"A GP\",\"date\":\"2026-03-08\"}]}}}";
  StringJsonSource sm(minimal, strlen(minimal));
  CHECK_EQ(parseCalendar(sm, 2026, cal, &st), FE_OK);
  CHECK_EQ(cal.count, 1);
  CHECK(!cal.races[0].hasTime);
  CHECK_STR(cal.races[0].circuit, "");
  // deeply nested garbage
  std::string deep(200, '[');
  StringJsonSource sd(deep.data(), deep.size());
  CHECK_EQ(parseDriverStandings(sd, 2026, ds, &st), FE_JSON_DEPTH);
  // unexpectedly large response: 400 races must not exhaust memory
  std::string big = "{\"MRData\":{\"RaceTable\":{\"Races\":[";
  for (int i = 0; i < 400; i++) {
    if (i) big += ",";
    big += "{\"season\":\"2026\",\"round\":\"" + std::to_string(i % 99 + 1) +
           "\",\"raceName\":\"Some Very Long Grand Prix Name Number " + std::to_string(i) +
           "\",\"Circuit\":{\"circuitName\":\"Circuit name that is quite long indeed\",\"Location\":{\"locality\":\"Town\","
           "\"country\":\"Country\"}},\"date\":\"2026-03-08\",\"time\":\"05:00:00Z\",\"FirstPractice\":{\"date\":\"2026-03-06\","
           "\"time\":\"01:30:00Z\"},\"Qualifying\":{\"date\":\"2026-03-07\",\"time\":\"05:00:00Z\"}}";
  }
  big += "]}}}";
  StringJsonSource sb(big.data(), big.size());
  FetchError be = parseCalendar(sb, 2026, cal, &st);
  printf("  400-race response (%zu bytes) -> %s, peak %zu\n", big.size(), fetchErrorName(be), st.peakJsonBytes);
  CHECK(be == FE_JSON_NOMEM);
  CHECK(st.peakJsonBytes <= kJsonBudgetBytes);
  // fields of the wrong type
  const char* wrongTypes =
      "{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":2026,\"round\":3,\"raceName\":\"Num GP\",\"date\":\"2026-03-08\","
      "\"time\":12345,\"Circuit\":\"oops\"}]}}}";
  StringJsonSource sw(wrongTypes, strlen(wrongTypes));
  CHECK_EQ(parseCalendar(sw, 2026, cal, &st), FE_OK);  // numeric season/round accepted, bad time ignored
  CHECK(!cal.races[0].hasTime);
  // results: driver without id => skipped; all skipped => schema error
  const char* noDrivers =
      "{\"MRData\":{\"RaceTable\":{\"Races\":[{\"season\":\"2026\",\"round\":\"2\",\"raceName\":\"X\",\"date\":\"2026-03-15\","
      "\"Results\":[{\"position\":\"1\",\"Driver\":{}}]}]}}}";
  StringJsonSource sn(noDrivers, strlen(noDrivers));
  CHECK_EQ(parseRaceResults(sn, 2026, rr, &st), FE_SCHEMA);
}

// Feed every prefix of every fixture: a truncated document must never be
// accepted, and nothing may crash (run under ASan/UBSan).
static void testTruncationSweep() {
  g_section = "truncation";
  const char* files[] = {"2023.json", "2023_22_driverstandings.json", "2023_22_constructorstandings.json",
                         "2023_9_results.json"};
  int accepted = 0, runs = 0;
  for (const char* f : files) {
    std::string js = fixture(f);
    for (size_t len = 0; len < js.size(); len += (len < 400 ? 1 : 37)) {
      StringJsonSource s(js.data(), len);
      FetchError e;
      if (strstr(f, "driver")) { DriverStandings o; e = parseDriverStandings(s, 2023, o); }
      else if (strstr(f, "constructor")) { ConstructorStandings o; e = parseConstructorStandings(s, 2023, o); }
      else if (strstr(f, "results")) { RaceResults o; e = parseRaceResults(s, 2023, o); }
      else { Calendar o; e = parseCalendar(s, 2023, o); }
      runs++;
      if (e == FE_OK) { accepted++; fprintf(stderr, "  accepted truncated %s at %zu\n", f, len); }
    }
  }
  printf("  truncation sweep: %d truncated documents, %d accepted\n", runs, accepted);
  CHECK_EQ(accepted, 0);
}

// Random byte mutations of the fixtures (crash/sanitizer test).
static void testMutationFuzz() {
  g_section = "fuzz";
  std::mt19937 rng(42);
  const char* files[] = {"2023.json", "2023_22_driverstandings.json", "2023_22_constructorstandings.json",
                         "2023_9_results.json"};
  int ok = 0, total = 0;
  for (int it = 0; it < 4000; it++) {
    std::string js = fixture(files[it % 4]);
    int muts = 1 + rng() % 8;
    for (int m = 0; m < muts; m++) {
      size_t p = rng() % js.size();
      switch (rng() % 4) {
        case 0: js[p] = (char)(rng() & 0xFF); break;
        case 1: js.erase(p, 1 + rng() % 20); break;
        case 2: js.insert(p, std::string(1 + rng() % 4, "{}[]\",:0\\"[rng() % 9])); break;
        case 3: js[p] = "{}[]\",:"[rng() % 7]; break;
      }
      if (js.empty()) js = "{";
    }
    StringJsonSource s(js.data(), js.size());
    FetchError e;
    switch (it % 4) {
      case 0: { Calendar o; e = parseCalendar(s, 2023, o); if (e == FE_OK) { CHECK(o.count > 0 && o.count <= kMaxRaces); } break; }
      case 1: { DriverStandings o; e = parseDriverStandings(s, 2023, o); if (e == FE_OK) { CHECK(o.count > 0 && o.count <= kMaxDrivers); } break; }
      case 2: { ConstructorStandings o; e = parseConstructorStandings(s, 2023, o); if (e == FE_OK) { CHECK(o.count > 0); } break; }
      default: { RaceResults o; e = parseRaceResults(s, 2023, o); if (e == FE_OK) { CHECK(o.count > 0 && o.count <= kMaxResults); } break; }
    }
    total++;
    if (e == FE_OK) ok++;
  }
  printf("  mutation fuzz: %d cases, %d still parsed as valid (structurally intact mutations)\n", total, ok);
  CHECK(total == 4000);
}

static void testPipeline() {
  g_section = "pipeline";
  std::mt19937 rng(7);
  std::string js = fixture("2023.json");
  auto calParse = [](JsonSource& s, Calendar& o) { return parseCalendar(s, 2023, o); };
  for (int rep = 0; rep < 30; rep++) {
    Calendar cal;
    bool chunked = rep % 2 == 0;
    size_t piece = 1 + rng() % 300;
    FetchOutcome r = runPipeline(httpResponse(js, chunked, rng), piece, cal, calParse);
    CHECK_EQ(r.err, FE_OK);
    CHECK_EQ(cal.count, 22);
  }
  // HTTP error with JSON body is not parsed
  {
    Calendar cal;
    FetchOutcome r = runPipeline(httpResponse("{\"detail\":\"Request was throttled.\"}", false, rng, 429), 50, cal, calParse);
    CHECK_EQ(r.err, FE_HTTP_429);
  }
  // body truncated mid-way (connection closed)
  {
    Calendar cal;
    std::string wire = httpResponse(js, false, rng);
    wire.resize(wire.size() - 500);
    FetchOutcome r = runPipeline(wire, 128, cal, calParse);
    CHECK_EQ(r.err, FE_TRUNCATED);
  }
  // chunked stream cut inside a chunk
  {
    Calendar cal;
    std::string wire = httpResponse(js, true, rng);
    wire.resize(wire.size() / 2);
    FetchOutcome r = runPipeline(wire, 128, cal, calParse);
    CHECK_EQ(r.err, FE_TRUNCATED);
  }
  // response larger than the byte limit
  {
    Calendar cal;
    FetchOutcome r = runPipeline(httpResponse(js, true, rng), 128, cal, calParse, 8000);
    CHECK_EQ(r.err, FE_TOO_LARGE);
  }
  // results through chunked pipeline
  {
    RaceResults rr;
    std::string jr = fixture("2023_9_results.json");
    auto resParse = [](JsonSource& s, RaceResults& o) { return parseRaceResults(s, 2023, o); };
    FetchOutcome r = runPipeline(httpResponse(jr, true, rng), 17, rr, resParse);
    CHECK_EQ(r.err, FE_OK);
    CHECK_EQ(rr.count, 20);
  }
}

static void testCache() {
  g_section = "cache";
  std::string js = fixture("2023_22_driverstandings.json");
  DriverStandings ds, back;
  StringJsonSource s(js.data(), js.size());
  CHECK_EQ(parseDriverStandings(s, 2023, ds), FE_OK);
  std::vector<uint8_t> blob(sizeof(CacheHeader) + sizeof(ds));
  size_t n = encodeCache(DS_DRIVERS, &ds, sizeof(ds), 1792848180LL, blob.data(), blob.size());
  CHECK_EQ(n, blob.size());
  int64_t when = 0;
  CHECK(decodeCache(blob.data(), n, DS_DRIVERS, &back, sizeof(back), when));
  CHECK_EQ(when, 1792848180LL);
  CHECK(memcmp(&ds, &back, sizeof(ds)) == 0);
  CHECK(!decodeCache(blob.data(), n, DS_TEAMS, &back, sizeof(back), when));       // wrong dataset
  CHECK(!decodeCache(blob.data(), n - 1, DS_DRIVERS, &back, sizeof(back), when));   // truncated file
  CHECK(!decodeCache(blob.data(), 10, DS_DRIVERS, &back, sizeof(back), when));
  for (size_t pos : {0ul, 5ul, 30ul, 100ul, (unsigned long)n - 1}) {
    std::vector<uint8_t> c = blob;
    c[pos] ^= 0x40;
    CHECK(!decodeCache(c.data(), n, DS_DRIVERS, &back, sizeof(back), when));  // any bit flip detected
  }
  CHECK_EQ(encodeCache(DS_DRIVERS, &ds, sizeof(ds), 0, blob.data(), 100), 0u);
  CHECK_EQ(crc32("123456789", 9), 0xCBF43926u);  // standard check value
}

static void testBackoff() {
  g_section = "backoff";
  uint32_t prev = 0;
  for (uint8_t f = 1; f <= 10; f++) {
    uint32_t d = retryDelaySeconds(f, FE_TIMEOUT, -1, 0.5f);
    CHECK(d >= prev);
    CHECK(d <= 1800);
    prev = d;
  }
  CHECK_EQ(retryDelaySeconds(1, FE_TIMEOUT, -1, 0.5f), 30u);
  CHECK(retryDelaySeconds(1, FE_TIMEOUT, -1, 0.0f) >= 25u);
  CHECK(retryDelaySeconds(1, FE_TIMEOUT, -1, 0.999f) <= 35u);
  CHECK(retryDelaySeconds(1, FE_HTTP_429, 600, 0.0f) >= 600u);
  CHECK(retryDelaySeconds(1, FE_HTTP_429, -1, 0.5f) >= 120u);
  CHECK(retryDelaySeconds(1, FE_SCHEMA, -1, 0.5f) >= 255u);
  CHECK_EQ(retryDelaySeconds(1, FE_EMPTY, -1, 0.5f), 3600u);
  CHECK(retryDelaySeconds(200, FE_TLS, -1, 0.99f) <= 2100u);
}

static Calendar makeCal(uint16_t season, int n, utc_t firstRace, bool withTime = true) {
  Calendar c;
  memset(&c, 0, sizeof(c));
  c.season = season;
  c.count = (uint8_t)n;
  for (int i = 0; i < n; i++) {
    Race& r = c.races[i];
    r.season = season;
    r.round = (uint8_t)(i + 1);
    r.raceUtc = firstRace + (utc_t)i * 14 * 86400;
    r.hasTime = withTime;
    snprintf(r.name, sizeof(r.name), "Race %d Grand Prix", i + 1);
    r.sessionCount = 2;
    r.sessions[0] = {r.raceUtc - 2 * 86400, SK_FP1, 1, {0}};
    r.sessions[1] = {r.raceUtc - 86400, SK_QUALI, 1, {0}};
  }
  return c;
}

static void testRaceLogic() {
  g_section = "race_logic";
  utc_t first = makeUtc(2026, 3, 8, 4, 0, 0);
  Calendar cal = makeCal(2026, 10, first);
  // before the season
  NextRace nr = findNextRace(first - 30 * 86400, &cal, nullptr);
  CHECK(nr.race == &cal.races[0]);
  CHECK(!nr.inProgress);
  CHECK(nr.nextSession == &cal.races[0].sessions[0]);
  // during qualifying day
  nr = findNextRace(first - 86400 + 600, &cal, nullptr);
  CHECK(nr.nextSession == &cal.races[0].sessions[1]);
  // race in progress
  nr = findNextRace(first + 3600, &cal, nullptr);
  CHECK(nr.race == &cal.races[0]);
  CHECK(nr.inProgress);
  CHECK(nr.nextSession == nullptr);
  // 3h after start the next race takes over
  nr = findNextRace(first + 3 * 3600, &cal, nullptr);
  CHECK(nr.race == &cal.races[1]);
  CHECK(latestCompletedRace(first + 3 * 3600, &cal) == &cal.races[0]);
  CHECK(latestCompletedRace(first, &cal) == nullptr);
  CHECK_EQ(nextRaceIndex(first + 3 * 3600, &cal), 1);
  // season over, no next calendar
  utc_t after = cal.races[9].raceUtc + 4 * 3600;
  nr = findNextRace(after, &cal, nullptr);
  CHECK(nr.race == nullptr);
  CHECK(seasonComplete(after, &cal));
  CHECK(!seasonComplete(first, &cal));
  CHECK_EQ(nextRaceIndex(after, &cal), -1);
  // season over, next season published
  Calendar next = makeCal(2027, 5, makeUtc(2027, 3, 14, 5, 0, 0));
  nr = findNextRace(after, &cal, &next);
  CHECK(nr.race == &next.races[0]);
  CHECK(nr.fromNextSeason);
  // race without a published start time stays "next" through race day
  Calendar nt = makeCal(2026, 2, makeUtc(2026, 6, 7, 0, 0, 0), false);
  nr = findNextRace(makeUtc(2026, 6, 7, 18, 0, 0), &nt, nullptr);
  CHECK(nr.race == &nt.races[0]);
  CHECK(!nr.inProgress);
  nr = findNextRace(makeUtc(2026, 6, 8, 7, 0, 0), &nt, nullptr);
  CHECK(nr.race == &nt.races[1]);
  // empty / missing calendars
  Calendar emptyCal;
  memset(&emptyCal, 0, sizeof(emptyCal));
  CHECK(findNextRace(first, &emptyCal, nullptr).race == nullptr);
  CHECK(findNextRace(first, nullptr, nullptr).race == nullptr);
  CHECK(!seasonComplete(first, &emptyCal));
}

static void testScheduler() {
  g_section = "scheduler";
  utc_t first = makeUtc(2026, 3, 8, 4, 0, 0);
  Calendar cal = makeCal(2026, 10, first);
  ScheduleState st;
  memset(&st, 0, sizeof(st));
  ScheduleContext ctx{first - 20 * 86400, 2026, nullptr, false};
  // nothing known: calendar first
  CHECK_EQ(pickDueDataset(ctx, st), DS_CALENDAR);
  // calendar fetched
  st.meta[DS_CALENDAR] = {ctx.now, ctx.now, ORIGIN_NETWORK, 0, 0, {0}};
  st.seasonOf[DS_CALENDAR] = 2026;
  ctx.calendar = &cal;
  CHECK_EQ(pickDueDataset(ctx, st), DS_DRIVERS);
  for (uint8_t ds : {DS_DRIVERS, DS_TEAMS, DS_RESULTS}) {
    st.meta[ds] = {ctx.now, ctx.now, ORIGIN_NETWORK, 0, 0, {0}};
    st.seasonOf[ds] = 2025;  // pre-season fallback data
  }
  CHECK_EQ(pickDueDataset(ctx, st), -1);
  CHECK_EQ(secondsUntilNextDue(ctx, st, 999999), 6u * 3600);
  // next-season calendar only when needed
  ctx.needNextSeasonCalendar = true;
  CHECK_EQ(pickDueDataset(ctx, st), DS_CALENDAR_NEXT);
  st.nextAttemptUtc[DS_CALENDAR_NEXT] = ctx.now + 6 * 3600;  // after EMPTY
  CHECK_EQ(pickDueDataset(ctx, st), -1);
  ctx.needNextSeasonCalendar = false;
  // network gate blocks everything
  st.networkRetryUtc = ctx.now + 600;
  ctx.now += 7 * 3600;
  st.networkRetryUtc = ctx.now + 600;
  CHECK_EQ(pickDueDataset(ctx, st), -1);
  st.networkRetryUtc = 0;
  CHECK_EQ(pickDueDataset(ctx, st), DS_CALENDAR);
  // restored from cache: no immediate refetch when younger than 10 minutes
  ScheduleState c2;
  memset(&c2, 0, sizeof(c2));
  utc_t now = first + 5 * 86400;
  for (uint8_t ds : {DS_CALENDAR, DS_DRIVERS, DS_TEAMS, DS_RESULTS}) {
    c2.meta[ds] = {now - 300, 0, ORIGIN_CACHE, 0, 0, {0}};
    c2.seasonOf[ds] = 2026;
    c2.roundOf[ds] = 1;
  }
  ScheduleContext ctx2{now, 2026, &cal, false};
  CHECK_EQ(pickDueDataset(ctx2, c2), -1);
  ctx2.now = now + 301;
  CHECK_EQ(pickDueDataset(ctx2, c2), DS_CALENDAR);
  // season rollover forces a calendar refresh even if fresh
  c2.meta[DS_CALENDAR].origin = ORIGIN_NETWORK;
  c2.meta[DS_CALENDAR].fetchedUtc = ctx2.now;
  ScheduleContext ctx3{ctx2.now, 2027, &cal, false};
  CHECK_EQ(pickDueDataset(ctx3, c2), DS_CALENDAR);
  // race weekend: awaiting results of round 2 => 10 minute interval
  ScheduleState c3;
  memset(&c3, 0, sizeof(c3));
  utc_t r2finish = raceFinishedAt(cal.races[1]);
  ScheduleContext ctx4{r2finish + 3600, 2026, &cal, false};
  for (uint8_t ds : {DS_DRIVERS, DS_TEAMS, DS_RESULTS}) { c3.seasonOf[ds] = 2026; c3.roundOf[ds] = 1; }
  CHECK_EQ(refreshIntervalSeconds(DS_RESULTS, ctx4, c3), 600u);
  c3.roundOf[DS_RESULTS] = 2;
  CHECK_EQ(refreshIntervalSeconds(DS_RESULTS, ctx4, c3), 3600u);
  CHECK_EQ(refreshIntervalSeconds(DS_DRIVERS, ctx4, c3), 600u);
  // far from any race
  ScheduleContext ctx5{cal.races[9].raceUtc + 40 * 86400, 2026, &cal, false};
  CHECK_EQ(refreshIntervalSeconds(DS_DRIVERS, ctx5, c3), 6u * 3600);
  CHECK_EQ(refreshIntervalSeconds(DS_CALENDAR, ctx5, c3), 6u * 3600);
  // retry gate per dataset
  ScheduleState c4;
  memset(&c4, 0, sizeof(c4));
  ScheduleContext ctx6{first, 2026, nullptr, false};
  c4.nextAttemptUtc[DS_CALENDAR] = first + 60;
  CHECK_EQ(pickDueDataset(ctx6, c4), DS_DRIVERS);
}

static void testCalibration() {
  g_section = "calibration";
  std::mt19937 rng(99);
  std::normal_distribution<float> noise(0.0f, 6.0f);
  // simulate a rotated & mirrored panel mapping
  struct Truth { float a, b, c, d, e, f; };
  const Truth truths[] = {{0.0, 0.088, -20, -0.066, 0, 262}, {0.087, 0.001, -18, 0.0005, 0.067, -14},
                          {-0.087, 0, 340, 0, -0.068, 255}};
  for (const Truth& t : truths) {
    const int32_t sx[] = {24, 296, 24, 296, 160};
    const int32_t sy[] = {24, 24, 216, 216, 120};
    int32_t rx[5], ry[5];
    for (int i = 0; i < 5; i++) {
      // invert the 2x2 to find raw values producing the screen point
      float det = t.a * t.e - t.b * t.d;
      float X = sx[i] - t.c, Y = sy[i] - t.f;
      rx[i] = (int32_t)((t.e * X - t.b * Y) / det + noise(rng));
      ry[i] = (int32_t)((-t.d * X + t.a * Y) / det + noise(rng));
    }
    TouchCal cal;
    float rms = 0;
    CHECK(fitTouchCal(rx, ry, sx, sy, 5, cal, &rms));
    CHECK(rms < 3.0f);
    int32_t x, y;
    applyTouchCal(cal, rx[4], ry[4], x, y);
    CHECK(abs(x - 160) <= 3 && abs(y - 120) <= 3);
  }
  // collinear points => rejected
  const int32_t lx[] = {100, 200, 300}, ly[] = {100, 200, 300};
  TouchCal bad;
  CHECK(!fitTouchCal(lx, ly, lx, ly, 3, bad, nullptr));
  // identity mapping is implausible for a 12-bit touch controller
  const int32_t ix[] = {0, 320, 0}, iy[] = {0, 0, 240};
  CHECK(!fitTouchCal(ix, iy, ix, iy, 3, bad, nullptr));
}

static void testRotation() {
  g_section = "rotation";
  for (uint8_t r = 0; r < 4; r++) {
    int W = rotWidth(r), H = rotHeight(r);
    CHECK_EQ(W * H, 320 * 240);
    for (int x = 0; x < W; x += 7)
      for (int y = 0; y < H; y += 7) {
        int32_t lx, ly, bx, by;
        rotToCanonical(r, x, y, lx, ly);
        CHECK(lx >= 0 && lx < kCanonW && ly >= 0 && ly < kCanonH);
        rotFromCanonical(r, lx, ly, bx, by);
        if (bx != x || by != y) { CHECK_EQ(bx, x); CHECK_EQ(by, y); }
      }
  }
  int32_t x, y;
  rotFromCanonical(1, 10, 20, x, y); CHECK_EQ(x, 10); CHECK_EQ(y, 20);
  rotFromCanonical(3, 0, 0, x, y); CHECK_EQ(x, 319); CHECK_EQ(y, 239);       // 180 degrees
  rotFromCanonical(0, 0, 0, x, y); CHECK_EQ(x, 239); CHECK_EQ(y, 0);         // 90 degrees
  rotFromCanonical(2, 0, 0, x, y); CHECK_EQ(x, 0); CHECK_EQ(y, 319);
  // rot0 and rot2 are 180 degrees apart, as are rot1 and rot3
  int32_t a, b, c, d;
  rotFromCanonical(0, 123, 45, a, b);
  rotFromCanonical(2, 123, 45, c, d);
  CHECK_EQ(a + c, 239); CHECK_EQ(b + d, 319);
  // Calibrating in portrait (rot 0): targets are converted to the canonical
  // frame, the fit maps raw -> canonical, touches map back to portrait.
  const float A = 0.0f, B = 0.088f, C0 = -20, D = -0.066f, E = 0, F = 262;  // a raw->canonical truth
  const int32_t px[5] = {28, 212, 212, 28, 120}, py[5] = {28, 28, 292, 292, 160};
  int32_t rx[5], ry[5], cx[5], cy[5];
  for (int i = 0; i < 5; i++) {
    rotToCanonical(0, px[i], py[i], cx[i], cy[i]);
    float det = A * E - B * D, X = cx[i] - C0, Y = cy[i] - F;
    rx[i] = (int32_t)lroundf((E * X - B * Y) / det);
    ry[i] = (int32_t)lroundf((-D * X + A * Y) / det);
  }
  TouchCal cal;
  float rms;
  CHECK(fitTouchCal(rx, ry, cx, cy, 5, cal, &rms));
  CHECK(rms < 2);
  int32_t mx, my, sx2, sy2;
  applyTouchCal(cal, rx[4], ry[4], mx, my);
  rotFromCanonical(0, mx, my, sx2, sy2);
  CHECK(abs(sx2 - 120) <= 2 && abs(sy2 - 160) <= 2);
  // the same calibration still works after rotating to landscape (rot 1)
  applyTouchCal(cal, rx[1], ry[1], mx, my);
  int32_t lx1, ly1;
  rotToCanonical(0, 212, 28, lx1, ly1);
  CHECK(abs(mx - lx1) <= 2 && abs(my - ly1) <= 2);
}

static void testTeams() {
  g_section = "teams";
  CHECK_STR(teamStyle("mclaren", nullptr).shortName, "McLaren");
  CHECK_EQ(teamStyle("ferrari", nullptr).rgb, 0xE8102Eu);
  CHECK_STR(teamStyle("unknown_team", "Unknown Racing").shortName, "Unknown Racing");
  CHECK_STR(teamStyle(nullptr, nullptr).shortName, "-");
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s <fixtures_dir> <tz_csv>\n", argv[0]); return 2; }
  g_fixtures = argv[1];
  testCivil();
  testIsraelTz(argv[2]);
  testIsoParse();
  testText();
  testHttp();
  testCalendarFixture();
  testStandingsFixtures();
  testResultsFixture();
  testResultsEdgeCases();
  testMalformed();
  testTruncationSweep();
  testMutationFuzz();
  testPipeline();
  testCache();
  testBackoff();
  testRaceLogic();
  testScheduler();
  testCalibration();
  testRotation();
  testTeams();
  printf("\n%d checks, %d failures\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
