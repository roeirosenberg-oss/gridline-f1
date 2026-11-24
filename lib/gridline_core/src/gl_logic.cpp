#include "gl_logic.h"
#include <math.h>
#include <string.h>

namespace gl {

// ---------------------------------------------------------------- CRC32
uint32_t crc32(const void* data, size_t len, uint32_t seed) {
  const uint8_t* p = (const uint8_t*)data;
  uint32_t c = ~seed;
  for (size_t i = 0; i < len; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

// ---------------------------------------------------------------- cache codec
size_t encodeCache(uint8_t dataset, const void* payload, size_t payloadSize, int64_t fetchedUtc, uint8_t* out,
                   size_t outCap) {
  if (outCap < sizeof(CacheHeader) + payloadSize) return 0;
  CacheHeader h;
  memset(&h, 0, sizeof(h));
  h.magic = kCacheMagic;
  h.schema = kCacheSchema;
  h.dataset = dataset;
  h.payloadSize = (uint32_t)payloadSize;
  h.payloadCrc = crc32(payload, payloadSize);
  h.fetchedUtc = fetchedUtc;
  h.headerCrc = crc32(&h, offsetof(CacheHeader, headerCrc));
  memcpy(out, &h, sizeof(h));
  memcpy(out + sizeof(h), payload, payloadSize);
  return sizeof(h) + payloadSize;
}

bool decodeCache(const uint8_t* blob, size_t blobSize, uint8_t dataset, void* payload, size_t payloadSize,
                 int64_t& fetchedUtc) {
  if (!blob || blobSize < sizeof(CacheHeader)) return false;
  CacheHeader h;
  memcpy(&h, blob, sizeof(h));
  if (h.magic != kCacheMagic || h.schema != kCacheSchema || h.dataset != dataset) return false;
  if (h.headerCrc != crc32(&h, offsetof(CacheHeader, headerCrc)) || h._pad != 0) return false;
  if (h.payloadSize != payloadSize || blobSize != sizeof(h) + payloadSize) return false;
  if (crc32(blob + sizeof(h), payloadSize) != h.payloadCrc) return false;
  memcpy(payload, blob + sizeof(h), payloadSize);
  fetchedUtc = h.fetchedUtc;
  return true;
}

// ---------------------------------------------------------------- backoff
uint32_t retryDelaySeconds(uint8_t failures, uint8_t error, int32_t retryAfterSec, float jitter01) {
  static const uint32_t table[] = {30, 60, 120, 300, 600, 1200, 1800};
  if (error == FE_EMPTY) return 3600;  // data simply does not exist yet
  uint8_t idx = failures == 0 ? 0 : (uint8_t)(failures - 1);
  if (idx >= sizeof(table) / sizeof(table[0])) idx = sizeof(table) / sizeof(table[0]) - 1;
  uint32_t base = table[idx];
  if (error == FE_HTTP_429) {
    uint32_t ra = retryAfterSec > 0 ? (uint32_t)retryAfterSec : 0;
    if (ra < 120) ra = 120;
    if (ra > 3600) ra = 3600;
    if (base < ra) base = ra;
  }
  // Data/format problems will not fix themselves within seconds.
  if ((error == FE_SCHEMA || error == FE_JSON_INVALID || error == FE_JSON_NOMEM || error == FE_JSON_DEPTH ||
       error == FE_TOO_LARGE || error == FE_HTTP_4XX || error == FE_HTTP_404) && base < 300)
    base = 300;
  if (jitter01 < 0) jitter01 = 0;
  if (jitter01 >= 1) jitter01 = 0.999f;
  float factor = 0.85f + 0.30f * jitter01;
  uint32_t d = (uint32_t)(base * factor);
  if (error == FE_HTTP_429 && retryAfterSec > 0 && d < (uint32_t)retryAfterSec) d = (uint32_t)retryAfterSec;
  return d < 10 ? 10 : d;
}

// ---------------------------------------------------------------- race logic
utc_t raceFinishedAt(const Race& r) {
  if (r.hasTime) return r.raceUtc + 3 * 3600;
  return r.raceUtc + 30 * 3600;  // end of race day in any time zone + margin
}

NextRace findNextRace(utc_t now, const Calendar* current, const Calendar* nextSeason) {
  NextRace nr;
  const Calendar* cals[2] = {current, nextSeason};
  for (int c = 0; c < 2 && !nr.race; c++) {
    const Calendar* cal = cals[c];
    if (!cal) continue;
    for (int i = 0; i < cal->count; i++) {
      const Race& r = cal->races[i];
      if (now < raceFinishedAt(r)) {
        nr.race = &r;
        nr.fromNextSeason = (c == 1);
        nr.inProgress = r.hasTime && now >= r.raceUtc;
        break;
      }
    }
  }
  if (nr.race) {
    for (int i = 0; i < nr.race->sessionCount; i++) {
      const Session& s = nr.race->sessions[i];
      utc_t end = s.startUtc + (s.hasTime ? 3600 * 2 : 86400);
      if (now < end) { nr.nextSession = &s; break; }
    }
  }
  return nr;
}

const Race* latestCompletedRace(utc_t now, const Calendar* cal) {
  if (!cal) return nullptr;
  const Race* best = nullptr;
  for (int i = 0; i < cal->count; i++)
    if (raceFinishedAt(cal->races[i]) <= now) best = &cal->races[i];
  return best;
}

bool seasonComplete(utc_t now, const Calendar* cal) {
  if (!cal || cal->count == 0) return false;
  return raceFinishedAt(cal->races[cal->count - 1]) <= now;
}

int nextRaceIndex(utc_t now, const Calendar* cal) {
  if (!cal) return -1;
  for (int i = 0; i < cal->count; i++)
    if (now < raceFinishedAt(cal->races[i])) return i;
  return -1;
}

// ---------------------------------------------------------------- scheduler
static const Race* hotRace(const ScheduleContext& ctx) {
  // the race whose weekend window (start-48h .. finish+27h) contains now
  if (!ctx.calendar) return nullptr;
  for (int i = 0; i < ctx.calendar->count; i++) {
    const Race& r = ctx.calendar->races[i];
    if (ctx.now >= r.raceUtc - 48 * 3600 && ctx.now <= raceFinishedAt(r) + 27 * 3600) return &r;
  }
  return nullptr;
}

uint32_t refreshIntervalSeconds(uint8_t ds, const ScheduleContext& ctx, const ScheduleState& st) {
  const Race* hot = hotRace(ctx);
  switch (ds) {
    case DS_CALENDAR: return hot ? 3 * 3600 : 6 * 3600;
    case DS_CALENDAR_NEXT: return 12 * 3600;
    default: break;
  }
  if (hot && ctx.now >= raceFinishedAt(*hot)) {
    bool awaiting = st.seasonOf[ds] != hot->season || st.roundOf[ds] < hot->round;
    return awaiting ? 10 * 60 : 60 * 60;
  }
  if (hot) return 60 * 60;
  // between weekends
  int idx = nextRaceIndex(ctx.now, ctx.calendar);
  if (idx >= 0 && ctx.calendar->races[idx].raceUtc - ctx.now < 7 * 86400) return 3 * 3600;
  return 6 * 3600;
}

static bool isRelevant(uint8_t ds, const ScheduleContext& ctx) {
  if (ds == DS_CALENDAR_NEXT) return ctx.needNextSeasonCalendar;
  return true;
}

static int64_t dueAt(uint8_t ds, const ScheduleContext& ctx, const ScheduleState& st) {
  const DatasetMeta& m = st.meta[ds];
  int64_t due;
  if (m.origin == ORIGIN_NONE || m.fetchedUtc <= 0) {
    due = ctx.now;
  } else if (ds == DS_CALENDAR && st.seasonOf[ds] != ctx.currentSeason) {
    due = ctx.now;  // new season started
  } else if (ds == DS_CALENDAR_NEXT && st.seasonOf[ds] != ctx.currentSeason + 1) {
    due = ctx.now;
  } else if (m.origin == ORIGIN_CACHE) {
    due = m.fetchedUtc + kBootRefetchMinAgeSec;
  } else {
    due = m.fetchedUtc + refreshIntervalSeconds(ds, ctx, st);
  }
  if (st.nextAttemptUtc[ds] > due) due = st.nextAttemptUtc[ds];
  if (st.networkRetryUtc > due) due = st.networkRetryUtc;
  return due;
}

int pickDueDataset(const ScheduleContext& ctx, const ScheduleState& st) {
  static const uint8_t order[] = {DS_CALENDAR, DS_DRIVERS, DS_RESULTS, DS_TEAMS, DS_CALENDAR_NEXT};
  for (uint8_t ds : order) {
    if (!isRelevant(ds, ctx)) continue;
    if (dueAt(ds, ctx, st) <= ctx.now) return ds;
  }
  return -1;
}

uint32_t secondsUntilNextDue(const ScheduleContext& ctx, const ScheduleState& st, uint32_t cap) {
  int64_t best = (int64_t)cap;
  for (uint8_t ds = 0; ds < DS_COUNT; ds++) {
    if (!isRelevant(ds, ctx)) continue;
    int64_t d = dueAt(ds, ctx, st) - ctx.now;
    if (d < best) best = d;
  }
  return best < 0 ? 0 : (uint32_t)best;
}

// ---------------------------------------------------------------- touch calibration
static bool solve3(double m[3][3], double v[3], double out[3]) {
  double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  if (fabs(det) < 1e-9) return false;
  for (int col = 0; col < 3; col++) {
    double t[3][3];
    memcpy(t, m, sizeof(t));
    for (int r = 0; r < 3; r++) t[r][col] = v[r];
    double d = t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) - t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) +
               t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0]);
    out[col] = d / det;
  }
  return true;
}

