#include "src/str.h"

#include <gtest/gtest.h>

TEST(StrTest, STR) {
  Str s = STR("hello");
  EXPECT_STREQ(s.buf, "hello");
  EXPECT_EQ(s.len, 5u);
}

TEST(StrTest, StrFromCStr) {
  const char* hello = "hello";
  Str s = str_from_cstr(hello);
  EXPECT_EQ(s.buf, hello);
  EXPECT_EQ(s.len, 5u);

  Str null_str = str_from_cstr(nullptr);
  EXPECT_EQ(null_str.buf, nullptr);
  EXPECT_EQ(null_str.len, 0u);
}

TEST(StrTest, StrEq) {
  Str a = STR("hello");
  Str b = STR("hello");
  Str c = STR("world");
  Str d = STR("hell");

  EXPECT_TRUE(str_eq(a, b));
  EXPECT_FALSE(str_eq(a, c));
  EXPECT_FALSE(str_eq(a, d));

  Str empty1 = STR("");
  Str empty2 = {nullptr, 0};
  EXPECT_TRUE(str_eq(empty1, empty2));
}
