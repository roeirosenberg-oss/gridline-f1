#include "gl_parse.h"
#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>
#include "gl_text.h"

namespace gl {

// ---------------------------------------------------------------------------
// Bounded allocator: every block carries an 8-byte size header so that the
// total live size can be tracked without relying on the platform allocator.
class BoundedAllocator : public ArduinoJson::Allocator {
 public:
  explicit BoundedAllocator(size_t limit) : limit_(limit) {}
  void* allocate(size_t size) override {
    if (used_ + size > limit_) { overflow_ = true; return nullptr; }
    uint8_t* p = (uint8_t*)malloc(size + kHdr);
    if (!p) { overflow_ = true; return nullptr; }
    memcpy(p, &size, sizeof(size));
    used_ += size;
    if (used_ > peak_) peak_ = used_;
    return p + kHdr;
  }
  void deallocate(void* ptr) override {
    if (!ptr) return;
    uint8_t* p = (uint8_t*)ptr - kHdr;
    size_t size;
    memcpy(&size, p, sizeof(size));
    used_ -= size;
    free(p);
  }
  void* reallocate(void* ptr, size_t newSize) override {
    if (!ptr) return allocate(newSize);
    uint8_t* p = (uint8_t*)ptr - kHdr;
    size_t oldSize;
    memcpy(&oldSize, p, sizeof(oldSize));
    if (newSize > oldSize && used_ + (newSize - oldSize) > limit_) { overflow_ = true; return nullptr; }
    uint8_t* q = (uint8_t*)realloc(p, newSize + kHdr);
    if (!q) { overflow_ = true; return nullptr; }
    memcpy(q, &newSize, sizeof(newSize));
    used_ = used_ - oldSize + newSize;
    if (used_ > peak_) peak_ = used_;
    return q + kHdr;
  }
  size_t peak() const { return peak_; }
  bool overflowed() const { return overflow_; }

 private:
  static const size_t kHdr = 8;
  size_t limit_;
  size_t used_ = 0;
  size_t peak_ = 0;
  bool overflow_ = false;
};

// ---------------------------------------------------------------------------
// helpers
static const char* str(JsonVariantConst v) {
  if (v.is<const char*>()) return v.as<const char*>();
  return nullptr;
}

static bool toLong(JsonVariantConst v, long& out) {
  if (v.isNull()) return false;
  if (v.is<long>()) { out = v.as<long>(); return true; }
  const char* s = str(v);
  if (!s || !*s) return false;
  long r = 0;
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    r = r * 10 + (*p - '0');
    if (r > 1000000) return false;
  }
  out = r;
  return true;
}

// points may be a string ("12.5") or a number
static void copyPoints(char* dst, size_t n, JsonVariantConst v) {
  dst[0] = 0;
  if (v.isNull()) return;
  if (const char* s = str(v)) {
    if (pointsToTenths(s) >= 0) copyUtf8(dst, n, s);
    return;
  }
  if (v.is<long>()) {
    snprintf(dst, n, "%ld", v.as<long>());
  } else if (v.is<float>()) {
    float f = v.as<float>();
    long t = (long)(f * 10 + 0.5f);
    if (t % 10 == 0) snprintf(dst, n, "%ld", t / 10);
    else snprintf(dst, n, "%ld.%ld", t / 10, t % 10);
  }
}

static void setDetail(ParseStats* st, const char* msg) {
  if (st) {
    strncpy(st->detail, msg, sizeof(st->detail) - 1);
    st->detail[sizeof(st->detail) - 1] = 0;
  }
}

