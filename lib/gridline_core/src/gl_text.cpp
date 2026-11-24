#include "gl_text.h"
#include <string.h>

namespace gl {

static size_t utf8SeqLen(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;  // stray continuation / invalid byte: treat as single
}

bool copyUtf8(char* dst, size_t n, const char* src) {
  if (!dst || n == 0) return false;
  dst[0] = '\0';
  if (!src) return true;
  size_t o = 0;
  const unsigned char* p = (const unsigned char*)src;
  while (*p) {
    size_t l = utf8SeqLen(*p);
    // ensure the whole sequence exists in the source
    for (size_t k = 1; k < l; k++) {
      if (p[k] == 0) { dst[o] = '\0'; return false; }
    }
    if (o + l >= n) { dst[o] = '\0'; return false; }
    // control characters are replaced by a space
    if (l == 1 && *p < 0x20) {
      dst[o++] = ' ';
    } else {
      memcpy(dst + o, p, l);
      o += l;
    }
    p += l;
  }
  dst[o] = '\0';
  return true;
}

size_t utf8Length(const char* s) {
  size_t n = 0;
  if (!s) return 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    if ((*p & 0xC0) != 0x80) n++;
  return n;
}

void shortenGrandPrix(char* dst, size_t n, const char* src) {
  if (!dst || n == 0) return;
  dst[0] = '\0';
  if (!src) return;
  const char* hit = strstr(src, " Grand Prix");
  if (!hit) { copyUtf8(dst, n, src); return; }
  size_t pre = (size_t)(hit - src);
  char tmp[96];
  if (pre >= sizeof(tmp) - 4) { copyUtf8(dst, n, src); return; }
  memcpy(tmp, src, pre);
  memcpy(tmp + pre, " GP", 3);
  tmp[pre + 3] = '\0';
  const char* rest = hit + strlen(" Grand Prix");
  size_t used = pre + 3;
  size_t restLen = strlen(rest);
  if (used + restLen < sizeof(tmp)) memcpy(tmp + used, rest, restLen + 1);
  copyUtf8(dst, n, tmp);
}

void upperAscii(char* dst, size_t n, const char* src) {
  if (!dst || n == 0) return;
  size_t i = 0;
  if (src) {
    for (; src[i] && i + 1 < n; i++) {
      char c = src[i];
      dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
  }
  dst[i] = '\0';
}

void fallbackDriverCode(char out[4], const char* family) {
  int k = 0;
  if (family) {
    for (const char* p = family; *p && k < 3; p++) {
      char c = *p;
      if (c >= 'a' && c <= 'z') c = (char)(c - 32);
      if (c >= 'A' && c <= 'Z') out[k++] = c;
    }
  }
  while (k < 3) out[k++] = '?';
  out[3] = '\0';
}

int32_t pointsToTenths(const char* s) {
  if (!s || !*s) return -1;
  int32_t whole = 0;
  int32_t tenth = 0;
  const char* p = s;
  bool any = false;
  while (*p >= '0' && *p <= '9') {
    whole = whole * 10 + (*p - '0');
    if (whole > 100000) return -1;
    p++;
    any = true;
  }
  if (*p == '.') {
    p++;
    if (*p >= '0' && *p <= '9') { tenth = *p - '0'; p++; any = true; }
    while (*p >= '0' && *p <= '9') p++;
  }
  if (*p != '\0' || !any) return -1;
  return whole * 10 + tenth;
}

bool startsWith(const char* s, const char* prefix) {
  if (!s || !prefix) return false;
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

}  // namespace gl
