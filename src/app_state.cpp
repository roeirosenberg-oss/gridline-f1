#include "app_state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sys/time.h>
#include "gl_time.h"

AppData g_data;
static SemaphoreHandle_t s_mutex = nullptr;
static int64_t s_buildUtc = 0;

static int64_t parseBuildDate() {
  // __DATE__ = "Oct 24 2026", __TIME__ = "16:50:00" (build machine local time)
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* d = __DATE__;
  char mon[4] = {d[0], d[1], d[2], 0};
  const char* p = strstr(months, mon);
  int m = p ? (int)((p - months) / 3) + 1 : 1;
  int day = atoi(d + 4);
  int year = atoi(d + 7);
  return gl::makeUtc(year, m, day, 0, 0, 0);
}

void appStateInit() {
  memset(&g_data, 0, sizeof(g_data));
  s_mutex = xSemaphoreCreateRecursiveMutex();
  s_buildUtc = parseBuildDate();
}

void dataLock() { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
void dataUnlock() { xSemaphoreGiveRecursive(s_mutex); }

int64_t buildUtc() { return s_buildUtc; }

bool clockIsValid() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  int64_t t = (int64_t)tv.tv_sec;
  int64_t minValid = s_buildUtc - 2 * 86400;
  if (minValid < gl::kMinValidUtc) minValid = gl::kMinValidUtc;
  return t >= minValid;
}

int64_t nowUtc() {
  if (!clockIsValid()) return 0;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (int64_t)tv.tv_sec;
}