static FetchError runDeserialize(JsonSource& in, JsonDocument& doc, const char* filterJson,
                                 BoundedAllocator& alloc, ParseStats* st) {
  JsonDocument filter;
  DeserializationError fe = deserializeJson(filter, filterJson);
  if (fe) { setDetail(st, "filter"); return FE_SCHEMA; }  // programming error
  DeserializationError err =
      deserializeJson(doc, in, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(12));
  if (st) st->peakJsonBytes = alloc.peak();
  // The transport error, if any, is the root cause of a failed parse.
  FetchError te = in.transportError();
  if (err) {
    if (te != FE_OK) return te;
    setDetail(st, err.c_str());
    if (err == DeserializationError::NoMemory) return FE_JSON_NOMEM;
    if (err == DeserializationError::TooDeep) return FE_JSON_DEPTH;
    return FE_JSON_INVALID;  // InvalidInput, IncompleteInput, EmptyInput
  }
  if (alloc.overflowed() || doc.overflowed()) { setDetail(st, "overflow"); return FE_JSON_NOMEM; }
  if (te != FE_OK) return te;
  if (!doc.is<JsonObject>()) { setDetail(st, "root not object"); return FE_SCHEMA; }
  if (!doc["MRData"].is<JsonObject>()) { setDetail(st, "no MRData"); return FE_SCHEMA; }
  return FE_OK;
}

const char* sessionShortName(uint8_t kind) {
  switch (kind) {
    case SK_FP1: return "FP1";
    case SK_FP2: return "FP2";
    case SK_FP3: return "FP3";
    case SK_SPRINT_QUALI: return "SPRINT QUALI";
    case SK_SPRINT: return "SPRINT";
    case SK_QUALI: return "QUALIFYING";
    case SK_RACE: return "RACE";
    default: return "";
  }
}

const char* finishClassShort(uint8_t fc) {
  switch (fc) {
    case FC_CLASSIFIED: return "";
    case FC_RETIRED: return "DNF";
    case FC_DISQUALIFIED: return "DSQ";
    case FC_EXCLUDED: return "EXC";
    case FC_WITHDRAWN: return "DNS";
    case FC_FAILED_QUALIFY: return "DNQ";
    default: return "NC";
  }
}

const char* datasetName(uint8_t ds) {
  switch (ds) {
    case DS_CALENDAR: return "calendar";
    case DS_CALENDAR_NEXT: return "calendar_next";
    case DS_DRIVERS: return "drivers";
    case DS_TEAMS: return "constructors";
    case DS_RESULTS: return "results";
    default: return "?";
  }
}

uint8_t classifyPositionText(const char* t) {
  if (!t || !*t) return FC_UNKNOWN;
  bool digits = true;
  for (const char* p = t; *p; p++)
    if (*p < '0' || *p > '9') { digits = false; break; }
  if (digits) return FC_CLASSIFIED;
  if (t[1] == 0) {
    switch (t[0]) {
      case 'R': case 'N': return FC_RETIRED;
      case 'D': return FC_DISQUALIFIED;
      case 'E': return FC_EXCLUDED;
      case 'W': return FC_WITHDRAWN;
      case 'F': return FC_FAILED_QUALIFY;
    }
  }
  return FC_UNKNOWN;
}

// ---------------------------------------------------------------------------
// Calendar
static const char kCalendarFilter[] =
    "{\"MRData\":{\"RaceTable\":{\"season\":true,\"Races\":[{\"season\":true,\"round\":true,"
    "\"raceName\":true,\"Circuit\":{\"circuitName\":true,\"Location\":{\"locality\":true,"
    "\"country\":true}},\"date\":true,\"time\":true,\"FirstPractice\":true,\"SecondPractice\":true,"
    "\"ThirdPractice\":true,\"Qualifying\":true,\"Sprint\":true,\"SprintQualifying\":true,"
    "\"SprintShootout\":true}]}}}";

static void addSession(Race& r, JsonVariantConst obj, uint8_t kind) {
  if (!obj.is<JsonObjectConst>()) return;
  if (r.sessionCount >= kMaxSessions) return;
  utc_t t;
  bool hasTime;
  if (!parseApiDateTime(str(obj["date"]), str(obj["time"]), t, hasTime)) return;
  Session& s = r.sessions[r.sessionCount++];
  memset(&s, 0, sizeof(s));
  s.startUtc = t;
  s.kind = kind;
  s.hasTime = hasTime ? 1 : 0;
  if (kind == SK_SPRINT) r.hasSprint = 1;
}

