#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/json.h"
#include "src/logging.h"
#include "src/platform.h"
#include "src/trace_data.h"
#include "src/trace_heatmap.h"
#include "src/trace_histogram.h"
#include "src/trace_loader.h"
#include "src/trace_viewer.h"
#include "src/track.h"

// Print the global usage and exit.
static void print_usage(const char* prog_name) {
  fprintf(stderr, "Usage: %s <subcommand> <trace_file> [options]\n\n",
          prog_name);
  fprintf(stderr, "Global Options:\n");
  fprintf(
      stderr,
      "  --pretty                     Enable pretty-printed JSON output\n\n");
  fprintf(stderr, "Subcommands:\n");
  fprintf(stderr,
          "  summary <trace_file>         Print high-level trace metadata "
          "(counts, duration).\n");
  fprintf(stderr,
          "  tracks <trace_file>          List all organized tracks (names, "
          "events, depths).\n");
  fprintf(stderr,
          "  inspect <trace_file>         Inspect detailed event parameters "
          "at a timestamp.\n");
  fprintf(stderr,
          "                               Options: --track <name> "
          "--ts <ts_us>\n");
  fprintf(stderr,
          "  query <trace_file>           Search and extract matching "
          "events.\n");
  fprintf(stderr,
          "                               Options: [--track <name>] "
          "[--match <substr>]\n");
  fprintf(stderr,
          "                                        [--t-start <us>] "
          "[--t-end <us>]\n");
  fprintf(stderr,
          "                                        [--max-depth <n>] "
          "[--limit <n>]\n");
  fprintf(stderr,
          "  heatmap <trace_file>         Calculate the 2D Activity Heatmap "
          "Grid.\n");
  fprintf(stderr,
          "  histogram <trace_file>       Compute duration histogram "
          "buckets.\n");
  fprintf(stderr,
          "                               Options: [--track <name>] "
          "[--match <substr>]\n");
  fprintf(stderr,
          "                                        [--t-start <us>] "
          "[--t-end <us>]\n");
}

typedef struct cli_args {
  const char* subcommand;
  const char* trace_file;
  bool pretty;

  // Histogram / Filtering options
  const char* track_filter;
  const char* match_filter;
  int64_t t_start;
  int64_t t_end;
  int max_depth;
  int limit;
  bool has_t_start;
  bool has_t_end;
  bool has_max_depth;
  bool has_limit;
} cli_args_t;

