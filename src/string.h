#ifndef SRC_STRING_H
#define SRC_STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
#include <string_view>
#endif

#include "core/allocator.h"
#include "core/assert.h"

#ifdef __cplusplus
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
typedef struct string_view {
  const char* ptr;
  size_t len;

#ifdef __cplusplus
  constexpr string_view();
  constexpr string_view(std::string_view sv);
  constexpr string_view(const char* str);
  explicit constexpr string_view(const char* p, size_t l);

  constexpr operator std::string_view() const;

  constexpr string_view& operator=(std::string_view sv);
  constexpr string_view& operator=(const char* str);

  constexpr const char* data() const;
  constexpr size_t size() const;
  constexpr bool empty() const;

  constexpr bool operator==(std::string_view other) const;
#endif
} string_view_t;

// A growable, null-terminated string buffer.
// Zero-Is-Initialization (ZII) compatible.
typedef struct string {
  char* ptr;
  size_t len;
  size_t cap;
} string_t;

static inline string_view_t string_view_from_parts(const char* ptr,
                                                   size_t len) {
  string_view_t sv;
  sv.ptr = ptr;
  sv.len = len;
  return sv;
}

static inline string_view_t string_view_from_cstr(const char* str) {
  string_view_t sv;
  sv.ptr = str;
  sv.len = str ? strlen(str) : 0;
  return sv;
}

static inline bool string_view_eq(string_view_t a, string_view_t b) {
  bool result = false;
  if (a.len == b.len) {
    result = memcmp(a.ptr, b.ptr, a.len) == 0;
  }
  return result;
}

// Convert a string_view_t to a null-terminated C string using the provided
// allocator. The returned pointer must be freed by the caller using
// allocator_free().
static inline char* string_view_to_cstr(string_view_t s, allocator_t a) {
  char* str = (char*)allocator_alloc(a, s.len + 1);
  memcpy(str, s.ptr, s.len);
  str[s.len] = '\0';
  return str;
}

// Growable string functions

static inline void string_deinit(string_t* s, allocator_t a) {
  if (s->ptr != nullptr) {
    allocator_free(a, s->ptr, s->cap);
  }
  string_t empty = {};
  *s = empty;
}

static inline void string_clear(string_t* s) {
  s->len = 0;
  if (s->ptr != nullptr) {
    s->ptr[0] = '\0';
  }
}

static inline size_t string_calculate_new_capacity(size_t current_capacity,
                                                   size_t min_capacity) {
  size_t new_capacity = current_capacity == 0 ? 16 : current_capacity * 2;
  if (new_capacity < min_capacity) {
    new_capacity = min_capacity;
  }
  // Check for overflow
  if (new_capacity < current_capacity) {
    new_capacity = (size_t)-1;
  }
  return new_capacity;
}

static inline void string_reserve(string_t* s, size_t new_cap, allocator_t a) {
  if (new_cap > s->cap) {
    void* new_ptr = allocator_realloc(a, s->ptr, s->cap, new_cap);
    s->ptr = (char*)new_ptr;
    s->cap = new_cap;
  }
}

static inline void string_ensure_capacity(string_t* s, size_t min_capacity,
                                          allocator_t a) {
  if (min_capacity > s->cap) {
    size_t new_cap = string_calculate_new_capacity(s->cap, min_capacity);
    string_reserve(s, new_cap, a);
  }
}

static inline void string_append_view(string_t* s, string_view_t sv,
                                      allocator_t a) {
  if (sv.len > 0) {
    string_ensure_capacity(s, s->len + sv.len + 1, a);
    memcpy(s->ptr + s->len, sv.ptr, sv.len);
    s->len += sv.len;
    s->ptr[s->len] = '\0';
  }
}

static inline void string_append_cstr(string_t* s, const char* str,
                                      allocator_t a) {
  CHECK(str != nullptr);
  size_t len = strlen(str);
  if (len > 0) {
    string_ensure_capacity(s, s->len + len + 1, a);
    memcpy(s->ptr + s->len, str, len);
    s->len += len;
    s->ptr[s->len] = '\0';
  }
}

static inline void string_append_char(string_t* s, char c, allocator_t a) {
  string_ensure_capacity(s, s->len + 2, a);
  s->ptr[s->len] = c;
  s->len++;
  s->ptr[s->len] = '\0';
}

static inline string_view_t string_to_view(const string_t* s) {
  return string_view_from_parts(s->ptr, s->len);
}

#ifdef __cplusplus
}  // extern "C"

// C++ Compatibility Layer Definitions
inline constexpr string_view::string_view() : ptr(nullptr), len(0) {}
inline constexpr string_view::string_view(std::string_view sv)
    : ptr(sv.data()), len(sv.size()) {}
inline constexpr string_view::string_view(const char* str)
    : ptr(str), len(str ? std::string_view(str).size() : 0) {}
inline constexpr string_view::string_view(const char* p, size_t l)
    : ptr(p), len(l) {}

inline constexpr string_view::operator std::string_view() const {
  return std::string_view(ptr, len);
}

inline constexpr string_view& string_view::operator=(std::string_view sv) {
  ptr = sv.data();
  len = sv.size();
  return *this;
}

inline constexpr string_view& string_view::operator=(const char* str) {
  ptr = str;
  len = str ? std::string_view(str).size() : 0;
  return *this;
}

inline constexpr const char* string_view::data() const { return ptr; }
inline constexpr size_t string_view::size() const { return len; }
inline constexpr bool string_view::empty() const { return len == 0; }

inline constexpr bool string_view::operator==(std::string_view other) const {
  return std::string_view(ptr, len) == other;
}

#define SV(lit) (string_view_t{(lit ""), sizeof(lit "") - 1})
#else
#define SV(lit) ((string_view_t){(lit ""), sizeof(lit "") - 1})
#endif

#endif  // SRC_STRING_H
