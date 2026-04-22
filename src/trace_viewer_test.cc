#include "src/trace_viewer.h"
#include <gtest/gtest.h>
#include "src/allocator.h"

class TraceViewerTest : public ::testing::Test {
protected:
    Allocator allocator;
    TraceViewer tv;
    TraceData td;

    void SetUp() override {
        allocator = allocator_get_default();
        tv = {};
        td = {};
    }

    void TearDown() override {
        trace_viewer_deinit(&tv, allocator);
        trace_data_deinit(&td, allocator);
    }
};

TEST_F(TraceViewerTest, ZoomInAroundMouse) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000000;
    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000000;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_width = 1000.0f;
    input.mouse_x = 500.0f; // Center
    input.mouse_wheel = 1.0f; // Zoom in
    input.is_ctrl_down = true;
    input.tracks_hovered = true;

    trace_viewer_step(&tv, &td, input, allocator);

    double new_duration = tv.viewport.end_time - tv.viewport.start_time;
    EXPECT_LT(new_duration, 1000000.0);
    // Should be centered around 500,000
    EXPECT_NEAR((tv.viewport.start_time + tv.viewport.end_time) * 0.5, 500000.0, 1.0);
}

TEST_F(TraceViewerTest, Panning) {
    tv.viewport.min_ts = 0;
    tv.viewport.max_ts = 1000000;
    tv.viewport.start_time = 100000;
    tv.viewport.end_time = 200000;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_width = 1000.0f;
    input.mouse_delta_x = 10.0f; // Pan right by 10 pixels
    input.is_mouse_down = true;
    input.tracks_hovered = true;

    // 10 pixels in 1000 pixels width is 1% of viewport duration (100,000) = 1,000 units
    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_NEAR(tv.viewport.start_time, 99000.0, 1.0);
    EXPECT_NEAR(tv.viewport.end_time, 199000.0, 1.0);
}

TEST_F(TraceViewerTest, HitTestingThreadEvent) {
    // Add a dummy event
    TraceEventPersisted e = {};
    e.ts = 500000;
    e.dur = 100000;
    e.name_ref = 0; 
    e.ph_ref = 0; // thread event
    array_list_push_back(&td.events, allocator, e);

    // Add a track
    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000000;
    tv.last_inner_width = 1000.0f;
    tv.last_tracks_x = 0;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_y = 0;
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.tracks_hovered = true;

    // Event is at [500,000, 600,000], which is [500px, 600px] on 1000px canvas
    // It is in the first lane (depth 0), which is at Y = ruler_height + lane_height = 40px
    input.mouse_x = 550.0f;
    input.mouse_y = 50.0f; // Middle of the lane [40, 60]

    trace_viewer_step(&tv, &td, input, allocator);

    ASSERT_EQ(tv.hover_matches.size, 1u);
    EXPECT_EQ(tv.hover_matches[0].track_idx, 0u);
    EXPECT_EQ(tv.hover_matches[0].rb.event_idx, 0u);
}

TEST_F(TraceViewerTest, SelectionOnClick) {
    // Add a dummy event
    TraceEventPersisted e = {};
    e.ts = 500000;
    e.dur = 100000;
    array_list_push_back(&td.events, allocator, e);

    Track t = {};
    t.type = TRACK_TYPE_THREAD;
    array_list_push_back(&t.event_indices, allocator, (size_t)0);
    track_update_max_dur(&t, &td);
    track_calculate_depths(&t, &td, allocator);
    array_list_push_back(&tv.tracks, allocator, t);

    tv.viewport.start_time = 0;
    tv.viewport.end_time = 1000000;
    tv.last_inner_width = 1000.0f;

    TraceViewerInput input = {};
    input.canvas_x = 0;
    input.canvas_y = 0;
    input.canvas_width = 1000.0f;
    input.canvas_height = 1000.0f;
    input.ruler_height = 20.0f;
    input.lane_height = 20.0f;
    input.tracks_hovered = true;
    input.mouse_x = 550.0f;
    input.mouse_y = 50.0f;
    input.is_mouse_released = true;
    input.drag_threshold = 5.0f;

    trace_viewer_step(&tv, &td, input, allocator);

    EXPECT_EQ(tv.selected_event_index, 0);
    EXPECT_TRUE(tv.show_details_panel);
}
