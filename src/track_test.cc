#include "src/track.h"

#include <gtest/gtest.h>

#include "src/colors.h"

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
  EXPECT_EQ(track_find_visible_start_index(&t, &td, 150), 0u);

  // Case 3: Viewport starts at 800.
  // Event 2 starts at 400, ends at 1000. It IS visible.
  EXPECT_EQ(track_find_visible_start_index(&t, &td, 800), 1u);

  // Case 4: Viewport starts at 1100.
  // Event 2 ends at 1000. Not visible.
  // Event 3 starts at 1200.
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

  EXPECT_EQ(t.depths[0], 0u);  // ev0
  EXPECT_EQ(t.depths[1], 1u);  // ev1
  EXPECT_EQ(t.depths[2], 1u);  // ev2 - should be depth 1, not 0 or 2

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

  EXPECT_EQ(t.depths[0], 0u);  // ev0
  EXPECT_EQ(
      t.depths[1],
      0u);  // ev1 - moved up to depth 0 because ev0 doesn't strictly contain it
  EXPECT_EQ(t.depths[2], 0u);  // ev2

  track_deinit(&t, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, OrganizeTracksEmpty) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  ArrayList<Track> tracks = {};
  int64_t min_ts = -1, max_ts = -1;
  track_organize(&td, a, &tracks, &min_ts, &max_ts);

  EXPECT_EQ(tracks.size, 0u);
  // min_ts/max_ts are not updated if no events
  EXPECT_EQ(min_ts, -1);
  EXPECT_EQ(max_ts, -1);

  array_list_deinit(&tracks, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, OrganizeTracksSorting) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  auto add_event = [&](int32_t pid, int32_t tid, int64_t ts) {
    TraceEvent e = {};
    e.ph = STR("X");
    e.pid = pid;
    e.tid = tid;
    e.ts = ts;
    e.dur = 10;
    trace_data_add_event(&td, a, theme_get_dark(), &e);
  };

  auto add_sort_idx = [&](int32_t pid, int32_t tid, int32_t sort_idx) {
    TraceEvent m = {};
    m.ph = STR("M");
    m.pid = pid;
    m.tid = tid;
    m.name = STR("thread_sort_index");
    TraceArg arg = {STR("sort_index"), STR("")};
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", sort_idx);
    arg.val = {buf, strlen(buf)};
    m.args = &arg;
    m.args_count = 1;
    trace_data_add_event(&td, a, theme_get_dark(), &m);
  };

  // Add tracks in "random" order
  add_event(10, 1, 100);
  add_event(1, 2, 100);
  add_event(1, 1, 100);
  add_sort_idx(10, 1, -5);  // Should be first
  add_sort_idx(1, 2, 5);    // Should be last

  ArrayList<Track> tracks = {};
  int64_t min_ts, max_ts;
  track_organize(&td, a, &tracks, &min_ts, &max_ts);

  ASSERT_EQ(tracks.size, 3u);

  // 1. PID 10, TID 1 (sort_index -5)
  EXPECT_EQ(tracks[0].pid, 10);
  EXPECT_EQ(tracks[0].sort_index, -5);

  // 2. PID 1, TID 1 (sort_index 0 default)
  EXPECT_EQ(tracks[1].pid, 1);
  EXPECT_EQ(tracks[1].tid, 1);
  EXPECT_EQ(tracks[1].sort_index, 0);

  // 3. PID 1, TID 2 (sort_index 5)
  EXPECT_EQ(tracks[2].pid, 1);
  EXPECT_EQ(tracks[2].tid, 2);
  EXPECT_EQ(tracks[2].sort_index, 5);

  for (size_t i = 0; i < tracks.size; i++) track_deinit(&tracks[i], a);
  array_list_deinit(&tracks, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, OrganizeTracksMetadataOnly) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  TraceEvent m = {};
  m.ph = STR("M");
  m.pid = 1;
  m.tid = 1;
  m.name = STR("thread_name");
  TraceArg arg = {STR("name"), STR("Meta Only")};
  m.args = &arg;
  m.args_count = 1;
  trace_data_add_event(&td, a, theme_get_dark(), &m);

  ArrayList<Track> tracks = {};
  int64_t min_ts = -1, max_ts = -1;
  track_organize(&td, a, &tracks, &min_ts, &max_ts);

  EXPECT_EQ(tracks.size, 1u);
  EXPECT_STREQ(trace_data_get_string(&td, tracks[0].name_ref).buf, "Meta Only");
  EXPECT_EQ(tracks[0].event_indices.size, 0u);

  // Viewport range should not be updated by metadata
  EXPECT_EQ(min_ts, 0);  // min_ts/max_ts are 0 as initialized in track_organize
  EXPECT_EQ(max_ts, 0);

  array_list_deinit(&tracks, a);
  trace_data_deinit(&td, a);
}

TEST(TrackTest, OrganizeTracksMixedOrder) {
  Allocator a = allocator_get_default();
  TraceData td;
  trace_data_init(&td, a);

  // 1. Regular event
  TraceEvent e1 = {};
  e1.ph = STR("X");
  e1.pid = 1;
  e1.tid = 1;
  e1.ts = 500;
  e1.dur = 100;
  trace_data_add_event(&td, a, theme_get_dark(), &e1);

  // 2. Metadata for same track
  TraceEvent m1 = {};
  m1.ph = STR("M");
  m1.pid = 1;
  m1.tid = 1;
  m1.name = STR("thread_name");
  TraceArg arg1 = {STR("name"), STR("Mixed")};
  m1.args = &arg1;
  m1.args_count = 1;
  trace_data_add_event(&td, a, theme_get_dark(), &m1);

  // 3. Regular event for another track
  TraceEvent e2 = {};
  e2.ph = STR("X");
  e2.pid = 2;
  e2.tid = 1;
  e2.ts = 100;
  e2.dur = 50;
  trace_data_add_event(&td, a, theme_get_dark(), &e2);

  ArrayList<Track> tracks = {};
  int64_t min_ts, max_ts;
  track_organize(&td, a, &tracks, &min_ts, &max_ts);

  ASSERT_EQ(tracks.size, 2u);

  // Sorted by PID (both have sort_index 0)
  EXPECT_EQ(tracks[0].pid, 1);
  EXPECT_STREQ(trace_data_get_string(&td, tracks[0].name_ref).buf, "Mixed");
  EXPECT_EQ(tracks[0].event_indices.size, 1u);
  EXPECT_EQ(tracks[0].event_indices[0], 0u);

  EXPECT_EQ(tracks[1].pid, 2);
  EXPECT_EQ(tracks[1].event_indices.size, 1u);
  EXPECT_EQ(tracks[1].event_indices[0], 2u);

  EXPECT_EQ(min_ts, 100);
  EXPECT_EQ(max_ts, 600);

  for (size_t i = 0; i < tracks.size; i++) track_deinit(&tracks[i], a);
  array_list_deinit(&tracks, a);
  trace_data_deinit(&td, a);
}
