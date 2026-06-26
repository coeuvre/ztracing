#include "src/track.h"

#include <gtest/gtest.h>

#include "core/arena.h"
#include "src/colors.h"
#include "src/trace_data.h"

#define trace_data_add_event(td, a, theme, ev)         \
  do {                                                 \
    (void)(theme);                                     \
    trace_event_matcher_t matcher = {};                \
    (trace_data_add_event)((td), (ev), &matcher, (a)); \
    trace_event_matcher_deinit(&matcher);              \
  } while (0)

#define track_organize(td, theme, out_tracks, out_min_ts, out_max_ts,     \
                       allocator)                                         \
  do {                                                                    \
    arena_t scratch_arena = {};                                           \
    arena_init(&scratch_arena, (allocator), 0);                           \
    ((void)(theme),                                                       \
     (track_organize)((td), (out_tracks), (out_min_ts), (out_max_ts),     \
                      (allocator), arena_get_allocator(&scratch_arena))); \
    arena_deinit(&scratch_arena);                                         \
  } while (0)

TEST(track_test, sort_events) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  trace_event_t ev1 = {};
  ev1.ts = 200;
  trace_data_add_event(td, a, theme_get_dark(), &ev1);

  trace_event_t ev2 = {};
  ev2.ts = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev2);

  track_t t = {};
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)0;
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)1;

  track_sort_events(&t, td, a);

  const size_t* event_indices = (const size_t*)t.event_indices.ptr;
  EXPECT_EQ(event_indices[0], 1u);
  EXPECT_EQ(event_indices[1], 0u);

  track_deinit(&t, a);
  trace_data_release(td, a);
}

TEST(track_test, update_max_dur) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  trace_event_t ev1 = {};
  ev1.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev1);

  trace_event_t ev2 = {};
  ev2.dur = 500;
  trace_data_add_event(td, a, theme_get_dark(), &ev2);

  track_t t = {};
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)0;
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)1;

  track_update_max_dur(&t, td, a);

  EXPECT_EQ(t.max_dur, 500);

  track_deinit(&t, a);
  trace_data_release(td, a);
}

TEST(track_test, find_visible_start_index) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // Event 0: [0, 100]
  trace_event_t ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev0);

  // Event 1: [200, 300]
  trace_event_t ev1 = {};
  ev1.ts = 200;
  ev1.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev1);

  // Event 2: [400, 1000] - very long
  trace_event_t ev2 = {};
  ev2.ts = 400;
  ev2.dur = 600;
  trace_data_add_event(td, a, theme_get_dark(), &ev2);

  // Event 3: [1200, 1300]
  trace_event_t ev3 = {};
  ev3.ts = 1200;
  ev3.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev3);

  track_t t = {};
  for (size_t i = 0; i < 4; i++) {
    *array_list_push((array_list_t*)&t.event_indices, size_t, a) = i;
  }

  track_sort_events(&t, td, a);
  track_update_max_dur(&t, td, a);  // max_dur should be 600

  // Case 1: Viewport starts at 50. Event 0 starts at 0, dur 100.
  // It should be visible.
  EXPECT_EQ(track_find_visible_start_index(&t, td, 50), 0u);

  // Case 2: Viewport starts at 150. Event 0 ends at 100. Not visible.
  // Event 1 starts at 200.
  // track_find_visible_start_index returns the FIRST POSSIBLE event.
  EXPECT_EQ(track_find_visible_start_index(&t, td, 150), 0u);

  // Case 3: Viewport starts at 800.
  // Event 2 starts at 400, ends at 1000. It IS visible.
  EXPECT_EQ(track_find_visible_start_index(&t, td, 800), 1u);

  // Case 4: Viewport starts at 1100.
  // Event 2 ends at 1000. Not visible.
  // Event 3 starts at 1200.
  EXPECT_EQ(track_find_visible_start_index(&t, td, 1100), 3u);

  track_deinit(&t, a);
  trace_data_release(td, a);
}

