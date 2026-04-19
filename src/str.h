#ifndef ZTRACING_SRC_STR_H_
#define ZTRACING_SRC_STR_H_

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct Str {
  const char* buf;
  size_t len;
};

// Create a Str from a string literal.
#define STR(literal) Str{(literal), sizeof(literal) - 1}

// Create a Str from a null-terminated string.
inline Str str_from_cstr(const char* s) {
  if (s == nullptr) return {nullptr, 0};
  return {s, strlen(s)};
}

// Check if two strings are equal.
inline bool str_eq(Str a, Str b) {
  if (a.len != b.len) return false;
  if (a.len == 0) return true;
  return memcmp(a.buf, b.buf, a.len) == 0;
}

// Compare two strings alphabetically. Returns -1 if a < b, 1 if a > b, 0 if equal.
inline int str_compare(Str a, Str b) {
  size_t min_len = a.len < b.len ? a.len : b.len;
  if (min_len > 0) {
    int res = memcmp(a.buf, b.buf, min_len);
    if (res != 0) return res < 0 ? -1 : 1;
  }
  if (a.len < b.len) return -1;
  if (a.len > b.len) return 1;
  return 0;
}

// Compare two strings alphabetically, ignoring case.
inline int str_compare_ignore_case(Str a, Str b) {
  size_t min_len = a.len < b.len ? a.len : b.len;
  for (size_t i = 0; i < min_len; i++) {
    char ca = a.buf[i];
    char cb = b.buf[i];
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
    if (ca < cb) return -1;
    if (ca > cb) return 1;
  }
  if (a.len < b.len) return -1;
  if (a.len > b.len) return 1;
  return 0;
}

// Simple string to number conversions.
// These handle up to 63 characters and require a temporary null-terminated
// buffer.
inline int64_t str_to_int64(Str s) {
  char tmp[64];
  size_t len = s.len < 63 ? s.len : 63;
  memcpy(tmp, s.buf, len);
  tmp[len] = '\0';
  return atoll(tmp);
}

inline int32_t str_to_int32(Str s) { return (int32_t)str_to_int64(s); }

inline double str_to_double(Str s) {
  // Simple check for common integer case to avoid slow atof/__floatscan
  bool is_int = true;
  for (size_t i = 0; i < s.len; i++) {
    if (s.buf[i] < '0' || s.buf[i] > '9') {
      if (i == 0 && s.buf[i] == '-') continue;
      is_int = false;
      break;
    }
  }
  if (is_int && s.len > 0 && (s.len > 1 || s.buf[0] != '-')) {
    return (double)str_to_int64(s);
  }

  char tmp[64];
  size_t len = s.len < 63 ? s.len : 63;
  memcpy(tmp, s.buf, len);
  tmp[len] = '\0';
  return atof(tmp);
}

#endif  // ZTRACING_SRC_STR_H_
