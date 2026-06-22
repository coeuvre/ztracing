#include <chrono>
#include <cstdio>
#include <vector>

#include "src/allocator.h"
#include "src/colors.h"
#include "src/trace_data.h"
#include "src/trace_loader.h"
#include "src/track.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <trace_file>\n", argv[0]);
    return 1;
  }

  const char* filename = argv[1];

  // Read magic bytes and file size for metadata reporting
  FILE* f = fopen(filename, "rb");
  if (!f) {
    fprintf(stderr, "error: could not open file %s\n", filename);
    return 1;
  }

  unsigned char magic[2];
  size_t magic_read = fread(magic, 1, 2, f);

  fseek(f, 0, SEEK_END);
  size_t file_size = (size_t)ftell(f);
  fclose(f);

  bool is_gzip = (magic_read == 2 && magic[0] == 0x1f && magic[1] == 0x8b);

  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  // 1. Benchmark Ingestion (Read + Decompress + Parse + Add)
  size_t decompressed_size = 0;
  auto ingest_start = std::chrono::high_resolution_clock::now();
  trace_data_t* td = trace_loader_load_file(filename, a, &decompressed_size);
  auto ingest_end = std::chrono::high_resolution_clock::now();

  if (!td) {
    fprintf(stderr, "error: failed to load trace file %s\n", filename);
    return 1;
  }

  std::chrono::duration<double> ingest_diff = ingest_end - ingest_start;
  size_t event_count = td->events.len;

  // 2. Benchmark Track Organization
  auto organize_start = std::chrono::high_resolution_clock::now();

  array_list_t tracks = {};
  int64_t min_ts, max_ts;
  track_organize(td, &tracks, &min_ts, &max_ts, a);

  auto organize_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> organize_diff = organize_end - organize_start;

  double total_time = ingest_diff.count() + organize_diff.count();
  size_t consumed_mem = counting_allocator_get_allocated_bytes(&ca);

  double ingest_speed_disk_mb_s =
      ((double)file_size / (1024.0 * 1024.0)) / ingest_diff.count();
  double ingest_speed_stream_mb_s =
      ((double)decompressed_size / (1024.0 * 1024.0)) / ingest_diff.count();
  double ingest_speed_events_s = (double)event_count / ingest_diff.count();

  printf("----------------------------------------\n");
  printf("TRACE LOADING BENCHMARK\n");
  printf("----------------------------------------\n");
  printf("File Size (Disk):      %.2f MB\n",
         (double)file_size / (1024.0 * 1024.0));
  if (is_gzip) {
    printf("Decompressed Size:     %.2f MB (%.1fx compression)\n",
           (double)decompressed_size / (1024.0 * 1024.0),
           (double)decompressed_size / (double)file_size);
  }
  printf("Compression:           %s\n",
         is_gzip ? "GZIP (decompressing on-the-fly)" : "None");
  printf("Total Events:          %zu\n", event_count);
  printf("Total Tracks:          %zu\n", tracks.len);
  printf("----------------------------------------\n");
  if (is_gzip) {
    printf("Ingest Time (Parse+Add): %.3f s\n", ingest_diff.count());
    printf("  Throughput (Disk Read):   %.2f MB/s (compressed)\n",
           ingest_speed_disk_mb_s);
    printf("  Throughput (Decompress):  %.2f MB/s (decompressed)\n",
           ingest_speed_stream_mb_s);
    printf("  Ingestion Rate:           %.2f ev/s\n", ingest_speed_events_s);
  } else {
    printf("Ingest Time (Parse+Add): %.3f s\n", ingest_diff.count());
    printf("  Throughput (Disk Read):   %.2f MB/s\n", ingest_speed_disk_mb_s);
    printf("  Ingestion Rate:           %.2f ev/s\n", ingest_speed_events_s);
  }
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
  trace_data_release(td, a);

  return 0;
}
