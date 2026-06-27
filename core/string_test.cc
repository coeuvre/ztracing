#include "core/string.h"

#include <gtest/gtest.h>
#include <string.h>

#include "core/allocator.h"

TEST(StringTest, ZeroInitialization) {
  string_t s = {};
  EXPECT_EQ(s.ptr, nullptr);
  EXPECT_EQ(s.len, 0u);
  EXPECT_EQ(s.cap, 0u);

  string_view_t sv = string_to_view(&s);
  EXPECT_EQ(sv.ptr, nullptr);
  EXPECT_EQ(sv.len, 0u);
}

TEST(StringTest, AppendChar) {
  allocator_t* a = c_allocator();
  string_t s = {};

  string_append_char(&s, 'a', a);
  EXPECT_EQ(s.len, 1u);
  EXPECT_GE(s.cap, 2u);
  EXPECT_STREQ(s.ptr, "a");

  string_append_char(&s, 'b', a);
  EXPECT_EQ(s.len, 2u);
  EXPECT_STREQ(s.ptr, "ab");

  string_deinit(&s, a);
  EXPECT_EQ(s.ptr, nullptr);
  EXPECT_EQ(s.len, 0u);
  EXPECT_EQ(s.cap, 0u);
}

TEST(StringTest, AppendCstr) {
  allocator_t* a = c_allocator();
  string_t s = {};

  string_append_cstr(&s, "hello", a);
  EXPECT_EQ(s.len, 5u);
  EXPECT_STREQ(s.ptr, "hello");

  string_append_cstr(&s, " world", a);
  EXPECT_EQ(s.len, 11u);
  EXPECT_STREQ(s.ptr, "hello world");

  string_append_cstr(&s, "", a);
  EXPECT_EQ(s.len, 11u);

  EXPECT_DEATH(string_append_cstr(&s, nullptr, a), "CHECK failed");

  string_deinit(&s, a);
}

TEST(StringTest, AppendStringView) {
  allocator_t* a = c_allocator();
  string_t s = {};

  string_view_t sv1 = SV("hello");
  string_append_view(&s, sv1, a);
  EXPECT_EQ(s.len, 5u);
  EXPECT_STREQ(s.ptr, "hello");

  string_view_t sv2 = SV(" world");
  string_append_view(&s, sv2, a);
  EXPECT_EQ(s.len, 11u);
  EXPECT_STREQ(s.ptr, "hello world");

  string_deinit(&s, a);
}

TEST(StringTest, Clear) {
  allocator_t* a = c_allocator();
  string_t s = {};

  string_append_cstr(&s, "hello", a);
  EXPECT_EQ(s.len, 5u);

  string_clear(&s);
  EXPECT_EQ(s.len, 0u);
  EXPECT_STREQ(s.ptr, "");

  // Can still append after clear
  string_append_cstr(&s, "world", a);
  EXPECT_EQ(s.len, 5u);
  EXPECT_STREQ(s.ptr, "world");

  string_deinit(&s, a);
}

TEST(StringTest, ToView) {
  allocator_t* a = c_allocator();
  string_t s = {};

  string_append_cstr(&s, "hello", a);
  string_view_t sv = string_to_view(&s);
  EXPECT_EQ(sv.len, 5u);
  EXPECT_EQ(memcmp(sv.ptr, "hello", 5), 0);

  string_deinit(&s, a);
}
