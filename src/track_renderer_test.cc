#include "src/track_renderer.h"

#include <gtest/gtest.h>

#include "src/allocator.h"
#include "src/colors.h"
#include "src/str.h"

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
  // Create 3 tiny events close to each other with same color (by using same
  // name)
  // inner_width = 1000, duration = 10000 -> 1us = 0.1px.
  // 5us = 0.5px (< 1.0px, so it's tiny).
  // Gap of 5us = 0.5px (exactly at the threshold).
  TraceEvent e1 = {STR("e"), STR("cat"), STR("B"), STR(""), 100,
                   5,        0,          0,        nullptr, 0};
  TraceEvent e2 = {STR("e"), STR("cat"), STR("B"), STR(""), 105,
                   5,        0,          0,        nullptr, 0};
  TraceEvent e3 = {STR("e"), STR("cat"), STR("B"), STR(""), 110,
                   5,        0,          0,        nullptr, 0};

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);

  EXPECT_EQ(td.events.size, 3u);
  EXPECT_EQ(td.events[0].color, td.events[1].color);
  EXPECT_EQ(td.events[0].color, td.events[2].color);

  t.event_indices.size = 0;
  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_push_back(&t.event_indices, allocator, (size_t)2);

  array_list_resize(&t.depths, allocator, 3);
  t.depths[0] = 0;
  t.depths[1] = 0;
  t.depths[2] = 0;
  t.max_depth = 0;

  // Viewport where these events are tiny (e.g., 5us is 0.5px < 1px)
  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  // Should be coalesced into 1 block
  EXPECT_EQ(blocks.size, 1u);
  EXPECT_FLOAT_EQ(blocks[0].x1, 10.0f);  // 100 * 0.1
  EXPECT_FLOAT_EQ(blocks[0].x2, 11.5f);  // 110*0.1 + 5*0.1 = 11.5
  EXPECT_NE(blocks[0].name_ref, 0u);
  EXPECT_EQ(blocks[0].count, 3u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CoalesceDifferentColors) {
  Track t = {};
  TraceEvent e1 = {STR("e1"), STR("cat"), STR("B"), STR(""), 100,
                   5,         0,          0,        nullptr, 0};
  TraceEvent e2 = {STR("e2"), STR("cat"), STR("B"), STR(""), 105,
                   5,         0,          0,        nullptr, 0};

  // Set different colors manually
  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  td.events[0].color = 0xFF0000FF;
  td.events[1].color = 0x00FF00FF;

  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_resize(&t.depths, allocator, 2);
  t.depths[0] = 0;
  t.depths[1] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  // Should be coalesced because colors no longer matter for tiny events
  EXPECT_EQ(blocks.size, 1u);
  EXPECT_EQ(blocks[0].count, 2u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, MultipleBlocksCloseTogether) {
  Track t = {};
  // Two tiny events with a 0.6px gap.
  // 1us = 0.1px.
  // e1: 100 to 105 -> 10.0 to 10.5.
  // e2: 111 to 116 -> 11.1 to 11.6.
  // Gap = 0.6px (> 0.5px threshold).
  TraceEvent e1 = {STR("e1"), STR("cat"), STR("B"), STR(""), 100,
                   5,         0,          0,        nullptr, 0};
  TraceEvent e2 = {STR("e2"), STR("cat"), STR("B"), STR(""), 111,
                   5,         0,          0,        nullptr, 0};

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);

  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_resize(&t.depths, allocator, 2);
  t.depths[0] = 0;
  t.depths[1] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  // We WANT these to be merged to avoid multiple tooltips, but currently
  // they produce 2 blocks because 0.6 > 0.5.
  EXPECT_EQ(blocks.size, 1u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, CullingAfterMergeFlush) {
  Track t = {};
  // Create 5 tiny events, 1us wide, 1us apart.
  // 1us = 0.1px.
  // E1: 100 to 101 -> 10.0 to 10.1
  // E2: 101 to 102 -> 10.1 to 10.2
  // E3: 130 to 131 -> 13.0 to 13.1
  // E4: 131 to 132 -> 13.1 to 13.2
  // E5: 132 to 133 -> 13.2 to 13.3

  TraceEvent e1 = {STR("e1"), STR("cat"), STR("B"), STR(""), 100, 1, 0, 0, nullptr, 0};
  TraceEvent e2 = {STR("e2"), STR("cat"), STR("B"), STR(""), 101, 1, 0, 0, nullptr, 0};
  TraceEvent e3 = {STR("e3"), STR("cat"), STR("B"), STR(""), 130, 1, 0, 0, nullptr, 0};
  TraceEvent e4 = {STR("e4"), STR("cat"), STR("B"), STR(""), 131, 1, 0, 0, nullptr, 0};
  TraceEvent e5 = {STR("e5"), STR("cat"), STR("B"), STR(""), 132, 1, 0, 0, nullptr, 0};

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e4);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e5);

  for (size_t i = 0; i < 5; i++) {
    array_list_push_back(&t.event_indices, allocator, i);
  }
  array_list_resize(&t.depths, allocator, 5);
  for (size_t i = 0; i < 5; i++) t.depths[i] = 0;
  t.max_depth = 0;

  // 1us = 0.1px.
  // E1+E2 will merge into [10.0, 10.2].
  // E3 is far away, so it flushes [10.0, 10.2].
  // E3 starts new merge.
  // E4 merges into E3 -> [13.0, 13.2].
  // E5 merges into E4 -> [13.0, 13.3].
  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  // We should have 2 blocks.
  EXPECT_EQ(blocks.size, 2u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventNeverSkipped) {
  Track t = {};
  TraceEvent e1 = {STR("e1"), STR("cat"), STR("B"), STR(""), 100,
                   10,        0,          0,        nullptr, 0};
  TraceEvent e2 = {
      STR("e2"), STR("cat"), STR("B"), STR(""), 101, 10,
      0,         0,          nullptr,  0};  // Overlaps e1 almost completely

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  td.events[0].color = 0xFF0000FF;
  td.events[1].color = 0xFF0000FF;

  array_list_push_back(&t.event_indices, allocator, (size_t)0);
  array_list_push_back(&t.event_indices, allocator, (size_t)1);
  array_list_resize(&t.depths, allocator, 2);
  t.depths[0] = 0;
  t.depths[1] = 0;
  t.max_depth = 0;

  // Normally e2 would be skipped by LOD or coalesced.
  // If e2 is selected, it must be drawn.
  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, 1, &state,
                              &blocks, allocator);

  EXPECT_EQ(blocks.size, 2u);
  EXPECT_TRUE(blocks[1].is_selected);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SameLaneOverlap) {
  Track t = {};
  // Two events that overlap but are not strictly nested.
  // E1: 100 to 200
  // E2: 150 to 250
  // Result: Both should be at depth 0.
  TraceEvent e1 = {STR("e1"), STR("cat"), STR("B"), STR(""), 100, 100, 0, 0, nullptr, 0};
  TraceEvent e2 = {STR("e2"), STR("cat"), STR("B"), STR(""), 150, 100, 0, 0, nullptr, 0};

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);

  for (size_t i = 0; i < 2; i++) {
    array_list_push_back(&t.event_indices, allocator, i);
  }
  array_list_resize(&t.depths, allocator, 2);
  t.depths[0] = 0;
  t.depths[1] = 0;
  t.max_depth = 0;

  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  // Both should be preserved as separate blocks on the same lane.
  // Hover logic in app.cc will pick the last one (E2) when hovering in [150, 200].
  EXPECT_EQ(blocks.size, 2u);
  EXPECT_EQ(blocks[0].depth, 0u);
  EXPECT_EQ(blocks[1].depth, 0u);
  EXPECT_FLOAT_EQ(blocks[0].x1, 10.0f);
  EXPECT_FLOAT_EQ(blocks[1].x1, 15.0f);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventOverlap) {
  Track t = {};
  // E1: 100 to 101 (tiny)
  // E2: 101 to 102 (tiny, SELECTED)
  // E3: 102 to 103 (tiny)
  TraceEvent e1 = {STR("e1"), STR("cat"), STR("B"), STR(""), 100, 1, 0, 0, nullptr, 0};
  TraceEvent e2 = {STR("e2"), STR("cat"), STR("B"), STR(""), 101, 1, 0, 0, nullptr, 0};
  TraceEvent e3 = {STR("e3"), STR("cat"), STR("B"), STR(""), 102, 1, 0, 0, nullptr, 0};

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);

  for (size_t i = 0; i < 3; i++) {
    array_list_push_back(&t.event_indices, allocator, i);
  }
  array_list_resize(&t.depths, allocator, 3);
  for (size_t i = 0; i < 3; i++) t.depths[i] = 0;
  t.max_depth = 0;

  // E2 (index 1) is selected.
  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, 1, &state,
                              &blocks, allocator);

  // E2 is selected, so it flushes E1 and is pushed separately.
  // E3 is tiny (10.2 to 10.3) and is culled against E2's end (10.2).
  // Result: RB1 (E1), RB2 (E2).
  // All on the same lane (depth 0).
  EXPECT_EQ(blocks.size, 2u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, SelectedEventNoOverlap) {
  Track t = {};
  // E1: 100 to 101
  // E2: 110 to 111 (SELECTED)
  // E3: 120 to 121
  TraceEvent e1 = {STR("e1"), STR("cat"), STR("B"), STR(""), 100, 1, 0, 0, nullptr, 0};
  TraceEvent e2 = {STR("e2"), STR("cat"), STR("B"), STR(""), 110, 1, 0, 0, nullptr, 0};
  TraceEvent e3 = {STR("e3"), STR("cat"), STR("B"), STR(""), 120, 1, 0, 0, nullptr, 0};

  trace_data_add_event(&td, allocator, theme_get_dark(), &e1);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e2);
  trace_data_add_event(&td, allocator, theme_get_dark(), &e3);

  for (size_t i = 0; i < 3; i++) {
    array_list_push_back(&t.event_indices, allocator, i);
  }
  array_list_resize(&t.depths, allocator, 3);
  for (size_t i = 0; i < 3; i++) t.depths[i] = 0;
  t.max_depth = 0;

  // E2 (index 1) is selected.
  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, 1, &state,
                              &blocks, allocator);

  // E2 is selected and far from E1 and E3.
  // Result: 3 blocks (E1, E2, E3).
  EXPECT_EQ(blocks.size, 3u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, ExtremeZoomOut) {
  Track t = {};
  // Create 1000 events, each 1us long, 1us apart.
  // Total span: 2000us.
  for (int i = 0; i < 1000; i++) {
    char name[16];
    snprintf(name, sizeof(name), "e%d", i);
    TraceEvent e = {str_from_cstr(name),
                    STR("cat"),
                    STR("B"),
                    STR(""),
                    (int64_t)i * 2,
                    1,
                    0,
                    0,
                    nullptr,
                    0};
    trace_data_add_event(&td, allocator, theme_get_dark(), &e);
    array_list_push_back(&t.event_indices, allocator, (size_t)i);
  }

  array_list_resize(&t.depths, allocator, 1000);
  for (size_t i = 0; i < 1000; i++) t.depths[i] = 0;
  t.max_depth = 0;

  // Viewport: 0 to 1,000,000us. inner_width = 1000px.
  // 1us = 0.001px.
  // 1000 events span 2000us = 2px total.
  track_compute_render_blocks(&t, &td, 0, 1000000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  // We should NOT have 1000 blocks.
  // Since they have different colors (names are e0, e1, ...),
  // they won't coalesce by color, but they should be skipped by LOD
  // if they fall in the same pixel range.
  // With x2 <= last_x2, many should be skipped.
  // However, because we use float x2, if they are 0.001px apart,
  // x2 will slightly increase each time, potentially bypassing the current
  // check.
  EXPECT_LT(blocks.size, 100u);

  track_deinit(&t, allocator);
}
