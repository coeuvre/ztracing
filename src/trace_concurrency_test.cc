#include "src/trace_concurrency.h"
#include <gtest/gtest.h>
#include "core/allocator.h"
#include "core/arena.h"
#include "src/trace_data.h"
#include "src/track.h"

static void add_event(trace_data_t* td, allocator_t* a, int32_t pid, int32_t tid,
                      const char* name, int64_t ts, int64_t dur) {
  trace_event_t e = {};
  e.ph = "X";
  e.pid = pid;
  e.tid = tid;
  e.name = name;
  e.ts = ts;
  e.dur = dur;
  trace_event_matcher_t matcher = {};
  trace_data_add_event(td, &e, &matcher, a);
  trace_event_matcher_deinit(&matcher);
}

TEST(trace_concurrency_test, basic) {
  allocator_t* a = c_allocator();
  trace_data_t* td = trace_data_create(a);

  // Thread 1: [1000, 2000]
  add_event(td, a, 1, 1, "task1", 1000, 1000);
  // Thread 2: [1500, 2500] (overlaps Thread 1)
  add_event(td, a, 1, 2, "task2", 1500, 1000);

  // Organize tracks
  darray_track_t tracks = {};
  int64_t min_ts, max_ts;
  arena_t* scratch_arena = arena_create();
  track_organize(td, &tracks, &min_ts, &max_ts, a, arena_get_allocator(scratch_arena));
  arena_destroy(scratch_arena);

  EXPECT_EQ(min_ts, 1000);
  EXPECT_EQ(max_ts, 2500);
  EXPECT_EQ(tracks.len, 2u);

  // Compute concurrency with 2 buckets
  // Bucket 0: [1000, 1750] (duration 750)
  //   Thread 1: overlaps [1000, 1750] (750) -> ratio 1.0
  //   Thread 2: overlaps [1500, 1750] (250) -> ratio 0.333
  //   Total concurrency: 1.333
  // Bucket 1: [1750, 2500] (duration 750)
  //   Thread 1: overlaps [1750, 2000] (250) -> ratio 0.333
  //   Thread 2: overlaps [1750, 2500] (750) -> ratio 1.0
  //   Total concurrency: 1.333

  darray_t(trace_concurrency_bucket_t) buckets = {};
  darray_resize(&buckets, 2, a);
  trace_concurrency_compute(&tracks, td, min_ts, max_ts, 2, buckets.ptr, a);

  const trace_concurrency_bucket_t* b = buckets.ptr;
  EXPECT_NEAR(b[0].average_concurrency, 1.333, 0.01);
  EXPECT_NEAR(b[1].average_concurrency, 1.333, 0.01);

  // Clean up
  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&((track_t*)tracks.ptr)[i], a);
  }
  darray_deinit(&tracks, a);
  darray_deinit(&buckets, a);
  trace_data_release(td, a);
}
