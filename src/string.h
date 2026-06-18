#ifndef ZTRACING_SRC_STRING_H_
#define ZTRACING_SRC_STRING_H_

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "src/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}  // Close extern "C" temporarily for C++ std::string_view include
#include <string_view>
extern "C" {
#endif

// A string representation consisting of a character pointer and an explicit
// length.
//
// CRITICAL API SAFETY WARNING:
// 1. This string is NOT guaranteed to be null-terminated.
// 2. You MUST NEVER pass `ptr` directly to standard C library functions that
//    expect null-terminated strings (e.g., `strlen`, `strcmp`, `printf("%s")`,
//    `atof`, etc.) as this will cause buffer overreads or segmentation faults.
// 3. To print the string safely, always specify the length using precision
//    formatting: `printf("%.*s", (int)s.len, s.ptr)`.
// 4. If a null-terminated string is strictly required by an external API, you
//    must first copy the content to a null-terminated stack or heap buffer.
typedef struct string {
  const char* ptr;
  size_t len;

#ifdef __cplusplus
  constexpr string() : ptr(nullptr), len(0) {}
  constexpr string(std::string_view sv) : ptr(sv.data()), len(sv.size()) {}
  constexpr string(const char* str)
      : ptr(str), len(str ? std::string_view(str).size() : 0) {}
  explicit constexpr string(const char* p, size_t l) : ptr(p), len(l) {}

  constexpr operator std::string_view() const {
    return std::string_view(ptr, len);
  }

  constexpr string& operator=(std::string_view sv) {
    ptr = sv.data();
    len = sv.size();
    return *this;
  }

  constexpr string& operator=(const char* str) {
    ptr = str;
    len = str ? std::string_view(str).size() : 0;
    return *this;
  }

  constexpr const char* data() const { return ptr; }
  constexpr size_t size() const { return len; }
  constexpr bool empty() const { return len == 0; }

  constexpr bool operator==(std::string_view other) const {
    return std::string_view(ptr, len) == other;
  }
#endif
} string_t;

#ifdef __cplusplus
}  // Close extern "C" early so inline helpers have C++ linkage in C++ mode,
   // resolving Clang's -Wreturn-type-c-linkage warnings.
#endif

#ifdef __cplusplus
#define string_lit(lit) (string_t{(lit ""), sizeof(lit "") - 1})
#else
#define string_lit(lit) ((string_t){(lit ""), sizeof(lit "") - 1})
#endif

static inline string_t string_from_parts(const char* ptr, size_t len) {
#ifdef __cplusplus
  return string_t(ptr, len);
#else
  return (string_t){ptr, len};
#endif
}

static inline string_t string_from_cstr(const char* str) {
#ifdef __cplusplus
  return string_t(str, str ? strlen(str) : 0);
#else
  return (string_t){str, str ? strlen(str) : 0};
#endif
}

static inline bool string_eq(string_t a, string_t b) {
  bool result = false;
  if (a.len == b.len) {
    result = memcmp(a.ptr, b.ptr, a.len) == 0;
  }
  return result;
}

// Convert a string_t to a null-terminated C string using the provided
// allocator. The returned pointer must be freed by the caller using
// allocator_free().
static inline char* string_to_cstr(string_t s, allocator_t a) {
  char* str = (char*)allocator_alloc(a, s.len + 1);
  memcpy(str, s.ptr, s.len);
  str[s.len] = '\0';
  return str;
}

#endif  // ZTRACING_SRC_STRING_H_
