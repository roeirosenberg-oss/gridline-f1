// Gridline F1 — background network task (core 0)
#pragma once
#include <Arduino.h>

void netRestoreCache();      // load cached datasets into g_data (call before netStart)
void netStart();             // start Wi-Fi + update task
void netRequestRefresh();    // user asked for an immediate refresh (rate-limited)
void netRequestSelfTest();   // run the TLS verification self-test
uint32_t netTaskStackHighWater();
