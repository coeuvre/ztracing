#include "src/trace_heatmap.h"

#include <gtest/gtest.h>

#include "core/allocator.h"
#include "src/array_list.h"
#include "src/trace_data.h"
#include "src/track.h"

class trace_heatmap_test : public ::testing::Test {
 protected:
  counting_allocator_t ca_;
  allocator_t allocator_;
  trace_data_t* td_;
  array_list_t tracks_;

  trace_heatmap_test()
      : ca_(counting_allocator_init(allocator_get_default())),
        allocator_(counting_allocator_get_allocator(&ca_)),
        td_(nullptr),
        tracks_({}) {}

  void SetUp() override { td_ = trace_data_create(allocator_); }

  void TearDown() override {
    // Clean up tracks
    if (tracks_.ptr != nullptr) {
      track_t* tracks_ptr = (track_t*)tracks_.ptr;
      for (size_t i = 0; i < tracks_.len; i++) {
        track_deinit(&tracks_ptr[i], allocator_);
      }
      array_list_deinit(&tracks_, allocator_);
    }
    trace_data_release(td_, allocator_);

    EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca_), 0u)
        << "Memory leak detected in trace_heatmap_test!";
  }
};

TEST_F(trace_heatmap_test, compute_heatmap_normal) {
  // 1. Setup mock trace events
  // Event A (track 0, ts = 500, Bucket 0, depth 0)
  trace_event_persisted_t e1 = {};
  e1.ts = 500;
  e1.dur = 100;
  size_t e1_idx = td_->events.len;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e1;

  // Event B (track 0, ts = 5500, Bucket 5, depth 0)
  trace_event_persisted_t e2 = {};
  e2.ts = 5500;
  e2.dur = 100;
  size_t e2_idx = td_->events.len;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e2;

  // Event C (track 1, ts = 5000, Bucket 5, depth 0)
  trace_event_persisted_t e3 = {};
  e3.ts = 5000;
  e3.dur = 100;
  size_t e3_idx = td_->events.len;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e3;

  // Event D (track 0, ts = 5600, Bucket 5, nested at depth 1)
  // Should be ignored in heat calculation because depth != 0
  trace_event_persisted_t e4 = {};
  e4.ts = 5600;
  e4.dur = 50;
  size_t e4_idx = td_->events.len;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e4;

  // 2. Setup mock tracks
  // track 0: holds Event A, Event B, and Event D
  track_t t0 = {};
  t0.type = TRACK_TYPE_THREAD;
  *array_list_push(&t0.event_indices, size_t, allocator_) = e1_idx;
  *array_list_push(&t0.depths, int, allocator_) = 0;  // Event A
  *array_list_push(&t0.event_indices, size_t, allocator_) = e2_idx;
  *array_list_push(&t0.depths, int, allocator_) = 0;  // Event B
  *array_list_push(&t0.event_indices, size_t, allocator_) = e4_idx;
  *array_list_push(&t0.depths, int, allocator_) = 1;  // Event D (ignored)
  *array_list_push(&tracks_, track_t, allocator_) = t0;

  // track 1: holds Event C
  track_t t1 = {};
  t1.type = TRACK_TYPE_THREAD;
  *array_list_push(&t1.event_indices, size_t, allocator_) = e3_idx;
  *array_list_push(&t1.depths, int, allocator_) = 0;  // Event C
  *array_list_push(&tracks_, track_t, allocator_) = t1;

  // 3. Compute heatmap in-place on a pre-allocated buffer!
  array_list_t heatmap_list = {};
  array_list_resize(&heatmap_list, tracks_.len, sizeof(trace_heatmap_t),
                    allocator_);
  trace_heatmap_t* densities = (trace_heatmap_t*)heatmap_list.ptr;

  trace_heatmap_compute(&tracks_, td_, 0, 16000, densities);

  // 4. Verify densities
  ASSERT_EQ(heatmap_list.len, 2u);

  const trace_heatmap_t& h0 = densities[0];
  EXPECT_EQ(h0.event_indices[0], e1_idx);  // Event A
  EXPECT_EQ(h0.event_indices[5], e2_idx);  // Event B (Event D skipped)
  EXPECT_EQ(h0.event_indices[1], (size_t)-1);
  EXPECT_EQ(h0.event_indices[15], (size_t)-1);

  const trace_heatmap_t& h1 = densities[1];
  EXPECT_EQ(h1.event_indices[5], e3_idx);  // Event C
  EXPECT_EQ(h1.event_indices[0], (size_t)-1);
  EXPECT_EQ(h1.event_indices[15], (size_t)-1);

  array_list_deinit(&heatmap_list, allocator_);
}

