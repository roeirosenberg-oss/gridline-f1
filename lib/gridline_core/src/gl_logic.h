// Gridline F1 — scheduling, retry/backoff, race selection, cache codec,
// touch calibration maths and team colours. Pure C++, host-testable.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gl_errors.h"
#include "gl_model.h"
#include "gl_time.h"

namespace gl {

// ---------------------------------------------------------------- CRC32
uint32_t crc32(const void* data, size_t len, uint32_t seed = 0);

// ---------------------------------------------------------------- cache codec
// Layout: [CacheHeader][payload]. The payload is the raw POD struct.
struct CacheHeader {
  uint32_t magic;          // 'GLC1'
  uint16_t schema;         // bump when any model struct changes
  uint8_t dataset;         // DatasetId
  uint8_t reserved;
  uint32_t payloadSize;
  uint32_t payloadCrc;
  int64_t fetchedUtc;
  uint32_t headerCrc;      // CRC of all preceding header bytes
  uint32_t _pad;
};
static const uint32_t kCacheMagic = 0x31434C47;  // "GLC1" little endian
static const uint16_t kCacheSchema = 3;

size_t encodeCache(uint8_t dataset, const void* payload, size_t payloadSize, int64_t fetchedUtc,
                   uint8_t* out, size_t outCap);
// Returns true and copies the payload when the blob is complete and intact.
bool decodeCache(const uint8_t* blob, size_t blobSize, uint8_t dataset, void* payload, size_t payloadSize,
                 int64_t& fetchedUtc);

// ---------------------------------------------------------------- backoff
// Delay before the next attempt after `failures` consecutive failures
// (failures >= 1). jitter01 in [0,1) spreads retries (+/-15%).
uint32_t retryDelaySeconds(uint8_t failures, uint8_t error, int32_t retryAfterSec, float jitter01);

// ---------------------------------------------------------------- race logic
struct NextRace {
  const Race* race = nullptr;       // nullptr when nothing upcoming is known
  bool fromNextSeason = false;      // race comes from the S+1 calendar
  bool inProgress = false;          // race started less than ~2.5 h ago
  const Session* nextSession = nullptr;  // next session (incl. none if race is next)
};

// A race counts as finished 3 h after its start (or at the end of the race
// day + 6 h when no start time is published).
utc_t raceFinishedAt(const Race& r);
NextRace findNextRace(utc_t now, const Calendar* current, const Calendar* nextSeason);
// Most recent race whose finish time has passed (nullptr if none).
const Race* latestCompletedRace(utc_t now, const Calendar* cal);
// True when every race of the calendar has finished.
bool seasonComplete(utc_t now, const Calendar* cal);
// Index of the race to highlight in the calendar list (next race or -1).
int nextRaceIndex(utc_t now, const Calendar* cal);

// ---------------------------------------------------------------- scheduler
struct ScheduleState {
  DatasetMeta meta[DS_COUNT];
  int64_t nextAttemptUtc[DS_COUNT];   // earliest retry after a failure (0 = none)
  int64_t networkRetryUtc;            // global gate after a network-level failure
  uint8_t networkFailures;
  uint16_t seasonOf[DS_COUNT];        // season of the data held (0 = none)
  uint8_t roundOf[DS_COUNT];          // round the standings/results refer to
};

struct ScheduleContext {
  utc_t now;
  uint16_t currentSeason;             // UTC year
  const Calendar* calendar;           // may be nullptr
  bool needNextSeasonCalendar;        // current calendar has no upcoming race
};

// Refresh interval for a dataset given the race weekend situation.
uint32_t refreshIntervalSeconds(uint8_t ds, const ScheduleContext& ctx, const ScheduleState& st);
// Returns the dataset to fetch now, or -1 if nothing is due.
int pickDueDataset(const ScheduleContext& ctx, const ScheduleState& st);
// Seconds until the next dataset becomes due (for idle sleeping), capped.
uint32_t secondsUntilNextDue(const ScheduleContext& ctx, const ScheduleState& st, uint32_t cap);

// A freshly booted device that restored data younger than this from flash
// does not refetch it immediately (protects the API from reboot loops).
static const uint32_t kBootRefetchMinAgeSec = 10 * 60;

// ---------------------------------------------------------------- touch calibration
struct TouchCal {
  float a, b, c;   // x = a*rx + b*ry + c
  float d, e, f;   // y = d*rx + e*ry + f
};
// Least-squares affine fit from n >= 3 raw/screen point pairs.
bool fitTouchCal(const int32_t* rawX, const int32_t* rawY, const int32_t* scrX, const int32_t* scrY, int n,
                 TouchCal& out, float* rmsErrorPx);
void applyTouchCal(const TouchCal& c, int32_t rx, int32_t ry, int32_t& x, int32_t& y);
bool touchCalPlausible(const TouchCal& c);

// ---------------------------------------------------------------- display rotation
// Touch calibration is stored in one canonical frame: TFT_eSPI rotation 1
// (landscape, 320 x 240). These convert between that frame and the screen
// coordinates of rotation 0..3 (each step is 90 degrees, as in TFT_eSPI).
// Calibrating in any rotation goes through rotToCanonical() and touches are
// mapped back with rotFromCanonical(), so a calibration made in the current
// rotation is always self-consistent.
static const int kCanonW = 320, kCanonH = 240;
void rotFromCanonical(uint8_t rot, int32_t lx, int32_t ly, int32_t& x, int32_t& y);
void rotToCanonical(uint8_t rot, int32_t x, int32_t y, int32_t& lx, int32_t& ly);
inline int rotWidth(uint8_t rot) { return (rot & 1) ? kCanonW : kCanonH; }
inline int rotHeight(uint8_t rot) { return (rot & 1) ? kCanonH : kCanonW; }

// ---------------------------------------------------------------- teams
struct TeamStyle { uint32_t rgb; const char* shortName; };
TeamStyle teamStyle(const char* constructorId, const char* fallbackName);

}  // namespace gl