TEST(track_test, calculate_depths) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // [0, 100]
  trace_event_t ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev0);

  // [10, 50] - nested in ev0
  trace_event_t ev1 = {};
  ev1.ts = 10;
  ev1.dur = 40;
  trace_data_add_event(td, a, theme_get_dark(), &ev1);

  // [10, 20] - nested in ev1, same start time as ev1 (if dur is shorter)
  trace_event_t ev2 = {};
  ev2.ts = 10;
  ev2.dur = 10;
  trace_data_add_event(td, a, theme_get_dark(), &ev2);

  // [60, 90] - nested in ev0, sequential to ev1
  trace_event_t ev3 = {};
  ev3.ts = 60;
  ev3.dur = 30;
  trace_data_add_event(td, a, theme_get_dark(), &ev3);

  // [110, 120] - sequential to ev0
  trace_event_t ev4 = {};
  ev4.ts = 110;
  ev4.dur = 10;
  trace_data_add_event(td, a, theme_get_dark(), &ev4);

  track_t t = {};
  for (size_t i = 0; i < 5; i++) {
    *array_list_push((array_list_t*)&t.event_indices, size_t, a) = i;
  }

  // We need to re-add them in a non-sorted order to test sorting too.
  array_list_clear((array_list_t*)&t.event_indices);
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)4;
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)2;
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)0;
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)3;
  *array_list_push((array_list_t*)&t.event_indices, size_t, a) = (size_t)1;

  track_sort_events(&t, td, a);
  track_calculate_depths(&t, td, a);

  // Sorted order should be ev0, ev1, ev2, ev3, ev4 because of our new sorting
  // rules.
  const size_t* event_indices = (const size_t*)t.event_indices.ptr;
  EXPECT_EQ(event_indices[0], 0u);
  EXPECT_EQ(event_indices[1], 1u);
  EXPECT_EQ(event_indices[2], 2u);
  EXPECT_EQ(event_indices[3], 3u);
  EXPECT_EQ(event_indices[4], 4u);

  const uint32_t* depths = (const uint32_t*)t.depths.ptr;
  EXPECT_EQ(depths[0], 0u);
  EXPECT_EQ(depths[1], 1u);
  EXPECT_EQ(depths[2], 2u);
  EXPECT_EQ(depths[3], 1u);
  EXPECT_EQ(depths[4], 0u);

  EXPECT_EQ(t.max_depth, 2u);

  const int64_t* self_durs = (const int64_t*)t.self_durs.ptr;
  EXPECT_EQ(self_durs[0], 30);  // ev0: 100 - ev1(40) - ev3(30) = 30
  EXPECT_EQ(self_durs[1], 30);  // ev1: 40 - ev2(10) = 30
  EXPECT_EQ(self_durs[2], 10);  // ev2: 10 (no children)
  EXPECT_EQ(self_durs[3], 30);  // ev3: 30 (no children)
  EXPECT_EQ(self_durs[4], 10);  // ev4: 10 (no children)

  track_deinit(&t, a);
  trace_data_release(td, a);
}

TEST(track_test, calculate_depths_siblings) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // Parent [0, 100]
  trace_event_t ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev0);

  // Child 1 [10, 50]
  trace_event_t ev1 = {};
  ev1.ts = 10;
  ev1.dur = 40;
  trace_data_add_event(td, a, theme_get_dark(), &ev1);

  // Child 2 [50, 100] - starts when ev1 ends, ends when ev0 ends
  trace_event_t ev2 = {};
  ev2.ts = 50;
  ev2.dur = 50;
  trace_data_add_event(td, a, theme_get_dark(), &ev2);

  track_t t = {};
  for (size_t i = 0; i < 3; i++) {
    *array_list_push((array_list_t*)&t.event_indices, size_t, a) = i;
  }

  track_sort_events(&t, td, a);
  track_calculate_depths(&t, td, a);

  const uint32_t* depths = (const uint32_t*)t.depths.ptr;
  EXPECT_EQ(depths[0], 0u);  // ev0
  EXPECT_EQ(depths[1], 1u);  // ev1
  EXPECT_EQ(depths[2], 1u);  // ev2 - should be depth 1, not 0 or 2

  const int64_t* self_durs = (const int64_t*)t.self_durs.ptr;
  EXPECT_EQ(self_durs[0], 10);  // ev0: 100 - ev1(40) - ev2(50) = 10
  EXPECT_EQ(self_durs[1], 40);  // ev1: 40
  EXPECT_EQ(self_durs[2], 50);  // ev2: 50

  track_deinit(&t, a);
  trace_data_release(td, a);
}

