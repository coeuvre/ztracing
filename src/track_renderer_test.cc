#include "src/track_renderer.h"

#include <gtest/gtest.h>

#include "core/allocator.h"
#include "src/colors.h"
#include "src/trace_data.h"

#define trace_data_add_event(td, a, theme, ev)         \
  do {                                                 \
    (void)(theme);                                     \
    trace_event_matcher_t matcher = {};                \
    (trace_data_add_event)((td), (ev), &matcher, (a)); \
    trace_event_matcher_deinit(&matcher);              \
  } while (0)

class TrackRendererTest : public ::testing::Test {
 protected:
  void SetUp() override {
    allocator = c_allocator();
    td = trace_data_create(allocator);
    state = {};
    blocks_impl = {};
    blocks = nullptr;
  }

  void TearDown() override {
    darray_deinit(&blocks_impl, allocator);
    blocks = blocks_impl.ptr;
    (void)blocks;
    track_renderer_state_deinit(&state, allocator);
    trace_data_release(td, allocator);
  }

  allocator_t* allocator;
  trace_data_t* td;
  track_renderer_state_t state;
  darray_track_render_block_t blocks_impl;
  track_render_block_t* blocks;
};

TEST_F(TrackRendererTest, CoalesceSameColor) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  trace_event_t e1 = {};
  e1.name = "e";
  e1.cat = "cat";
  e1.ph = "B";
  e1.ts = 100;
  e1.dur = 5;
  trace_event_t e2 = {};
  e2.name = "e";
  e2.cat = "cat";
  e2.ph = "B";
  e2.ts = 105;
  e2.dur = 5;
  trace_event_t e3 = {};
  e3.name = "e";
  e3.cat = "cat";
  e3.ph = "B";
  e3.ts = 110;
  e3.dur = 5;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(td, allocator, theme_get_dark(), &e3);

  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  darray_push(&t.event_indices, (size_t)2, allocator);

  darray_resize(&t.depths, 3, allocator);
  depths = (uint32_t*)t.depths.ptr;
  depths[0] = 0;
  depths[1] = 0;
  depths[2] = 0;
  t.max_depth = 0;

  darray_int64_t selected = {};
  (void)selected;
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  // Viewport: 0 to 10000us, 1000px. 1px = 10us. Bucket size = 3px = 30us.
  // Events: 100, 105, 110. All fall into bucket [90, 120)us.
  // Visual range: [9px, 12px).
  EXPECT_EQ(blocks_impl.len, 1u);
  EXPECT_FLOAT_EQ(blocks[0].x1, 9.0f);
  EXPECT_FLOAT_EQ(blocks[0].x2, 12.0f);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, ThreadBucketingStability) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  // e0: ts=85, dur=20 (85 to 105). Ends after 90 (start of first bucket when
  // panned to 95). e1, e2, e3: ts=100, 105, 110, dur=1. Tiny events.
  trace_event_t e0 = {};
  e0.name = "e0";
  e0.ts = 85;
  e0.dur = 20;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.ts = 100;
  e1.dur = 1;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.ts = 105;
  e2.dur = 1;
  trace_event_t e3 = {};
  e3.name = "e3";
  e3.ts = 110;
  e3.dur = 1;

  trace_data_add_event(td, allocator, theme_get_dark(), &e0);
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(td, allocator, theme_get_dark(), &e3);

  for (size_t i = 0; i < 4; ++i) darray_push(&t.event_indices, i, allocator);
  darray_resize(&t.depths, 4, allocator);
  depths = (uint32_t*)t.depths.ptr;
  for (size_t i = 0; i < 4; ++i) depths[i] = 0;
  t.max_depth = 0;
  t.max_dur = 20;

  // Viewport A: 0 to 10000. 1px = 10us. Bucket = 30us.
  // Buckets: [0, 30), [30, 60), [60, 90), [90, 120)...
  // e0 (85) in [60, 90).
  // e1, e2, e3 (100, 105, 110) in [90, 120).
  darray_int64_t selected = {};
  (void)selected;
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  uint32_t count_90_120 = 0;
  for (size_t i = 0; i < blocks_impl.len; ++i) {
    if (blocks[i].x1 == 9.0f) count_90_120 = blocks[i].count;
  }
  EXPECT_EQ(count_90_120, 3u);

  // Viewport B: 95 to 10095. Panned.
  // Bucket size 30us.
  // First bucket start: floor(95/30)*30 = 90.
  // Bucket: [90, 120).
  // Before fix, e0 (85) would be included in [90, 120) because it was the first
  // bucket. After fix, e0 should be SKIPPED for [90, 120) because it starts
  // before 90.
  darray_clear(&blocks_impl);
  track_compute_render_blocks(&t, td, 95, 10095, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  uint32_t count_90_120_panned = 0;
  for (size_t i = 0; i < blocks_impl.len; ++i) {
    if (blocks[i].x1 == -0.5f)
      count_90_120_panned = blocks[i].count;  // (90 - 95) * 0.1 = -0.5
  }
  EXPECT_EQ(count_90_120_panned, 3u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CoalesceDifferentColors) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.cat = "cat";
  e1.ph = "B";
  e1.ts = 100;
  e1.dur = 5;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.cat = "cat";
  e2.ph = "B";
  e2.ts = 105;
  e2.dur = 5;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  (((trace_event_persisted_t*)td->events.ptr)[0]).palette_index = 0;
  (((trace_event_persisted_t*)td->events.ptr)[1]).palette_index = 1;

  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  darray_resize(&t.depths, 2, allocator);
  depths = (uint32_t*)t.depths.ptr;
  depths[0] = 0;
  depths[1] = 0;
  t.max_depth = 0;

  darray_int64_t selected = {};
  (void)selected;
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  EXPECT_EQ(blocks_impl.len, 1u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, MultipleBlocksCloseTogether) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.cat = "cat";
  e1.ph = "B";
  e1.ts = 100;
  e1.dur = 5;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.cat = "cat";
  e2.ph = "B";
  e2.ts = 111;
  e2.dur = 5;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);

  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  darray_resize(&t.depths, 2, allocator);
  depths = (uint32_t*)t.depths.ptr;
  depths[0] = 0;
  depths[1] = 0;
  t.max_depth = 0;

  darray_int64_t selected = {};
  (void)selected;
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  EXPECT_EQ(blocks_impl.len, 1u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CullingAfterMergeFlush) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.cat = "cat";
  e1.ph = "B";
  e1.ts = 100;
  e1.dur = 1;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.cat = "cat";
  e2.ph = "B";
  e2.ts = 101;
  e2.dur = 1;
  trace_event_t e3 = {};
  e3.name = "e3";
  e3.cat = "cat";
  e3.ph = "B";
  e3.ts = 130;
  e3.dur = 1;
  trace_event_t e4 = {};
  e4.name = "e4";
  e4.cat = "cat";
  e4.ph = "B";
  e4.ts = 131;
  e4.dur = 1;
  trace_event_t e5 = {};
  e5.name = "e5";
  e5.cat = "cat";
  e5.ph = "B";
  e5.ts = 132;
  e5.dur = 1;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(td, allocator, theme_get_dark(), &e3);
  trace_data_add_event(td, allocator, theme_get_dark(), &e4);
  trace_data_add_event(td, allocator, theme_get_dark(), &e5);

  for (size_t i = 0; i < 5; i++) darray_push(&t.event_indices, i, allocator);
  darray_resize(&t.depths, 5, allocator);
  depths = (uint32_t*)t.depths.ptr;
  for (size_t i = 0; i < 5; i++) depths[i] = 0;
  t.max_depth = 0;

  darray_int64_t selected = {};
  (void)selected;
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  EXPECT_EQ(blocks_impl.len, 2u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventNeverSkipped) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.cat = "cat";
  e1.ph = "B";
  e1.ts = 100;
  e1.dur = 10;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.cat = "cat";
  e2.ph = "B";
  e2.ts = 101;
  e2.dur = 10;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  (((trace_event_persisted_t*)td->events.ptr)[0]).palette_index = 0;
  (((trace_event_persisted_t*)td->events.ptr)[1]).palette_index = 0;

  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  darray_resize(&t.depths, 2, allocator);
  depths = (uint32_t*)t.depths.ptr;
  depths[0] = 0;
  depths[1] = 0;
  t.max_depth = 0;

  darray_int64_t selected = {};
  darray_push(&selected, (int64_t)1, allocator);
  track_renderer_update_selection_bitset(&state, td, &selected, allocator);
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  EXPECT_EQ(blocks_impl.len, 2u);
  EXPECT_TRUE(blocks[1].is_selected);
  darray_deinit(&selected, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SameLaneOverlap) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.cat = "cat";
  e1.ph = "B";
  e1.ts = 100;
  e1.dur = 100;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.cat = "cat";
  e2.ph = "B";
  e2.ts = 150;
  e2.dur = 100;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);

  for (size_t i = 0; i < 2; i++) darray_push(&t.event_indices, i, allocator);
  darray_resize(&t.depths, 2, allocator);
  depths = (uint32_t*)t.depths.ptr;
  depths[0] = 0;
  depths[1] = 0;
  t.max_depth = 0;

  darray_int64_t selected = {};
  (void)selected;
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  EXPECT_EQ(blocks_impl.len, 2u);
  EXPECT_EQ(blocks[0].depth, 0u);
  EXPECT_EQ(blocks[1].depth, 0u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventOverlap) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.cat = "cat";
  e1.ph = "B";
  e1.ts = 100;
  e1.dur = 1;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.cat = "cat";
  e2.ph = "B";
  e2.ts = 101;
  e2.dur = 1;
  trace_event_t e3 = {};
  e3.name = "e3";
  e3.cat = "cat";
  e3.ph = "B";
  e3.ts = 102;
  e3.dur = 1;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(td, allocator, theme_get_dark(), &e3);

  for (size_t i = 0; i < 3; i++) darray_push(&t.event_indices, i, allocator);
  darray_resize(&t.depths, 3, allocator);
  depths = (uint32_t*)t.depths.ptr;
  for (size_t i = 0; i < 3; i++) depths[i] = 0;
  t.max_depth = 0;

  darray_int64_t selected = {};
  darray_push(&selected, (int64_t)1, allocator);
  track_renderer_update_selection_bitset(&state, td, &selected, allocator);
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  EXPECT_EQ(blocks_impl.len, 2u);
  darray_deinit(&selected, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventNoOverlap) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.cat = "cat";
  e1.ph = "B";
  e1.ts = 100;
  e1.dur = 1;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.cat = "cat";
  e2.ph = "B";
  e2.ts = 110;
  e2.dur = 1;
  trace_event_t e3 = {};
  e3.name = "e3";
  e3.cat = "cat";
  e3.ph = "B";
  e3.ts = 120;
  e3.dur = 1;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(td, allocator, theme_get_dark(), &e3);

  for (size_t i = 0; i < 3; i++) darray_push(&t.event_indices, i, allocator);
  darray_resize(&t.depths, 3, allocator);
  depths = (uint32_t*)t.depths.ptr;
  for (size_t i = 0; i < 3; i++) depths[i] = 0;
  t.max_depth = 0;

  darray_int64_t selected = {};
  darray_push(&selected, (int64_t)1, allocator);
  track_renderer_update_selection_bitset(&state, td, &selected, allocator);
  track_compute_render_blocks(&t, td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  EXPECT_EQ(blocks_impl.len, 3u);
  darray_deinit(&selected, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, ExtremeZoomOut) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  for (int i = 0; i < 1000; i++) {
    char name[16];
    snprintf(name, sizeof(name), "e%d", i);
    trace_event_t e = {};
    e.name = name;
    e.cat = "cat";
    e.ph = "B";
    e.ts = (int64_t)i * 2;
    e.dur = 1;
    trace_data_add_event(td, allocator, theme_get_dark(), &e);
    darray_push(&t.event_indices, (size_t)i, allocator);
  }
  darray_resize(&t.depths, 1000, allocator);
  depths = (uint32_t*)t.depths.ptr;
  for (size_t i = 0; i < 1000; i++) depths[i] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, td, 0, 1000000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  EXPECT_LT(blocks_impl.len, 100u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterBucketing) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;

  // E1: t=100, E2: t=101, E3: t=102, E4: t=200
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 101;
  trace_event_t e3 = {};
  e3.name = "c";
  e3.ts = 102;
  trace_event_t e4 = {};
  e4.name = "c";
  e4.ts = 200;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(td, allocator, theme_get_dark(), &e3);
  trace_data_add_event(td, allocator, theme_get_dark(), &e4);

  for (size_t i = 0; i < 4; i++) darray_push(&t.event_indices, i, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  // Viewport: 0 to 1000, 1000px. 1us = 1px. Bucket=3px.
  track_compute_counter_render_blocks(&t, td, 0, 1000, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  // Should be few blocks due to merging and bucketing
  EXPECT_GT(c_blocks_impl.len, 0u);
  EXPECT_LT(c_blocks_impl.len, 50u);
  // First block should NOT be a gap
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);

  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterFirstEventGap) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 0, 200, 1000.0f, 0.0f, -1, &state,
                                      &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  // Expect no blocks before 100, and no blocks after 150.
  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  EXPECT_NE(c_blocks[c_blocks_impl.len - 1].event_idx, (size_t)-1);

  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterMidViewport) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 50;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 100, 200, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_EQ(c_blocks[0].event_idx, 0u);
  EXPECT_NE(c_blocks[c_blocks_impl.len - 1].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterDrawTooFarLeft) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 50, 150, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterCanvasOffset) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 0, 200, 1000.0f, 100.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_GE(c_blocks[0].x1, 100.0f);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterBeforeFirstEvent) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  darray_push(&t.event_indices, (size_t)0, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 0, 50, 1000.0f, 0.0f, -1, &state,
                                      &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_EQ(c_blocks_impl.len, 0u);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterFirstEventAtStart) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  darray_push(&t.event_indices, (size_t)0, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 100, 200, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  // Still 0 because viewport_start (100) >= track_last_ts (100)
  ASSERT_EQ(c_blocks_impl.len, 0u);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterClampedGapBug) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 0;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 100;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 50, 150, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_EQ(c_blocks[0].event_idx, 0u);
  // Last block should be at 100 (track_last_ts)
  EXPECT_NE(c_blocks[c_blocks_impl.len - 1].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterViewportFarLeft) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  darray_push(&t.event_indices, (size_t)0, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, -200, -100, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_EQ(c_blocks_impl.len, 0u);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterFirstEventJustBefore) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 90;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  darray_push(&t.event_indices, (size_t)0, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 100, 200, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  // 0 because viewport_start (100) >= track_last_ts (90)
  ASSERT_EQ(c_blocks_impl.len, 0u);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterSessionStartGap) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 0, 200, 1000.0f, 0.0f, -1, &state,
                                      &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  // First block should NOT be a gap anymore
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  EXPECT_NE(c_blocks[c_blocks_impl.len - 1].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterExactStart) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 100, 200, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}
TEST_F(TrackRendererTest, CounterViewportNegative) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, -100, 200, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  EXPECT_NE(c_blocks[c_blocks_impl.len - 1].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterPartialStart) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 50;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 100, 200, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_EQ(c_blocks[0].event_idx, 0u);
  EXPECT_NE(c_blocks[c_blocks_impl.len - 1].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterViewportFarRight) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  darray_push(&t.event_indices, (size_t)0, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 200, 300, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_EQ(c_blocks_impl.len, 0u);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterLastEventAtEnd) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 200;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 0, 300, 3000.0f, 0.0f, -1, &state,
                                      &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  // Last block should NOT be a gap anymore
  EXPECT_NE(c_blocks[c_blocks_impl.len - 1].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterPeakPreservation) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;

  // series "a"
  trace_arg_t a1 = {"a", "10", 10.0};
  trace_arg_t a2 = {"a", "1", 1.0};

  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 10;
  e1.args = &a1;
  e1.args_count = 1;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 11;
  e2.args = &a2;
  e2.args_count = 1;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  darray_push(&t.counter_series, trace_data_push_string(td, SV("a"), allocator),
              allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;

  // Viewport: 0 to 100, 100px wide. 1us = 1px. Bucket = 3px = 3us.
  // E1 and E2 fall into same bucket [9, 12).
  track_compute_counter_render_blocks(&t, td, 0, 100, 100.0f, 0.0f, -1, &state,
                                      &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  bool found_bucket = false;
  for (size_t i = 0; i < c_blocks_impl.len; i++) {
    if (c_blocks[i].x1 >= 9.0f - 0.001f && c_blocks[i].x2 <= 12.0f + 0.001f) {
      EXPECT_DOUBLE_EQ(
          ((double*)state.counter_peaks.ptr)[i * t.counter_series.len + 0],
          10.0);  // Peak preserved!
      found_bucket = true;
    }
  }
  EXPECT_TRUE(found_bucket);

  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterBucketingStability) {
  track_renderer_state_clear(&state);
  track_renderer_state_t state_b = {};

  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;

  // Event at t=10 and t=20
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 10;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 20;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);

  darray_counter_render_block_t blocks_a_impl = {};
  counter_render_block_t* blocks_a = nullptr;
  darray_counter_render_block_t blocks_b_impl = {};
  counter_render_block_t* blocks_b = nullptr;

  // Viewport A: 0 to 1000, 1000px wide. 1us = 1px. Bucket = 3px = 3us.
  // track_first_ts = 10, track_last_ts = 20.
  // floor(10 / 3) * 3 = 9.
  // First bucket: current_bucket_ts = 9, next_bucket_ts = 12.
  // draw_start_ts = max(9, 10) = 10, draw_end_ts = min(12, 20) = 12.
  // Block 0: x1 = 10, x2 = 12.
  track_compute_counter_render_blocks(&t, td, 0, 1000, 1000.0f, 0.0f, -1,
                                      &state, &blocks_a_impl, allocator);
  blocks_a = blocks_a_impl.ptr;
  (void)blocks_a;

  // Viewport B: 1 to 1001, 1000px wide. Same scale.
  // Buckets still align from 10 -> 9.
  // First bucket: draw_start_ts = max(9, 10) = 10.
  // x1 = 10 - 1 = 9. x2 = 12 - 1 = 11.
  track_compute_counter_render_blocks(&t, td, 1, 1001, 1000.0f, 0.0f, -1,
                                      &state_b, &blocks_b_impl, allocator);
  blocks_b = blocks_b_impl.ptr;
  (void)blocks_b;

  // Find the block containing t=11 in both.
  auto find_block_at = [](const darray_counter_render_block_t& b_impl,
                          float x_offset) -> size_t {
    const counter_render_block_t* b = (const counter_render_block_t*)b_impl.ptr;
    for (size_t i = 0; i < b_impl.len; i++) {
      if (x_offset >= b[i].x1 - 0.001f && x_offset < b[i].x2 + 0.001f)
        return b[i].event_idx;
    }
    return (size_t)-2;
  };

  // In Viewport A, t=11 is at x=11.
  // In Viewport B, t=11 is at x=10 (11 - 1).
  EXPECT_EQ(find_block_at(blocks_a_impl, 11.0f), 0u);
  EXPECT_EQ(find_block_at(blocks_b_impl, 10.0f), 0u);

  // Check absolute boundary stability:
  bool found_stable_boundary = false;
  for (size_t i = 0; i < blocks_a_impl.len; i++) {
    if (blocks_a[i].event_idx == 0u) {
      EXPECT_NEAR(blocks_a[i].x1, 10.0f, 0.01f);
      EXPECT_NEAR(blocks_a[i].x2, 12.0f, 0.01f);
      found_stable_boundary = true;
      break;
    }
  }
  EXPECT_TRUE(found_stable_boundary);

  found_stable_boundary = false;
  for (size_t i = 0; i < blocks_b_impl.len; i++) {
    if (blocks_b[i].event_idx == 0u) {
      EXPECT_NEAR(blocks_b[i].x1, 9.0f, 0.01f);
      EXPECT_NEAR(blocks_b[i].x2, 11.0f, 0.01f);
      found_stable_boundary = true;
      break;
    }
  }
  EXPECT_TRUE(found_stable_boundary);

  darray_deinit(&blocks_a_impl, allocator);
  darray_deinit(&blocks_b_impl, allocator);
  track_renderer_state_deinit(&state_b, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterGapInitialState) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 50;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  darray_push(&t.event_indices, (size_t)0, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 75, 100, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  // 0 because viewport_start (75) >= track_last_ts (50)
  ASSERT_EQ(c_blocks_impl.len, 0u);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterStartIdxBug) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 50;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  darray_push(&t.event_indices, (size_t)0, allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 0, 50, 1000.0f, 0.0f, -1, &state,
                                      &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  // 0 because viewport_end (50) <= track_first_ts (50)
  ASSERT_EQ(c_blocks_impl.len, 0u);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterMaxDurBug) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 50;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  t.max_dur = 0;

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  track_compute_counter_render_blocks(&t, td, 100, 200, 1000.0f, 0.0f, -1,
                                      &state, &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  EXPECT_EQ(c_blocks[0].event_idx, 0u);
  EXPECT_NE(c_blocks[c_blocks_impl.len - 1].event_idx, (size_t)-1);
  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterDropStubFix) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;

  // Carry over value 100
  trace_arg_t a1 = {"a", "100", 100.0};
  // Drop to 10
  trace_arg_t a2 = {"a", "10", 10.0};

  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 0;
  e1.args = &a1;
  e1.args_count = 1;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 10;
  e2.args = &a2;
  e2.args_count = 1;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  darray_push(&t.counter_series, trace_data_push_string(td, SV("a"), allocator),
              allocator);

  darray_counter_render_block_t counter_blocks_impl = {};
  counter_render_block_t* counter_blocks = nullptr;

  // Viewport: 0 to 100, 100px wide. 1us = 1px. Bucket = 3px = 3us.
  // B1: [0, 3) contains E1(100). Peak=100.
  // B2: [3, 6) contains no events. Peak=100 (carry).
  // B3: [6, 9) contains no events. Peak=100 (carry).
  // B4: [9, 12) contains E2(10). Peak should be 10 (Fix stub!).
  track_compute_counter_render_blocks(&t, td, 0, 100, 100.0f, 0.0f, -1, &state,
                                      &counter_blocks_impl, allocator);
  counter_blocks = counter_blocks_impl.ptr;
  (void)counter_blocks;

  bool found_b4 = false;
  for (size_t i = 0; i < counter_blocks_impl.len; i++) {
    if (counter_blocks[i].x1 >= 9.0f - 0.001f &&
        counter_blocks[i].x2 <= 12.0f + 0.001f) {
      EXPECT_DOUBLE_EQ(
          ((double*)state.counter_peaks.ptr)[i * t.counter_series.len + 0],
          10.0);  // No more stub!
      found_b4 = true;
    }
  }
  EXPECT_TRUE(found_b4);

  darray_deinit(&counter_blocks_impl, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, ThreadBucketingStabilityPanned) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  // e0: ts=85, dur=20 (85 to 105)
  // e1: ts=100, dur=1
  // e2: ts=105, dur=1
  // e3: ts=110, dur=1
  trace_event_t e0 = {};
  e0.name = "e0";
  e0.ts = 85;
  e0.dur = 20;
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.ts = 100;
  e1.dur = 1;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.ts = 105;
  e2.dur = 1;
  trace_event_t e3 = {};
  e3.name = "e3";
  e3.ts = 110;
  e3.dur = 1;

  trace_data_add_event(td, allocator, theme_get_dark(), &e0);
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(td, allocator, theme_get_dark(), &e3);

  for (size_t i = 0; i < 4; ++i) darray_push(&t.event_indices, i, allocator);
  darray_resize(&t.depths, 4, allocator);
  depths = (uint32_t*)t.depths.ptr;
  for (size_t i = 0; i < 4; ++i) depths[i] = 0;
  t.max_depth = 0;
  t.max_dur = 20;

  float width = 1000.0f;  // 1px = 10us, Bucket = 30us

  // Viewport A: starts at 0.
  // Buckets: [0, 30), [30, 60), [60, 90), [90, 120)...
  // e0 (85) in [60, 90).
  // e1, e2, e3 (100, 105, 110) in [90, 120).
  track_compute_render_blocks(&t, td, 0, 10000, width, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  uint32_t count_90_120_A = 0;
  for (size_t i = 0; i < blocks_impl.len; ++i) {
    if (std::abs(blocks[i].x1 - 9.0f) < 0.001f)
      count_90_120_A = blocks[i].count;
  }
  EXPECT_EQ(count_90_120_A, 3u);

  // Viewport B: starts at 95.
  // current_bucket_ts should align to ... 60, 90, 120 ...
  // Bucket [90, 120) should still only contain e1, e2, e3.
  darray_clear(&blocks_impl);
  track_compute_render_blocks(&t, td, 95, 10095, width, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  uint32_t count_90_120_B = 0;
  for (size_t i = 0; i < blocks_impl.len; ++i) {
    // (90 - 95) * 0.1 = -0.5
    if (std::abs(blocks[i].x1 - (-0.5f)) < 0.001f)
      count_90_120_B = blocks[i].count;
  }
  EXPECT_EQ(count_90_120_B, 3u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, ThreadBucketingStabilityThreshold) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  // Bucket size = 30us. e0 dur = 30us.
  // At inv_duration = 0.1, e0 width = 3.0px.
  trace_event_t e0 = {};
  e0.name = "e0";
  e0.ts = 100;
  e0.dur = 30;
  trace_data_add_event(td, allocator, theme_get_dark(), &e0);

  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_resize(&t.depths, 1, allocator);
  depths = (uint32_t*)t.depths.ptr;
  depths[0] = 0;
  t.max_depth = 0;
  t.max_dur = 30;

  float width = 1000.0f;

  // Viewport A: duration 10000. inv_duration = 0.1. e0 width = 3.0.
  // It should be 'large'.
  track_compute_render_blocks(&t, td, 0, 10000, width, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;
  ASSERT_EQ(blocks_impl.len, 1u);
  EXPECT_EQ(blocks[0].count, 1u);
  EXPECT_TRUE(blocks[0].x2 - blocks[0].x1 >= 3.0f - 0.001f);

  // Viewport B: slightly shifted duration due to jitter.
  // inv_duration = 1000.0 / 10000.0000001 = 0.099999999999
  // width = 30 * 0.0999... = 2.9999...
  // Without epsilon, it might become 'tiny' and merged.
  // Even if alone, if it was 'large' it uses its own TS for x1, x2.
  // If it was 'tiny', it uses bucket boundaries for x1, x2.
  // Bucket for 100: [90, 120).
  // So x1 would jump from (100-0)*0.1 = 10.0 to (90-0)*0.1 = 9.0.
  // THAT is the dancing!
  darray_clear(&blocks_impl);
  track_compute_render_blocks(&t, td, 0, 10000.0001, width, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  ASSERT_EQ(blocks_impl.len, 1u);
  // We check if x1 is still based on e.ts (10.0) or bucket start (9.0).
  // With jitter, it should be near 10.0.
  EXPECT_NEAR(blocks[0].x1, 10.0f, 0.01f)
      << "Event x1 should be based on its own TS (large), not bucket start "
         "(tiny)";

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, ZoomedInSpanningEvent) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  // 1,000,000 events.
  // Event 0: starts at 0, dur 60s (60,000,000us).
  // Other 999,999 events: 10us each, starting at 60s+.
  trace_event_t e_monster = {};
  e_monster.name = "monster";
  e_monster.ts = 0;
  e_monster.dur = 60000000;
  trace_data_add_event(td, allocator, theme_get_dark(), &e_monster);

  for (int i = 0; i < 999999; i++) {
    trace_event_t e = {};
    e.name = "tiny";
    e.ts = 60000000 + i * 20;
    e.dur = 10;
    trace_data_add_event(td, allocator, theme_get_dark(), &e);
  }

  for (size_t i = 0; i < 1000000; i++) {
    darray_push(&t.event_indices, i, allocator);
  }

  darray_resize(&t.depths, 1000000, allocator);
  depths = (uint32_t*)t.depths.ptr;
  for (size_t i = 0; i < 1000000; i++) depths[i] = 0;
  t.max_depth = 0;

  // This will calculate block_max_durs.
  track_update_max_dur(&t, td, allocator);

  // Viewport zoomed into 100ms at the end of the monster event.
  double viewport_start = 59900000;  // 59.9s
  double viewport_end = 60000000;    // 60.0s
  float width = 1000.0f;

  // Measure time.
  auto start_time = std::chrono::high_resolution_clock::now();
  track_compute_render_blocks(&t, td, viewport_start, viewport_end, width, 0.0f,
                              -1, &state, &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;
  auto end_time = std::chrono::high_resolution_clock::now();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();

  // Without optimization, this could take >100ms or even seconds due to
  // millions of buckets. With optimization, it should be <1ms.
  // We'll be conservative and expect <10ms for CI environments.
  EXPECT_LT(elapsed_ms, 10);

  // Verify monster event is rendered.
  bool found_monster = false;
  for (size_t i = 0; i < blocks_impl.len; i++) {
    if (blocks[i].event_idx == 0) {
      found_monster = true;
      // It should span from before viewport to the end of viewport (in this
      // case).
      EXPECT_LE(blocks[i].x1, 0.0f);
      EXPECT_GE(blocks[i].x2, width);
    }
  }
  EXPECT_TRUE(found_monster);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CorrectnessSpanningAndCoalesced) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  // Block size is 1024.
  // Block 0:
  // Event 0: 0 to 2000. (Spanning)
  // Events 1-1023: 0 to 10. (Invisible)
  trace_event_t e_spanning = {};
  e_spanning.name = "spanning";
  e_spanning.ts = 0;
  e_spanning.dur = 2000;
  trace_data_add_event(td, allocator, theme_get_dark(), &e_spanning);
  darray_push(&t.event_indices, (size_t)0, allocator);

  for (int i = 1; i < 1024; i++) {
    trace_event_t e = {};
    e.name = "tiny_invis";
    e.ts = 0;
    e.dur = 10;
    trace_data_add_event(td, allocator, theme_get_dark(), &e);
    darray_push(&t.event_indices, (size_t)i, allocator);
  }

  // Block 1:
  // Events 1024-2047: starting at 1000, 1us duration, spaced by 1us.
  for (int i = 0; i < 1024; i++) {
    trace_event_t e = {};
    e.name = "tiny_vis";
    e.ts = 1000 + i * 2;
    e.dur = 1;
    trace_data_add_event(td, allocator, theme_get_dark(), &e);
    darray_push(&t.event_indices, (size_t)(1024 + i), allocator);
  }

  darray_resize(&t.depths, 2048, allocator);
  depths = (uint32_t*)t.depths.ptr;
  for (size_t i = 0; i < 1024; i++) depths[i] = 0;     // Spanning is depth 0
  for (size_t i = 1024; i < 2048; i++) depths[i] = 1;  // Tiny vis are depth 1
  t.max_depth = 1;

  track_update_max_dur(&t, td, allocator);

  // Viewport: 1000 to 1100. (100us)
  // Width: 1000px. 1px = 1us. Bucket = 3us.
  track_compute_render_blocks(&t, td, 1000, 1100, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  // Should have:
  // 1. Spanning event at depth 0.
  // 2. Coalesced blocks at depth 1.
  bool found_spanning = false;
  int depth1_blocks = 0;
  for (size_t i = 0; i < blocks_impl.len; i++) {
    if (blocks[i].depth == 0) {
      EXPECT_EQ(blocks[i].event_idx, 0u);
      found_spanning = true;
    } else if (blocks[i].depth == 1) {
      depth1_blocks++;
    }
  }
  EXPECT_TRUE(found_spanning);
  EXPECT_GT(depth1_blocks, 0);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, FocusedEventNeverSkipped) {
  track_t t = {};
  uint32_t* depths = (uint32_t*)t.depths.ptr;
  (void)depths;
  // Create two very small events that would normally be bucketed
  trace_event_t e1 = {};
  e1.name = "e1";
  e1.ts = 100;
  e1.dur = 1;
  trace_event_t e2 = {};
  e2.name = "e2";
  e2.ts = 101;
  e2.dur = 1;

  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);

  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  darray_resize(&t.depths, 2, allocator);
  depths = (uint32_t*)t.depths.ptr;
  depths[0] = 0;
  depths[1] = 0;
  t.max_depth = 0;

  // Viewport where events are tiny (1px each, bucket is 3px)
  // Inv duration = 1.0 (1us per pixel).
  // Bucket [99, 102).

  // 1. Without focus: they should be coalesced into 1 block
  track_compute_render_blocks(&t, td, 0, 1000, 1000.0f, 0.0f, -1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;
  EXPECT_EQ(blocks_impl.len, 1u);
  EXPECT_EQ(blocks[0].count, 2u);

  // 2. With focus on e2 (index 1): they should NOT be coalesced
  darray_clear(&blocks_impl);
  track_compute_render_blocks(&t, td, 0, 1000, 1000.0f, 0.0f, 1, &state,
                              &blocks_impl, allocator);
  blocks = blocks_impl.ptr;
  (void)blocks;

  // Should have 2 blocks:
  // - One bucket for e1 (count 1)
  // - One explicit block for e2 (is_focused = true)
  EXPECT_EQ(blocks_impl.len, 2u);
  bool found_focused = false;
  for (size_t i = 0; i < blocks_impl.len; i++) {
    if (blocks[i].is_focused) {
      EXPECT_EQ(blocks[i].event_idx, 1u);
      found_focused = true;
    }
  }
  EXPECT_TRUE(found_focused);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterFocusedHighlight) {
  track_renderer_state_clear(&state);
  track_t t = {};
  t.type = TRACK_TYPE_COUNTER;

  trace_arg_t arg = {"value", "10", 10.0};
  trace_event_t e1 = {};
  e1.name = "c";
  e1.ts = 100;
  e1.args = &arg;
  e1.args_count = 1;
  trace_event_t e2 = {};
  e2.name = "c";
  e2.ts = 150;
  e2.args = &arg;
  e2.args_count = 1;
  trace_data_add_event(td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(td, allocator, theme_get_dark(), &e2);
  darray_push(&t.event_indices, (size_t)0, allocator);
  darray_push(&t.event_indices, (size_t)1, allocator);
  darray_push(&t.counter_series,
              trace_data_push_string(td, SV("value"), allocator), allocator);

  darray_counter_render_block_t c_blocks_impl = {};
  counter_render_block_t* c_blocks = nullptr;
  // Focus on e1 (index 0)
  track_compute_counter_render_blocks(&t, td, 0, 200, 1000.0f, 0.0f, 0, &state,
                                      &c_blocks_impl, allocator);
  c_blocks = c_blocks_impl.ptr;
  (void)c_blocks;

  ASSERT_GT(c_blocks_impl.len, 0u);
  bool found_focused = false;
  for (size_t i = 0; i < c_blocks_impl.len; i++) {
    if (c_blocks[i].is_focused) {
      EXPECT_EQ(c_blocks[i].event_idx, 0u);
      found_focused = true;
    }
  }
  EXPECT_TRUE(found_focused);

  darray_deinit(&c_blocks_impl, allocator);
  track_deinit(&t, allocator);
}
