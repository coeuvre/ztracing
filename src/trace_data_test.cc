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

  TraceEventMatcher matcher = {};
  trace_data_add_event(&td, a, theme_get_dark(), &ev, &matcher);

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

  trace_event_matcher_deinit(&matcher, a);
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
  TraceEventMatcher matcher = {};
  trace_data_add_event(&td, a, theme_get_dark(), &ev, &matcher);

  trace_data_clear(&td, a);

  EXPECT_EQ(td.string_buffer.size, 0u);
  EXPECT_EQ(td.string_table.size, 0u);
  EXPECT_EQ(td.events.size, 0u);
  EXPECT_EQ(td.string_lookup.size, 0u);

  // Should still be usable
  StringRef ref = trace_data_push_string(&td, a, "foo");
  EXPECT_EQ(ref, 1u);
  EXPECT_EQ(trace_data_get_string(&td, ref), "foo");

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}

TEST(TraceDataTest, BeginEndEventsBasic) {
  Allocator a = allocator_get_default();
  TraceData td = {};
  TraceEventMatcher matcher = {};

  // 1. Thread 1 Begin event
  TraceEvent b1 = {};
  b1.name = "event1";
  b1.cat = "cat1";
  b1.ph = "B";
  b1.ts = 100;
  b1.pid = 1;
  b1.tid = 2;
  trace_data_add_event(&td, a, theme_get_dark(), &b1, &matcher);

  ASSERT_EQ(td.events.size, 1u);
  EXPECT_EQ(td.events[0].dur, 0);

  // 2. Thread 1 End event
  TraceEvent e1 = {};
  e1.ph = "E";
  e1.ts = 150;
  e1.pid = 1;
  e1.tid = 2;
  trace_data_add_event(&td, a, theme_get_dark(), &e1, &matcher);

  // End event should not create a new event, but resolve duration of B
  ASSERT_EQ(td.events.size, 1u);
  EXPECT_EQ(td.events[0].dur, 50);

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}

TEST(TraceDataTest, BeginEndEventsNestedAndThreadIsolated) {
  Allocator a = allocator_get_default();
  TraceData td = {};
  TraceEventMatcher matcher = {};

  // 1. Thread 1 B1
  TraceEvent b1 = {
    .name = "parent",
    .ph = "B",
    .ts = 100,
    .pid = 1,
    .tid = 1
  };
  trace_data_add_event(&td, a, theme_get_dark(), &b1, &matcher);

  // 2. Thread 2 B2 (Different thread)
  TraceEvent b2 = {
    .name = "other",
    .ph = "B",
    .ts = 110,
    .pid = 1,
    .tid = 2
  };
  trace_data_add_event(&td, a, theme_get_dark(), &b2, &matcher);

  // 3. Thread 1 B3 (Nested on thread 1)
  TraceEvent b3 = {
    .name = "child",
    .ph = "B",
    .ts = 120,
    .pid = 1,
    .tid = 1
  };
  trace_data_add_event(&td, a, theme_get_dark(), &b3, &matcher);

  ASSERT_EQ(td.events.size, 3u);
  EXPECT_EQ(trace_data_get_string(&td, td.events[0].name_ref), "parent");
  EXPECT_EQ(trace_data_get_string(&td, td.events[1].name_ref), "other");
  EXPECT_EQ(trace_data_get_string(&td, td.events[2].name_ref), "child");

  // 4. Thread 1 End (Should match B3 "child")
  TraceEvent e3 = {
    .ph = "E",
    .ts = 130,
    .pid = 1,
    .tid = 1
  };
  trace_data_add_event(&td, a, theme_get_dark(), &e3, &matcher);

  EXPECT_EQ(td.events[2].dur, 10);
  EXPECT_EQ(td.events[0].dur, 0); // Still 0

  // 5. Thread 2 End (Should match B2 "other")
  TraceEvent e2 = {
    .ph = "E",
    .ts = 140,
    .pid = 1,
    .tid = 2
  };
  trace_data_add_event(&td, a, theme_get_dark(), &e2, &matcher);

  EXPECT_EQ(td.events[1].dur, 30);

  // 6. Thread 1 End (Should match B1 "parent")
  TraceEvent e1 = {
    .ph = "E",
    .ts = 150,
    .pid = 1,
    .tid = 1
  };
  trace_data_add_event(&td, a, theme_get_dark(), &e1, &matcher);

  EXPECT_EQ(td.events[0].dur, 50);

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}

TEST(TraceDataTest, BeginEndEventsArgsMerging) {
  Allocator a = allocator_get_default();
  TraceData td = {};
  TraceEventMatcher matcher = {};

  // 1. Begin with arguments
  TraceEvent b = {};
  b.name = "ev";
  b.ph = "B";
  b.ts = 100;
  b.pid = 1;
  b.tid = 1;
  TraceArg b_args[2];
  b_args[0] = {"arg1", "val1", 0.0};
  b_args[1] = {"arg2", "", 42.0};
  b.args = b_args;
  b.args_count = 2;
  trace_data_add_event(&td, a, theme_get_dark(), &b, &matcher);

  // 2. End with arguments (one duplicate, one new)
  TraceEvent e = {};
  e.ph = "E";
  e.ts = 200;
  e.pid = 1;
  e.tid = 1;
  TraceArg e_args[2];
  e_args[0] = {"arg2", "", 99.0}; // Overrides arg2
  e_args[1] = {"arg3", "val3", 0.0}; // New arg
  e.args = e_args;
  e.args_count = 2;
  trace_data_add_event(&td, a, theme_get_dark(), &e, &matcher);

  const TraceEventPersisted& p = td.events[0];
  EXPECT_EQ(p.args_count, 3u);

  // Check the merged args
  const TraceArgPersisted& arg1 = td.args[p.args_offset];
  EXPECT_EQ(trace_data_get_string(&td, arg1.key_ref), "arg1");
  EXPECT_EQ(trace_data_get_string(&td, arg1.val_ref), "val1");

  const TraceArgPersisted& arg2 = td.args[p.args_offset + 1];
  EXPECT_EQ(trace_data_get_string(&td, arg2.key_ref), "arg2");
  EXPECT_EQ(arg2.val_double, 99.0);

  const TraceArgPersisted& arg3 = td.args[p.args_offset + 2];
  EXPECT_EQ(trace_data_get_string(&td, arg3.key_ref), "arg3");
  EXPECT_EQ(trace_data_get_string(&td, arg3.val_ref), "val3");

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}
