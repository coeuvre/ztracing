#include <chrono>
#include <cstdio>
#include <vector>

#include "src/allocator.h"
#include "src/trace_parser.h"

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

  TraceParser p = trace_parser_init(a);

  char buf[65536];
  size_t event_count = 0;
  TraceEvent event;

  auto start = std::chrono::high_resolution_clock::now();

  while (true) {
    size_t n = fread(buf, 1, sizeof(buf), f);
    bool is_eof = n < sizeof(buf);
    trace_parser_feed(&p, buf, n, is_eof);

    while (trace_parser_next(&p, &event)) {
      event_count++;
    }

    if (is_eof) break;
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;

  size_t consumed_mem = counting_allocator_get_allocated_bytes(&ca);

  double speed_mb_s = ((double)file_size / (1024.0 * 1024.0)) / diff.count();
  double speed_events_s = (double)event_count / diff.count();

  printf("parsed %zu events in %.3f seconds\n", event_count, diff.count());
  printf("file size: %.2f MB\n", (double)file_size / (1024.0 * 1024.0));
  printf("parsing speed: %.2f MB/s (%.2f events/s)\n", speed_mb_s,
         speed_events_s);
  printf("consumed memory: %.2f MB (%zu bytes)\n",
         (double)consumed_mem / (1024.0 * 1024.0), consumed_mem);

  trace_parser_deinit(&p);
  fclose(f);

  return 0;
}
