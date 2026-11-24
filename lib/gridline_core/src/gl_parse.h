// Gridline F1 — Jolpica (Ergast-compatible) JSON parsers
//
// Each parser streams the response through an ArduinoJson filter so only
// the fields the display needs are kept in RAM, enforces a hard memory
// budget, validates the structure and writes into a caller-provided output
// struct. The output must only be committed by the caller when FE_OK is
// returned; on any error the previous (cached) data stays untouched.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gl_errors.h"
#include "gl_model.h"

namespace gl {

// Abstract character source compatible with ArduinoJson's custom reader
// protocol. transportError() lets the parser report the root cause
// (timeout, truncation, size limit) instead of a generic JSON error.
class JsonSource {
 public:
  virtual ~JsonSource() {}
  virtual int read() = 0;
  virtual size_t readBytes(char* buffer, size_t length) = 0;
  virtual FetchError transportError() const { return FE_OK; }
};

// In-memory source (tests, cache migration).
class StringJsonSource : public JsonSource {
 public:
  StringJsonSource(const char* data, size_t len) : p_(data), end_(data + len) {}
  int read() override { return p_ < end_ ? (unsigned char)*p_++ : -1; }
  size_t readBytes(char* b, size_t n) override {
    size_t k = 0;
    while (k < n && p_ < end_) b[k++] = *p_++;
    return k;
  }

 private:
  const char* p_;
  const char* end_;
};

struct ParseStats {
  size_t peakJsonBytes = 0;   // peak heap used by the JSON document
  int entriesSeen = 0;        // entries present in the response
  int entriesSkipped = 0;     // entries rejected by validation
  bool truncated = false;     // more entries than the model can hold
  char detail[48] = {0};      // short reason for SCHEMA / JSON errors
};

// Memory budget for one JSON document (bytes). Responses that need more are
// rejected with FE_JSON_NOMEM instead of exhausting the heap.
static const size_t kJsonBudgetBytes = 48 * 1024;

FetchError parseCalendar(JsonSource& in, uint16_t expectedSeason, Calendar& out, ParseStats* stats = nullptr);
FetchError parseDriverStandings(JsonSource& in, uint16_t expectedSeason, DriverStandings& out,
                                ParseStats* stats = nullptr);
FetchError parseConstructorStandings(JsonSource& in, uint16_t expectedSeason, ConstructorStandings& out,
                                     ParseStats* stats = nullptr);
// expectedSeason == 0 accepts any season (used for the ".../last/results" fallback).
FetchError parseRaceResults(JsonSource& in, uint16_t expectedSeason, RaceResults& out, ParseStats* stats = nullptr);

uint8_t classifyPositionText(const char* positionText);

}  // namespace gl
