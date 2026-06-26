#include "src/trace_loader.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "src/assert.h"
#include "src/platform.h"
#include "src/task.h"
#include "src/trace_load_task.h"
#include "src/track.h"

static const size_t BACKPRESSURE_THRESHOLD = 32 * 1024 * 1024;
static const size_t IN_BUF_SIZE = 256 * 1024;    // 256KB compressed buffer
static const size_t OUT_BUF_SIZE = 1024 * 1024;  // 1MB decompressed chunk buffer

// Helper to reap a completed chunk completion from the queue.
// Decrements active reference counts, frees payload data, and adopts the completed
// trace data (and optionally organized tracks/stats) on EOF.
static bool reap_completion_sync(task_queue_t* queue, trace_data_t** out_td,
                                 array_list_t* out_tracks, int64_t* out_min_ts, int64_t* out_max_ts,
                                 double* out_ingest_duration_ms,
                                 double* out_organize_duration_ms,
                                 size_t* out_reaped_count, allocator_t a) {
  task_completion_t cqe;
  if (!task_queue_peek_completion(queue, &cqe)) {
    return true;
  }

  if (out_reaped_count) {
    (*out_reaped_count)++;
  }

  trace_load_task_chunk_t* payload = (trace_load_task_chunk_t*)cqe.user_data;
  bool success = (cqe.status == TASK_STATUS_OK);

  if (success) {
    if (payload->is_eof) {
      // Adopt the parsed trace data!
      *out_td = payload->completed_td;

      // Extract background telemetry stats
      if (out_ingest_duration_ms) *out_ingest_duration_ms = payload->stats.ingestion_duration_ms;
      if (out_organize_duration_ms) *out_organize_duration_ms = payload->stats.organize_duration_ms;

      // Adopt organized tracks and timestamps if requested
      if (out_tracks) {
        *out_tracks = payload->completed_tracks;
        if (out_min_ts) *out_min_ts = payload->completed_min_ts;
        if (out_max_ts) *out_max_ts = payload->completed_max_ts;
        // Clear payload array list to transfer ownership and prevent deinitialization below
        payload->completed_tracks = (array_list_t){};
      }

      // Deinitialize organized tracks if they weren't adopted (fallback path)
      track_t* tracks_data = (track_t*)payload->completed_tracks.ptr;
      for (size_t i = 0; i < payload->completed_tracks.len; i++) {
        track_deinit(&tracks_data[i], a);
      }
      array_list_deinit(&payload->completed_tracks, a);
    }
  }

  // Clean up completion payload resources
  if (payload->data != nullptr && payload->size > 0) {
    allocator_free(a, payload->data, payload->size);
  }
  trace_load_task_t* t = payload->task;
  allocator_free(a, payload, sizeof(trace_load_task_chunk_t));
  trace_load_task_release(t);
  task_queue_remove_completion(queue);

  return success;
}

