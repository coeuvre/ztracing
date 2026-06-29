#include "src/trace_aggregate.h"
#include <gtest/gtest.h>
#include "core/allocator.h"
#include "src/trace_data.h"

static void add_event(trace_data_t* td, allocator_t* a, const char* name,
                      const char* cat, int64_t dur) {
  trace_event_t e = {};
  e.ph = "X";
  e.pid = 1;
  e.tid = 1;
  e.name = name;
  e.cat = cat;
  e.ts = 1000;
  e.dur = dur;
  trace_event_matcher_t matcher = {};
  trace_data_add_event(td, &e, &matcher, a);
  trace_event_matcher_deinit(&matcher);
}

TEST(trace_aggregate_test, basic) {
  allocator_t* a = c_allocator();
  trace_data_t* td = trace_data_create(a);

  add_event(td, a, "task1", "cpu", 100);
  add_event(td, a, "task2", "gpu", 200);
  add_event(td, a, "task1", "cpu", 150);

  // Aggregate by name, sort by duration
  darray_trace_aggregate_entry_t entries = {};
  trace_aggregate_compute(td, SV("name"), SV("duration"), &entries, a);

  ASSERT_EQ(entries.len, 2u);
  // task1 should be first because total dur is 250 > 200
  EXPECT_EQ(trace_data_get_string(td, entries.ptr[0].key_ref), "task1");
  EXPECT_DOUBLE_EQ(entries.ptr[0].total_duration, 250.0);
  EXPECT_EQ(entries.ptr[0].count, 2u);

  EXPECT_EQ(trace_data_get_string(td, entries.ptr[1].key_ref), "task2");
  EXPECT_DOUBLE_EQ(entries.ptr[1].total_duration, 200.0);
  EXPECT_EQ(entries.ptr[1].count, 1u);

  darray_deinit(&entries, a);

  // Aggregate by category, sort by count
  darray_trace_aggregate_entry_t entries_cat = {};
  trace_aggregate_compute(td, SV("category"), SV("count"), &entries_cat, a);

  ASSERT_EQ(entries_cat.len, 2u);
  // cpu should be first because count is 2 > 1
  EXPECT_EQ(trace_data_get_string(td, entries_cat.ptr[0].key_ref), "cpu");
  EXPECT_EQ(entries_cat.ptr[0].count, 2u);

  EXPECT_EQ(trace_data_get_string(td, entries_cat.ptr[1].key_ref), "gpu");
  EXPECT_EQ(entries_cat.ptr[1].count, 1u);

  darray_deinit(&entries_cat, a);
  trace_data_release(td, a);
}
