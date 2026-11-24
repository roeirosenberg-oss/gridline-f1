#include "log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>

static SemaphoreHandle_t s_logMutex = nullptr;

void logInit() {
  Serial.begin(115200);
  s_logMutex = xSemaphoreCreateMutex();
}

void logf(const char* tag, const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  uint32_t ms = millis();
  if (s_logMutex) xSemaphoreTake(s_logMutex, pdMS_TO_TICKS(200));
  Serial.printf("[%6lu.%03lu] %-5s %s\n", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000), tag, buf);
  if (s_logMutex) xSemaphoreGive(s_logMutex);
}

void logMemory(const char* where) {
  logf("MEM", "%s: heap free %u, min %u, largest block %u, stack hw %u", where, (unsigned)ESP.getFreeHeap(),
       (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
       (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}
