#include "src/format.h"

#include <gtest/gtest.h>

TEST(FormatTest, FormatDuration) {
  char buf[32];

  format_duration(buf, sizeof(buf), 0.0);
  EXPECT_STREQ(buf, "0");

  format_duration(buf, sizeof(buf), 500.0);
  EXPECT_STREQ(buf, "500 us");

  format_duration(buf, sizeof(buf), 1000.0);
  EXPECT_STREQ(buf, "1 ms");

  format_duration(buf, sizeof(buf), 1500.0);
  EXPECT_STREQ(buf, "1.5 ms");

  format_duration(buf, sizeof(buf), 1000000.0);
  EXPECT_STREQ(buf, "1 s");

  format_duration(buf, sizeof(buf), 2500000.0);
  EXPECT_STREQ(buf, "2.5 s");
}

TEST(FormatTest, FormatDurationNegative) {
  char buf[32];

  format_duration(buf, sizeof(buf), -500.0);
  EXPECT_STREQ(buf, "-500 us");

  format_duration(buf, sizeof(buf), -1000.0);
  EXPECT_STREQ(buf, "-1 ms");

  format_duration(buf, sizeof(buf), -1000000.0);
  EXPECT_STREQ(buf, "-1 s");
}

TEST(FormatTest, FormatDurationWithInterval) {
  char buf[32];

  // Large value, large interval -> s
  format_duration(buf, sizeof(buf), 136567000.0, 1000000.0);
  EXPECT_STREQ(buf, "136.567 s");

  // Large value, medium interval -> ms
  format_duration(buf, sizeof(buf), 136567000.0, 1000.0);
  EXPECT_STREQ(buf, "136567 ms");

  // Large value, small interval -> us
  format_duration(buf, sizeof(buf), 136567000.0, 10.0);
  EXPECT_STREQ(buf, "136567000 us");
}

TEST(FormatTest, CalculateTickInterval) {
  // Normal cases
  // 1s duration, 1000px width, 100px min_tick_width -> max_ticks=10,
  // interval=100ms
  EXPECT_DOUBLE_EQ(calculate_tick_interval(1000000.0, 1000.0, 100.0), 100000.0);

  // 1s duration, 1000px width, 250px min_tick_width -> max_ticks=4,
  // interval=250ms -> rounded to 200ms p=100000, m=2.5. 1.5 < 2.5 < 3.5 -> 2.0
  // * 100000 = 200000.
  EXPECT_DOUBLE_EQ(calculate_tick_interval(1000000.0, 1000.0, 250.0), 200000.0);

  // Edge cases: Invalid inputs
  EXPECT_DOUBLE_EQ(calculate_tick_interval(0.0, 1000.0, 100.0), 1.0);
  EXPECT_DOUBLE_EQ(calculate_tick_interval(-100.0, 1000.0, 100.0), 1.0);
  EXPECT_DOUBLE_EQ(calculate_tick_interval(1000000.0, 0.0, 100.0), 1.0);
  EXPECT_DOUBLE_EQ(calculate_tick_interval(1000000.0, -50.0, 100.0), 1.0);

  // Edge case: min_tick_width > width -> max_ticks clamped to 2
  // duration=1000, width=100, min_tick_width=200 -> max_ticks=2, interval=500
  EXPECT_DOUBLE_EQ(calculate_tick_interval(1000.0, 100.0, 200.0), 500.0);

  // Edge case: tick_interval < 1.0 -> clamped to 1.0
  EXPECT_DOUBLE_EQ(calculate_tick_interval(0.5, 1000.0, 100.0), 1.0);
  EXPECT_DOUBLE_EQ(calculate_tick_interval(5.0, 1000.0, 100.0),
                   1.0);  // 5/10 = 0.5 -> rounded to 1.0?
  // p=0.1, m=5. 3.5 < 5 < 7.5 -> 5.0 * 0.1 = 0.5. Clamped to 1.0.

  // Rounding boundaries
  // m < 1.5 -> 1.0 * p
  EXPECT_DOUBLE_EQ(calculate_tick_interval(140.0, 100.0, 10.0),
                   10.0);  // raw=14, p=10, m=1.4
  // 1.5 <= m < 3.5 -> 2.0 * p
  EXPECT_DOUBLE_EQ(calculate_tick_interval(160.0, 100.0, 10.0),
                   20.0);  // raw=16, p=10, m=1.6
  EXPECT_DOUBLE_EQ(calculate_tick_interval(340.0, 100.0, 10.0),
                   20.0);  // raw=34, p=10, m=3.4
  // 3.5 <= m < 7.5 -> 5.0 * p
  EXPECT_DOUBLE_EQ(calculate_tick_interval(360.0, 100.0, 10.0),
                   50.0);  // raw=36, p=10, m=3.6
  EXPECT_DOUBLE_EQ(calculate_tick_interval(740.0, 100.0, 10.0),
                   50.0);  // raw=74, p=10, m=7.4
  // m >= 7.5 -> 10.0 * p
  EXPECT_DOUBLE_EQ(calculate_tick_interval(760.0, 100.0, 10.0),
                   100.0);  // raw=76, p=10, m=7.6

  // Large values
  EXPECT_DOUBLE_EQ(calculate_tick_interval(1e15, 1000.0, 100.0), 1e14);
}