TEST_F(trace_heatmap_test, compute_heatmap_zero_duration) {
  track_t t0 = {};
  t0.type = TRACK_TYPE_THREAD;
  *array_list_push(&tracks_, track_t, allocator_) = t0;

  array_list_t heatmap_list = {};
  array_list_resize(&heatmap_list, tracks_.len, sizeof(trace_heatmap_t),
                    allocator_);
  trace_heatmap_t* densities = (trace_heatmap_t*)heatmap_list.ptr;

  trace_heatmap_compute(&tracks_, td_, 0, 0, densities);

  ASSERT_EQ(heatmap_list.len, 1u);
  const trace_heatmap_t& h0 = densities[0];
  for (int b = 0; b < TRACE_HEATMAP_BUCKET_COUNT; b++) {
    EXPECT_EQ(h0.event_indices[b], (size_t)-1);
  }
  array_list_deinit(&heatmap_list, allocator_);
}

TEST_F(trace_heatmap_test, compute_heatmap_empty_inputs) {
  // Test NULL pointers - should exit cleanly
  trace_heatmap_compute(nullptr, nullptr, 0, 1000, nullptr);

  // Test empty tracks
  trace_heatmap_compute(&tracks_, td_, 0, 1000, nullptr);
}

TEST_F(trace_heatmap_test, compute_heatmap_counter_track) {
  // Setup mock counter event at ts = 5000 (Bucket 5)
  trace_event_persisted_t e = {};
  e.ts = 5000;
  e.dur = 100;
  size_t e_idx = td_->events.len;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e;

  // Setup counter track (does not filter by depth)
  track_t t0 = {};
  t0.type = TRACK_TYPE_COUNTER;
  *array_list_push(&t0.event_indices, size_t, allocator_) = e_idx;
  *array_list_push(&tracks_, track_t, allocator_) = t0;

  array_list_t heatmap_list = {};
  array_list_resize(&heatmap_list, tracks_.len, sizeof(trace_heatmap_t),
                    allocator_);
  trace_heatmap_t* densities = (trace_heatmap_t*)heatmap_list.ptr;

  trace_heatmap_compute(&tracks_, td_, 0, 16000, densities);

  ASSERT_EQ(heatmap_list.len, 1u);
  const trace_heatmap_t& h0 = densities[0];
  EXPECT_EQ(h0.event_indices[5], e_idx);  // Successfully map to Bucket 5
  EXPECT_EQ(h0.event_indices[0], (size_t)-1);

  array_list_deinit(&heatmap_list, allocator_);
}

TEST_F(trace_heatmap_test, compute_heatmap_out_of_bounds_index) {
  // Setup track with an invalid/out-of-bounds event index (e.g., 99999)
  track_t t0 = {};
  t0.type = TRACK_TYPE_THREAD;
  size_t invalid_idx = 99999;
  *array_list_push(&t0.event_indices, size_t, allocator_) = invalid_idx;
  *array_list_push(&t0.depths, int, allocator_) = 0;
  *array_list_push(&tracks_, track_t, allocator_) = t0;

  array_list_t heatmap_list = {};
  array_list_resize(&heatmap_list, tracks_.len, sizeof(trace_heatmap_t),
                    allocator_);
  trace_heatmap_t* densities = (trace_heatmap_t*)heatmap_list.ptr;

  // Run computation - should not crash!
  trace_heatmap_compute(&tracks_, td_, 0, 1000, densities);

  ASSERT_EQ(heatmap_list.len, 1u);
  const trace_heatmap_t& h0 = densities[0];
  for (int b = 0; b < TRACE_HEATMAP_BUCKET_COUNT; b++) {
    EXPECT_EQ(h0.event_indices[b], (size_t)-1);  // All must remain idle
  }

  array_list_deinit(&heatmap_list, allocator_);
}

TEST_F(trace_heatmap_test, compute_heatmap_viewport_clamping) {
  // Setup mock events
  // e1: ts = -500 (before viewport)
  trace_event_persisted_t e1 = {};
  e1.ts = -500;
  e1.dur = 100;
  size_t e1_idx = td_->events.len;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e1;

  // e2: ts = 20000 (after viewport)
  trace_event_persisted_t e2 = {};
  e2.ts = 20000;
  e2.dur = 100;
  size_t e2_idx = td_->events.len;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e2;

  // Setup track
  track_t t0 = {};
  t0.type = TRACK_TYPE_THREAD;
  *array_list_push(&t0.event_indices, size_t, allocator_) = e1_idx;
  *array_list_push(&t0.depths, int, allocator_) = 0;
  *array_list_push(&t0.event_indices, size_t, allocator_) = e2_idx;
  *array_list_push(&t0.depths, int, allocator_) = 0;
  *array_list_push(&tracks_, track_t, allocator_) = t0;

  array_list_t heatmap_list = {};
  array_list_resize(&heatmap_list, tracks_.len, sizeof(trace_heatmap_t),
                    allocator_);
  trace_heatmap_t* densities = (trace_heatmap_t*)heatmap_list.ptr;

  // Viewport: 0 to 16000
  trace_heatmap_compute(&tracks_, td_, 0, 16000, densities);

  ASSERT_EQ(heatmap_list.len, 1u);
  const trace_heatmap_t& h0 = densities[0];
  EXPECT_EQ(h0.event_indices[0], e1_idx);   // Clamped to Bucket 0
  EXPECT_EQ(h0.event_indices[15], e2_idx);  // Clamped to Bucket 15

  array_list_deinit(&heatmap_list, allocator_);
}
