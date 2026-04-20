#include "src/track_renderer.h"

#include <gtest/gtest.h>

#include "src/allocator.h"
#include "src/colors.h"

class TrackRendererTest : public ::testing::Test {
 protected:
  void SetUp() override {
    allocator = allocator_get_default();
    trace_data_init(&td, allocator);
    state = {};
    blocks = {};
  }

  void TearDown() override {
    array_list_deinit(&blocks, allocator);
    track_renderer_state_deinit(&state, allocator);
    trace_data_deinit(&td, allocator);
  }

  Allocator allocator;
  TraceData td;
  TrackRendererState state;
  ArrayList<TrackRenderBlock> blocks;
};

TEST_F(TrackRendererTest, CoalesceSameColor) {
  Track t = {};
  TraceEvent e1 = {}; e1.name = "e"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 5;
  TraceEvent e2 = {}; e2.name = "e"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 105; e2.dur = 5;
  TraceEvent e3 = {}; e3.name = "e"; e3.cat = "cat"; e3.ph = "B"; e3.ts = 110; e3.dur = 5;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);

  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_push_back(&t.event_indices, allocator, (size_t)2);

  array_list_resize(&t.depths, allocator, 3);
  t.depths[0] = 0; t.depths[1] = 0; t.depths[2] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  // Viewport: 0 to 10000us, 1000px. 1px = 10us. Bucket size = 3px = 30us.
  // Events: 100, 105, 110. All fall into bucket [90, 120)us.
  // Visual range: [9px, 12px).
  EXPECT_EQ(blocks.size, 1u);
  EXPECT_FLOAT_EQ(blocks[0].x1, 9.0f);
  EXPECT_FLOAT_EQ(blocks[0].x2, 12.0f);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, ThreadBucketingStability) {
  Track t = {};
  // Tiny events at 100, 105, 110.
  TraceEvent e1 = {}; e1.name = "e"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 1;
  TraceEvent e2 = {}; e2.name = "e"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 105; e2.dur = 1;
  TraceEvent e3 = {}; e3.name = "e"; e3.cat = "cat"; e3.ph = "B"; e3.ts = 110; e3.dur = 1;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);

  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_push_back(&t.event_indices, allocator, (size_t)2);

  array_list_resize(&t.depths, allocator, 3);
  t.depths[0] = 0; t.depths[1] = 0; t.depths[2] = 0;
  t.max_depth = 0;

  // Viewport A: 0 to 10000. 1px = 10us. Bucket = 30us.
  // First bucket start: floor(0/30)*30 = 0.
  // Bucket containing events: [90, 120).
  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);
  EXPECT_EQ(blocks.size, 1u);
  EXPECT_FLOAT_EQ(blocks[0].x1, 9.0f);
  EXPECT_FLOAT_EQ(blocks[0].x2, 12.0f);

  // Viewport B: 5 to 10005. Panned slightly.
  // Bucket size still 30us.
  // First bucket start: floor(5/30)*30 = 0.
  // Bucket containing events still: [90, 120).
  // x1 = (90 - 5) * 0.1 = 8.5
  // x2 = (120 - 5) * 0.1 = 11.5
  array_list_clear(&blocks);
  track_compute_render_blocks(&t, &td, 5, 10005, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);
  EXPECT_EQ(blocks.size, 1u);
  EXPECT_FLOAT_EQ(blocks[0].x1, 8.5f);
  EXPECT_FLOAT_EQ(blocks[0].x2, 11.5f);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CoalesceDifferentColors) {
  Track t = {};
  TraceEvent e1 = {}; e1.name = "e1"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 5;
  TraceEvent e2 = {}; e2.name = "e2"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 105; e2.dur = 5;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  td.events[0].color = 0xFF0000FF;
  td.events[1].color = 0x00FF00FF;

  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_resize(&t.depths, allocator, 2);
  t.depths[0] = 0; t.depths[1] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  EXPECT_EQ(blocks.size, 1u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, MultipleBlocksCloseTogether) {
  Track t = {};
  TraceEvent e1 = {}; e1.name = "e1"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 5;
  TraceEvent e2 = {}; e2.name = "e2"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 111; e2.dur = 5;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);

  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_resize(&t.depths, allocator, 2);
  t.depths[0] = 0; t.depths[1] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  EXPECT_EQ(blocks.size, 1u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CullingAfterMergeFlush) {
  Track t = {};
  TraceEvent e1 = {}; e1.name = "e1"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 1;
  TraceEvent e2 = {}; e2.name = "e2"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 101; e2.dur = 1;
  TraceEvent e3 = {}; e3.name = "e3"; e3.cat = "cat"; e3.ph = "B"; e3.ts = 130; e3.dur = 1;
  TraceEvent e4 = {}; e4.name = "e4"; e4.cat = "cat"; e4.ph = "B"; e4.ts = 131; e4.dur = 1;
  TraceEvent e5 = {}; e5.name = "e5"; e5.cat = "cat"; e5.ph = "B"; e5.ts = 132; e5.dur = 1;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e4);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e5);

  for (size_t i = 0; i < 5; i++) array_list_push_back(&t.event_indices, allocator, i);
  array_list_resize(&t.depths, allocator, 5);
  for (size_t i = 0; i < 5; i++) t.depths[i] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  EXPECT_EQ(blocks.size, 2u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventNeverSkipped) {
  Track t = {};
  TraceEvent e1 = {}; e1.name = "e1"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 10;
  TraceEvent e2 = {}; e2.name = "e2"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 101; e2.dur = 10;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  td.events[0].color = 0xFF0000FF;
  td.events[1].color = 0xFF0000FF;

  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_resize(&t.depths, allocator, 2);
  t.depths[0] = 0; t.depths[1] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, 1, &state,
                              &blocks, allocator);

  EXPECT_EQ(blocks.size, 2u);
  EXPECT_TRUE(blocks[1].is_selected);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SameLaneOverlap) {
  Track t = {};
  TraceEvent e1 = {}; e1.name = "e1"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 100;
  TraceEvent e2 = {}; e2.name = "e2"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 150; e2.dur = 100;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);

  for (size_t i = 0; i < 2; i++) array_list_push_back(&t.event_indices, allocator, i);
  array_list_resize(&t.depths, allocator, 2);
  t.depths[0] = 0; t.depths[1] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  EXPECT_EQ(blocks.size, 2u);
  EXPECT_EQ(blocks[0].depth, 0u);
  EXPECT_EQ(blocks[1].depth, 0u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventOverlap) {
  Track t = {};
  TraceEvent e1 = {}; e1.name = "e1"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 1;
  TraceEvent e2 = {}; e2.name = "e2"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 101; e2.dur = 1;
  TraceEvent e3 = {}; e3.name = "e3"; e3.cat = "cat"; e3.ph = "B"; e3.ts = 102; e3.dur = 1;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);

  for (size_t i = 0; i < 3; i++) array_list_push_back(&t.event_indices, allocator, i);
  array_list_resize(&t.depths, allocator, 3);
  for (size_t i = 0; i < 3; i++) t.depths[i] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, 1, &state,
                              &blocks, allocator);

  EXPECT_EQ(blocks.size, 2u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventNoOverlap) {
  Track t = {};
  TraceEvent e1 = {}; e1.name = "e1"; e1.cat = "cat"; e1.ph = "B"; e1.ts = 100; e1.dur = 1;
  TraceEvent e2 = {}; e2.name = "e2"; e2.cat = "cat"; e2.ph = "B"; e2.ts = 110; e2.dur = 1;
  TraceEvent e3 = {}; e3.name = "e3"; e3.cat = "cat"; e3.ph = "B"; e3.ts = 120; e3.dur = 1;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);

  for (size_t i = 0; i < 3; i++) array_list_push_back(&t.event_indices, allocator, i);
  array_list_resize(&t.depths, allocator, 3);
  for (size_t i = 0; i < 3; i++) t.depths[i] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, 1, &state,
                              &blocks, allocator);

  EXPECT_EQ(blocks.size, 3u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, ExtremeZoomOut) {
  Track t = {};
  for (int i = 0; i < 1000; i++) {
    char name[16];
    snprintf(name, sizeof(name), "e%d", i);
    TraceEvent e = {};
    e.name = name; e.cat = "cat"; e.ph = "B"; e.ts = (int64_t)i * 2; e.dur = 1;
    trace_data_add_event(&td, allocator, theme_get_dark(), &e);
    array_list_push_back(&t.event_indices, allocator, (size_t)i);
  }
  array_list_resize(&t.depths, allocator, 1000);
  for (size_t i = 0; i < 1000; i++) t.depths[i] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 1000000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  EXPECT_LT(blocks.size, 100u);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterBucketing) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;

  // E1: t=100, E2: t=101, E3: t=102, E4: t=200
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 101;
  TraceEvent e3 = {}; e3.name = "c"; e3.ts = 102;
  TraceEvent e4 = {}; e4.name = "c"; e4.ts = 200;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e4);

  for (size_t i = 0; i < 4; i++) array_list_push_back(&t.event_indices, allocator, i);

  ArrayList<CounterRenderBlock> c_blocks = {};
  // Viewport: 0 to 1000, 1000px. 1us = 1px. Bucket=3px.
  track_compute_counter_render_blocks(&t, &td, 0, 1000, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  // Should be few blocks due to merging and bucketing
  EXPECT_GT(c_blocks.size, 0u);
  EXPECT_LT(c_blocks.size, 50u);
  // First block should NOT be a gap
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);

  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterFirstEventGap) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 0, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  // Expect no blocks before 100, and no blocks after 150.
  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  EXPECT_NE(c_blocks[c_blocks.size-1].event_idx, (size_t)-1);

  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterMidViewport) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 50;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 100, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_EQ(c_blocks[0].event_idx, 0u); 
  EXPECT_NE(c_blocks[c_blocks.size-1].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterDrawTooFarLeft) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 50, 150, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1); 
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterCanvasOffset) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 0, 200, 1000.0f, 100.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_GE(c_blocks[0].x1, 100.0f);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterBeforeFirstEvent) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 0, 50, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_EQ(c_blocks.size, 0u);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterFirstEventAtStart) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 100, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  // Still 0 because viewport_start (100) >= track_last_ts (100)
  ASSERT_EQ(c_blocks.size, 0u);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterClampedGapBug) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 0;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 100;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 50, 150, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_EQ(c_blocks[0].event_idx, 0u);
  // Last block should be at 100 (track_last_ts)
  EXPECT_NE(c_blocks[c_blocks.size-1].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterViewportFarLeft) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, -200, -100, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_EQ(c_blocks.size, 0u);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterFirstEventJustBefore) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 90;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 100, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  // 0 because viewport_start (100) >= track_last_ts (90)
  ASSERT_EQ(c_blocks.size, 0u);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterSessionStartGap) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 0, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  // First block should NOT be a gap anymore
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  EXPECT_NE(c_blocks[c_blocks.size-1].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterExactStart) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 100, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}
TEST_F(TrackRendererTest, CounterViewportNegative) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, -100, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_NE(c_blocks[0].event_idx, (size_t)-1);
  EXPECT_NE(c_blocks[c_blocks.size-1].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterPartialStart) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 50;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 100, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_EQ(c_blocks[0].event_idx, 0u);
  EXPECT_NE(c_blocks[c_blocks.size-1].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterViewportFarRight) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 200, 300, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_EQ(c_blocks.size, 0u);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterLastEventAtEnd) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 100;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 200;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 0, 300, 3000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  // Last block should NOT be a gap anymore
  EXPECT_NE(c_blocks[c_blocks.size - 1].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterPeakPreservation) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;

  // series "a"
  TraceArg a1 = {"a", "10", 10.0};
  TraceArg a2 = {"a", "1", 1.0};
  
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 10; e1.args = &a1; e1.args_count = 1;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 11; e2.args = &a2; e2.args_count = 1;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_push_back(&t.counter_series, allocator, trace_data_push_string(&td, allocator, "a"));

  ArrayList<CounterRenderBlock> c_blocks = {};

  // Viewport: 0 to 100, 100px wide. 1us = 1px. Bucket = 3px = 3us.
  // E1 and E2 fall into same bucket [9, 12).
  track_compute_counter_render_blocks(&t, &td, 0, 100, 100.0f, 0.0f, &state, &c_blocks, allocator);

  bool found_bucket = false;
  for (size_t i = 0; i < c_blocks.size; i++) {
    if (c_blocks[i].x1 >= 9.0f - 0.001f && c_blocks[i].x2 <= 12.0f + 0.001f) {
      EXPECT_DOUBLE_EQ(state.counter_peaks[i * t.counter_series.size + 0], 10.0); // Peak preserved!
      found_bucket = true;
    }
  }
  EXPECT_TRUE(found_bucket);

  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterBucketingStability) {
  track_renderer_state_clear(&state);
  TrackRendererState state_b = {};

  Track t = {};
  t.type = TRACK_TYPE_COUNTER;

  // Event at t=10 and t=20
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 10;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 20;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);

  ArrayList<CounterRenderBlock> blocks_a = {};
  ArrayList<CounterRenderBlock> blocks_b = {};

  // Viewport A: 0 to 1000, 1000px wide. 1us = 1px. Bucket = 3px = 3us.
  // track_first_ts = 10, track_last_ts = 20.
  // floor(10 / 3) * 3 = 9.
  // First bucket: current_bucket_ts = 9, next_bucket_ts = 12.
  // draw_start_ts = max(9, 10) = 10, draw_end_ts = min(12, 20) = 12.
  // Block 0: x1 = 10, x2 = 12.
  track_compute_counter_render_blocks(&t, &td, 0, 1000, 1000.0f, 0.0f, &state, &blocks_a, allocator);

  // Viewport B: 1 to 1001, 1000px wide. Same scale.
  // Buckets still align from 10 -> 9.
  // First bucket: draw_start_ts = max(9, 10) = 10.
  // x1 = 10 - 1 = 9. x2 = 12 - 1 = 11.
  track_compute_counter_render_blocks(&t, &td, 1, 1001, 1000.0f, 0.0f, &state_b, &blocks_b, allocator);

  // Find the block containing t=11 in both.
  auto find_block_at = [](const ArrayList<CounterRenderBlock>& b, float x_offset) -> size_t {
    for (size_t i = 0; i < b.size; i++) {
      if (x_offset >= b[i].x1 - 0.001f && x_offset < b[i].x2 + 0.001f) return b[i].event_idx;
    }
    return (size_t)-2;
  };

  // In Viewport A, t=11 is at x=11.
  // In Viewport B, t=11 is at x=10 (11 - 1).
  EXPECT_EQ(find_block_at(blocks_a, 11.0f), 0u);
  EXPECT_EQ(find_block_at(blocks_b, 10.0f), 0u);

  // Check absolute boundary stability:
  bool found_stable_boundary = false;
  for (size_t i = 0; i < blocks_a.size; i++) {
    if (blocks_a[i].event_idx == 0u) {
       EXPECT_NEAR(blocks_a[i].x1, 10.0f, 0.01f);
       EXPECT_NEAR(blocks_a[i].x2, 12.0f, 0.01f);
       found_stable_boundary = true;
       break;
    }
  }
  EXPECT_TRUE(found_stable_boundary);

  found_stable_boundary = false;
  for (size_t i = 0; i < blocks_b.size; i++) {
    if (blocks_b[i].event_idx == 0u) {
       EXPECT_NEAR(blocks_b[i].x1, 9.0f, 0.01f);
       EXPECT_NEAR(blocks_b[i].x2, 11.0f, 0.01f);
       found_stable_boundary = true;
       break;
    }
  }
  EXPECT_TRUE(found_stable_boundary);

  array_list_deinit(&blocks_a, allocator);
  array_list_deinit(&blocks_b, allocator);
  track_renderer_state_deinit(&state_b, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterGapInitialState) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 50;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 75, 100, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  // 0 because viewport_start (75) >= track_last_ts (50)
  ASSERT_EQ(c_blocks.size, 0u);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterStartIdxBug) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 50;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 0, 50, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  // 0 because viewport_end (50) <= track_first_ts (50)
  ASSERT_EQ(c_blocks.size, 0u);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterMaxDurBug) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;
  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 50;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 150;
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  t.max_dur = 0;

  ArrayList<CounterRenderBlock> c_blocks = {};
  track_compute_counter_render_blocks(&t, &td, 100, 200, 1000.0f, 0.0f, &state, &c_blocks, allocator);

  ASSERT_GT(c_blocks.size, 0u);
  EXPECT_EQ(c_blocks[0].event_idx, 0u);
  EXPECT_NE(c_blocks[c_blocks.size-1].event_idx, (size_t)-1);
  array_list_deinit(&c_blocks, allocator);
  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CounterDropStubFix) {
  track_renderer_state_clear(&state);
  Track t = {};
  t.type = TRACK_TYPE_COUNTER;

  // Carry over value 100
  TraceArg a1 = {"a", "100", 100.0};
  // Drop to 10
  TraceArg a2 = {"a", "10", 10.0};

  TraceEvent e1 = {}; e1.name = "c"; e1.ts = 0; e1.args = &a1; e1.args_count = 1;
  TraceEvent e2 = {}; e2.name = "c"; e2.ts = 10; e2.args = &a2; e2.args_count = 1;

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_push_back(&t.counter_series, allocator, trace_data_push_string(&td, allocator, "a"));

  ArrayList<CounterRenderBlock> counter_blocks = {};

  // Viewport: 0 to 100, 100px wide. 1us = 1px. Bucket = 3px = 3us.
  // B1: [0, 3) contains E1(100). Peak=100.
  // B2: [3, 6) contains no events. Peak=100 (carry).
  // B3: [6, 9) contains no events. Peak=100 (carry).
  // B4: [9, 12) contains E2(10). Peak should be 10 (Fix stub!).
  track_compute_counter_render_blocks(&t, &td, 0, 100, 100.0f, 0.0f, &state, &counter_blocks, allocator);

  bool found_b4 = false;
  for (size_t i = 0; i < counter_blocks.size; i++) {
    if (counter_blocks[i].x1 >= 9.0f - 0.001f && counter_blocks[i].x2 <= 12.0f + 0.001f) {
      EXPECT_DOUBLE_EQ(state.counter_peaks[i * t.counter_series.size + 0], 10.0); // No more stub!
      found_b4 = true;
    }
  }
  EXPECT_TRUE(found_b4);

  array_list_deinit(&counter_blocks, allocator);
  track_deinit(&t, allocator);
}
