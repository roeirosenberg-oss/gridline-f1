#include "gl_time.h"
#include <stdio.h>
#include <string.h>

namespace gl {

int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

void civilFromDays(int64_t z, int& y, int& m, int& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yy + (m <= 2));
}

int weekdayFromDays(int64_t z) {
  // 1970-01-01 was a Thursday (4)
  int64_t w = (z + 4) % 7;
  if (w < 0) w += 7;
  return (int)w;
}

utc_t makeUtc(int y, int m, int d, int hh, int mm, int ss) {
  return daysFromCivil(y, m, d) * 86400LL + hh * 3600LL + mm * 60LL + ss;
}

CivilTime toCivilUtc(utc_t t) {
  CivilTime c;
  int64_t days = t / 86400;
  int64_t rem = t % 86400;
  if (rem < 0) { rem += 86400; days -= 1; }
  civilFromDays(days, c.year, c.month, c.day);
  c.hour = (int)(rem / 3600);
  c.minute = (int)((rem % 3600) / 60);
  c.second = (int)(rem % 60);
  c.weekday = weekdayFromDays(days);
  return c;
}

static int lastSundayOfMonth(int y, int m) {
  // day-of-month of the last Sunday
  int nextY = m == 12 ? y + 1 : y;
  int nextM = m == 12 ? 1 : m + 1;
  int64_t lastDay = daysFromCivil(nextY, nextM, 1) - 1;
  int wd = weekdayFromDays(lastDay);
  int yy, mm, dd;
  civilFromDays(lastDay - wd, yy, mm, dd);
  return dd;
}

utc_t israelDstStartUtc(int year) {
  // Friday before the last Sunday of March, 02:00 IST = 00:00 UTC
  int sun = lastSundayOfMonth(year, 3);
  return makeUtc(year, 3, sun - 2, 0, 0, 0);
}

utc_t israelDstEndUtc(int year) {
  // Last Sunday of October, 02:00 IDT = 23:00 UTC on Saturday
  int sun = lastSundayOfMonth(year, 10);
  return makeUtc(year, 10, sun, 0, 0, 0) - 3600;
}

bool israelIsDst(utc_t t) {
  CivilTime u = toCivilUtc(t);
  int y = u.year;
  return t >= israelDstStartUtc(y) && t < israelDstEndUtc(y);
}

int israelOffsetSeconds(utc_t t) { return israelIsDst(t) ? 3 * 3600 : 2 * 3600; }

CivilTime toIsrael(utc_t t) { return toCivilUtc(t + israelOffsetSeconds(t)); }

static bool readDigits(const char*& p, int count, int& out) {
  out = 0;
  for (int i = 0; i < count; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    out = out * 10 + (p[i] - '0');
  }
  p += count;
  return true;
}

bool parseIsoDate(const char* s, int& y, int& m, int& d) {
  if (!s) return false;
  const char* p = s;
  if (!readDigits(p, 4, y)) return false;
  if (*p++ != '-') return false;
  if (!readDigits(p, 2, m)) return false;
  if (*p++ != '-') return false;
  if (!readDigits(p, 2, d)) return false;
  if (*p != '\0') return false;
  if (y < 1950 || y > 2200 || m < 1 || m > 12 || d < 1) return false;
  static const int mdays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (d > mdays[m - 1]) return false;
  if (m == 2 && d == 29) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    if (!leap) return false;
  }
  return true;
}

bool parseIsoTimeUtc(const char* s, int& secondsOfDay) {
  if (!s || !*s) return false;
  const char* p = s;
  int hh, mm, ss = 0;
  if (!readDigits(p, 2, hh)) return false;
  if (*p++ != ':') return false;
  if (!readDigits(p, 2, mm)) return false;
  if (*p == ':') {
    p++;
    if (!readDigits(p, 2, ss)) return false;
    if (*p == '.') {  // fractional seconds: skip digits
      p++;
      if (*p < '0' || *p > '9') return false;
      while (*p >= '0' && *p <= '9') p++;
    }
  }
  if (*p == 'Z') {
    p++;
  } else if (*p == '+' || *p == '-') {
    // Only an explicit zero offset is accepted; the API documents UTC.
    if (strcmp(p + 1, "00:00") != 0 && strcmp(p + 1, "0000") != 0) return false;
    p += strlen(p);
  }
  if (*p != '\0') return false;
  if (hh > 23 || mm > 59 || ss > 60) return false;
  if (ss == 60) ss = 59;  // leap second: clamp
  secondsOfDay = hh * 3600 + mm * 60 + ss;
  return true;
}

bool parseApiDateTime(const char* date, const char* time, utc_t& out, bool& hasTime) {
  int y, m, d;
  if (!parseIsoDate(date, y, m, d)) return false;
  int sod = 0;
  hasTime = parseIsoTimeUtc(time, sod);
  out = makeUtc(y, m, d, 0, 0, 0) + (hasTime ? sod : 0);
  return true;
}

const char* weekdayShort(int wd) {
  static const char* n[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return (wd >= 0 && wd < 7) ? n[wd] : "???";
}

const char* monthShort(int m) {
  static const char* n[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return (m >= 1 && m <= 12) ? n[m - 1] : "???";
}

void formatDayDate(const CivilTime& c, char* out, size_t n) {
  snprintf(out, n, "%s %d %s", weekdayShort(c.weekday), c.day, monthShort(c.month));
}

void formatShortDate(const CivilTime& c, char* out, size_t n) {
  snprintf(out, n, "%d %s", c.day, monthShort(c.month));
}

void formatHm(const CivilTime& c, char* out, size_t n) {
  snprintf(out, n, "%02d:%02d", c.hour, c.minute);
}

Countdown splitCountdown(utc_t now, utc_t target) {
  Countdown cd;
  int64_t diff = target - now;
  cd.past = diff <= 0;
  if (diff < 0) diff = 0;
  cd.days = diff / 86400;
  diff %= 86400;
  cd.hours = (int)(diff / 3600);
  cd.minutes = (int)((diff % 3600) / 60);
  cd.seconds = (int)(diff % 60);
  return cd;
}

}  // namespace gl