TEST(track_test, calculate_depths_duplicates) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // Two events with same ts and dur
  trace_event_t ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev0);

  trace_event_t ev1 = {};
  ev1.ts = 0;
  ev1.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev1);

  track_t t = {};
  for (size_t i = 0; i < 2; i++) {
    *array_list_push((array_list_t*)&t.event_indices, size_t, a) = i;
  }

  track_sort_events(&t, td, a);
  track_calculate_depths(&t, td, a);

  // Since they are identical, one will be a child of another.
  const uint32_t* depths = (const uint32_t*)t.depths.ptr;
  EXPECT_EQ(depths[0], 0u);
  EXPECT_EQ(depths[1], 1u);

  const int64_t* self_durs = (const int64_t*)t.self_durs.ptr;
  EXPECT_EQ(self_durs[0],
            0);  // ev0: 100 - ev1(100) = 0 (overlapped by duplicate)
  EXPECT_EQ(self_durs[1], 100);  // ev1: 100

  track_deinit(&t, a);
  trace_data_release(td, a);
}

TEST(track_test, calculate_depths_non_strict) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // Parent [0, 100]
  trace_event_t ev0 = {};
  ev0.ts = 0;
  ev0.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev0);

  // Child [10, 110] - outlives parent!
  trace_event_t ev1 = {};
  ev1.ts = 10;
  ev1.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &ev1);

  // New [105, 120] - sequential to ev0, should be at depth 0
  trace_event_t ev2 = {};
  ev2.ts = 105;
  ev2.dur = 15;
  trace_data_add_event(td, a, theme_get_dark(), &ev2);

  track_t t = {};
  for (size_t i = 0; i < 3; i++) {
    *array_list_push((array_list_t*)&t.event_indices, size_t, a) = i;
  }

  track_sort_events(&t, td, a);
  track_calculate_depths(&t, td, a);

  const uint32_t* depths = (const uint32_t*)t.depths.ptr;
  EXPECT_EQ(depths[0], 0u);  // ev0
  EXPECT_EQ(
      depths[1],
      0u);  // ev1 - moved up to depth 0 because ev0 doesn't strictly contain it
  EXPECT_EQ(depths[2], 0u);  // ev2

  const int64_t* self_durs = (const int64_t*)t.self_durs.ptr;
  EXPECT_EQ(self_durs[0], 100);  // ev0: 100 (ev1 does not strictly fit)
  EXPECT_EQ(self_durs[1], 100);  // ev1: 100 (ev2 does not strictly fit)
  EXPECT_EQ(self_durs[2], 15);   // ev2: 15

  track_deinit(&t, a);
  trace_data_release(td, a);
}

TEST(track_test, organize_tracks_empty) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  array_list_t tracks = {};
  int64_t min_ts = -1, max_ts = -1;
  track_organize(td, theme_get_dark(), &tracks, &min_ts, &max_ts, a);

  EXPECT_EQ(tracks.len, 0u);
  // min_ts/max_ts are not updated if no events
  EXPECT_EQ(min_ts, -1);
  EXPECT_EQ(max_ts, -1);

  array_list_deinit(&tracks, a);
  trace_data_release(td, a);
}