FetchError parseCalendar(JsonSource& in, uint16_t expectedSeason, Calendar& out, ParseStats* st) {
  BoundedAllocator alloc(kJsonBudgetBytes);
  JsonDocument doc(&alloc);
  FetchError e = runDeserialize(in, doc, kCalendarFilter, alloc, st);
  if (e != FE_OK) return e;
  JsonVariantConst races = doc["MRData"]["RaceTable"]["Races"];
  if (!races.is<JsonArrayConst>()) { setDetail(st, "no Races array"); return FE_SCHEMA; }
  JsonArrayConst arr = races.as<JsonArrayConst>();
  if (arr.size() == 0) return FE_EMPTY;

  memset(&out, 0, sizeof(out));
  out.season = expectedSeason;
  int seen = 0, skipped = 0;
  for (JsonVariantConst rv : arr) {
    seen++;
    long season = 0, round = 0;
    if (!toLong(rv["season"], season) || !toLong(rv["round"], round) || round < 1 || round > 99) { skipped++; continue; }
    if (expectedSeason && season != expectedSeason) { setDetail(st, "season mismatch"); return FE_SCHEMA; }
    const char* name = str(rv["raceName"]);
    if (!name || !*name) { skipped++; continue; }
    utc_t t;
    bool hasTime;
    if (!parseApiDateTime(str(rv["date"]), str(rv["time"]), t, hasTime)) { skipped++; continue; }
    if (out.count >= kMaxRaces) { out.truncated = 1; continue; }
    Race& r = out.races[out.count];
    memset(&r, 0, sizeof(r));
    r.season = (uint16_t)season;
    r.round = (uint8_t)round;
    r.raceUtc = t;
    r.hasTime = hasTime ? 1 : 0;
    copyUtf8(r.name, sizeof(r.name), name);
    copyUtf8(r.circuit, sizeof(r.circuit), str(rv["Circuit"]["circuitName"]));
    copyUtf8(r.locality, sizeof(r.locality), str(rv["Circuit"]["Location"]["locality"]));
    copyUtf8(r.country, sizeof(r.country), str(rv["Circuit"]["Location"]["country"]));
    addSession(r, rv["FirstPractice"], SK_FP1);
    addSession(r, rv["SecondPractice"], SK_FP2);
    addSession(r, rv["ThirdPractice"], SK_FP3);
    addSession(r, rv["SprintQualifying"], SK_SPRINT_QUALI);
    addSession(r, rv["SprintShootout"], SK_SPRINT_QUALI);
    addSession(r, rv["Sprint"], SK_SPRINT);
    addSession(r, rv["Qualifying"], SK_QUALI);
    // chronological order (insertion sort, max 6 items)
    for (int i = 1; i < r.sessionCount; i++) {
      Session k = r.sessions[i];
      int j = i - 1;
      while (j >= 0 && r.sessions[j].startUtc > k.startUtc) { r.sessions[j + 1] = r.sessions[j]; j--; }
      r.sessions[j + 1] = k;
    }
    out.count++;
  }
  if (st) { st->entriesSeen = seen; st->entriesSkipped = skipped; st->truncated = out.truncated; }
  if (out.count == 0) { setDetail(st, "no valid races"); return FE_SCHEMA; }
  if (skipped * 2 > seen) { setDetail(st, "too many invalid races"); return FE_SCHEMA; }
  // order by round, reject duplicates
  for (int i = 1; i < out.count; i++) {
    Race k = out.races[i];
    int j = i - 1;
    while (j >= 0 && out.races[j].round > k.round) { out.races[j + 1] = out.races[j]; j--; }
    out.races[j + 1] = k;
  }
  for (int i = 1; i < out.count; i++)
    if (out.races[i].round == out.races[i - 1].round) { setDetail(st, "duplicate round"); return FE_SCHEMA; }
  if (!expectedSeason) out.season = out.races[0].season;
  return FE_OK;
}

