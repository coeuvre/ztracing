#include "src/trace_load_task.h"

#include <stdbool.h>
#include <stddef.h>

#include "src/app_msg.h"
#include "src/assert.h"
#include "src/logging.h"
#include "src/platform.h"
#include "src/trace_data.h"
#include "src/trace_parser.h"
#include "src/track.h"

// Context structure for the background loader thread
typedef struct {
  const theme_t* theme;
  channel_t* app_channel;
  channel_t* trace_load_channel;
  allocator_t allocator;
  double start_time;
} trace_load_task_t;

// Background worker thread function
static void trace_load_run(void* arg) {
  trace_load_task_t* task = (trace_load_task_t*)arg;
  allocator_t allocator = task->allocator;

  LOG_DEBUG("trace_load_run background task started");

  trace_parser_t parser = {};          // ZII
  trace_event_matcher_t matcher = {};  // ZII

  // Allocate the persistent trace data shell
  trace_data_t* td = trace_data_create(allocator);

  size_t total_discarded_bytes = 0;
  size_t last_report_event_count = 0;
  bool aborted = false;

  double total_wait_time_ms = 0.0;
  trace_load_msg_t msg;
  // Block-receive loader commands (chunks or abort signals)
  while (true) {
    double wait_start = platform_get_now();
    bool received = channel_recv(task->trace_load_channel, &msg);
    double wait_duration = platform_get_now() - wait_start;

    // Accumulate wait time only after we have received the first chunk
    // to exclude initial stream startup latency.
    if (td->events.len > 0) {
      total_wait_time_ms += wait_duration;
    }

    if (!received) {
      aborted = true;
      break;
    }
    if (msg.type == MSG_TRACE_LOAD_ABORT) {
      aborted = true;
      break;
    }

    if (msg.type == MSG_TRACE_LOAD_CHUNK) {
      trace_load_chunk_t chunk = msg.as.chunk;

      // 1. Feed raw chunk into parser
      total_discarded_bytes += trace_parser_feed(
          &parser, chunk.data, chunk.size, chunk.is_eof, allocator);

      // 2. Release the raw chunk buffer immediately after feeding to keep
      // memory footprint low
      allocator_free(allocator, chunk.data, chunk.size);

      // 3. Parse all available events in this chunk
      trace_event_t event;
      while (trace_parser_next(&parser, &event, allocator)) {
        trace_data_add_event(td, task->theme, &event, &matcher, allocator);

        // Periodically check for abort signals mid-parse at chunk boundaries
      }

      // 4. Throttled progress updates: notify UI thread every 5000 events
      size_t current_count = td->events.len;
      if (current_count - last_report_event_count >= 5000) {
        last_report_event_count = current_count;
        app_send_load_progress(task->app_channel, current_count,
                               total_discarded_bytes + parser.pos);
      }

      // 5. Break if EOF chunk parsed successfully
      if (chunk.is_eof) {
        break;
      }
    }
  }

  if (aborted) {
    LOG_DEBUG("trace_load_run worker aborted by UI thread request");
    trace_data_release(td, allocator);

    // Notify App UI thread that loading was aborted
    app_send_load_aborted(task->app_channel);
  } else {
    double size_mb =
        (double)(total_discarded_bytes + parser.pos) / (1024.0 * 1024.0);
    double parse_end_time = platform_get_now();
    double duration_ms = parse_end_time - task->start_time;
    double duration_s = duration_ms / 1000.0;
    double speed_mb_s = duration_s > 0.0 ? size_mb / duration_s : 0.0;
    double starvation_pct =
        duration_ms > 0.0 ? (total_wait_time_ms / duration_ms) * 100.0 : 0.0;
    LOG_INFO(
        "parsed %zu events, %.2f MB in %.3f ms (%.2f mb/s) [starvation: "
        "%.3f ms (%.2f%%)]",
        td->events.len, size_mb, duration_ms, speed_mb_s, total_wait_time_ms,
        starvation_pct);

    LOG_DEBUG("trace_load_run completed parsing, starting track organization");
    double organize_start_time = platform_get_now();

    // 1. Organize events into tracks
    array_list_t tracks = {};  // ZII
    int64_t min_ts = 0;
    int64_t max_ts = 0;
    track_organize(td, task->theme, &tracks, &min_ts, &max_ts, allocator);

    double organize_duration_ms = platform_get_now() - organize_start_time;
    LOG_INFO("organized %zu tracks in %.3f ms", tracks.len,
             organize_duration_ms);

    // 2. Transmit complete results.
    // If the send fails, app_send_load_complete AUTOMATICALLY cleans up td and
    // tracks!
    app_send_load_complete(task->app_channel, td, tracks, min_ts, max_ts,
                           allocator);
  }

  // Clean up parser and matcher local allocations
  trace_event_matcher_deinit(&matcher, allocator);
  trace_parser_deinit(&parser, allocator);

  // Clean up task context structure
  allocator_free(allocator, task, sizeof(trace_load_task_t));

  LOG_DEBUG("trace_load_run background task exiting");
}

void trace_load_start(const theme_t* theme, channel_t* app_channel,
                      channel_t* trace_load_channel, allocator_t allocator) {
  CHECK(theme != nullptr);
  CHECK(app_channel != nullptr);
  CHECK(trace_load_channel != nullptr);

  // Allocate and package thread context
  trace_load_task_t* task =
      (trace_load_task_t*)allocator_alloc(allocator, sizeof(trace_load_task_t));
  task->theme = theme;
  task->app_channel = app_channel;
  task->trace_load_channel = trace_load_channel;
  task->allocator = allocator;
  task->start_time = platform_get_now();

  // Submit background worker to the persistent platform thread pool
  platform_submit_job(trace_load_run, task);
}

void trace_load_msg_deinit(trace_load_msg_t* msg, allocator_t allocator) {
  CHECK(msg != nullptr);
  if (msg->type == MSG_TRACE_LOAD_CHUNK) {
    if (msg->as.chunk.data != nullptr && msg->as.chunk.size > 0) {
      allocator_free(allocator, msg->as.chunk.data, msg->as.chunk.size);
      msg->as.chunk.data = nullptr;
    }
  }
}

bool trace_load_send_chunk(channel_t* trace_load_channel, char* data,
                           size_t size, size_t input_consumed_bytes,
                           bool is_eof, allocator_t allocator) {
  CHECK(trace_load_channel != nullptr);
  CHECK(size == 0 || data != nullptr);

  trace_load_msg_t msg = {
      .type = MSG_TRACE_LOAD_CHUNK,
      .as = {.chunk = {.data = data,
                       .size = size,
                       .input_consumed_bytes = input_consumed_bytes,
                       .is_eof = is_eof}}};

  bool ok = channel_try_send(trace_load_channel, &msg);
  if (!ok) {
    trace_load_msg_deinit(&msg, allocator);
  }
  return ok;
}

bool trace_load_send_abort(channel_t* trace_load_channel) {
  CHECK(trace_load_channel != nullptr);
  trace_load_msg_t msg = {.type = MSG_TRACE_LOAD_ABORT};
  return channel_try_send(trace_load_channel, &msg);
}
