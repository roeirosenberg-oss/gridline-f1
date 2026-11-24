#include "gl_http.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace gl {

// ---------------- errors ----------------
const char* fetchErrorName(uint8_t e) {
  static const char* names[] = {
      "OK",          "NO_WIFI",      "CLOCK_INVALID", "DNS_FAIL",     "CONNECT_FAIL", "TLS_FAIL",
      "TIMEOUT",     "HTTP_429",     "HTTP_404",      "HTTP_4XX",     "HTTP_5XX",     "HTTP_OTHER",
      "BAD_HEADER",  "TOO_LARGE",    "TRUNCATED",     "BAD_CHUNK",    "JSON_INVALID", "JSON_NOMEM",
      "JSON_DEPTH",  "SCHEMA",       "EMPTY",         "STORAGE"};
  return e < FE_COUNT ? names[e] : "UNKNOWN";
}

const char* fetchErrorUi(uint8_t e) {
  switch (e) {
    case FE_OK: return "Updated";
    case FE_NO_WIFI: return "Offline";
    case FE_CLOCK: return "Syncing clock";
    case FE_DNS: return "DNS error";
    case FE_CONNECT: return "No connection";
    case FE_TLS: return "TLS error";
    case FE_TIMEOUT: return "Timeout";
    case FE_HTTP_429: return "Rate limited";
    case FE_HTTP_404:
    case FE_HTTP_4XX:
    case FE_HTTP_5XX:
    case FE_HTTP_OTHER: return "API error";
    case FE_EMPTY: return "No data yet";
    case FE_STORAGE: return "Storage error";
    default: return "Data error";
  }
}

bool isNetworkLevelError(uint8_t e) {
  switch (e) {
    case FE_NO_WIFI: case FE_CLOCK: case FE_DNS: case FE_CONNECT:
    case FE_TLS: case FE_TIMEOUT: case FE_HTTP_429: case FE_HTTP_5XX:
      return true;
    default:
      return false;
  }
}

// ---------------- BufferedSource ----------------
BufferedSource::BufferedSource(ByteSource& src, uint32_t idleTimeoutMs, uint32_t totalTimeoutMs)
    : src_(src), idleTimeoutMs_(idleTimeoutMs), totalTimeoutMs_(totalTimeoutMs) {
  startMs_ = src_.nowMs();
}

int BufferedSource::get() {
  if (pos_ < len_) { consumed_++; return buf_[pos_++]; }
  if (eof_) return -1;
  if (timedOut_) return -2;
  uint32_t waitStart = src_.nowMs();
  for (;;) {
    int r = src_.readSome(buf_, sizeof(buf_));
    if (r > 0) {
      len_ = (size_t)r; pos_ = 1; consumed_++;
      return buf_[0];
    }
    if (r < 0) { eof_ = true; return -1; }
    uint32_t now = src_.nowMs();
    if ((uint32_t)(now - waitStart) >= idleTimeoutMs_ || (uint32_t)(now - startMs_) >= totalTimeoutMs_) {
      timedOut_ = true;
      return -2;
    }
    src_.idle();
  }
}

// ---------------- header parsing ----------------
static bool ieq(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++; b++;
  }
  return *a == *b;
}