// ---------------------------------------------------------------------------
// Driver standings
static const char kDriverFilter[] =
    "{\"MRData\":{\"StandingsTable\":{\"season\":true,\"round\":true,\"StandingsLists\":[{\"season\":true,"
    "\"round\":true,\"DriverStandings\":[{\"position\":true,\"positionText\":true,\"points\":true,"
    "\"wins\":true,\"Driver\":{\"driverId\":true,\"permanentNumber\":true,\"code\":true,"
    "\"givenName\":true,\"familyName\":true},\"Constructors\":[{\"constructorId\":true,\"name\":true}]}]}]}}}";

static FetchError standingsList(JsonDocument& doc, uint16_t expectedSeason, const char* key,
                                JsonArrayConst& rows, long& season, long& round, ParseStats* st) {
  JsonVariantConst table = doc["MRData"]["StandingsTable"];
  if (!table.is<JsonObjectConst>()) { setDetail(st, "no StandingsTable"); return FE_SCHEMA; }
  JsonVariantConst lists = table["StandingsLists"];
  if (!lists.is<JsonArrayConst>()) { setDetail(st, "no StandingsLists"); return FE_SCHEMA; }
  if (lists.size() == 0) return FE_EMPTY;
  JsonVariantConst first = lists[0];
  if (!toLong(first["season"], season)) { setDetail(st, "no season"); return FE_SCHEMA; }
  if (expectedSeason && season != expectedSeason) { setDetail(st, "season mismatch"); return FE_SCHEMA; }
  if (!toLong(first["round"], round)) round = 0;
  JsonVariantConst arr = first[key];
  if (!arr.is<JsonArrayConst>()) { setDetail(st, "no standings array"); return FE_SCHEMA; }
  rows = arr.as<JsonArrayConst>();
  if (rows.size() == 0) return FE_EMPTY;
  return FE_OK;
}

static void copyPosition(JsonVariantConst posV, JsonVariantConst textV, uint8_t& pos, char* posText, size_t n) {
  long p = 0;
  pos = (toLong(posV, p) && p > 0 && p < 256) ? (uint8_t)p : 0;
  const char* t = str(textV);
  if (t && *t) copyUtf8(posText, n, t);
  else if (pos) snprintf(posText, n, "%u", pos);
  else copyUtf8(posText, n, "-");
}

FetchError parseDriverStandings(JsonSource& in, uint16_t expectedSeason, DriverStandings& out, ParseStats* st) {
  BoundedAllocator alloc(kJsonBudgetBytes);
  JsonDocument doc(&alloc);
  FetchError e = runDeserialize(in, doc, kDriverFilter, alloc, st);
  if (e != FE_OK) return e;
  JsonArrayConst rows;
  long season = 0, round = 0;
  e = standingsList(doc, expectedSeason, "DriverStandings", rows, season, round, st);
  if (e != FE_OK) return e;
  memset(&out, 0, sizeof(out));
  out.season = (uint16_t)season;
  out.round = (uint8_t)(round > 255 ? 0 : round);
  int seen = 0, skipped = 0;
  for (JsonVariantConst rv : rows) {
    seen++;
    JsonVariantConst drv = rv["Driver"];
    const char* id = str(drv["driverId"]);
    const char* fam = str(drv["familyName"]);
    if (!id || !*id || !fam || !*fam) { skipped++; continue; }
    char pts[8];
    copyPoints(pts, sizeof(pts), rv["points"]);
    if (!pts[0]) { skipped++; continue; }
    if (out.count >= kMaxDrivers) { out.truncated = 1; continue; }
    DriverRow& d = out.rows[out.count];
    memset(&d, 0, sizeof(d));
    copyPosition(rv["position"], rv["positionText"], d.pos, d.posText, sizeof(d.posText));
    long wins = 0;
    if (toLong(rv["wins"], wins) && wins < 256) d.wins = (uint8_t)wins;
    memcpy(d.points, pts, sizeof(d.points));
    copyUtf8(d.driverId, sizeof(d.driverId), id);
    copyUtf8(d.family, sizeof(d.family), fam);
    copyUtf8(d.given, sizeof(d.given), str(drv["givenName"]));
    copyUtf8(d.number, sizeof(d.number), str(drv["permanentNumber"]));
    const char* code = str(drv["code"]);
    if (code && strlen(code) == 3) upperAscii(d.code, sizeof(d.code), code);
    else fallbackDriverCode(d.code, fam);
    JsonVariantConst teams = rv["Constructors"];
    if (teams.is<JsonArrayConst>() && teams.size() > 0) {
      JsonVariantConst last = teams[teams.size() - 1];  // most recent team
      copyUtf8(d.teamId, sizeof(d.teamId), str(last["constructorId"]));
      copyUtf8(d.team, sizeof(d.team), str(last["name"]));
    }
    out.count++;
  }
  if (st) { st->entriesSeen = seen; st->entriesSkipped = skipped; st->truncated = out.truncated; }
  if (out.count == 0 || skipped * 2 > seen) { setDetail(st, "invalid driver rows"); return FE_SCHEMA; }
  return FE_OK;
}

