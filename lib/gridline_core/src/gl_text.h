// Gridline F1 — small string helpers (pure C++)
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gl {

// Copies at most n-1 bytes and always terminates. Never splits a UTF-8
// multi-byte sequence. Returns true when the input fitted completely.
bool copyUtf8(char* dst, size_t n, const char* src);

// Number of Unicode code points in a UTF-8 string.
size_t utf8Length(const char* s);

// Replaces " Grand Prix" with " GP" (in place, into dst).
void shortenGrandPrix(char* dst, size_t n, const char* src);

// Uppercase ASCII copy (non-ASCII bytes copied unchanged).
void upperAscii(char* dst, size_t n, const char* src);

// Builds a three-letter code from a family name when the API has no code.
void fallbackDriverCode(char out[4], const char* family);

// Parses a decimal points string ("412", "12.5") into tenths. Returns -1 on error.
int32_t pointsToTenths(const char* s);

bool startsWith(const char* s, const char* prefix);

}  // namespace gl
