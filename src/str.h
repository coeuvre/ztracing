#ifndef ZTRACING_SRC_STR_H_
#define ZTRACING_SRC_STR_H_

#include <stddef.h>
#include <string.h>

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

#endif  // ZTRACING_SRC_STR_H_