TEST(track_test, organize_tracks_sorting) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  auto add_event = [&](int32_t pid, int32_t tid, int64_t ts) {
    trace_event_t e = {};
    e.ph = "X";
    e.pid = pid;
    e.tid = tid;
    e.ts = ts;
    e.dur = 10;
    trace_data_add_event(td, a, theme_get_dark(), &e);
  };

  auto add_sort_idx = [&](int32_t pid, int32_t tid, int32_t sort_idx) {
    trace_event_t m = {};
    m.ph = "M";
    m.pid = pid;
    m.tid = tid;
    m.name = "thread_sort_index";
    trace_arg_t arg = {"sort_index", "", 0.0};
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", sort_idx);
    arg.val = {buf, strlen(buf)};
    m.args = &arg;
    m.args_count = 1;
    trace_data_add_event(td, a, theme_get_dark(), &m);
  };

  // Add tracks in "random" order
  add_event(10, 1, 100);
  add_event(1, 2, 100);
  add_event(1, 1, 100);
  add_sort_idx(10, 1, -5);  // Should be first
  add_sort_idx(1, 2, 5);    // Should be last

  array_list_t tracks = {};
  int64_t min_ts, max_ts;
  track_organize(td, theme_get_dark(), &tracks, &min_ts, &max_ts, a);

  ASSERT_EQ(tracks.len, 3u);

  const track_t* tracks_data = (const track_t*)tracks.ptr;

  // 1. PID 10, TID 1 (sort_index -5)
  EXPECT_EQ(tracks_data[0].pid, 10);
  EXPECT_EQ(tracks_data[0].sort_index, -5);
  EXPECT_EQ(tracks_data[0].type, TRACK_TYPE_THREAD);

  // 2. PID 1, TID 1 (sort_index 0 default)
  EXPECT_EQ(tracks_data[1].pid, 1);
  EXPECT_EQ(tracks_data[1].tid, 1);
  EXPECT_EQ(tracks_data[1].sort_index, 0);
  EXPECT_EQ(tracks_data[1].type, TRACK_TYPE_THREAD);

  // 3. PID 1, TID 2 (sort_index 5)
  EXPECT_EQ(tracks_data[2].pid, 1);
  EXPECT_EQ(tracks_data[2].tid, 2);
  EXPECT_EQ(tracks_data[2].sort_index, 5);
  EXPECT_EQ(tracks_data[2].type, TRACK_TYPE_THREAD);

  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&((track_t*)tracks.ptr)[i], a);
  }
  array_list_deinit(&tracks, a);
  trace_data_release(td, a);
}

TEST(track_test, organize_tracks_metadata_only) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  trace_event_t m = {};
  m.ph = "M";
  m.pid = 1;
  m.tid = 1;
  m.name = "thread_name";
  trace_arg_t arg = {"name", "Meta Only", 0.0};
  m.args = &arg;
  m.args_count = 1;
  trace_data_add_event(td, a, theme_get_dark(), &m);

  array_list_t tracks = {};
  int64_t min_ts = -1, max_ts = -1;
  track_organize(td, theme_get_dark(), &tracks, &min_ts, &max_ts, a);

  EXPECT_EQ(tracks.len, 1u);
  const track_t* tracks_data = (const track_t*)tracks.ptr;
  EXPECT_EQ(trace_data_get_string(td, tracks_data[0].name_ref), "Meta Only");
  EXPECT_EQ(tracks_data[0].event_indices.len, 0u);

  // Viewport range should not be updated by metadata
  EXPECT_EQ(min_ts, 0);  // min_ts/max_ts are 0 as initialized in track_organize
  EXPECT_EQ(max_ts, 0);

  track_deinit(&((track_t*)tracks.ptr)[0], a);
  array_list_deinit(&tracks, a);
  trace_data_release(td, a);
}