bool fitTouchCal(const int32_t* rx, const int32_t* ry, const int32_t* sx, const int32_t* sy, int n, TouchCal& out,
                 float* rms) {
  if (n < 3) return false;
  double m[3][3] = {{0}}, vx[3] = {0}, vy[3] = {0};
  for (int i = 0; i < n; i++) {
    double x = rx[i], y = ry[i];
    m[0][0] += x * x; m[0][1] += x * y; m[0][2] += x;
    m[1][0] += x * y; m[1][1] += y * y; m[1][2] += y;
    m[2][0] += x;     m[2][1] += y;     m[2][2] += 1;
    vx[0] += x * sx[i]; vx[1] += y * sx[i]; vx[2] += sx[i];
    vy[0] += x * sy[i]; vy[1] += y * sy[i]; vy[2] += sy[i];
  }
  double cx[3], cy[3];
  if (!solve3(m, vx, cx) || !solve3(m, vy, cy)) return false;
  out.a = (float)cx[0]; out.b = (float)cx[1]; out.c = (float)cx[2];
  out.d = (float)cy[0]; out.e = (float)cy[1]; out.f = (float)cy[2];
  if (rms) {
    double acc = 0;
    for (int i = 0; i < n; i++) {
      int32_t x, y;
      applyTouchCal(out, rx[i], ry[i], x, y);
      acc += (double)(x - sx[i]) * (x - sx[i]) + (double)(y - sy[i]) * (y - sy[i]);
    }
    *rms = (float)sqrt(acc / n);
  }
  return touchCalPlausible(out);
}

