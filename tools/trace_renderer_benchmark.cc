#include <chrono>
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

#include "src/allocator.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/trace_parser.h"
#include "src/track.h"
#include "src/track_renderer.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <trace_file>\n", argv[0]);
    return 1;
  }

  const char* filename = argv[1];
  FILE* f = fopen(filename, "rb");
  if (!f) {
    fprintf(stderr, "error: could not open file %s\n", filename);
    return 1;
  }

  fseek(f, 0, SEEK_END);
  size_t file_size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);

  CountingAllocator ca = counting_allocator_init(allocator_get_default());
  Allocator a = counting_allocator_get_allocator(&ca);

  TraceParser p = {};
  TraceData td = {};
  TraceEventMatcher matcher = {};

  char buf[65536];
  TraceEvent event;

  // Ingest Trace
  while (true) {
    size_t n = fread(buf, 1, sizeof(buf), f);
    bool is_eof = n < sizeof(buf);
    trace_parser_feed(&p, a, buf, n, is_eof);

    while (trace_parser_next(&p, a, &event)) {
      trace_data_add_event(&td, a, theme_get_dark(), &event, &matcher);
    }
    if (is_eof) break;
  }
  fclose(f);

  // Organize Tracks
  ArrayList<Track> tracks = {};
  int64_t min_ts, max_ts;
  track_organize(&td, a, theme_get_dark(), &tracks, &min_ts, &max_ts);

  // 1. Vertical Scan: Find the heaviest contiguous block of N tracks (Viewport)
  // that contains the maximum total number of events when fully zoomed out.
  const size_t VIEWPORT_TRACKS = 25;
  size_t best_track_start = 0;
  size_t max_viewport_events = 0;

  if (tracks.size >= VIEWPORT_TRACKS) {
    for (size_t i = 0; i <= tracks.size - VIEWPORT_TRACKS; i++) {
      size_t sum_events = 0;
      for (size_t j = 0; j < VIEWPORT_TRACKS; j++) {
        sum_events += tracks[i + j].event_indices.size;
      }
      if (sum_events > max_viewport_events) {
        max_viewport_events = sum_events;
        best_track_start = i;
      }
    }
  } else {
    best_track_start = 0;
    for (size_t i = 0; i < tracks.size; i++) {
      max_viewport_events += tracks[i].event_indices.size;
    }
  }

  // Count track types in the selected viewport for reporting
  size_t thread_track_count = 0;
  size_t counter_track_count = 0;
  size_t actual_viewport_tracks = std::min(VIEWPORT_TRACKS, tracks.size);
  for (size_t j = 0; j < actual_viewport_tracks; j++) {
    Track* t = &tracks[best_track_start + j];
    if (t->type == TRACK_TYPE_THREAD) {
      thread_track_count++;
    } else if (t->type == TRACK_TYPE_COUNTER) {
      counter_track_count++;
    }
  }

  printf("----------------------------------------\n");
  printf("TRACE RENDERER BENCHMARK (VERTICAL VIEWPORT SCAN)\n");
  printf("----------------------------------------\n");
  printf("File Size:             %.2f MB\n", (double)file_size / (1024.0 * 1024.0));
  printf("Total Tracks:          %zu\n", tracks.size);
  printf("Viewport Size:         %zu tracks\n", actual_viewport_tracks);
  printf("  Thread Tracks:       %zu\n", thread_track_count);
  printf("  Counter Tracks:      %zu\n", counter_track_count);
  printf("  Total Viewport Events: %zu\n", max_viewport_events);
  printf("  Hottest Track Block: index %zu to %zu\n", best_track_start, best_track_start + actual_viewport_tracks - 1);
  printf("----------------------------------------\n");

  const int ITERATIONS = 100;
  TrackRendererState state = {};
  
  // We will pre-allocate temporary lists for each track to avoid allocation overhead in the benchmark loop
  std::vector<ArrayList<TrackRenderBlock>> thread_blocks(actual_viewport_tracks);
  std::vector<ArrayList<CounterRenderBlock>> counter_blocks(actual_viewport_tracks);
  
  // Benchmark Full-Viewport Rendering (Fully Zoomed Out)
  auto start = std::chrono::high_resolution_clock::now();
  
  for (int iter = 0; iter < ITERATIONS; iter++) {
    for (size_t j = 0; j < actual_viewport_tracks; j++) {
      Track* t = &tracks[best_track_start + j];
      if (t->type == TRACK_TYPE_THREAD) {
        track_compute_render_blocks(t, &td, (double)min_ts, (double)max_ts, 1000.0f, 0.0f, -1, &state, &thread_blocks[j], a);
      } else if (t->type == TRACK_TYPE_COUNTER) {
        track_compute_counter_render_blocks(t, &td, (double)min_ts, (double)max_ts, 1000.0f, 0.0f, -1, &state, &counter_blocks[j], a);
      }
    }
    
    for (size_t j = 0; j < actual_viewport_tracks; j++) {
      array_list_clear(&thread_blocks[j]);
      array_list_clear(&counter_blocks[j]);
    }
  }
  
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> diff = end - start;
  
  printf("Full Viewport Render (Fully Zoomed Out):\n");
  printf("  Total Time:          %.3f ms\n", diff.count());
  printf("  Average Frame Time:  %.3f ms (avg of %d runs)\n", diff.count() / ITERATIONS, ITERATIONS);
  printf("----------------------------------------\n");

  // Deinit
  for (size_t j = 0; j < actual_viewport_tracks; j++) {
    array_list_deinit(&thread_blocks[j], a);
    array_list_deinit(&counter_blocks[j], a);
  }
  track_renderer_state_deinit(&state, a);
  for (size_t i = 0; i < tracks.size; i++) {
    track_deinit(&tracks[i], a);
  }
  array_list_deinit(&tracks, a);
  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
  return 0;
}
