#ifndef ZTRACING_SRC_STRING_H_
#define ZTRACING_SRC_STRING_H_

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct string {
  const char* ptr;
  size_t len;
} string_t;

#define string_lit(lit) ((string_t){(lit ""), sizeof(lit "") - 1})

inline string_t string_from_parts(const char* ptr, size_t len) {
  return (string_t){ptr, len};
}

inline string_t string_from_cstr(const char* str) {
  return (string_t){str, str ? strlen(str) : 0};
}

inline bool string_eq(string_t a, string_t b) {
  bool result = false;
  if (a.len == b.len) {
    result = memcmp(a.ptr, b.ptr, a.len) == 0;
  }
  return result;
}

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_STRING_H_
