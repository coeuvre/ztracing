#include "src/cli_table.h"
#include <gtest/gtest.h>
#include <stdlib.h>
#include "core/allocator.h"

TEST(cli_table_test, basic) {
  cli_table_t t;
  cli_table_init(&t);

  cli_table_add_column(&t, SV("Col1"), CLI_ALIGN_LEFT, 0, true);
  cli_table_add_column(&t, SV("Col2"), CLI_ALIGN_RIGHT, 10, false);

  EXPECT_EQ(t.columns.len, 2u);
  EXPECT_EQ(string_get_view(&t.columns.ptr[0].header), "Col1");
  EXPECT_EQ(t.columns.ptr[0].align, CLI_ALIGN_LEFT);
  EXPECT_TRUE(t.columns.ptr[0].dynamic);

  cli_table_add_row(&t);
  cli_table_set_cell(&t, 0, SV("value1"));
  cli_table_set_cell_fmt(&t, 1, "num %d", 42);

  EXPECT_EQ(t.rows.len, 1u);
  EXPECT_EQ(t.rows.ptr[0].cells.len, 2u);
  EXPECT_EQ(string_get_view(&t.rows.ptr[0].cells.ptr[0]), "value1");
  EXPECT_EQ(string_get_view(&t.rows.ptr[0].cells.ptr[1]), "num 42");

  // Test dynamic width calculation
  cli_table_print(&t); // This will calculate widths
  
  // "Col1" is 4 chars, "value1" is 6 chars. So width should be 6.
  EXPECT_EQ(t.columns.ptr[0].width, 6);
  // "Col2" has fixed width 10.
  EXPECT_EQ(t.columns.ptr[1].width, 10);

  cli_table_deinit(&t);
}

TEST(cli_table_test, utf8_width) {
  cli_table_t t;
  cli_table_init(&t);

  // "█" is 3 bytes but 1 visual char.
  cli_table_add_column(&t, SV("Bar"), CLI_ALIGN_LEFT, 0, true);
  cli_table_add_row(&t);
  cli_table_set_cell(&t, 0, SV("████")); // 4 blocks, 12 bytes, 4 visual chars

  cli_table_print(&t);

  // "Bar" is 3 chars, "████" is 4 visual chars. Width should be 4.
  EXPECT_EQ(t.columns.ptr[0].width, 4);

  cli_table_deinit(&t);
}

TEST(cli_table_test, terminal_width_truncation) {
  // Force terminal width via environment variable
  setenv("COLUMNS", "20", 1);

  cli_table_t t;
  cli_table_init(&t);

  // Total width budget is 20.
  // We have 2 columns. Separator " | " takes 3.
  // Available for columns is 17.
  // Col1 is fixed width 8.
  // Col2 is dynamic. Remaining for Col2 is 17 - 8 = 9.
  cli_table_add_column(&t, SV("FixedCol"), CLI_ALIGN_LEFT, 8, false);
  cli_table_add_column(&t, SV("DynamicColLongName"), CLI_ALIGN_LEFT, 0, true);

  cli_table_add_row(&t);
  cli_table_set_cell(&t, 0, SV("12345678"));
  cli_table_set_cell(&t, 1, SV("VeryLongValueThatWillBeTruncated"));

  cli_table_print(&t);

  // FixedCol should remain 8.
  EXPECT_EQ(t.columns.ptr[0].width, 8);
  // DynamicColLongName should be shrunk to fit the remaining 9.
  EXPECT_EQ(t.columns.ptr[1].width, 9);

  cli_table_deinit(&t);
  unsetenv("COLUMNS");
}
