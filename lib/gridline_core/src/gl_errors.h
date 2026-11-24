// Gridline F1 — fetch error classification
#pragma once
#include <stdint.h>

namespace gl {

enum FetchError : uint8_t {
  FE_OK = 0,
  FE_NO_WIFI,          // station not connected
  FE_CLOCK,            // clock not synchronised (TLS refuses to start)
  FE_DNS,              // host name could not be resolved
  FE_CONNECT,          // TCP connection failed / refused / timed out
  FE_TLS,              // TLS handshake or certificate verification failed
  FE_TIMEOUT,          // no data within the response / idle timeout
  FE_HTTP_429,         // rate limited
  FE_HTTP_404,
  FE_HTTP_4XX,
  FE_HTTP_5XX,
  FE_HTTP_OTHER,       // 1xx/3xx or unexpected
  FE_BAD_HEADER,       // malformed status line / headers
  FE_TOO_LARGE,        // body exceeds the configured limit
  FE_TRUNCATED,        // connection closed before the body was complete
  FE_BAD_CHUNK,        // malformed chunked transfer encoding
  FE_JSON_INVALID,     // syntax error / incomplete JSON
  FE_JSON_NOMEM,       // JSON document exceeds the memory budget
  FE_JSON_DEPTH,       // nesting too deep
  FE_SCHEMA,           // valid JSON, unexpected structure
  FE_EMPTY,            // valid response but contains no entries
  FE_STORAGE,          // could not persist (not used to reject data)
  FE_COUNT
};

const char* fetchErrorName(uint8_t e);   // stable identifier for serial logs
const char* fetchErrorUi(uint8_t e);     // very short label for the display

// Errors that indicate the network path itself is failing; the scheduler
// pauses all datasets when one of these occurs.
bool isNetworkLevelError(uint8_t e);

}  // namespace gl
