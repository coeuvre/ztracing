#include "src/trace_data.h"

#include <gtest/gtest.h>

#include "src/colors.h"

TEST(TraceDataTest, Basic) {
  Allocator a = allocator_get_default();
  TraceData td = {};

  TraceEvent ev = {};
  ev.name = "event1";
  ev.cat = "cat1";
  ev.ph = "X";
  ev.ts = 100;
  ev.dur = 50;
  ev.pid = 1;
  ev.tid = 2;

  TraceArg args[2];
  args[0] = {"key1", "val1", 0.0};
  args[1] = {"key2", "val2", 0.0};
  ev.args = args;
  ev.args_count = 2;

  trace_data_add_event(&td, a, theme_get_dark(), &ev);

  ASSERT_EQ(td.events.size, 1u);
  const TraceEventPersisted& p = td.events[0];
  EXPECT_EQ(trace_data_get_string(&td, p.name_ref), "event1");
  EXPECT_EQ(trace_data_get_string(&td, p.cat_ref), "cat1");
  EXPECT_EQ(trace_data_get_string(&td, p.ph_ref), "X");
  EXPECT_EQ(p.ts, 100);
  EXPECT_EQ(p.dur, 50);
  EXPECT_EQ(p.pid, 1);
  EXPECT_EQ(p.tid, 2);
  EXPECT_EQ(p.args_count, 2u);

  ASSERT_EQ(td.args.size, 2u);
  const TraceArgPersisted& pa1 = td.args[p.args_offset];
  EXPECT_EQ(trace_data_get_string(&td, pa1.key_ref), "key1");
  EXPECT_EQ(trace_data_get_string(&td, pa1.val_ref), "val1");

  const TraceArgPersisted& pa2 = td.args[p.args_offset + 1];
  EXPECT_EQ(trace_data_get_string(&td, pa2.key_ref), "key2");
  EXPECT_EQ(trace_data_get_string(&td, pa2.val_ref), "val2");

  trace_data_deinit(&td, a);
}

TEST(TraceDataTest, DeDuplication) {
  Allocator a = allocator_get_default();
  TraceData td = {};

  StringRef ref1 = trace_data_push_string(&td, a, "foo");
  StringRef ref2 = trace_data_push_string(&td, a, "bar");
  StringRef ref3 = trace_data_push_string(&td, a, "foo");

  EXPECT_EQ(ref1, ref3);
  EXPECT_NE(ref1, ref2);
  EXPECT_EQ(td.string_table.size, 2u);

  EXPECT_EQ(trace_data_get_string(&td, ref1), "foo");
  EXPECT_EQ(trace_data_get_string(&td, ref2), "bar");

  trace_data_deinit(&td, a);
}

TEST(TraceDataTest, Clear) {
  Allocator a = allocator_get_default();
  TraceData td = {};

  trace_data_push_string(&td, a, "foo");
  TraceEvent ev = {};
  ev.name = "foo";
  trace_data_add_event(&td, a, theme_get_dark(), &ev);

  trace_data_clear(&td, a);

  EXPECT_EQ(td.string_buffer.size, 0u);
  EXPECT_EQ(td.string_table.size, 0u);
  EXPECT_EQ(td.events.size, 0u);
  EXPECT_EQ(td.string_lookup.size, 0u);

  // Should still be usable
  StringRef ref = trace_data_push_string(&td, a, "foo");
  EXPECT_EQ(ref, 1u);
  EXPECT_EQ(trace_data_get_string(&td, ref), "foo");

  trace_data_deinit(&td, a);
}
