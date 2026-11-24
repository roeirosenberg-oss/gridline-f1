// Gridline F1 — data shared between the network task and the UI
#pragma once
#include <Arduino.h>
#include "gl_errors.h"
#include "gl_model.h"

enum WifiState : uint8_t { WS_NO_CREDS = 0, WS_CONNECTING, WS_CONNECTED, WS_LOST };
enum Activity : uint8_t { ACT_IDLE = 0, ACT_CONNECTING, ACT_SYNCING, ACT_UPDATING, ACT_SELFTEST };

struct NetStatus {
  uint8_t wifi;            // WifiState
  uint8_t activity;        // Activity
  uint8_t clockValid;
  uint8_t lastError;       // most recent FetchError (0 = last attempt OK)
  int8_t rssi;
  uint8_t _pad[3];
  int64_t lastSuccessUtc;  // last successful API update
  uint32_t requests;
  uint32_t failures;
  char ip[16];
  char ssid[33];
  char lastErrorDetail[48];
  char selfTest[96];       // result of the TLS self-test ("" = not run)
};

struct AppData {
  gl::Calendar calendar;
  gl::Calendar calendarNext;
  gl::DriverStandings drivers;
  gl::ConstructorStandings teams;
  gl::RaceResults results;
  gl::DatasetMeta meta[gl::DS_COUNT];
  uint32_t version[gl::DS_COUNT];  // bumped whenever a dataset changes
  NetStatus net;
  uint32_t netVersion;             // bumped whenever net status changes
};

extern AppData g_data;

void appStateInit();
void dataLock();
void dataUnlock();

struct DataLock {
  DataLock() { dataLock(); }
  ~DataLock() { dataUnlock(); }
};

// Current UTC time in seconds, or 0 when the clock is not yet valid.
int64_t nowUtc();
bool clockIsValid();
int64_t buildUtc();