TEST(track_test, organize_tracks_mixed_order) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // 1. Regular event
  trace_event_t e1 = {};
  e1.ph = "X";
  e1.pid = 1;
  e1.tid = 1;
  e1.ts = 500;
  e1.dur = 100;
  trace_data_add_event(td, a, theme_get_dark(), &e1);

  // 2. Metadata for same track
  trace_event_t m1 = {};
  m1.ph = "M";
  m1.pid = 1;
  m1.tid = 1;
  m1.name = "thread_name";
  trace_arg_t arg1 = {"name", "Mixed", 0.0};
  m1.args = &arg1;
  m1.args_count = 1;
  trace_data_add_event(td, a, theme_get_dark(), &m1);

  // 3. Regular event for another track
  trace_event_t e2 = {};
  e2.ph = "X";
  e2.pid = 2;
  e2.tid = 1;
  e2.ts = 100;
  e2.dur = 50;
  trace_data_add_event(td, a, theme_get_dark(), &e2);

  array_list_t tracks = {};
  int64_t min_ts, max_ts;
  track_organize(td, theme_get_dark(), &tracks, &min_ts, &max_ts, a);

  ASSERT_EQ(tracks.len, 2u);

  const track_t* tracks_data = (const track_t*)tracks.ptr;

  // Sorted by PID (both have sort_index 0)
  EXPECT_EQ(tracks_data[0].pid, 1);
  EXPECT_EQ(trace_data_get_string(td, tracks_data[0].name_ref), "Mixed");
  EXPECT_EQ(tracks_data[0].event_indices.len, 1u);
  const size_t* event_indices_0 =
      (const size_t*)tracks_data[0].event_indices.ptr;
  EXPECT_EQ(event_indices_0[0], 0u);

  EXPECT_EQ(tracks_data[1].pid, 2);
  EXPECT_EQ(tracks_data[1].event_indices.len, 1u);
  const size_t* event_indices_1 =
      (const size_t*)tracks_data[1].event_indices.ptr;
  EXPECT_EQ(event_indices_1[0], 2u);

  EXPECT_EQ(min_ts, 100);
  EXPECT_EQ(max_ts, 600);

  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&((track_t*)tracks.ptr)[i], a);
  }
  array_list_deinit(&tracks, a);
  trace_data_release(td, a);
}

TEST(track_test, organize_tracks_counters) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // 1. Regular thread event
  trace_event_t e1 = {};
  e1.ph = "X";
  e1.pid = 1;
  e1.tid = 1;
  e1.ts = 100;
  trace_data_add_event(td, a, theme_get_dark(), &e1);

  // 2. Counter event for same PID
  trace_event_t c1 = {};
  c1.ph = "C";
  c1.name = "my_counter";
  c1.pid = 1;
  c1.tid = 1;  // TID is usually ignored for counters in grouping
  c1.ts = 150;
  trace_arg_t arg1 = {"val", "10", 10.0};
  c1.args = &arg1;
  c1.args_count = 1;
  trace_data_add_event(td, a, theme_get_dark(), &c1);

  // 3. Counter event with ID
  trace_event_t c2 = {};
  c2.ph = "C";
  c2.name = "my_counter";
  c2.id = "1";
  c2.pid = 1;
  c2.ts = 200;
  trace_data_add_event(td, a, theme_get_dark(), &c2);

  array_list_t tracks = {};
  int64_t min_ts, max_ts;
  track_organize(td, theme_get_dark(), &tracks, &min_ts, &max_ts, a);

  ASSERT_EQ(tracks.len, 3u);

  const track_t* tracks_data = (const track_t*)tracks.ptr;

  // Counter track (no ID) - Type 0
  EXPECT_EQ(tracks_data[0].pid, 1);
  EXPECT_EQ(tracks_data[0].tid, -1);
  EXPECT_EQ(tracks_data[0].type, TRACK_TYPE_COUNTER);
  EXPECT_EQ(trace_data_get_string(td, tracks_data[0].name_ref), "my_counter");
  EXPECT_EQ(tracks_data[0].id_ref, 0u);

  // Counter track (with ID) - Type 0
  EXPECT_EQ(tracks_data[1].pid, 1);
  EXPECT_EQ(tracks_data[1].tid, -1);
  EXPECT_EQ(tracks_data[1].type, TRACK_TYPE_COUNTER);
  EXPECT_EQ(trace_data_get_string(td, tracks_data[1].name_ref), "my_counter");
  EXPECT_EQ(trace_data_get_string(td, tracks_data[1].id_ref), "1");

  // Thread track - Type 1
  EXPECT_EQ(tracks_data[2].pid, 1);
  EXPECT_EQ(tracks_data[2].tid, 1);
  EXPECT_EQ(tracks_data[2].type, TRACK_TYPE_THREAD);

  // Verify counter tracks have self_durs initialized to 0
  const int64_t* self_durs_0 = (const int64_t*)tracks_data[0].self_durs.ptr;
  EXPECT_EQ(self_durs_0[0], 0);

  const int64_t* self_durs_1 = (const int64_t*)tracks_data[1].self_durs.ptr;
  EXPECT_EQ(self_durs_1[0], 0);

  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&((track_t*)tracks.ptr)[i], a);
  }
  array_list_deinit(&tracks, a);
  trace_data_release(td, a);
}

