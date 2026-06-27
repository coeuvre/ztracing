#include "core/string.h"

#include <stdarg.h>
#include <stdio.h>

#include "core/allocator.h"

void string_free(string_t s, allocator_t* a) {
  if (s.ptr) {
    allocator_free(a, s.ptr, s.cap);
  }
}

// ─── string: owned growable buffer functions ───────────────────────────────

void string_ensure(string_t* s, size_t needed, allocator_t* a) {
  if (s->len + needed + 1 <= s->cap) {
    return;
  }
  size_t new_cap = s->cap;
  if (new_cap == 0) {
    new_cap = 64;
  }
  while (new_cap < s->len + needed + 1) {
    new_cap *= 2;
  }
  s->ptr = (char*)allocator_realloc_uninitialized(a, s->ptr, s->cap, new_cap);
  s->cap = new_cap;
}

void string_append_cstr(string_t* s, const char* str, allocator_t* a) {
  if (!str || !*str) {
    return;
  }
  size_t len = strlen(str);
  string_ensure(s, len, a);
  memcpy(s->ptr + s->len, str, len);
  s->len += len;
  s->ptr[s->len] = '\0';
}

void string_append(string_t* s, string_view_t view, allocator_t* a) {
  if (view.len == 0) {
    return;
  }
  string_ensure(s, view.len, a);
  memcpy(s->ptr + s->len, view.ptr, view.len);
  s->len += view.len;
  s->ptr[s->len] = '\0';
}

void string_append_char(string_t* s, char c, allocator_t* a) {
  string_ensure(s, 1, a);
  s->ptr[s->len++] = c;
  s->ptr[s->len] = '\0';
}

void string_printf(string_t* s, allocator_t* a, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char stack[256];
  va_list args2;
  va_copy(args2, args);
  int n = vsnprintf(stack, sizeof(stack), fmt, args2);
  va_end(args2);
  if (n <= 0) {
    va_end(args);
    return;
  }
  size_t needed = (size_t)n;
  if (needed < sizeof(stack)) {
    string_ensure(s, needed, a);
    memcpy(s->ptr + s->len, stack, needed);
    s->len += needed;
    s->ptr[s->len] = '\0';
    va_end(args);
    return;
  }
  string_ensure(s, needed, a);
  vsnprintf(s->ptr + s->len, needed + 1, fmt, args);
  va_end(args);
  s->len += needed;
}

string_t string_into_owned(string_t* s, allocator_t* a) {
  if (s->len == 0) {
    return (string_t){};
  }
  char* ptr = s->ptr;
  size_t cap = s->cap;
  if (s->len + 1 < s->cap) {
    ptr = (char*)allocator_realloc(a, s->ptr, s->cap, s->len + 1);
    cap = s->len + 1;
  }
  ptr[s->len] = '\0';
  string_t result = (string_t){.ptr = ptr, .len = s->len, .cap = cap};
  s->ptr = nullptr;
  s->len = 0;
  s->cap = 0;
  return result;
}

string_t string_from_cstr(const char* s, allocator_t* a) {
  string_t result = {};
  string_append_cstr(&result, s, a);
  return result;
}

string_t string_from_view(string_view_t s, allocator_t* a) {
  string_t result = {};
  string_append(&result, s, a);
  return result;
}
