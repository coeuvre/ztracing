#include <chrono>
#include <cstdio>
#include <vector>

#include "src/allocator.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/trace_parser.h"
#include "src/track.h"

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

  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  trace_parser_t p = {};
  trace_data_t td = {};
  trace_event_matcher_t matcher = {};

  char buf[65536];
  size_t event_count = 0;
  trace_event_t event;

  // 1. Benchmark Parsing + Ingestion
  auto ingest_start = std::chrono::high_resolution_clock::now();

  while (true) {
    size_t n = fread(buf, 1, sizeof(buf), f);
    bool is_eof = n < sizeof(buf);
    trace_parser_feed(&p, buf, n, is_eof, a);

    while (trace_parser_next(&p, &event, a)) {
      trace_data_add_event(&td, theme_get_dark(), &event, &matcher, a);
      event_count++;
    }

    if (is_eof) break;
  }

  auto ingest_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> ingest_diff = ingest_end - ingest_start;

  // 2. Benchmark Track Organization
  auto organize_start = std::chrono::high_resolution_clock::now();

  array_list_t tracks = {};
  int64_t min_ts, max_ts;
  track_organize(&td, theme_get_dark(), &tracks, &min_ts, &max_ts, a);

  auto organize_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> organize_diff = organize_end - organize_start;

  double total_time = ingest_diff.count() + organize_diff.count();
  size_t consumed_mem = counting_allocator_get_allocated_bytes(&ca);

  double ingest_speed_mb_s =
      ((double)file_size / (1024.0 * 1024.0)) / ingest_diff.count();
  double ingest_speed_events_s = (double)event_count / ingest_diff.count();

  printf("----------------------------------------\n");
  printf("TRACE LOADING BENCHMARK\n");
  printf("----------------------------------------\n");
  printf("File Size:             %.2f MB\n", (double)file_size / (1024.0 * 1024.0));
  printf("Total Events:          %zu\n", event_count);
  printf("Total Tracks:          %zu\n", tracks.len);
  printf("----------------------------------------\n");
  printf("Ingest Time (Parse+Add): %.3f s (%.2f MB/s, %.2f ev/s)\n",
         ingest_diff.count(), ingest_speed_mb_s, ingest_speed_events_s);
  printf("Track Organize Time:     %.3f ms (%.5f s)\n",
         organize_diff.count() * 1000.0, organize_diff.count());
  printf("Total Ingestion Time:    %.3f s\n", total_time);
  printf("----------------------------------------\n");
  printf("Consumed Memory:       %.2f MB (%zu bytes)\n",
         (double)consumed_mem / (1024.0 * 1024.0), consumed_mem);
  printf("----------------------------------------\n");

  // Deinit
  track_t* track_array = (track_t*)tracks.ptr;
  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&track_array[i], a);
  }
  array_list_deinit(&tracks, a);
  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
  trace_parser_deinit(&p, a);

  fclose(f);
  return 0;
}