void applyTouchCal(const TouchCal& c, int32_t rx, int32_t ry, int32_t& x, int32_t& y) {
  x = (int32_t)lroundf(c.a * rx + c.b * ry + c.c);
  y = (int32_t)lroundf(c.d * rx + c.e * ry + c.f);
}

bool touchCalPlausible(const TouchCal& c) {
  float vals[6] = {c.a, c.b, c.c, c.d, c.e, c.f};
  for (float v : vals)
    if (isnan(v) || isinf(v)) return false;
  float sx = sqrtf(c.a * c.a + c.b * c.b);
  float sy = sqrtf(c.d * c.d + c.e * c.e);
  // 320 px across roughly 3000..4000 raw units => ~0.08 px/unit
  if (sx < 0.02f || sx > 0.5f || sy < 0.02f || sy > 0.5f) return false;
  float det = c.a * c.e - c.b * c.d;
  if (fabsf(det) < 0.0004f) return false;
  return true;
}

// ---------------------------------------------------------------- display rotation
// Derived from the ST7789/ILI9341 MADCTL settings used by TFT_eSPI:
// rot0 (portrait) = canonical rotated by -90 degrees, rot2 = rot0 + 180,
// rot3 = canonical + 180.
void rotFromCanonical(uint8_t rot, int32_t lx, int32_t ly, int32_t& x, int32_t& y) {
  switch (rot & 3) {
    case 0: x = (kCanonH - 1) - ly; y = lx; break;
    case 2: x = ly; y = (kCanonW - 1) - lx; break;
    case 3: x = (kCanonW - 1) - lx; y = (kCanonH - 1) - ly; break;
    default: x = lx; y = ly; break;
  }
}

void rotToCanonical(uint8_t rot, int32_t x, int32_t y, int32_t& lx, int32_t& ly) {
  switch (rot & 3) {
    case 0: lx = y; ly = (kCanonH - 1) - x; break;
    case 2: lx = (kCanonW - 1) - y; ly = x; break;
    case 3: lx = (kCanonW - 1) - x; ly = (kCanonH - 1) - y; break;
    default: lx = x; ly = y; break;
  }
}

// ---------------------------------------------------------------- teams
TeamStyle teamStyle(const char* id, const char* fallbackName) {
  struct Entry { const char* id; uint32_t rgb; const char* name; };
  static const Entry k[] = {
      {"mclaren", 0xFF8A1E, "McLaren"},      {"ferrari", 0xE8102E, "Ferrari"},
      {"red_bull", 0x3D6BE0, "Red Bull"},    {"mercedes", 0x2CE8C8, "Mercedes"},
      {"aston_martin", 0x1F9E74, "Aston Martin"}, {"alpine", 0x2B8CFF, "Alpine"},
      {"williams", 0x5AA0FF, "Williams"},    {"rb", 0x7B95FF, "Racing Bulls"},
      {"sauber", 0x47D35A, "Sauber"},        {"audi", 0xB6BBC4, "Audi"},
      {"haas", 0xEDEDED, "Haas"},            {"cadillac", 0xC9A227, "Cadillac"},
      {"alphatauri", 0x5E7BC4, "AlphaTauri"}, {"alfa", 0xB12039, "Alfa Romeo"},
      {"racing_point", 0xF596C8, "Racing Point"}, {"renault", 0xFFD400, "Renault"},
      {"toro_rosso", 0x4A6EDB, "Toro Rosso"}, {"force_india", 0xF59BC9, "Force India"},
  };
  if (id) {
    for (const Entry& e : k)
      if (strcmp(e.id, id) == 0) return TeamStyle{e.rgb, e.name};
  }
  return TeamStyle{0x8B93A3, (fallbackName && *fallbackName) ? fallbackName : "-"};
}

}  // namespace gl
