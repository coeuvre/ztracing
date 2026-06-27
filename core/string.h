#ifndef CORE_STRING_H
#define CORE_STRING_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
#include <ostream>
#include <string_view>
#endif

// Forward declaration
typedef struct allocator allocator_t;

// ─── string_view: counted string slice (not null-terminated) ────────────────

typedef struct string_view {
  const char* ptr;  // pointer to string data
  size_t len;       // length excluding null terminator
#ifdef __cplusplus
  // Constructors
  constexpr string_view() : ptr(nullptr), len(0) {}
  constexpr string_view(std::string_view sv) : ptr(sv.data()), len(sv.size()) {}
  constexpr string_view(const char* str) : ptr(str), len(str ? std::string_view(str).size() : 0) {}
  constexpr string_view(const char* p, size_t l) : ptr(p), len(l) {}

  // Implicit conversion to std::string_view
  operator std::string_view() const { return std::string_view(ptr, len); }

  // Helper methods
  constexpr const char* data() const { return ptr; }
  constexpr size_t size() const { return len; }
  constexpr bool empty() const { return len == 0; }

  // C++ Helper Operators
  constexpr bool operator==(std::string_view other) const {
    return std::string_view(ptr, len) == other;
  }

  friend std::ostream& operator<<(std::ostream& os, string_view s) {
    return os << std::string_view(s);
  }
#endif
} string_view_t;
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __cplusplus
// Create from a C string literal (length computed at compile time).
#define SV(s) string_view_t{(const char*)(s ""), sizeof(s) - 1}
// Brace initializer for static arrays.
#define SV_INIT(s) {(const char*)(s), sizeof(s) - 1}

static inline string_view_t string_view_from_cstr(const char* s) {
  if (!s) {
    return SV("");
  }
  return string_view_t{s, strlen(s)};
}

static inline string_view_t string_view_from_parts(const char* ptr,
                                                   size_t len) {
  return string_view_t{ptr, len};
}
#else
// Create from a C string literal (length computed at compile time).
#define SV(s) ((string_view_t){(const char*)(s ""), sizeof(s) - 1})
// Brace initializer for static arrays.
#define SV_INIT(s) {(const char*)(s), sizeof(s) - 1}

static inline string_view_t string_view_from_cstr(const char* s) {
  if (!s) {
    return SV("");
  }
  return (string_view_t){s, strlen(s)};
}

static inline string_view_t string_view_from_parts(const char* ptr,
                                                   size_t len) {
  return (string_view_t){ptr, len};
}
#endif

// Return true if the string view is zero-length.
static inline bool string_view_is_empty(string_view_t s) { return s.len == 0; }

// Compare two string_view_t values for equality (uses memcmp on known lengths).
static inline bool string_view_eq(string_view_t a, string_view_t b) {
  if (a.len != b.len) {
    return false;
  }
  if (a.len == 0) {
    return true;
  }
  return memcmp(a.ptr, b.ptr, a.len) == 0;
}

// Slice a string view from start to end (exclusive).
static inline string_view_t string_view_slice(string_view_t s, size_t start,
                                              size_t end) {
  if (start >= s.len || start >= end) {
    return SV("");
  }
  if (end > s.len) {
    end = s.len;
  }
  return string_view_from_parts(s.ptr + start, end - start);
}

// Substring of a string view from start with a given length.
static inline string_view_t string_view_substr(string_view_t s, size_t start,
                                               size_t len) {
  if (start >= s.len || len == 0) {
    return SV("");
  }
  if (start + len > s.len) {
    len = s.len - start;
  }
  return string_view_from_parts(s.ptr + start, len);
}

// Find the last occurrence of character `c` in the string view.
// Returns true if found, writing the index to *out_index.
static inline bool string_view_rfind_char(string_view_t s, char c,
                                          size_t* out_index) {
  bool found = false;
  for (size_t i = s.len; i > 0; i--) {
    if (s.ptr[i - 1] == c) {
      *out_index = i - 1;
      found = true;
      break;
    }
  }
  return found;
}

// ─── string: an owned, growable null-terminated string buffer ───────────────

typedef struct string {
  char* ptr;   // pointer to null-terminated owned buffer
  size_t len;  // current string length (not including null terminator)
  size_t cap;  // capacity (including space for null terminator)
} string_t;

// Zero-initialize to get an empty string. No allocation happens until
// the first append. Example: string_t s = {};

// Reset length to 0, keeping allocated capacity for reuse.
static inline void string_reset(string_t* s) {
  s->len = 0;
  if (s->ptr) {
    s->ptr[0] = '\0';
  }
}

// Return true if the string is empty.
static inline bool string_is_empty(const string_t* s) { return s->len == 0; }

// Compare two string_t values for equality.
static inline bool string_eq(const string_t* a, const string_t* b) {
  if (a->len != b->len) {
    return false;
  }
  if (a->len == 0) {
    return true;
  }
  return memcmp(a->ptr, b->ptr, a->len) == 0;
}

// Compare a string_t and a string_view_t for equality.
static inline bool string_eq_view(const string_t* a, string_view_t b) {
  if (a->len != b.len) {
    return false;
  }
  if (a->len == 0) {
    return true;
  }
  return memcmp(a->ptr, b.ptr, a->len) == 0;
}

// Return the current content as a string_view_t (aliases internal buffer).
static inline string_view_t string_get_view(const string_t* s) {
  if (!s->ptr) {
    return SV("");
  }
  return string_view_from_parts(s->ptr, s->len);
}

// Return a const char* for use with %s / C APIs. Guaranteed to be
// null-terminated.
static inline const char* string_get_cstr(const string_t* s) {
  if (!s->ptr) {
    return "";
  }
  return s->ptr;
}

// ─── Allocator-aware string functions ───────────────────────────────────────

// Free an allocated string_t.
void string_free(string_t s, allocator_t* a);

// Consume the string: shrink the allocation to exactly `len + 1` bytes,
// return the content as a string_t, and reset the original string struct to
// empty.
string_t string_into_owned(string_t* s, allocator_t* a);

// Duplicate a C string into a string_t allocated from the given allocator.
string_t string_from_cstr(const char* s, allocator_t* a);

// Duplicate a string_view_t into a new string_t (allocates fresh copy).
string_t string_from_view(string_view_t s, allocator_t* a);

// Ensure at least `needed` bytes of free space (not counting null).
void string_ensure(string_t* s, size_t needed, allocator_t* a);

// Append a C string (null-terminated).
void string_append_cstr(string_t* s, const char* str, allocator_t* a);

// Append a string view.
void string_append(string_t* s, string_view_t view, allocator_t* a);

// Append a single character.
void string_append_char(string_t* s, char c, allocator_t* a);

// printf-style format/append. No fixed-size stack buffer is used internally.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void string_printf(string_t* s, allocator_t* a, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif  // CORE_STRING_H
