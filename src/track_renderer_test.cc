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
  TraceEvent e1 = {STR("e"), STR("cat"), STR("B"), STR(""), 100,
                   10,       0,          0,        nullptr, 0};
  TraceEvent e2 = {STR("e"), STR("cat"), STR("B"), STR(""), 112,
                   10,       0,          0,        nullptr, 0};
  TraceEvent e3 = {STR("e"), STR("cat"), STR("B"), STR(""), 125,
                   10,       0,          0,        nullptr, 0};

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

  // Viewport where these events are tiny (e.g., 10us is < 2px)
  // inner_width = 1000, duration = 10000 -> 1us = 0.1px. 10us = 1px.
  track_compute_render_blocks(&t, &td, 0, 10000, 1000.0f, 0.0f, -1, &state,
                              &blocks, allocator);

  // Should be coalesced into 1 block
  EXPECT_EQ(blocks.size, 1u);
  EXPECT_FLOAT_EQ(blocks[0].x1, 10.0f);  // 100 * 0.1
  EXPECT_FLOAT_EQ(blocks[0].x2, 13.5f);  // 125*0.1 + 10*0.1 = 13.5
  EXPECT_EQ(blocks[0].name_ref, 0u);

  track_deinit(&t, allocator);
}

TEST_F(TrackRendererTest, DoNotCoalesceDifferentColors) {
  Track t = {};
  TraceEvent e1 = {STR("e1"), STR("cat"), STR("B"), STR(""), 100,
                   10,        0,          0,        nullptr, 0};
  TraceEvent e2 = {STR("e2"), STR("cat"), STR("B"), STR(""), 112,
                   10,        0,          0,        nullptr, 0};

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

  // Should NOT be coalesced because colors differ
  // BUT the second one might be skipped if it falls in the same pixel range
  // (LOD) 112*0.1 = 11.2, 100*0.1+1 = 11.0. 11.2 > 11.0, so it's NOT skipped by
  // LOD.
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