// ---------------------------------------------------------------------------
// Constructor standings
static const char kTeamFilter[] =
    "{\"MRData\":{\"StandingsTable\":{\"season\":true,\"round\":true,\"StandingsLists\":[{\"season\":true,"
    "\"round\":true,\"ConstructorStandings\":[{\"position\":true,\"positionText\":true,\"points\":true,"
    "\"wins\":true,\"Constructor\":{\"constructorId\":true,\"name\":true}}]}]}}}";

FetchError parseConstructorStandings(JsonSource& in, uint16_t expectedSeason, ConstructorStandings& out,
                                     ParseStats* st) {
  BoundedAllocator alloc(kJsonBudgetBytes);
  JsonDocument doc(&alloc);
  FetchError e = runDeserialize(in, doc, kTeamFilter, alloc, st);
  if (e != FE_OK) return e;
  JsonArrayConst rows;
  long season = 0, round = 0;
  e = standingsList(doc, expectedSeason, "ConstructorStandings", rows, season, round, st);
  if (e != FE_OK) return e;
  memset(&out, 0, sizeof(out));
  out.season = (uint16_t)season;
  out.round = (uint8_t)(round > 255 ? 0 : round);
  int seen = 0, skipped = 0;
  for (JsonVariantConst rv : rows) {
    seen++;
    JsonVariantConst c = rv["Constructor"];
    const char* id = str(c["constructorId"]);
    const char* name = str(c["name"]);
    if (!id || !*id || !name || !*name) { skipped++; continue; }
    char pts[8];
    copyPoints(pts, sizeof(pts), rv["points"]);
    if (!pts[0]) { skipped++; continue; }
    if (out.count >= kMaxTeams) { out.truncated = 1; continue; }
    TeamRow& t = out.rows[out.count];
    memset(&t, 0, sizeof(t));
    copyPosition(rv["position"], rv["positionText"], t.pos, t.posText, sizeof(t.posText));
    long wins = 0;
    if (toLong(rv["wins"], wins) && wins < 256) t.wins = (uint8_t)wins;
    memcpy(t.points, pts, sizeof(t.points));
    copyUtf8(t.teamId, sizeof(t.teamId), id);
    copyUtf8(t.name, sizeof(t.name), name);
    out.count++;
  }
  if (st) { st->entriesSeen = seen; st->entriesSkipped = skipped; st->truncated = out.truncated; }
  if (out.count == 0 || skipped * 2 > seen) { setDetail(st, "invalid team rows"); return FE_SCHEMA; }
  return FE_OK;
}

// ---------------------------------------------------------------------------
// Race results
static const char kResultsFilter[] =
    "{\"MRData\":{\"RaceTable\":{\"season\":true,\"round\":true,\"Races\":[{\"season\":true,\"round\":true,"
    "\"raceName\":true,\"Circuit\":{\"circuitName\":true,\"Location\":{\"country\":true}},\"date\":true,"
    "\"time\":true,\"Results\":[{\"position\":true,\"positionText\":true,\"points\":true,\"grid\":true,"
    "\"laps\":true,\"status\":true,\"Time\":{\"time\":true},\"Driver\":{\"driverId\":true,\"code\":true,"
    "\"familyName\":true},\"Constructor\":{\"constructorId\":true}}]}]}}}";

