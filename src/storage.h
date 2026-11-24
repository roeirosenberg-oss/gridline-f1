// Gridline F1 — LittleFS cache of the last good API data
#pragma once
#include <Arduino.h>

bool storageBegin();
// Atomic write: data goes to a temporary file which then replaces the old
// file in one rename, so an interrupted write never corrupts the cache.
bool cacheSave(uint8_t dataset, const void* payload, size_t size, int64_t fetchedUtc);
bool cacheLoad(uint8_t dataset, void* payload, size_t size, int64_t& fetchedUtc);
void storageInfo(size_t& used, size_t& total);
