// Gridline F1 — minimal, bounded HTTP/1.1 response reader (pure C++)
//
// The ESP32 Arduino HTTPClient does not de-chunk responses exposed through
// getStream() and does not bound body sizes, so the firmware uses this small
// reader on top of a raw TLS byte source instead. It is transport agnostic
// and fully unit-tested on the host.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gl_errors.h"

namespace gl {

class ByteSource {
 public:
  virtual ~ByteSource() {}
  // >0: bytes read; 0: nothing available right now; -1: closed / EOF
  virtual int readSome(uint8_t* buf, size_t n) = 0;
  virtual uint32_t nowMs() = 0;
  virtual void idle() {}  // yield / short delay while waiting for data
};

// Small read-ahead buffer with idle timeout handling.
class BufferedSource {
 public:
  BufferedSource(ByteSource& src, uint32_t idleTimeoutMs, uint32_t totalTimeoutMs);
  // Returns 0..255, or -1 on EOF, or -2 on timeout.
  int get();
  uint32_t bytesConsumed() const { return consumed_; }
  bool timedOut() const { return timedOut_; }

 private:
  ByteSource& src_;
  uint8_t buf_[256];
  size_t len_ = 0, pos_ = 0;
  uint32_t idleTimeoutMs_, totalTimeoutMs_, startMs_;
  uint32_t consumed_ = 0;
  bool eof_ = false, timedOut_ = false;
};

struct HttpHead {
  int status = 0;
  int64_t contentLength = -1;   // -1 = not provided
  bool chunked = false;
  int32_t retryAfterSec = -1;   // -1 = not provided
  char contentType[48] = {0};
};

// Reads the status line and headers. Skips interim 1xx responses.
FetchError readHttpHead(BufferedSource& in, HttpHead& head);

// Decoded body reader with byte limit. Provides the custom-reader interface
// ArduinoJson expects (read / readBytes).
class BodyReader {
 public:
  BodyReader(BufferedSource& in, const HttpHead& head, size_t maxBytes);
  int read();
  size_t readBytes(char* buffer, size_t length);
  // Consume the rest of the body and verify that the transfer completed
  // correctly. Returns FE_OK or the first transfer error.
  FetchError finish();
  FetchError error() const { return err_; }
  size_t bytesDelivered() const { return delivered_; }

 private:
  bool nextChunkHeader();
  BufferedSource& in_;
  bool chunked_;
  int64_t remainingInChunk_;   // chunked: bytes left in current chunk
  int64_t remainingTotal_;     // content-length mode: bytes left (-1 unknown)
  size_t maxBytes_;
  size_t delivered_ = 0;
  bool done_ = false;          // body fully consumed (EOF seen properly)
  FetchError err_ = FE_OK;
};

// Map an HTTP status code to a FetchError (FE_OK for 200..299).
FetchError classifyHttpStatus(int status);

// Builds a GET request into out. Returns length or -1 when it does not fit.
int buildGetRequest(char* out, size_t n, const char* host, const char* path, const char* userAgent);

}  // namespace gl