// Synchronously loads a Chrome trace file, preferring success path under if and SESE.
trace_data_t* trace_loader_load_file(const char* filename, allocator_t a,
                                     size_t* out_decompressed_size,
                                     array_list_t* out_tracks,
                                     int64_t* out_min_ts,
                                     int64_t* out_max_ts,
                                     double* out_ingest_duration_ms,
                                     double* out_organize_duration_ms) {
  trace_data_t* td = nullptr;
  FILE* f = fopen(filename, "rb");

  if (f) {
    // 1. Create a concurrent Task Queue (dispatched to background thread pool) and Loading Task
    task_queue_t* queue = task_queue_create(1024, platform_submit_job, a);
    trace_load_task_t* load_task = trace_load_task_create(queue, 1, a);

    // Read first 2 bytes to check gzip magic
    unsigned char magic[2];
    size_t magic_read = fread(magic, 1, 2, f);

    // Seek back to the beginning of the file
    fseek(f, 0, SEEK_SET);

    bool is_gzip = (magic_read == 2 && magic[0] == 0x1f && magic[1] == 0x8b);
    bool init_success = true;
    size_t file_bytes_read = 0;
    size_t decompressed_size_accum = 0;
    size_t submitted_chunks = 0;
    size_t reaped_chunks = 0;

    // Allocate streaming buffers on the heap to prevent WASM Stack Overflow
    char* in_buf = (char*)allocator_alloc(a, IN_BUF_SIZE);
    char* out_buf = (char*)allocator_alloc(a, OUT_BUF_SIZE);
    bool is_eof = false;

    if (is_gzip) {
      z_stream strm = {};  // ZII
      if (inflateInit2(&strm, 16 + MAX_WBITS) == Z_OK) {
        int status = Z_OK;
        while (status != Z_STREAM_END && !is_eof && init_success) {
          if (strm.avail_in == 0) {
            size_t n = fread(in_buf, 1, IN_BUF_SIZE, f);
            file_bytes_read += n;
            if (n < IN_BUF_SIZE) {
              is_eof = true;
            }
            strm.next_in = (Bytef*)in_buf;
            strm.avail_in = (uInt)n;
          }

          do {
            strm.next_out = (Bytef*)out_buf;
            strm.avail_out = OUT_BUF_SIZE;

            status = inflate(&strm, Z_NO_FLUSH);
            if (status == Z_NEED_DICT || status == Z_DATA_ERROR ||
                status == Z_MEM_ERROR) {
              fprintf(stderr, "Error: Gzip decompression failed (code %d)\n",
                      status);
              init_success = false;
              break;
            }

            size_t decompressed_size = OUT_BUF_SIZE - strm.avail_out;
            if (decompressed_size > 0) {
              bool parser_eof = (status == Z_STREAM_END);

              // A. Apply backpressure: Block-wait if too many bytes are buffered in-flight
              while (trace_load_task_get_buffered_bytes(load_task) > BACKPRESSURE_THRESHOLD && init_success) {
                task_completion_t cqe;
                task_queue_wait_completion(queue, &cqe);
                if (!reap_completion_sync(queue, &td, out_tracks, out_min_ts, out_max_ts,
                                          out_ingest_duration_ms, out_organize_duration_ms,
                                          &reaped_chunks, a)) {
                  init_success = false;
                }
              }

              if (!init_success) break;

              // B. Submit the chunk to the background thread pool
              char* chunk_data = (char*)allocator_alloc(a, decompressed_size);
              memcpy(chunk_data, out_buf, decompressed_size);

              task_submission_t* sub = task_queue_get_submission(queue);
              if (sub == nullptr) {
                allocator_free(a, chunk_data, decompressed_size);
                init_success = false;
                break;
              }

              decompressed_size_accum += decompressed_size;
              trace_load_task_prep_chunk(load_task, sub, chunk_data, decompressed_size,
                                         file_bytes_read, parser_eof);
              task_queue_submit(queue);
              submitted_chunks++;

              // C. Non-blocking poll of completed chunks to keep the queue draining
              task_completion_t cqe;
              while (task_queue_peek_completion(queue, &cqe)) {
                if (!reap_completion_sync(queue, &td, out_tracks, out_min_ts, out_max_ts,
                                          out_ingest_duration_ms, out_organize_duration_ms,
                                          &reaped_chunks, a)) {
                  init_success = false;
                }
              }
            }
          } while (strm.avail_out == 0 && status != Z_STREAM_END && init_success);
        }
        inflateEnd(&strm);
      } else {
        fprintf(stderr, "Error: Failed to initialize zlib decompression\n");
        init_success = false;
      }
    } else {
      // Direct raw JSON reading loop
      while (!is_eof && init_success) {
        size_t n = fread(out_buf, 1, OUT_BUF_SIZE, f);
        file_bytes_read += n;
        if (n < OUT_BUF_SIZE) {
          is_eof = true;
        }

        // A. Apply backpressure: Block-wait if too many bytes are buffered in-flight
        while (trace_load_task_get_buffered_bytes(load_task) > BACKPRESSURE_THRESHOLD && init_success) {
          task_completion_t cqe;
          task_queue_wait_completion(queue, &cqe);
          if (!reap_completion_sync(queue, &td, out_tracks, out_min_ts, out_max_ts,
                                    out_ingest_duration_ms, out_organize_duration_ms,
                                    &reaped_chunks, a)) {
            init_success = false;
          }
        }

        if (!init_success) break;

        // B. Submit the chunk to the background thread pool
        char* chunk_data = (char*)allocator_alloc(a, n);
        memcpy(chunk_data, out_buf, n);

        task_submission_t* sub = task_queue_get_submission(queue);
        if (sub == nullptr) {
          allocator_free(a, chunk_data, n);
          init_success = false;
          break;
        }

        decompressed_size_accum += n;
        trace_load_task_prep_chunk(load_task, sub, chunk_data, n, file_bytes_read, is_eof);
        task_queue_submit(queue);
        submitted_chunks++;

        // C. Non-blocking poll of completed chunks to keep the queue draining
        task_completion_t cqe;
        while (task_queue_peek_completion(queue, &cqe)) {
          if (!reap_completion_sync(queue, &td, out_tracks, out_min_ts, out_max_ts,
                                    out_ingest_duration_ms, out_organize_duration_ms,
                                    &reaped_chunks, a)) {
            init_success = false;
          }
        }
      }
    }

    // 2. Abort if any error occurred, then drain all remaining in-flight chunks to prevent leaks
    if (!init_success) {
      trace_load_task_abort(load_task);
    }

    while (reaped_chunks < submitted_chunks) {
      task_completion_t cqe;
      task_queue_wait_completion(queue, &cqe);
      reap_completion_sync(queue, &td, out_tracks, out_min_ts, out_max_ts,
                           out_ingest_duration_ms, out_organize_duration_ms,
                           &reaped_chunks, a);
    }

    fclose(f);

    // Free heap-allocated streaming buffers
    allocator_free(a, in_buf, IN_BUF_SIZE);
    allocator_free(a, out_buf, OUT_BUF_SIZE);

    // Release the local loading task reference
    trace_load_task_release(load_task);

    // Destroy the local task queue
    task_queue_destroy(queue);

    if (!init_success) {
      if (td != nullptr) {
        trace_data_release(td, a);
        td = nullptr;
      }
    } else {
      if (out_decompressed_size) {
        *out_decompressed_size = decompressed_size_accum;
      }
    }
  } else {
    fprintf(stderr, "Error: Failed to open trace file '%s'\n", filename);
  }

  return td;
}
