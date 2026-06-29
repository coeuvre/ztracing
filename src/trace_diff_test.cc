#include "src/trace_diff.h"
#include <gtest/gtest.h>
#include "core/allocator.h"
#include "src/trace_data.h"

static void add_event(trace_data_t* td, allocator_t* a, const char* name,
                      int64_t dur) {
  trace_event_t e = {};
  e.ph = "X";
  e.pid = 1;
  e.tid = 1;
  e.name = name;
  e.ts = 1000;
  e.dur = dur;
  trace_event_matcher_t matcher = {};
  trace_data_add_event(td, &e, &matcher, a);
  trace_event_matcher_deinit(&matcher);
}

TEST(trace_diff_test, basic) {
  allocator_t* a = c_allocator();
  trace_data_t* td_base = trace_data_create(a);
  trace_data_t* td_target = trace_data_create(a);

  // Baseline: task1 (100), task2 (200)
  add_event(td_base, a, "task1", 100);
  add_event(td_base, a, "task2", 200);

  // Target: task1 (150), task3 (300)
  add_event(td_target, a, "task1", 150);
  add_event(td_target, a, "task3", 300);

  darray_trace_diff_entry_t entries = {};
  trace_diff_compute(td_base, td_target, SV("name"), SV("dur-delta"), &entries, a);

  // Sorted by delta duration descending:
  // 1. task3: delta +300
  // 2. task1: delta +50
  // 3. task2: delta -200
  ASSERT_EQ(entries.len, 3u);

  EXPECT_EQ(entries.ptr[0].key, "task3");
  EXPECT_DOUBLE_EQ(entries.ptr[0].delta_duration, 300.0);
  EXPECT_EQ(entries.ptr[0].baseline_count, 0u);
  EXPECT_EQ(entries.ptr[0].target_count, 1u);

  EXPECT_EQ(entries.ptr[1].key, "task1");
  EXPECT_DOUBLE_EQ(entries.ptr[1].delta_duration, 50.0);
  EXPECT_EQ(entries.ptr[1].baseline_count, 1u);
  EXPECT_EQ(entries.ptr[1].target_count, 1u);

  EXPECT_EQ(entries.ptr[2].key, "task2");
  EXPECT_DOUBLE_EQ(entries.ptr[2].delta_duration, -200.0);
  EXPECT_EQ(entries.ptr[2].baseline_count, 1u);
  EXPECT_EQ(entries.ptr[2].target_count, 0u);

  darray_deinit(&entries, a);
  trace_data_release(td_base, a);
  trace_data_release(td_target, a);
}