FetchError parseRaceResults(JsonSource& in, uint16_t expectedSeason, RaceResults& out, ParseStats* st) {
  BoundedAllocator alloc(kJsonBudgetBytes);
  JsonDocument doc(&alloc);
  FetchError e = runDeserialize(in, doc, kResultsFilter, alloc, st);
  if (e != FE_OK) return e;
  JsonVariantConst races = doc["MRData"]["RaceTable"]["Races"];
  if (!races.is<JsonArrayConst>()) { setDetail(st, "no Races array"); return FE_SCHEMA; }
  if (races.size() == 0) return FE_EMPTY;
  JsonVariantConst race = races[0];
  long season = 0, round = 0;
  if (!toLong(race["season"], season) || !toLong(race["round"], round) || round < 1 || round > 99) {
    setDetail(st, "bad season/round");
    return FE_SCHEMA;
  }
  if (expectedSeason && season != expectedSeason) { setDetail(st, "season mismatch"); return FE_SCHEMA; }
  JsonVariantConst results = race["Results"];
  if (results.isNull()) return FE_EMPTY;
  if (!results.is<JsonArrayConst>()) { setDetail(st, "Results not array"); return FE_SCHEMA; }
  if (results.size() == 0) return FE_EMPTY;

  memset(&out, 0, sizeof(out));
  out.season = (uint16_t)season;
  out.round = (uint8_t)round;
  bool hasTime;
  utc_t t = 0;
  if (!parseApiDateTime(str(race["date"]), str(race["time"]), t, hasTime)) { setDetail(st, "bad date"); return FE_SCHEMA; }
  out.raceUtc = t;
  out.hasTime = hasTime ? 1 : 0;
  copyUtf8(out.name, sizeof(out.name), str(race["raceName"]));
  copyUtf8(out.circuit, sizeof(out.circuit), str(race["Circuit"]["circuitName"]));
  copyUtf8(out.country, sizeof(out.country), str(race["Circuit"]["Location"]["country"]));
  if (!out.name[0]) { setDetail(st, "no race name"); return FE_SCHEMA; }

  int seen = 0, skipped = 0;
  for (JsonVariantConst rv : results.as<JsonArrayConst>()) {
    seen++;
    JsonVariantConst drv = rv["Driver"];
    const char* id = str(drv["driverId"]);
    const char* fam = str(drv["familyName"]);
    if (!id || !*id || !fam || !*fam) { skipped++; continue; }
    if (out.count >= kMaxResults) { out.truncated = 1; continue; }
    ResultRow& r = out.rows[out.count];
    memset(&r, 0, sizeof(r));
    copyPosition(rv["position"], rv["positionText"], r.pos, r.posText, sizeof(r.posText));
    r.finishClass = classifyPositionText(str(rv["positionText"]));
    if (r.finishClass == FC_UNKNOWN && r.pos > 0 && str(rv["positionText"]) == nullptr) r.finishClass = FC_CLASSIFIED;
    long v = 0;
    if (toLong(rv["laps"], v) && v < 256) r.laps = (uint8_t)v;
    if (toLong(rv["grid"], v) && v < 256) r.grid = (uint8_t)v;
    copyPoints(r.points, sizeof(r.points), rv["points"]);
    copyUtf8(r.status, sizeof(r.status), str(rv["status"]));
    copyUtf8(r.time, sizeof(r.time), str(rv["Time"]["time"]));
    copyUtf8(r.driverId, sizeof(r.driverId), id);
    copyUtf8(r.family, sizeof(r.family), fam);
    const char* code = str(drv["code"]);
    if (code && strlen(code) == 3) upperAscii(r.code, sizeof(r.code), code);
    else fallbackDriverCode(r.code, fam);
    copyUtf8(r.teamId, sizeof(r.teamId), str(rv["Constructor"]["constructorId"]));
    out.count++;
  }
  if (st) { st->entriesSeen = seen; st->entriesSkipped = skipped; st->truncated = out.truncated; }
  if (out.count == 0 || skipped * 2 > seen) { setDetail(st, "invalid result rows"); return FE_SCHEMA; }
  return FE_OK;
}

}  // namespace gl