static const char* trimLeft(const char* s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

static void trimRight(char* s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

// Reads one line (without CR/LF). Returns 1 ok, -1 eof, -2 timeout, -3 too long.
static int readLine(BufferedSource& in, char* line, size_t cap) {
  size_t n = 0;
  bool overflow = false;
  for (;;) {
    int c = in.get();
    if (c == -1) return -1;
    if (c == -2) return -2;
    if (c == '\n') break;
    if (n + 1 < cap) line[n++] = (char)c; else overflow = true;
  }
  line[n] = 0;
  if (n && line[n - 1] == '\r') line[--n] = 0;
  return overflow ? -3 : 1;
}

static bool containsTokenCI(const char* value, const char* token) {
  // comma separated list, case insensitive token match
  size_t tl = strlen(token);
  const char* p = value;
  while (*p) {
    p = trimLeft(p);
    const char* start = p;
    while (*p && *p != ',') p++;
    const char* end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    if ((size_t)(end - start) == tl) {
      bool same = true;
      for (size_t i = 0; i < tl; i++)
        if (tolower((unsigned char)start[i]) != tolower((unsigned char)token[i])) { same = false; break; }
      if (same) return true;
    }
    if (*p == ',') p++;
  }
  return false;
}

FetchError readHttpHead(BufferedSource& in, HttpHead& head) {
  char line[512];
  for (int attempt = 0; attempt < 4; attempt++) {
    head = HttpHead();
    int r = readLine(in, line, sizeof(line));
    if (r == -2) return FE_TIMEOUT;
    if (r == -1) return line[0] ? FE_TRUNCATED : FE_CONNECT;  // closed without a response
    if (r == -3) return FE_BAD_HEADER;
    // "HTTP/1.1 200 OK"
    if (strncmp(line, "HTTP/1.", 7) != 0 || !(line[7] == '0' || line[7] == '1') || line[8] != ' ')
      return FE_BAD_HEADER;
    const char* sp = line + 9;
    if (!isdigit((unsigned char)sp[0]) || !isdigit((unsigned char)sp[1]) || !isdigit((unsigned char)sp[2]))
      return FE_BAD_HEADER;
    if (sp[3] != ' ' && sp[3] != '\0') return FE_BAD_HEADER;
    head.status = (sp[0] - '0') * 100 + (sp[1] - '0') * 10 + (sp[2] - '0');

    bool badEncoding = false;
    int headerCount = 0;
    for (;;) {
      r = readLine(in, line, sizeof(line));
      if (r == -2) return FE_TIMEOUT;
      if (r == -1) return FE_TRUNCATED;
      if (r == -3) return FE_BAD_HEADER;
      if (line[0] == 0) break;  // end of headers
      if (++headerCount > 64) return FE_BAD_HEADER;
      char* colon = strchr(line, ':');
      if (!colon) return FE_BAD_HEADER;
      *colon = 0;
      char* value = (char*)trimLeft(colon + 1);
      trimRight(value);
      trimRight(line);
      if (ieq(line, "Content-Length")) {
        if (!*value) return FE_BAD_HEADER;
        int64_t v = 0;
        for (const char* p = value; *p; p++) {
          if (!isdigit((unsigned char)*p)) return FE_BAD_HEADER;
          v = v * 10 + (*p - '0');
          if (v > 100000000LL) return FE_BAD_HEADER;
        }
        if (head.contentLength >= 0 && head.contentLength != v) return FE_BAD_HEADER;
        head.contentLength = v;
      } else if (ieq(line, "Transfer-Encoding")) {
        if (containsTokenCI(value, "chunked")) head.chunked = true;
        // anything other than plain "chunked" (e.g. gzip) cannot be decoded
        if (!ieq(value, "chunked") && !ieq(value, "identity")) badEncoding = true;
      } else if (ieq(line, "Content-Encoding")) {
        if (*value && !ieq(value, "identity")) badEncoding = true;
      } else if (ieq(line, "Retry-After")) {
        int32_t v = 0;
        bool ok = *value != 0;
        for (const char* p = value; *p; p++) {
          if (!isdigit((unsigned char)*p)) { ok = false; break; }
          v = v * 10 + (*p - '0');
          if (v > 86400) { v = 86400; }
        }
        head.retryAfterSec = ok ? v : -1;  // HTTP-date form is ignored
      } else if (ieq(line, "Content-Type")) {
        strncpy(head.contentType, value, sizeof(head.contentType) - 1);
        head.contentType[sizeof(head.contentType) - 1] = 0;
      }
    }
    if (head.status >= 100 && head.status < 200) continue;  // interim response
    if (badEncoding) return FE_BAD_HEADER;
    if (head.chunked) head.contentLength = -1;  // RFC 7230 3.3.3
    return FE_OK;
  }
  return FE_BAD_HEADER;
}

FetchError classifyHttpStatus(int status) {
  if (status >= 200 && status < 300) return FE_OK;
  if (status == 429) return FE_HTTP_429;
  if (status == 404) return FE_HTTP_404;
  if (status >= 400 && status < 500) return FE_HTTP_4XX;
  if (status >= 500 && status < 600) return FE_HTTP_5XX;
  return FE_HTTP_OTHER;
}

// ---------------- BodyReader ----------------
BodyReader::BodyReader(BufferedSource& in, const HttpHead& head, size_t maxBytes)
    : in_(in), chunked_(head.chunked), remainingInChunk_(0),
      remainingTotal_(head.chunked ? -1 : head.contentLength), maxBytes_(maxBytes) {
  if (!chunked_ && head.contentLength >= 0 && (uint64_t)head.contentLength > maxBytes_) err_ = FE_TOO_LARGE;
  if (!chunked_ && head.contentLength == 0) done_ = true;
}

bool BodyReader::nextChunkHeader() {
  // called when remainingInChunk_ == 0 (at the beginning, or after CRLF of the previous chunk)
  char line[80];
  int r = readLine(in_, line, sizeof(line));
  if (r == -2) { err_ = FE_TIMEOUT; return false; }
  if (r == -1) { err_ = FE_TRUNCATED; return false; }
  if (r == -3) { err_ = FE_BAD_CHUNK; return false; }
  int64_t size = 0;
  int digits = 0;
  for (const char* p = line; *p && *p != ';'; p++) {
    char c = *p;
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else if (c == ' ' || c == '\t') continue;
    else { err_ = FE_BAD_CHUNK; return false; }
    size = size * 16 + v;
    if (++digits > 8) { err_ = FE_BAD_CHUNK; return false; }
  }
  if (digits == 0) { err_ = FE_BAD_CHUNK; return false; }
  if (size == 0) {
    // last chunk: consume optional trailers until an empty line
    for (int i = 0; i < 32; i++) {
      r = readLine(in_, line, sizeof(line));
      if (r == -2) { err_ = FE_TIMEOUT; return false; }
      if (r == -1) { err_ = FE_TRUNCATED; return false; }
      if (r == -3) { err_ = FE_BAD_CHUNK; return false; }
      if (line[0] == 0) { done_ = true; return false; }
    }
    err_ = FE_BAD_CHUNK;
    return false;
  }
  remainingInChunk_ = size;
  return true;
}

int BodyReader::read() {
  if (err_ != FE_OK || done_) return -1;
  if (delivered_ >= maxBytes_) { err_ = FE_TOO_LARGE; return -1; }
  if (chunked_) {
    if (remainingInChunk_ == 0) {
      if (!nextChunkHeader()) return -1;
    }
    int c = in_.get();
    if (c == -2) { err_ = FE_TIMEOUT; return -1; }
    if (c < 0) { err_ = FE_TRUNCATED; return -1; }
    remainingInChunk_--;
    if (remainingInChunk_ == 0) {
      // expect CRLF after chunk data
      int a = in_.get();
      if (a == '\r') a = in_.get();
      if (a == -2) err_ = FE_TIMEOUT;
      else if (a == -1) err_ = FE_TRUNCATED;
      else if (a != '\n') err_ = FE_BAD_CHUNK;
      // the byte itself is still valid; the error surfaces on the next read
    }
    delivered_++;
    return c;
  }
  if (remainingTotal_ == 0) { done_ = true; return -1; }
  int c = in_.get();
  if (c == -2) { err_ = FE_TIMEOUT; return -1; }
  if (c < 0) {
    if (remainingTotal_ > 0) err_ = FE_TRUNCATED;
    else done_ = true;  // read-until-close body ended
    return -1;
  }
  if (remainingTotal_ > 0) remainingTotal_--;
  delivered_++;
  return c;
}

size_t BodyReader::readBytes(char* buffer, size_t length) {
  size_t n = 0;
  while (n < length) {
    int c = read();
    if (c < 0) break;
    buffer[n++] = (char)c;
  }
  return n;
}

FetchError BodyReader::finish() {
  // Drain whatever is left (still bounded by maxBytes_) to validate the framing.
  while (err_ == FE_OK && !done_) {
    if (read() < 0) break;
  }
  return err_;
}

int buildGetRequest(char* out, size_t n, const char* host, const char* path, const char* userAgent) {
  int len = snprintf(out, n,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: %s\r\n"
                     "Accept: application/json\r\n"
                     "Accept-Encoding: identity\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     path, host, userAgent);
  if (len < 0 || (size_t)len >= n) return -1;
  return len;
}

}  // namespace gl