TEST(track_test, organize_tracks_counters_sorting) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // Add counter events in non-alphabetical order
  trace_event_t c1 = {};
  c1.ph = "C";
  c1.name = "zebra";
  c1.pid = 1;
  trace_event_t c2 = {};
  c2.ph = "C";
  c2.name = "apple";
  c2.pid = 1;
  trace_event_t c3 = {};
  c3.ph = "C";
  c3.name = "apple";
  c3.id = "2";
  c3.pid = 1;
  trace_event_t c4 = {};
  c4.ph = "C";
  c4.name = "apple";
  c4.id = "1";
  c4.pid = 1;

  trace_data_add_event(td, a, theme_get_dark(), &c1);
  trace_data_add_event(td, a, theme_get_dark(), &c2);
  trace_data_add_event(td, a, theme_get_dark(), &c3);
  trace_data_add_event(td, a, theme_get_dark(), &c4);

  array_list_t tracks = {};
  int64_t min_ts, max_ts;
  track_organize(td, theme_get_dark(), &tracks, &min_ts, &max_ts, a);

  ASSERT_EQ(tracks.len, 4u);

  const track_t* tracks_data = (const track_t*)tracks.ptr;

  // Expected order (TRACK_TYPE_COUNTER is 0, so they all come before threads):
  // 1. apple (no id)
  // 2. apple (id 1)
  // 3. apple (id 2)
  // 4. zebra
  EXPECT_EQ(trace_data_get_string(td, tracks_data[0].name_ref), "apple");
  EXPECT_EQ(tracks_data[0].id_ref, 0u);

  EXPECT_EQ(trace_data_get_string(td, tracks_data[1].name_ref), "apple");
  EXPECT_EQ(trace_data_get_string(td, tracks_data[1].id_ref), "1");

  EXPECT_EQ(trace_data_get_string(td, tracks_data[2].name_ref), "apple");
  EXPECT_EQ(trace_data_get_string(td, tracks_data[2].id_ref), "2");

  EXPECT_EQ(trace_data_get_string(td, tracks_data[3].name_ref), "zebra");

  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&((track_t*)tracks.ptr)[i], a);
  }
  array_list_deinit(&tracks, a);
  trace_data_release(td, a);
}

TEST(track_test, organize_tracks_counters_sorting_ignore_case) {
  allocator_t a = allocator_get_default();
  trace_data_t* td = trace_data_create(a);

  // Add counter events with mixed case names
  trace_event_t c1 = {};
  c1.ph = "C";
  c1.name = "zebra";
  c1.pid = 1;
  trace_event_t c2 = {};
  c2.ph = "C";
  c2.name = "APPLE";
  c2.pid = 1;
  trace_event_t c3 = {};
  c3.ph = "C";
  c3.name = "apple";
  c3.id = "1";
  c3.pid = 1;

  trace_data_add_event(td, a, theme_get_dark(), &c1);
  trace_data_add_event(td, a, theme_get_dark(), &c2);
  trace_data_add_event(td, a, theme_get_dark(), &c3);

  array_list_t tracks = {};
  int64_t min_ts, max_ts;
  track_organize(td, theme_get_dark(), &tracks, &min_ts, &max_ts, a);

  ASSERT_EQ(tracks.len, 3u);

  const track_t* tracks_data = (const track_t*)tracks.ptr;

  // Expected order:
  // 1. APPLE (case-insensitive 'a' comes before 'z')
  // 2. apple (id 1)
  // 3. zebra
  EXPECT_EQ(trace_data_get_string(td, tracks_data[0].name_ref), "APPLE");
  EXPECT_EQ(trace_data_get_string(td, tracks_data[1].name_ref), "apple");
  EXPECT_EQ(trace_data_get_string(td, tracks_data[2].name_ref), "zebra");

  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&((track_t*)tracks.ptr)[i], a);
  }
  array_list_deinit(&tracks, a);
  trace_data_release(td, a);
}
