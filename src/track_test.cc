#include "src/track.h"
#include "src/colors.h"

#include <gtest/gtest.h>

TEST(TrackTest, SortEvents) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  TraceEvent ev1 = {};
  ev1.ts = 200;
  trace_data_add_event(&td, a, theme_get_dark(), &ev1);

  TraceEvent ev2 = {};
  ev2.ts = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev2);

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
  trace_data_add_event(&td, a, theme_get_dark(), &ev1);

  TraceEvent ev2 = {};
  ev2.dur = 500;
  trace_data_add_event(&td, a, theme_get_dark(), &ev2);

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
  trace_data_add_event(&td, a, theme_get_dark(), &ev0);

  // Event 1: [200, 300]
  TraceEvent ev1 = {};
  ev1.ts = 200;
  ev1.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev1);

  // Event 2: [400, 1000] - very long
  TraceEvent ev2 = {};
  ev2.ts = 400;
  ev2.dur = 600;
  trace_data_add_event(&td, a, theme_get_dark(), &ev2);

  // Event 3: [1200, 1300]
  TraceEvent ev3 = {};
  ev3.ts = 1200;
  ev3.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev3);

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

TEST(TrackTest, CalculateDepths) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  // [0, 100]
  TraceEvent ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev0);

  // [10, 50] - nested in ev0
  TraceEvent ev1 = {};
  ev1.ts = 10;
  ev1.dur = 40;
  trace_data_add_event(&td, a, theme_get_dark(), &ev1);

  // [10, 20] - nested in ev1, same start time as ev1 (if dur is shorter)
  TraceEvent ev2 = {};
  ev2.ts = 10;
  ev2.dur = 10;
  trace_data_add_event(&td, a, theme_get_dark(), &ev2);

  // [60, 90] - nested in ev0, sequential to ev1
  TraceEvent ev3 = {};
  ev3.ts = 60;
  ev3.dur = 30;
  trace_data_add_event(&td, a, theme_get_dark(), &ev3);

  // [110, 120] - sequential to ev0
  TraceEvent ev4 = {};
  ev4.ts = 110;
  ev4.dur = 10;
  trace_data_add_event(&td, a, theme_get_dark(), &ev4);

  Track t = {};
  for (size_t i = 0; i < 5; i++) {
    array_list_push_back(&t.event_indices, a, i);
  }

  // We need to re-add them in a non-sorted order to test sorting too.
  array_list_clear(&t.event_indices);
  array_list_push_back(&t.event_indices, a, (size_t)4);
  array_list_push_back(&t.event_indices, a, (size_t)2);
  array_list_push_back(&t.event_indices, a, (size_t)0);
  array_list_push_back(&t.event_indices, a, (size_t)3);
  array_list_push_back(&t.event_indices, a, (size_t)1);

  track_sort_events(&t, &td);
  track_calculate_depths(&t, &td, a);

  // Sorted order should be ev0, ev1, ev2, ev3, ev4 because of our new sorting
  // rules.
  EXPECT_EQ(t.event_indices[0], 0u);
  EXPECT_EQ(t.event_indices[1], 1u);
  EXPECT_EQ(t.event_indices[2], 2u);
  EXPECT_EQ(t.event_indices[3], 3u);
  EXPECT_EQ(t.event_indices[4], 4u);

  EXPECT_EQ(t.depths[0], 0u);
  EXPECT_EQ(t.depths[1], 1u);
  EXPECT_EQ(t.depths[2], 2u);
  EXPECT_EQ(t.depths[3], 1u);
  EXPECT_EQ(t.depths[4], 0u);

  EXPECT_EQ(t.max_depth, 2u);

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, CalculateDepthsSiblings) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  // Parent [0, 100]
  TraceEvent ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev0);

  // Child 1 [10, 50]
  TraceEvent ev1 = {};
  ev1.ts = 10;
  ev1.dur = 40;
  trace_data_add_event(&td, a, theme_get_dark(), &ev1);

  // Child 2 [50, 100] - starts when ev1 ends, ends when ev0 ends
  TraceEvent ev2 = {};
  ev2.ts = 50;
  ev2.dur = 50;
  trace_data_add_event(&td, a, theme_get_dark(), &ev2);

  Track t = {};
  for (size_t i = 0; i < 3; i++) {
    array_list_push_back(&t.event_indices, a, i);
  }

  track_sort_events(&t, &td);
  track_calculate_depths(&t, &td, a);

  EXPECT_EQ(t.depths[0], 0u); // ev0
  EXPECT_EQ(t.depths[1], 1u); // ev1
  EXPECT_EQ(t.depths[2], 1u); // ev2 - should be depth 1, not 0 or 2

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, CalculateDepthsDuplicates) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  // Two events with same ts and dur
  TraceEvent ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev0);

  TraceEvent ev1 = {};
  ev1.ts = 0;
  ev1.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev1);

  Track t = {};
  for (size_t i = 0; i < 2; i++) {
    array_list_push_back(&t.event_indices, a, i);
  }

  track_sort_events(&t, &td);
  track_calculate_depths(&t, &td, a);

  // Since they are identical, one will be a child of another.
  EXPECT_EQ(t.depths[0], 0u);
  EXPECT_EQ(t.depths[1], 1u);

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, CalculateDepthsNonStrict) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  // Parent [0, 100]
  TraceEvent ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev0);

  // Child [10, 110] - outlives parent!
  TraceEvent ev1 = {};
  ev1.ts = 10;
  ev1.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev1);

  // New [105, 120] - sequential to ev0, should be at depth 0
  TraceEvent ev2 = {};
  ev2.ts = 105;
  ev2.dur = 15;
  trace_data_add_event(&td, a, theme_get_dark(), &ev2);

  Track t = {};
  for (size_t i = 0; i < 3; i++) {
    array_list_push_back(&t.event_indices, a, i);
  }

  track_sort_events(&t, &td);
  track_calculate_depths(&t, &td, a);

  EXPECT_EQ(t.depths[0], 0u); // ev0
  EXPECT_EQ(t.depths[1], 0u); // ev1 - moved up to depth 0 because ev0 doesn't strictly contain it
  EXPECT_EQ(t.depths[2], 0u); // ev2

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, CalculateDepthsComprehensive) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  // Scenario 1: Strict Nesting (A [B [C]])
  // ev0: [0, 100]
  TraceEvent ev0 = {}; ev0.ts = 0; ev0.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev0);
  // ev1: [10, 50] - child of ev0
  TraceEvent ev1 = {}; ev1.ts = 10; ev1.dur = 40;
  trace_data_add_event(&td, a, theme_get_dark(), &ev1);
  // ev2: [20, 30] - child of ev1
  TraceEvent ev2 = {}; ev2.ts = 20; ev2.dur = 10;
  trace_data_add_event(&td, a, theme_get_dark(), &ev2);

  // Scenario 2: Non-Strict Overlap (Outliving Parent)
  // ev3: [110, 150]
  TraceEvent ev3 = {}; ev3.ts = 110; ev3.dur = 40;
  trace_data_add_event(&td, a, theme_get_dark(), &ev3);
  // ev4: [120, 160] - overlaps ev3, but ends LATER. Should be depth 0.
  TraceEvent ev4 = {}; ev4.ts = 120; ev4.dur = 40;
  trace_data_add_event(&td, a, theme_get_dark(), &ev4);

  // Scenario 3: Multiple Siblings inside Parent
  // ev5: [200, 300] - Parent
  TraceEvent ev5 = {}; ev5.ts = 200; ev5.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev5);
  // ev6: [210, 240] - child of ev5
  TraceEvent ev6 = {}; ev6.ts = 210; ev6.dur = 30;
  trace_data_add_event(&td, a, theme_get_dark(), &ev6);
  // ev7: [250, 290] - child of ev5, sequential to ev6
  TraceEvent ev7 = {}; ev7.ts = 250; ev7.dur = 40;
  trace_data_add_event(&td, a, theme_get_dark(), &ev7);

  // Scenario 4: "Stepping Down" (Overlapping stairs)
  // ev8: [400, 500]
  TraceEvent ev8 = {}; ev8.ts = 400; ev8.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev8);
  // ev9: [410, 510] - overlaps ev8, same level
  TraceEvent ev9 = {}; ev9.ts = 410; ev9.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev9);
  // ev10: [420, 520] - overlaps ev9, same level
  TraceEvent ev10 = {}; ev10.ts = 420; ev10.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &ev10);

  Track t = {};
  for (size_t i = 0; i < 11; i++) {
    array_list_push_back(&t.event_indices, a, i);
  }

  track_sort_events(&t, &td);
  track_calculate_depths(&t, &td, a);

  // Scenario 1
  EXPECT_EQ(t.depths[0], 0u); // ev0
  EXPECT_EQ(t.depths[1], 1u); // ev1
  EXPECT_EQ(t.depths[2], 2u); // ev2

  // Scenario 2
  EXPECT_EQ(t.depths[3], 0u); // ev3
  EXPECT_EQ(t.depths[4], 0u); // ev4 - outlives ev3, moves up

  // Scenario 3
  EXPECT_EQ(t.depths[5], 0u); // ev5
  EXPECT_EQ(t.depths[6], 1u); // ev6
  EXPECT_EQ(t.depths[7], 1u); // ev7 - moved up inside ev5

  // Scenario 4
  EXPECT_EQ(t.depths[8], 0u); // ev8
  EXPECT_EQ(t.depths[9], 0u); // ev9
  EXPECT_EQ(t.depths[10], 0u); // ev10

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}
