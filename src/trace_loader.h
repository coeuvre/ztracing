#ifndef SRC_TRACE_LOADER_H
#define SRC_TRACE_LOADER_H

#include "core/allocator.h"
#include "src/trace_data.h"

#ifdef __cplusplus
extern "C" {
#endif

// Synchronously loads a Chrome trace file (either raw JSON or gzipped JSON)
// and populates a new trace_data_t instance.
//
// Performance Features:
// 1. Transparent gzip decompression: Automatically detects compression via the
//    0x1f 0x8b magic number and decompresses on-the-fly.
// 2. In-memory streaming: Avoids creating temporary files on disk.
//
// Returns the populated trace_data_t, or nullptr on failure.
// If out_decompressed_size is not null, it will be populated with the total
// decompressed bytes fed to the parser.
// The returned pointer must be released by the caller using
// trace_data_release().
trace_data_t* trace_loader_load_file(const char* filename, allocator_t a,
                                     size_t* out_decompressed_size,
                                     array_list_t* out_tracks,
                                     int64_t* out_min_ts, int64_t* out_max_ts,
                                     double* out_ingest_duration_ms,
                                     double* out_organize_duration_ms);

#ifdef __cplusplus
}
#endif

#endif  // SRC_TRACE_LOADER_H