// Parses CLI arguments manually.
static bool parse_arguments(int argc, char* argv[], cli_args_t* out_args) {
  bool success = true;

  if (argc < 2) {
    success = false;
  }

  int i = 1;
  // Parse global flags or subcommand
  while (success && i < argc) {
    string_view_t arg = string_view_from_cstr(argv[i]);
    if (string_view_eq(arg, SV("--pretty"))) {
      out_args->pretty = true;
    } else if (string_view_eq(arg, SV("-h")) ||
               string_view_eq(arg, SV("--help"))) {
      success = false;
    } else if (arg.len > 0 && arg.ptr[0] == '-') {
      fprintf(stderr, "Error: Unknown global option '%s'\n", argv[i]);
      success = false;
    } else {
      out_args->subcommand = argv[i];
      i++;
      break;
    }
    i++;
  }

  if (success && !out_args->subcommand) {
    fprintf(stderr, "Error: No subcommand provided.\n");
    success = false;
  }

  // Next argument must be the trace file
  if (success) {
    if (i < argc) {
      string_view_t arg = string_view_from_cstr(argv[i]);
      if (string_view_eq(arg, SV("-h")) || string_view_eq(arg, SV("--help"))) {
        success = false;
      } else {
        out_args->trace_file = argv[i];
        i++;
      }
    } else {
      fprintf(stderr, "Error: Missing trace file argument.\n");
      success = false;
    }
  }

  // Parse remaining options
  while (success && i < argc) {
    string_view_t arg = string_view_from_cstr(argv[i]);
    if (string_view_eq(arg, SV("--pretty"))) {
      out_args->pretty = true;
    } else if (string_view_eq(arg, SV("--ts"))) {
      if (i + 1 < argc) {
        out_args->t_start = (int64_t)atoll(argv[i + 1]);
        out_args->has_t_start = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--ts'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--track"))) {
      if (i + 1 < argc) {
        out_args->track_filter = argv[i + 1];
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--track'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--match"))) {
      if (i + 1 < argc) {
        out_args->match_filter = argv[i + 1];
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--match'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--t-start"))) {
      if (i + 1 < argc) {
        out_args->t_start = (int64_t)atoll(argv[i + 1]);
        out_args->has_t_start = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--t-start'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--t-end"))) {
      if (i + 1 < argc) {
        out_args->t_end = (int64_t)atoll(argv[i + 1]);
        out_args->has_t_end = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--t-end'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--max-depth"))) {
      if (i + 1 < argc) {
        out_args->max_depth = atoi(argv[i + 1]);
        out_args->has_max_depth = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--max-depth'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--limit"))) {
      if (i + 1 < argc) {
        out_args->limit = atoi(argv[i + 1]);
        out_args->has_limit = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--limit'\n");
        success = false;
      }
    } else {
      fprintf(stderr, "Error: Unknown option '%s' for subcommand '%s'\n",
              argv[i], out_args->subcommand);
      success = false;
    }
    i++;
  }

  return success;
}

// Handles the 'summary' subcommand.
static int handle_summary(const trace_data_t* td, size_t track_count, int64_t min_ts, int64_t max_ts, bool pretty, allocator_t a) {
  // Serialize summary
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, pretty, &json_buf, a);

  json_writer_begin_object(&w);

  json_writer_name(&w, SV("event_count"));
  json_writer_number_int(&w, (int64_t)td->events.len);

  json_writer_name(&w, SV("track_count"));
  json_writer_number_int(&w, (int64_t)track_count);

  json_writer_name(&w, SV("min_ts_us"));
  json_writer_number_double(&w, (double)min_ts);

  json_writer_name(&w, SV("max_ts_us"));
  json_writer_number_double(&w, (double)max_ts);

  json_writer_name(&w, SV("duration_ms"));
  json_writer_number_double(&w, (double)(max_ts - min_ts) / 1000.0);

  json_writer_end_object(&w);

  // Null terminate the JSON string buffer
  *array_list_push(&json_buf, char, a) = '\0';

  // Output to stdout
  printf("%s\n", (const char*)json_buf.ptr);

  // Clean up
  array_list_deinit(&json_buf, a);

  return 0;
}

// Handles the 'tracks' subcommand.
static int handle_tracks(const trace_data_t* td, const array_list_t* tracks, bool pretty, allocator_t a) {
  // Serialize tracks
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, pretty, &json_buf, a);

  json_writer_begin_array(&w);

  track_t* tracks_data = (track_t*)tracks->ptr;
  for (size_t i = 0; i < tracks->len; i++) {
    const track_t* t = &tracks_data[i];

    json_writer_begin_object(&w);

    json_writer_name(&w, SV("index"));
    json_writer_number_int(&w, (int64_t)i);

    json_writer_name(&w, SV("name"));
    string_view_t track_name = trace_data_get_string(td, t->name_ref);
    json_writer_string(&w, track_name);

    json_writer_name(&w, SV("type"));
    if (t->type == TRACK_TYPE_THREAD) {
      json_writer_string(&w, SV("THREAD"));
    } else {
      json_writer_string(&w, SV("COUNTER"));
    }

    json_writer_name(&w, SV("pid"));
    json_writer_number_int(&w, (int64_t)t->pid);

    json_writer_name(&w, SV("tid"));
    json_writer_number_int(&w, (int64_t)t->tid);

    json_writer_name(&w, SV("event_count"));
    json_writer_number_int(&w, (int64_t)t->event_indices.len);

    json_writer_name(&w, SV("max_depth"));
    json_writer_number_int(&w, (int64_t)t->max_depth);

    json_writer_end_object(&w);
  }

  json_writer_end_array(&w);

  // Null terminate the JSON string buffer
  *array_list_push(&json_buf, char, a) = '\0';

  // Output to stdout
  printf("%s\n", (const char*)json_buf.ptr);

  // Clean up
  array_list_deinit(&json_buf, a);

  return 0;
}

// Handles the 'heatmap' subcommand.
static int handle_heatmap(const trace_data_t* td, const array_list_t* tracks, int64_t min_ts, int64_t max_ts, bool pretty, allocator_t a) {
  // Preallocate the heatmap densities buffer (1 trace_heatmap_t per track)
  array_list_t heatmap_list = {};
  array_list_resize(&heatmap_list, tracks->len, sizeof(trace_heatmap_t), a);
  trace_heatmap_t* densities = (trace_heatmap_t*)heatmap_list.ptr;

  // Compute heatmap
  trace_heatmap_compute(tracks, td, min_ts, max_ts, densities);

  // Serialize to JSON
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, pretty, &json_buf, a);

  json_writer_begin_array(&w);

  track_t* tracks_data = (track_t*)tracks->ptr;
  for (size_t i = 0; i < tracks->len; i++) {
    const track_t* t = &tracks_data[i];
    const trace_heatmap_t* h = &densities[i];

    // Check if this track has any active buckets
    bool has_active = false;
    for (int b = 0; b < TRACE_HEATMAP_BUCKET_COUNT; b++) {
      if (h->event_indices[b] != (size_t)-1) {
        has_active = true;
        break;
      }
    }

    // Only serialize the track if it has at least one active bucket
    if (has_active) {
      json_writer_begin_object(&w);

      string_view_t track_name = trace_data_get_string(td, t->name_ref);
      json_writer_name(&w, SV("track_name"));
      json_writer_string(&w, track_name);

      json_writer_name(&w, SV("track_index"));
      json_writer_number_int(&w, (int64_t)i);

      json_writer_name(&w, SV("buckets"));
      json_writer_begin_array(&w);

      for (int b = 0; b < TRACE_HEATMAP_BUCKET_COUNT; b++) {
        size_t event_idx = h->event_indices[b];
        if (event_idx != (size_t)-1 && event_idx < td->events.len) {
          const trace_event_persisted_t* e =
              &((const trace_event_persisted_t*)td->events.ptr)[event_idx];

          json_writer_begin_object(&w);

          json_writer_name(&w, SV("bucket"));
          json_writer_number_int(&w, (int64_t)b);

          json_writer_name(&w, SV("ts_us"));
          json_writer_number_double(&w, (double)e->ts);

          json_writer_name(&w, SV("name"));
          string_view_t event_name = trace_data_get_string(td, e->name_ref);
          json_writer_string(&w, event_name);

          json_writer_name(&w, SV("dur_us"));
          json_writer_number_double(&w, (double)e->dur);

          json_writer_end_object(&w);
        }
      }

      json_writer_end_array(&w);   // end buckets array
      json_writer_end_object(&w);  // end track object
    }
  }

  json_writer_end_array(&w);  // end top-level array

  // Null terminate and print
  *array_list_push(&json_buf, char, a) = '\0';
  printf("%s\n", (const char*)json_buf.ptr);

  // Clean up
  array_list_deinit(&json_buf, a);
  array_list_deinit(&heatmap_list, a);

  return 0;
}

// Handles the 'histogram' subcommand.
static int handle_histogram(const trace_data_t* td, const array_list_t* tracks, const cli_args_t* args,
                            allocator_t a) {
  // Gather all event indices matching the filters
  array_list_t selected_indices = {};
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;

  bool has_track_filter = (args->track_filter != nullptr);

  if (has_track_filter) {
    string_view_t target_track = string_view_from_cstr(args->track_filter);
    track_t* tracks_data = (track_t*)tracks->ptr;
    for (size_t i = 0; i < tracks->len; i++) {
      const track_t* t = &tracks_data[i];
      string_view_t track_name = trace_data_get_string(td, t->name_ref);
      if (string_view_eq(track_name, target_track)) {
        const size_t* idx_ptr = (const size_t*)t->event_indices.ptr;
        for (size_t k = 0; k < t->event_indices.len; k++) {
          size_t event_idx = idx_ptr[k];
          if (event_idx < td->events.len) {
            const trace_event_persisted_t* e = &events[event_idx];

            if (args->match_filter) {
              string_view_t name = trace_data_get_string(td, e->name_ref);
              string_view_t cat = trace_data_get_string(td, e->cat_ref);
              bool match =
                  trace_viewer_str_contains_case_insensitive(
                      name, args->match_filter, strlen(args->match_filter)) ||
                  trace_viewer_str_contains_case_insensitive(
                      cat, args->match_filter, strlen(args->match_filter));
              if (!match) continue;
            }

            if (args->has_t_start && e->ts < args->t_start) continue;
            if (args->has_t_end && e->ts > args->t_end) continue;

            *array_list_push(&selected_indices, int64_t, a) =
                (int64_t)event_idx;
          }
        }
      }
    }
  } else {
    for (size_t i = 0; i < td->events.len; i++) {
      const trace_event_persisted_t* e = &events[i];

      if (args->match_filter) {
        string_view_t name = trace_data_get_string(td, e->name_ref);
        string_view_t cat = trace_data_get_string(td, e->cat_ref);
        bool match =
            trace_viewer_str_contains_case_insensitive(
                name, args->match_filter, strlen(args->match_filter)) ||
            trace_viewer_str_contains_case_insensitive(
                cat, args->match_filter, strlen(args->match_filter));
        if (!match) continue;
      }

      if (args->has_t_start && e->ts < args->t_start) continue;
      if (args->has_t_end && e->ts > args->t_end) continue;

      *array_list_push(&selected_indices, int64_t, a) = (int64_t)i;
    }
  }

  // Compute histogram
  trace_histogram_t h = {};
  trace_histogram_compute(&selected_indices, td, &h);

  // Serialize to JSON
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, args->pretty, &json_buf, a);

  json_writer_begin_object(&w);

  json_writer_name(&w, SV("scale"));
  if (h.has_non_zero_durations && (h.num_buckets > 0)) {
    int64_t width_first = h.buckets[0].max_dur - h.buckets[0].min_dur;
    int64_t width_last = h.buckets[h.num_buckets - 1].max_dur -
                         h.buckets[h.num_buckets - 1].min_dur;
    if (width_last > width_first * 2) {
      json_writer_string(&w, SV("logarithmic"));
    } else {
      json_writer_string(&w, SV("linear"));
    }
  } else {
    json_writer_string(&w, SV("linear"));
  }

  json_writer_name(&w, SV("total_events"));
  json_writer_number_int(&w, (int64_t)selected_indices.len);

  json_writer_name(&w, SV("buckets"));
  json_writer_begin_array(&w);

  for (int i = 0; i < h.num_buckets; i++) {
    const trace_histogram_bucket_t* b = &h.buckets[i];

    json_writer_begin_object(&w);

    json_writer_name(&w, SV("bucket_idx"));
    json_writer_number_int(&w, (int64_t)i);

    json_writer_name(&w, SV("min_dur_us"));
    json_writer_number_double(&w, (double)b->min_dur);

    json_writer_name(&w, SV("max_dur_us"));
    json_writer_number_double(&w, (double)b->max_dur);

    json_writer_name(&w, SV("count"));
    json_writer_number_int(&w, (int64_t)b->count);

    json_writer_end_object(&w);
  }

  json_writer_end_array(&w);   // end buckets array
  json_writer_end_object(&w);  // end top-level object

  // Null terminate and print
  *array_list_push(&json_buf, char, a) = '\0';
  printf("%s\n", (const char*)json_buf.ptr);

  // Clean up
  array_list_deinit(&json_buf, a);
  array_list_deinit(&selected_indices, a);

  return 0;
}

// Handles the 'inspect' subcommand.
static int handle_inspect(const trace_data_t* td, const array_list_t* tracks, const cli_args_t* args,
                          allocator_t a) {
  if (!args->track_filter) {
    fprintf(stderr,
            "Error: Missing required option '--track <name>' for inspect "
            "subcommand.\n");
    return 1;
  }
  if (!args->has_t_start) {
    fprintf(stderr,
            "Error: Missing required option '--ts <ts_us>' for inspect "
            "subcommand.\n");
    return 1;
  }

  int64_t target_ts = args->t_start;  // target ts in us

  // Find the target track
  const track_t* target_track = nullptr;
  string_view_t target_track_name = string_view_from_cstr(args->track_filter);
  track_t* tracks_data = (track_t*)tracks->ptr;

  for (size_t i = 0; i < tracks->len; i++) {
    string_view_t track_name =
        trace_data_get_string(td, tracks_data[i].name_ref);
    if (string_view_eq(track_name, target_track_name)) {
      target_track = &tracks_data[i];
      break;
    }
  }

  if (!target_track) {
    fprintf(stderr, "Error: Track '%s' not found.\n", args->track_filter);
    return 1;
  }

  // Serialize to JSON array
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, args->pretty, &json_buf, a);

  json_writer_begin_array(&w);

  const size_t* event_indices = (const size_t*)target_track->event_indices.ptr;
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;

  // Use binary search to find the first event with ts >= target_ts
  size_t start_k = trace_data_events_lower_bound(
      event_indices, target_track->event_indices.len, events, target_ts);

  // Inspect all events starting at target_ts
  for (size_t k = start_k; k < target_track->event_indices.len; k++) {
    size_t event_idx = event_indices[k];
    const trace_event_persisted_t* e = &events[event_idx];
    if (e->ts != target_ts) {
      break;  // Since events are sorted by ts, we stop as soon as ts differs
    }

    json_writer_begin_object(&w);

    json_writer_name(&w, SV("name"));
    json_writer_string(&w, trace_data_get_string(td, e->name_ref));

    json_writer_name(&w, SV("track"));
    json_writer_string(&w, target_track_name);

    json_writer_name(&w, SV("ts_us"));
    json_writer_number_double(&w, (double)e->ts);

    json_writer_name(&w, SV("dur_us"));
    json_writer_number_double(&w, (double)e->dur);

    if (target_track->type == TRACK_TYPE_THREAD) {
      // Self Time & Depth
      const int64_t* self_durs = (const int64_t*)target_track->self_durs.ptr;
      const int* depths = (const int*)target_track->depths.ptr;

      json_writer_name(&w, SV("self_time_us"));
      json_writer_number_double(&w, (double)self_durs[k]);

      json_writer_name(&w, SV("depth"));
      json_writer_number_int(&w, (int64_t)depths[k]);

      int depth_target = depths[k];

      // 1. Find Parent: nearest preceding event with depth == depth_target - 1
      const trace_event_persisted_t* parent_event = nullptr;
      for (int prev = (int)k - 1; prev >= 0; prev--) {
        if (depths[prev] == depth_target - 1) {
          parent_event = &events[event_indices[prev]];
          break;
        }
      }

      if (parent_event) {
        json_writer_name(&w, SV("parent"));
        json_writer_begin_object(&w);
        json_writer_name(&w, SV("track"));
        json_writer_string(&w, target_track_name);
        json_writer_name(&w, SV("ts_us"));
        json_writer_number_double(&w, (double)parent_event->ts);
        json_writer_end_object(&w);
      }

      // 2. Find Children: subsequent events with depth == depth_target + 1,
      // until depth <= depth_target
      json_writer_name(&w, SV("children"));
      json_writer_begin_array(&w);
      for (size_t next = k + 1; next < target_track->event_indices.len;
           next++) {
        int next_depth = depths[next];
        if (next_depth <= depth_target) {
          break;  // Stop at sibling or parent close frame
        }
        if (next_depth == depth_target + 1) {
          const trace_event_persisted_t* child_event =
              &events[event_indices[next]];
          json_writer_begin_object(&w);
          json_writer_name(&w, SV("track"));
          json_writer_string(&w, target_track_name);
          json_writer_name(&w, SV("ts_us"));
          json_writer_number_double(&w, (double)child_event->ts);
          json_writer_end_object(&w);
        }
      }
      json_writer_end_array(&w);
    }

    // Custom Arguments
    if (e->args_count > 0) {
      json_writer_name(&w, SV("args"));
      json_writer_begin_object(&w);
      const trace_arg_persisted_t* args_ptr =
          (const trace_arg_persisted_t*)td->args.ptr + e->args_offset;
      for (uint32_t a_idx = 0; a_idx < e->args_count; a_idx++) {
        const trace_arg_persisted_t* arg = &args_ptr[a_idx];
        string_view_t key = trace_data_get_string(td, arg->key_ref);
        json_writer_name(&w, key);
        if (arg->val_ref != 0) {
          string_view_t val = trace_data_get_string(td, arg->val_ref);
          json_writer_string(&w, val);
        } else {
          json_writer_number_double(&w, arg->val_double);
        }
      }
      json_writer_end_object(&w);
    }

    json_writer_end_object(&w);
  }

  json_writer_end_array(&w);

  // Null terminate and print
  *array_list_push(&json_buf, char, a) = '\0';
  printf("%s\n", (const char*)json_buf.ptr);

  // Clean up
  array_list_deinit(&json_buf, a);

  return 0;
}

typedef struct {
  size_t event_idx;
  const track_t* track;
  int depth;
  int64_t ts;
} query_match_t;

static int compare_query_matches(const void* a_ptr, const void* b_ptr) {
  const query_match_t* am = (const query_match_t*)a_ptr;
  const query_match_t* bm = (const query_match_t*)b_ptr;
  if (am->ts < bm->ts) return -1;
  if (am->ts > bm->ts) return 1;
  return 0;
}

// Handles the 'query' subcommand.
static int handle_query(const trace_data_t* td, const array_list_t* tracks, const cli_args_t* args,
                        allocator_t a) {
  track_t* tracks_data = (track_t*)tracks->ptr;

  // Find the target track if filtering by track
  const track_t* track_filter = nullptr;
  if (args->track_filter) {
    string_view_t target_track_name = string_view_from_cstr(args->track_filter);
    for (size_t i = 0; i < tracks->len; i++) {
      string_view_t track_name =
          trace_data_get_string(td, tracks_data[i].name_ref);
      if (string_view_eq(track_name, target_track_name)) {
        track_filter = &tracks_data[i];
        break;
      }
    }
    if (!track_filter) {
      fprintf(stderr, "Error: Track '%s' not found.\n", args->track_filter);
      return 1;
    }
  }

  // Collect matches
  array_list_t matches = {};

  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;

  // Loop over tracks (either the filtered one, or all of them)
  size_t start_track = 0;
  size_t end_track = tracks->len;
  if (track_filter) {
    for (size_t i = 0; i < tracks->len; i++) {
      if (&tracks_data[i] == track_filter) {
        start_track = i;
        end_track = i + 1;
        break;
      }
    }
  }

  for (size_t t_idx = start_track; t_idx < end_track; t_idx++) {
    const track_t* t = &tracks_data[t_idx];
    const size_t* event_indices = (const size_t*)t->event_indices.ptr;
    const int* depths = (const int*)t->depths.ptr;

    for (size_t k = 0; k < t->event_indices.len; k++) {
      size_t event_idx = event_indices[k];
      const trace_event_persisted_t* e = &events[event_idx];

      // 1. Time-window check: overlap with [t_start, t_end]
      // e->ts <= t_end && e->ts + e->dur >= t_start
      if (args->has_t_start && (e->ts + e->dur < args->t_start)) {
        continue;
      }
      if (args->has_t_end && (e->ts > args->t_end)) {
        continue;
      }

      // 2. Substring match check
      if (args->match_filter) {
        string_view_t name = trace_data_get_string(td, e->name_ref);
        string_view_t cat = trace_data_get_string(td, e->cat_ref);
        bool match =
            trace_viewer_str_contains_case_insensitive(
                name, args->match_filter, strlen(args->match_filter)) ||
            trace_viewer_str_contains_case_insensitive(
                cat, args->match_filter, strlen(args->match_filter));
        if (!match) continue;
      }

      // 3. Max depth check
      if (args->has_max_depth) {
        int depth = (t->type == TRACK_TYPE_THREAD) ? depths[k] : 0;
        if (depth > args->max_depth) {
          continue;
        }
      }

      // We have a match!
      query_match_t* m = array_list_push(&matches, query_match_t, a);
      m->event_idx = event_idx;
      m->track = t;
      m->depth = (t->type == TRACK_TYPE_THREAD) ? depths[k] : 0;
      m->ts = e->ts;
    }
  }

  // Sort matches chronologically
  if (matches.len > 0) {
    qsort(matches.ptr, matches.len, sizeof(query_match_t),
          compare_query_matches);
  }

  // Serialize to JSON array with limit
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, args->pretty, &json_buf, a);

  json_writer_begin_array(&w);

  size_t limit = args->has_limit ? (size_t)args->limit : matches.len;
  size_t print_count = (matches.len < limit) ? matches.len : limit;

  const query_match_t* matches_data = (const query_match_t*)matches.ptr;
  for (size_t i = 0; i < print_count; i++) {
    const query_match_t* m = &matches_data[i];
    const trace_event_persisted_t* e = &events[m->event_idx];

    json_writer_begin_object(&w);

    json_writer_name(&w, SV("name"));
    json_writer_string(&w, trace_data_get_string(td, e->name_ref));

    json_writer_name(&w, SV("track"));
    json_writer_string(&w, trace_data_get_string(td, m->track->name_ref));

    json_writer_name(&w, SV("ts_us"));
    json_writer_number_double(&w, (double)e->ts);

    json_writer_name(&w, SV("dur_us"));
    json_writer_number_double(&w, (double)e->dur);

    json_writer_name(&w, SV("depth"));
    json_writer_number_int(&w, (int64_t)m->depth);

    json_writer_end_object(&w);
  }

  json_writer_end_array(&w);

  // Null terminate and print
  *array_list_push(&json_buf, char, a) = '\0';
  printf("%s\n", (const char*)json_buf.ptr);

  // Clean up
  array_list_deinit(&json_buf, a);
  array_list_deinit(&matches, a);

  return 0;
}

