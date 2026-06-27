#include "core/string.h"

#include <gtest/gtest.h>
#include <string.h>

#include "core/allocator.h"
#include "core/arena.h"

TEST(string_test, string_view_is_empty) {
  // SV("") is empty
  EXPECT_TRUE(string_view_is_empty(SV("")));

  // Zero-initialized {nullptr, 0} — len 0 so considered empty
  string_view_t zeroed = {nullptr, 0};
  EXPECT_TRUE(string_view_is_empty(zeroed));

  // Non-empty string
  string_view_t non_empty = SV("hello");
  EXPECT_FALSE(string_view_is_empty(non_empty));
}

TEST(string_test, string_format) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t s = {};
  string_printf(&s, alloc, "hello %s %d", "world", 42);
  ASSERT_NE(string_get_cstr(&s), nullptr);
  EXPECT_STREQ(string_get_cstr(&s), "hello world 42");

  string_free(s, alloc);
  arena_destroy(a);
}

TEST(string_test, string_view_slice_and_substr) {
  string_view_t orig = SV("hello world");

  // string_view_slice
  string_view_t slice1 = string_view_slice(orig, 0, 5);
  EXPECT_EQ(slice1, "hello");
  EXPECT_EQ(slice1.ptr, orig.ptr);  // zero-copy verification

  string_view_t slice2 = string_view_slice(orig, 6, 11);
  EXPECT_EQ(slice2, "world");
  EXPECT_EQ(slice2.ptr, orig.ptr + 6);  // zero-copy verification

  // string_view_substr
  string_view_t sub1 = string_view_substr(orig, 0, 5);
  EXPECT_EQ(sub1, "hello");
  EXPECT_EQ(sub1.ptr, orig.ptr);

  string_view_t sub2 = string_view_substr(orig, 6, 5);
  EXPECT_EQ(sub2, "world");
  EXPECT_EQ(sub2.ptr, orig.ptr + 6);

  // Boundary cases
  EXPECT_TRUE(string_view_is_empty(string_view_slice(orig, 12, 15)));
  EXPECT_TRUE(string_view_is_empty(string_view_substr(orig, 12, 5)));
  EXPECT_TRUE(string_view_is_empty(string_view_slice(orig, 5, 2)));
}

// ─── string: growable buffer tests ──────────────────────────────────────────

TEST(string_buf_test, empty_by_default) {
  string_t sb = {};
  EXPECT_EQ(sb.ptr, nullptr);
  EXPECT_EQ(sb.len, (size_t)0);
  EXPECT_EQ(sb.cap, (size_t)0);
  EXPECT_TRUE(string_is_empty(&sb));
  EXPECT_STREQ(string_get_cstr(&sb), "");
}

TEST(string_buf_test, append_cstr) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_append_cstr(&sb, "hello", alloc);
  EXPECT_EQ(sb.len, (size_t)5);
  EXPECT_STREQ(string_get_cstr(&sb), "hello");

  string_append_cstr(&sb, " world", alloc);
  EXPECT_EQ(sb.len, (size_t)11);
  EXPECT_STREQ(string_get_cstr(&sb), "hello world");

  arena_destroy(a);
}

TEST(string_buf_test, append_view) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_append(&sb, SV("foo"), alloc);
  string_append(&sb, SV("bar"), alloc);
  EXPECT_EQ(sb.len, (size_t)6);
  EXPECT_STREQ(string_get_cstr(&sb), "foobar");

  arena_destroy(a);
}

TEST(string_buf_test, append_char) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_append_char(&sb, 'a', alloc);
  string_append_char(&sb, 'b', alloc);
  string_append_char(&sb, 'c', alloc);
  EXPECT_EQ(sb.len, (size_t)3);
  EXPECT_STREQ(string_get_cstr(&sb), "abc");

  arena_destroy(a);
}

TEST(string_buf_test, append_empty_is_noop) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_append_cstr(&sb, "", alloc);
  string_append(&sb, SV(""), alloc);
  EXPECT_EQ(sb.len, (size_t)0);
  EXPECT_EQ(sb.ptr, nullptr);

  arena_destroy(a);
}

TEST(string_buf_test, growth_beyond_initial_capacity) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  for (int i = 0; i < 100; i++) {
    string_append_char(&sb, 'x', alloc);
  }
  EXPECT_EQ(sb.len, (size_t)100);
  EXPECT_GE(sb.cap, sb.len + 1);

  const char* cstr = string_get_cstr(&sb);
  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(cstr[i], 'x');
  }
  EXPECT_EQ(cstr[100], '\0');

  arena_destroy(a);
}

TEST(string_buf_test, reset_keeps_capacity) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_append_cstr(&sb, "hello world", alloc);
  size_t cap = sb.cap;

  string_reset(&sb);
  EXPECT_EQ(sb.len, (size_t)0);
  EXPECT_EQ(sb.cap, cap);
  EXPECT_STREQ(string_get_cstr(&sb), "");

  string_append_cstr(&sb, "reused", alloc);
  EXPECT_EQ(sb.cap, cap);
  EXPECT_STREQ(string_get_cstr(&sb), "reused");

  arena_destroy(a);
}

TEST(string_buf_test, printf_small) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_printf(&sb, alloc, "hello %s %d", "world", 42);
  EXPECT_STREQ(string_get_cstr(&sb), "hello world 42");

  arena_destroy(a);
}

TEST(string_buf_test, printf_large) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_printf(&sb, alloc, "%2000s", "x");
  EXPECT_EQ(sb.len, (size_t)2000);

  arena_destroy(a);
}

TEST(string_buf_test, into_string_transfers_ownership) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_append_cstr(&sb, "hello", alloc);

  string_t s = string_into_owned(&sb, alloc);
  EXPECT_STREQ(string_get_cstr(&s), "hello");
  EXPECT_EQ(s.len, (size_t)5);

  EXPECT_EQ(sb.ptr, nullptr);
  EXPECT_EQ(sb.len, (size_t)0);
  EXPECT_EQ(sb.cap, (size_t)0);

  string_free(s, alloc);
  arena_destroy(a);
}

TEST(string_buf_test, into_string_empty_returns_empty) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t sb = {};
  string_t s = string_into_owned(&sb, alloc);
  EXPECT_TRUE(string_is_empty(&s));

  arena_destroy(a);
}

TEST(string_from_test, from_cstr) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_t s = string_from_cstr("hello world", alloc);
  EXPECT_STREQ(string_get_cstr(&s), "hello world");
  EXPECT_EQ(s.len, (size_t)11);
  EXPECT_GE(s.cap, (size_t)12);

  string_free(s, alloc);
  arena_destroy(a);
}

TEST(string_from_test, from_view) {
  arena_t* a = arena_create();
  allocator_t* alloc = arena_get_allocator(a);

  string_view_t view = SV("hello view");
  string_t s = string_from_view(view, alloc);
  EXPECT_STREQ(string_get_cstr(&s), "hello view");
  EXPECT_EQ(s.len, (size_t)10);
  EXPECT_GE(s.cap, (size_t)11);

  string_free(s, alloc);
  arena_destroy(a);
}
