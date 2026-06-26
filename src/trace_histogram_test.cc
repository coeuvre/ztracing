#include "src/trace_histogram.h"

#include <gtest/gtest.h>

#include "core/allocator.h"
#include "src/trace_data.h"

class trace_histogram_test : public ::testing::Test {
 protected:
  counting_allocator_t ca_;
  allocator_t allocator_;
  trace_data_t* td_;

  trace_histogram_test()
      : ca_(counting_allocator_init(allocator_get_default())),
        allocator_(counting_allocator_get_allocator(&ca_)),
        td_(nullptr) {}

  void SetUp() override { td_ = trace_data_create(allocator_); }

  void TearDown() override {
    trace_data_release(td_, allocator_);
    EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca_), 0u)
        << "Memory leak detected in trace_histogram_test!";
  }
};

TEST_F(trace_histogram_test, compute_histogram) {
  // 1. Add some test events
  // Zero-duration events
  trace_event_persisted_t e0 = {};
  e0.ts = 100;
  e0.dur = 0;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e0;

  // Small-duration events
  trace_event_persisted_t e1 = {};
  e1.ts = 150;
  e1.dur = 50;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e1;

  // Large-duration events
  trace_event_persisted_t e2 = {};
  e2.ts = 200;
  e2.dur = 5000;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e2;

  // Setup input index list
  array_list_t selected_indices = {};
  *array_list_push(&selected_indices, int64_t, allocator_) = (int64_t)0;
  *array_list_push(&selected_indices, int64_t, allocator_) = (int64_t)1;
  *array_list_push(&selected_indices, int64_t, allocator_) = (int64_t)2;

  trace_histogram_t h = {};
  trace_histogram_compute(&selected_indices, td_, &h);

  EXPECT_GE(h.num_buckets, 2);
  EXPECT_TRUE(h.has_non_zero_durations);

  // Verify Zero-Duration bucket counts correctly
  EXPECT_EQ(h.buckets[0].min_dur, 0);
  EXPECT_EQ(h.buckets[0].max_dur, 0);
  EXPECT_EQ(h.buckets[0].count, 1u);

  array_list_deinit(&selected_indices, allocator_);
}

TEST_F(trace_histogram_test, compute_histogram_empty_results) {
  array_list_t empty_results = {};
  trace_histogram_t h = {};
  trace_histogram_compute(&empty_results, td_, &h);

  EXPECT_EQ(h.num_buckets, 0);
  EXPECT_EQ(h.total_count, 0u);
  EXPECT_FALSE(h.has_non_zero_durations);
}

TEST_F(trace_histogram_test, compute_histogram_linear_vs_logarithmic) {
  // 1. Linear Range Test (Range ratio: 200 / 10 = 20x <= 100x)
  trace_event_persisted_t e1 = {.ts = 100, .dur = 10};
  trace_event_persisted_t e2 = {.ts = 200, .dur = 100};
  trace_event_persisted_t e3 = {.ts = 300, .dur = 200};
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e1;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e2;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e3;

  array_list_t linear_results = {};
  *array_list_push(&linear_results, int64_t, allocator_) = 0;
  *array_list_push(&linear_results, int64_t, allocator_) = 1;
  *array_list_push(&linear_results, int64_t, allocator_) = 2;

  trace_histogram_t h_linear = {};
  trace_histogram_compute(&linear_results, td_, &h_linear);

  // The buckets should be linearly spaced.
  // Linear check: Bucket widths should be roughly equal (except rounding).
  int64_t width1 = h_linear.buckets[0].max_dur - h_linear.buckets[0].min_dur;
  int64_t width2 = h_linear.buckets[1].max_dur - h_linear.buckets[1].min_dur;
  EXPECT_NEAR((double)width1, (double)width2, 5.0);

  // 2. Logarithmic Range Test (Range ratio: 100000 / 2 = 50000x > 100x)
  trace_event_persisted_t e4 = {.ts = 400, .dur = 2};
  trace_event_persisted_t e5 = {.ts = 500, .dur = 1000};
  trace_event_persisted_t e6 = {.ts = 600, .dur = 100000};
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e4;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e5;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e6;

  array_list_t log_results = {};
  *array_list_push(&log_results, int64_t, allocator_) = 3;
  *array_list_push(&log_results, int64_t, allocator_) = 4;
  *array_list_push(&log_results, int64_t, allocator_) = 5;

  trace_histogram_t h_log = {};
  trace_histogram_compute(&log_results, td_, &h_log);

  // The buckets should be exponentially spaced (logarithmic).
  // Bucket widths must increase drastically!
  int64_t log_width_first = h_log.buckets[0].max_dur - h_log.buckets[0].min_dur;
  int64_t log_width_last = h_log.buckets[h_log.num_buckets - 1].max_dur -
                           h_log.buckets[h_log.num_buckets - 1].min_dur;
  EXPECT_GT(log_width_last,
            log_width_first * 100);  // Massive width difference!

  array_list_deinit(&linear_results, allocator_);
  array_list_deinit(&log_results, allocator_);
}

TEST_F(trace_histogram_test, compute_histogram_narrow_range_bin_clamping) {
  // Narrow range: min_dur = 10, max_dur = 13 (range = 3)
  trace_event_persisted_t e1 = {.ts = 100, .dur = 10};
  trace_event_persisted_t e2 = {.ts = 200, .dur = 13};
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e1;
  *array_list_push(&td_->events, trace_event_persisted_t, allocator_) = e2;

  array_list_t results = {};
  *array_list_push(&results, int64_t, allocator_) = 0;
  *array_list_push(&results, int64_t, allocator_) = 1;

  trace_histogram_t h = {};
  trace_histogram_compute(&results, td_, &h);

  // Since range is 3, bins should be clamped to range + 1 = 4 buckets!
  EXPECT_EQ(h.num_buckets, 4);
  EXPECT_EQ(h.buckets[0].min_dur, 10);
  EXPECT_EQ(h.buckets[3].max_dur, 13);

  array_list_deinit(&results, allocator_);
}

TEST_F(trace_histogram_test, compute_histogram_invalid_index_ignored) {
  // Push an invalid index (99999)
  array_list_t results = {};
  *array_list_push(&results, int64_t, allocator_) = 99999;

  trace_histogram_t h = {};
  // Should exit cleanly without crashing
  trace_histogram_compute(&results, td_, &h);

  EXPECT_EQ(h.num_buckets, 0);
  EXPECT_EQ(h.total_count,
            1u);  // Total count is recorded from results list length
  EXPECT_FALSE(h.has_non_zero_durations);

  array_list_deinit(&results, allocator_);
}
