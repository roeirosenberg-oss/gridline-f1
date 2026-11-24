// Gridline F1 — thread-safe serial logging (115200 baud)
// Never pass Wi-Fi passwords or other secrets to these functions.
#pragma once
#include <Arduino.h>

void logInit();
void logf(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void logMemory(const char* where);

#define LOGI(tag, ...) logf(tag, __VA_ARGS__)