// main entry point preferring success path under if.
int main(int argc, char* argv[]) {
  int exit_code = 0;
  cli_args_t args = {};

  if (parse_arguments(argc, argv, &args)) {
    allocator_t a = allocator_get_default();
    array_list_t tracks = {};
    int64_t min_ts = 0;
    int64_t max_ts = 0;
    trace_data_t* td = trace_loader_load_file(args.trace_file, a, nullptr, &tracks, &min_ts, &max_ts, nullptr, nullptr);

    if (td) {
      string_view_t sub = string_view_from_cstr(args.subcommand);

      if (string_view_eq(sub, SV("summary"))) {
        exit_code = handle_summary(td, tracks.len, min_ts, max_ts, args.pretty, a);
      } else if (string_view_eq(sub, SV("tracks"))) {
        exit_code = handle_tracks(td, &tracks, args.pretty, a);
      } else if (string_view_eq(sub, SV("heatmap"))) {
        exit_code = handle_heatmap(td, &tracks, min_ts, max_ts, args.pretty, a);
      } else if (string_view_eq(sub, SV("histogram"))) {
        exit_code = handle_histogram(td, &tracks, &args, a);
      } else if (string_view_eq(sub, SV("inspect"))) {
        exit_code = handle_inspect(td, &tracks, &args, a);
      } else if (string_view_eq(sub, SV("query"))) {
        exit_code = handle_query(td, &tracks, &args, a);
      } else {
        fprintf(stderr, "Error: Subcommand '%s' is not yet implemented.\n",
                args.subcommand);
        exit_code = 1;
      }

      // Clean up pre-organized tracks
      track_t* tracks_data = (track_t*)tracks.ptr;
      for (size_t i = 0; i < tracks.len; i++) {
        track_deinit(&tracks_data[i], a);
      }
      array_list_deinit(&tracks, a);
      trace_data_release(td, a);
    } else {
      exit_code = 1;
    }
  } else {
    print_usage(argv[0]);
    exit_code = 1;
  }

  return exit_code;
}
