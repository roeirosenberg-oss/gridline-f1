// Gridline F1 — data model (fixed-size, heap-free, cache-serialisable)
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "gl_time.h"

namespace gl {

static const int kMaxRaces = 30;
static const int kMaxSessions = 6;
static const int kMaxDrivers = 32;
static const int kMaxTeams = 16;
static const int kMaxResults = 32;

enum SessionKind : uint8_t {
  SK_NONE = 0,
  SK_FP1,
  SK_FP2,
  SK_FP3,
  SK_SPRINT_QUALI,
  SK_SPRINT,
  SK_QUALI,
  SK_RACE,
};
const char* sessionShortName(uint8_t kind);  // "FP1", "SQ", "SPRINT", "QUALI", "RACE"

struct Session {
  int64_t startUtc;
  uint8_t kind;
  uint8_t hasTime;
  uint8_t _pad[6];
};

struct Race {
  int64_t raceUtc;   // race start; 00:00 UTC of race day when hasTime == 0
  uint16_t season;
  uint8_t round;
  uint8_t hasTime;
  uint8_t sessionCount;
  uint8_t hasSprint;
  uint8_t _pad[2];
  char name[40];       // "Mexico City Grand Prix"
  char circuit[48];    // "Autódromo Hermanos Rodríguez"
  char locality[28];   // "Mexico City"
  char country[24];    // "Mexico"
  Session sessions[kMaxSessions];  // chronological, excluding the race itself
};

struct Calendar {
  uint16_t season;
  uint8_t count;
  uint8_t truncated;   // 1 if the API returned more races than kMaxRaces
  uint8_t _pad[4];
  Race races[kMaxRaces];
};

struct DriverRow {
  uint8_t pos;          // 0 when not classified / null
  uint8_t wins;
  char posText[4];      // "1", "-", "D", "E"
  char code[4];         // "NOR"
  char number[4];       // "4"
  char driverId[24];    // "norris"
  char given[20];
  char family[24];
  char teamId[24];      // constructorId of the most recent team
  char team[28];
  char points[8];       // "412" / "12.5"
};

struct DriverStandings {
  uint16_t season;
  uint8_t round;
  uint8_t count;
  uint8_t truncated;
  uint8_t _pad[3];
  DriverRow rows[kMaxDrivers];
};

struct TeamRow {
  uint8_t pos;
  uint8_t wins;
  char posText[4];
  char teamId[24];
  char name[28];
  char points[8];
};

struct ConstructorStandings {
  uint16_t season;
  uint8_t round;
  uint8_t count;
  uint8_t truncated;
  uint8_t _pad[3];
  TeamRow rows[kMaxTeams];
};

enum FinishClass : uint8_t {
  FC_CLASSIFIED = 0,   // numeric positionText
  FC_RETIRED,          // "R"  (and legacy "N" not classified)
  FC_DISQUALIFIED,     // "D"
  FC_EXCLUDED,         // "E"
  FC_WITHDRAWN,        // "W"  did not start
  FC_FAILED_QUALIFY,   // "F"
  FC_UNKNOWN,
};
const char* finishClassShort(uint8_t fc);  // "", "DNF", "DSQ", "EXC", "DNS", "DNQ", "?"

struct ResultRow {
  uint8_t pos;          // 0 when null
  uint8_t finishClass;  // FinishClass
  uint8_t laps;
  uint8_t grid;
  char posText[4];
  char code[4];
  char driverId[24];
  char family[24];
  char teamId[24];
  char points[8];
  char status[24];      // "Finished", "+1 Lap", "Lapped", "Retired", ...
  char time[16];        // "1:32:05.120" / "+5.123" / ""
};

struct RaceResults {
  int64_t raceUtc;
  uint16_t season;
  uint8_t round;
  uint8_t count;
  uint8_t hasTime;
  uint8_t truncated;
  uint8_t _pad[2];
  char name[40];
  char circuit[48];
  char country[24];
  ResultRow rows[kMaxResults];
};

// ---- Dataset bookkeeping ----
enum DatasetId : uint8_t {
  DS_CALENDAR = 0,      // season S (current UTC year)
  DS_CALENDAR_NEXT,     // season S+1, only used when S has no upcoming race
  DS_DRIVERS,
  DS_TEAMS,
  DS_RESULTS,
  DS_COUNT
};
const char* datasetName(uint8_t ds);

enum DataOrigin : uint8_t {
  ORIGIN_NONE = 0,     // nothing available
  ORIGIN_CACHE,        // restored from flash after boot (not refreshed yet)
  ORIGIN_NETWORK,      // retrieved from the API during this boot
};

struct DatasetMeta {
  int64_t fetchedUtc;     // when the data was retrieved from the API (0 = unknown)
  int64_t lastAttemptUtc;
  uint8_t origin;         // DataOrigin
  uint8_t lastError;      // FetchError of the most recent attempt (0 = OK)
  uint8_t consecutiveFailures;
  uint8_t _pad[5];
};

}  // namespace gl
