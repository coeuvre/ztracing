#include "src/track.h"

#include <gtest/gtest.h>

TEST(TrackTest, SortEvents) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  TraceEvent ev1 = {};
  ev1.ts = 200;
  trace_data_add_event(&td, a, &ev1);

  TraceEvent ev2 = {};
  ev2.ts = 100;
  trace_data_add_event(&td, a, &ev2);

  Track t = {};
  array_list_push_back(&t.event_indices, a, (size_t)0);
  array_list_push_back(&t.event_indices, a, (size_t)1);

  track_sort_events(&t, &td);

  EXPECT_EQ(t.event_indices[0], 1u);
  EXPECT_EQ(t.event_indices[1], 0u);

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, UpdateMaxDur) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  TraceEvent ev1 = {};
  ev1.dur = 100;
  trace_data_add_event(&td, a, &ev1);

  TraceEvent ev2 = {};
  ev2.dur = 500;
  trace_data_add_event(&td, a, &ev2);

  Track t = {};
  array_list_push_back(&t.event_indices, a, (size_t)0);
  array_list_push_back(&t.event_indices, a, (size_t)1);

  track_update_max_dur(&t, &td);

  EXPECT_EQ(t.max_dur, 500);

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, FindVisibleStartIndex) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  // Event 0: [0, 100]
  TraceEvent ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(&td, a, &ev0);

  // Event 1: [200, 300]
  TraceEvent ev1 = {};
  ev1.ts = 200;
  ev1.dur = 100;
  trace_data_add_event(&td, a, &ev1);

  // Event 2: [400, 1000] - very long
  TraceEvent ev2 = {};
  ev2.ts = 400;
  ev2.dur = 600;
  trace_data_add_event(&td, a, &ev2);

  // Event 3: [1200, 1300]
  TraceEvent ev3 = {};
  ev3.ts = 1200;
  ev3.dur = 100;
  trace_data_add_event(&td, a, &ev3);

  Track t = {};
  for (size_t i = 0; i < 4; i++) {
    array_list_push_back(&t.event_indices, a, i);
  }

  track_sort_events(&t, &td);
  track_update_max_dur(&t, &td);  // max_dur should be 600

  // Case 1: Viewport starts at 50. Event 0 starts at 0, dur 100.
  // It should be visible.
  EXPECT_EQ(track_find_visible_start_index(&t, &td, 50), 0u);

  // Case 2: Viewport starts at 150. Event 0 ends at 100. Not visible.
  // Event 1 starts at 200.
  // track_find_visible_start_index returns the FIRST POSSIBLE event.
  // search_ts = 150 - 600 = -450.
  // lower_bound for -450 will give index 0.
  // This is fine, the rendering loop will then skip events that end before 150.
  EXPECT_EQ(track_find_visible_start_index(&t, &td, 150), 0u);

  // Case 3: Viewport starts at 800.
  // Event 2 starts at 400, ends at 1000. It IS visible.
  // search_ts = 800 - 600 = 200.
  // lower_bound for 200 will give index 1 (Event 1 starts at 200).
  EXPECT_EQ(track_find_visible_start_index(&t, &td, 800), 1u);

  // Case 4: Viewport starts at 1100.
  // Event 2 ends at 1000. Not visible.
  // Event 3 starts at 1200.
  // search_ts = 1100 - 600 = 500.
  // lower_bound for 500 will give index 3 (Event 3 starts at 1200).
  // Wait, Event 3 starts at 1200. lower_bound(500) on [0, 200, 400, 1200] is
  // index 3.
  EXPECT_EQ(track_find_visible_start_index(&t, &td, 1100), 3u);

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}
