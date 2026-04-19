#include "src/trace_data.h"
#include "src/colors.h"

#include <gtest/gtest.h>

TEST(TraceDataTest, Basic) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  TraceArg args[2] = {
      {STR("key1"), STR("val1")},
      {STR("key2"), STR("val2")},
  };

  TraceEvent ev = {};
  ev.name = STR("event1");
  ev.cat = STR("cat1");
  ev.ph = STR("X");
  ev.ts = 100;
  ev.dur = 50;
  ev.pid = 1;
  ev.tid = 2;
  ev.args = args;
  ev.args_count = 2;

  trace_data_add_event(&td, a, theme_get_dark(), &ev);

  EXPECT_EQ(td.events.size, 1u);
  EXPECT_EQ(td.args.size, 2u);

  const TraceEventPersisted& p = td.events[0];
  EXPECT_TRUE(str_eq(trace_data_get_string(&td, p.name_offset), STR("event1")));
  EXPECT_TRUE(str_eq(trace_data_get_string(&td, p.cat_offset), STR("cat1")));
  EXPECT_TRUE(str_eq(trace_data_get_string(&td, p.ph_offset), STR("X")));
  EXPECT_EQ(p.ts, 100);
  EXPECT_EQ(p.dur, 50);
  EXPECT_EQ(p.pid, 1);
  EXPECT_EQ(p.tid, 2);
  EXPECT_EQ(p.args_count, 2u);

  const TraceArgPersisted& pa1 = td.args[p.args_offset];
  EXPECT_TRUE(str_eq(trace_data_get_string(&td, pa1.key_offset), STR("key1")));
  EXPECT_TRUE(str_eq(trace_data_get_string(&td, pa1.val_offset), STR("val1")));

  const TraceArgPersisted& pa2 = td.args[p.args_offset + 1];
  EXPECT_TRUE(str_eq(trace_data_get_string(&td, pa2.key_offset), STR("key2")));
  EXPECT_TRUE(str_eq(trace_data_get_string(&td, pa2.val_offset), STR("val2")));

  trace_data_deinit(&td, a);
}

TEST(TraceDataTest, Clear) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  TraceEvent ev = {};
  ev.name = STR("foo");
  trace_data_add_event(&td, a, theme_get_dark(), &ev);

  EXPECT_EQ(td.events.size, 1u);

  trace_data_clear(&td, a);
  EXPECT_EQ(td.events.size, 0u);
  EXPECT_EQ(td.args.size, 0u);
  // String pool should at least contain the null terminator at offset 0
  EXPECT_GT(td.string_pool.size, 0u);
  EXPECT_EQ(td.string_pool[0], '\0');

  trace_data_add_event(&td, a, theme_get_dark(), &ev);
  EXPECT_EQ(td.events.size, 1u);
  EXPECT_TRUE(str_eq(trace_data_get_string(&td, td.events[0].name_offset), STR("foo")));

  trace_data_deinit(&td, a);
}

TEST(TraceDataTest, MemoryLeak) {
  CountingAllocator ca;
  counting_allocator_init(&ca, allocator_get_default());
  Allocator a = counting_allocator_get_allocator(&ca);

  {
    TraceData td;
    trace_data_init(&td, a);

    TraceArg args[1] = {{STR("k"), STR("v")}};
    TraceEvent ev = {};
    ev.name = STR("event");
    ev.args = args;
    ev.args_count = 1;

    trace_data_add_event(&td, a, theme_get_dark(), &ev);
    trace_data_deinit(&td, a);
  }

  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
