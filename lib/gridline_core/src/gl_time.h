// Gridline F1 — time utilities (pure C++, host-testable)
//
// All timestamps inside the firmware are UTC seconds since 1970-01-01
// (stored as int64_t to avoid the 2038 problem on 32-bit time_t).
// Israel local time is derived with an explicit rule set instead of a
// fixed UTC offset, see israelOffsetSeconds().
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace gl {

typedef int64_t utc_t;

struct CivilTime {
  int year;    // e.g. 2026
  int month;   // 1..12
  int day;     // 1..31
  int hour;    // 0..23
  int minute;  // 0..59
  int second;  // 0..59
  int weekday; // 0 = Sunday .. 6 = Saturday
};

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
int64_t daysFromCivil(int y, int m, int d);
// Inverse of daysFromCivil.
void civilFromDays(int64_t z, int& y, int& m, int& d);
// 0 = Sunday .. 6 = Saturday
int weekdayFromDays(int64_t z);

utc_t makeUtc(int y, int m, int d, int hh, int mm, int ss);
CivilTime toCivilUtc(utc_t t);

// ---- Israel (Asia/Jerusalem) ----
// Rule in force since 2013 (Israel Time Determination Law, 2013 amendment):
//   IDT (UTC+3) starts on the Friday before the last Sunday of March at
//   02:00 local standard time (= 00:00 UTC);
//   IST (UTC+2) resumes on the last Sunday of October at 02:00 local
//   daylight time (= 23:00 UTC on the preceding Saturday).
// If Israeli law changes, update this function and rebuild.
int israelOffsetSeconds(utc_t t);
bool israelIsDst(utc_t t);
CivilTime toIsrael(utc_t t);
// UTC instants of the DST transitions for a given year (for tests / diagnostics).
utc_t israelDstStartUtc(int year);
utc_t israelDstEndUtc(int year);

// ---- Parsing of API date/time strings ----
// "2026-10-25" -> true and fields.
bool parseIsoDate(const char* s, int& y, int& m, int& d);
// "13:00:00Z", "13:00:00", "13:00Z", "13:00:00.000Z", "13:00:00+00:00" -> seconds of day (UTC).
// Returns false when missing or malformed or if a non-zero offset is given.
bool parseIsoTimeUtc(const char* s, int& secondsOfDay);
// Combines both; hasTime=false when the time string is absent/invalid
// (result is then 00:00 UTC of that date).
bool parseApiDateTime(const char* date, const char* time, utc_t& out, bool& hasTime);

// ---- Formatting (English, compact, for the 320x240 display) ----
const char* weekdayShort(int wd);   // "Sun"
const char* monthShort(int m);      // "Oct"
// "Sun 26 Oct"
void formatDayDate(const CivilTime& c, char* out, size_t n);
// "26 Oct"
void formatShortDate(const CivilTime& c, char* out, size_t n);
// "14:05"
void formatHm(const CivilTime& c, char* out, size_t n);

// Countdown split.
struct Countdown { int64_t days; int hours; int minutes; int seconds; bool past; };
Countdown splitCountdown(utc_t now, utc_t target);

// A clock is considered valid once it is past this instant (2025-01-01T00:00Z).
// The firmware additionally checks against its own build date.
static const utc_t kMinValidUtc = 1735689600LL;

}  // namespace gl
