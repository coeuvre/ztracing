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
  bool has_t_start;
  bool has_t_end;
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
    string_t arg = string_from_cstr(argv[i]);
    if (string_eq(arg, string_lit("--pretty"))) {
      out_args->pretty = true;
    } else if (string_eq(arg, string_lit("-h")) ||
               string_eq(arg, string_lit("--help"))) {
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
      string_t arg = string_from_cstr(argv[i]);
      if (string_eq(arg, string_lit("-h")) ||
          string_eq(arg, string_lit("--help"))) {
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
    string_t arg = string_from_cstr(argv[i]);
    if (string_eq(arg, string_lit("--pretty"))) {
      out_args->pretty = true;
    } else if (string_eq(arg, string_lit("--track"))) {
      if (i + 1 < argc) {
        out_args->track_filter = argv[i + 1];
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--track'\n");
        success = false;
      }
    } else if (string_eq(arg, string_lit("--match"))) {
      if (i + 1 < argc) {
        out_args->match_filter = argv[i + 1];
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--match'\n");
        success = false;
      }
    } else if (string_eq(arg, string_lit("--t-start"))) {
      if (i + 1 < argc) {
        out_args->t_start = (int64_t)atoll(argv[i + 1]);
        out_args->has_t_start = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--t-start'\n");
        success = false;
      }
    } else if (string_eq(arg, string_lit("--t-end"))) {
      if (i + 1 < argc) {
        out_args->t_end = (int64_t)atoll(argv[i + 1]);
        out_args->has_t_end = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--t-end'\n");
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
static int handle_summary(const trace_data_t* td, bool pretty, allocator_t a) {
  // Organize tracks to get track count and exact timestamp bounds
  array_list_t tracks = {};
  int64_t min_ts = 0;
  int64_t max_ts = 0;
  track_organize(td, &tracks, &min_ts, &max_ts, a);

  // Serialize summary
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, pretty, &json_buf, a);

  json_writer_begin_object(&w);

  json_writer_name(&w, string_lit("event_count"));
  json_writer_number_int(&w, (int64_t)td->events.len);

  json_writer_name(&w, string_lit("track_count"));
  json_writer_number_int(&w, (int64_t)tracks.len);

  json_writer_name(&w, string_lit("min_ts_us"));
  json_writer_number_double(&w, (double)min_ts);

  json_writer_name(&w, string_lit("max_ts_us"));
  json_writer_number_double(&w, (double)max_ts);

  json_writer_name(&w, string_lit("duration_ms"));
  json_writer_number_double(&w, (double)(max_ts - min_ts) / 1000.0);

  json_writer_end_object(&w);

  // Null terminate the JSON string buffer
  *array_list_push(&json_buf, char, a) = '\0';

  // Output to stdout
  printf("%s\n", (const char*)json_buf.ptr);

  // Clean up
  array_list_deinit(&json_buf, a);

  // Clean up organized tracks
  track_t* tracks_data = (track_t*)tracks.ptr;
  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&tracks_data[i], a);
  }
  array_list_deinit(&tracks, a);

  return 0;
}

// Handles the 'tracks' subcommand.
static int handle_tracks(const trace_data_t* td, bool pretty, allocator_t a) {
  // Organize tracks
  array_list_t tracks = {};
  int64_t min_ts = 0;
  int64_t max_ts = 0;
  track_organize(td, &tracks, &min_ts, &max_ts, a);

  // Serialize tracks
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, pretty, &json_buf, a);

  json_writer_begin_array(&w);

  track_t* tracks_data = (track_t*)tracks.ptr;
  for (size_t i = 0; i < tracks.len; i++) {
    const track_t* t = &tracks_data[i];

    json_writer_begin_object(&w);

    json_writer_name(&w, string_lit("index"));
    json_writer_number_int(&w, (int64_t)i);

    json_writer_name(&w, string_lit("name"));
    string_t track_name = trace_data_get_string(td, t->name_ref);
    json_writer_string(&w, track_name);

    json_writer_name(&w, string_lit("type"));
    if (t->type == TRACK_TYPE_THREAD) {
      json_writer_string(&w, string_lit("THREAD"));
    } else {
      json_writer_string(&w, string_lit("COUNTER"));
    }

    json_writer_name(&w, string_lit("pid"));
    json_writer_number_int(&w, (int64_t)t->pid);

    json_writer_name(&w, string_lit("tid"));
    json_writer_number_int(&w, (int64_t)t->tid);

    json_writer_name(&w, string_lit("event_count"));
    json_writer_number_int(&w, (int64_t)t->event_indices.len);

    json_writer_name(&w, string_lit("max_depth"));
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

  // Clean up organized tracks
  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&tracks_data[i], a);
  }
  array_list_deinit(&tracks, a);

  return 0;
}

// Handles the 'heatmap' subcommand.
static int handle_heatmap(const trace_data_t* td, bool pretty, allocator_t a) {
  // Organize tracks to get min/max ts and the tracks list
  array_list_t tracks = {};
  int64_t min_ts = 0;
  int64_t max_ts = 0;
  track_organize(td, &tracks, &min_ts, &max_ts, a);

  // Preallocate the heatmap densities buffer (1 trace_heatmap_t per track)
  array_list_t heatmap_list = {};
  array_list_resize(&heatmap_list, tracks.len, sizeof(trace_heatmap_t), a);
  trace_heatmap_t* densities = (trace_heatmap_t*)heatmap_list.ptr;

  // Compute heatmap
  trace_heatmap_compute(&tracks, td, min_ts, max_ts, densities);

  // Serialize to JSON
  array_list_t json_buf = {};
  json_writer_t w;
  json_writer_init(&w, pretty, &json_buf, a);

  json_writer_begin_array(&w);

  track_t* tracks_data = (track_t*)tracks.ptr;
  for (size_t i = 0; i < tracks.len; i++) {
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

      string_t track_name = trace_data_get_string(td, t->name_ref);
      json_writer_name(&w, string_lit("track_name"));
      json_writer_string(&w, track_name);

      json_writer_name(&w, string_lit("track_index"));
      json_writer_number_int(&w, (int64_t)i);

      json_writer_name(&w, string_lit("buckets"));
      json_writer_begin_array(&w);

      for (int b = 0; b < TRACE_HEATMAP_BUCKET_COUNT; b++) {
        size_t event_idx = h->event_indices[b];
        if (event_idx != (size_t)-1 && event_idx < td->events.len) {
          const trace_event_persisted_t* e =
              &((const trace_event_persisted_t*)td->events.ptr)[event_idx];

          json_writer_begin_object(&w);

          json_writer_name(&w, string_lit("bucket"));
          json_writer_number_int(&w, (int64_t)b);

          json_writer_name(&w, string_lit("ts_us"));
          json_writer_number_double(&w, (double)e->ts);

          json_writer_name(&w, string_lit("name"));
          string_t event_name = trace_data_get_string(td, e->name_ref);
          json_writer_string(&w, event_name);

          json_writer_name(&w, string_lit("dur_us"));
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
  for (size_t i = 0; i < tracks.len; i++) {
    track_deinit(&tracks_data[i], a);
  }
  array_list_deinit(&tracks, a);

  return 0;
}

// Handles the 'histogram' subcommand.
static int handle_histogram(const trace_data_t* td, const cli_args_t* args,
                            allocator_t a) {
  // Gather all event indices matching the filters
  array_list_t selected_indices = {};
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;

  bool has_track_filter = (args->track_filter != nullptr);
  array_list_t tracks = {};
  int64_t min_ts = 0;
  int64_t max_ts = 0;
  if (has_track_filter) {
    track_organize(td, &tracks, &min_ts, &max_ts, a);
  }

  if (has_track_filter) {
    string_t target_track = string_from_cstr(args->track_filter);
    track_t* tracks_data = (track_t*)tracks.ptr;
    for (size_t i = 0; i < tracks.len; i++) {
      const track_t* t = &tracks_data[i];
      string_t track_name = trace_data_get_string(td, t->name_ref);
      if (string_eq(track_name, target_track)) {
        const size_t* idx_ptr = (const size_t*)t->event_indices.ptr;
        for (size_t k = 0; k < t->event_indices.len; k++) {
          size_t event_idx = idx_ptr[k];
          if (event_idx < td->events.len) {
            const trace_event_persisted_t* e = &events[event_idx];

            if (args->match_filter) {
              string_t name = trace_data_get_string(td, e->name_ref);
              string_t cat = trace_data_get_string(td, e->cat_ref);
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
        string_t name = trace_data_get_string(td, e->name_ref);
        string_t cat = trace_data_get_string(td, e->cat_ref);
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

  json_writer_name(&w, string_lit("scale"));
  if (h.has_non_zero_durations && (h.num_buckets > 0)) {
    int64_t width_first = h.buckets[0].max_dur - h.buckets[0].min_dur;
    int64_t width_last = h.buckets[h.num_buckets - 1].max_dur -
                         h.buckets[h.num_buckets - 1].min_dur;
    if (width_last > width_first * 2) {
      json_writer_string(&w, string_lit("logarithmic"));
    } else {
      json_writer_string(&w, string_lit("linear"));
    }
  } else {
    json_writer_string(&w, string_lit("linear"));
  }

  json_writer_name(&w, string_lit("total_events"));
  json_writer_number_int(&w, (int64_t)selected_indices.len);

  json_writer_name(&w, string_lit("buckets"));
  json_writer_begin_array(&w);

  for (int i = 0; i < h.num_buckets; i++) {
    const trace_histogram_bucket_t* b = &h.buckets[i];

    json_writer_begin_object(&w);

    json_writer_name(&w, string_lit("bucket_idx"));
    json_writer_number_int(&w, (int64_t)i);

    json_writer_name(&w, string_lit("min_dur_us"));
    json_writer_number_double(&w, (double)b->min_dur);

    json_writer_name(&w, string_lit("max_dur_us"));
    json_writer_number_double(&w, (double)b->max_dur);

    json_writer_name(&w, string_lit("count"));
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
  if (has_track_filter) {
    track_t* tracks_data = (track_t*)tracks.ptr;
    for (size_t i = 0; i < tracks.len; i++) {
      track_deinit(&tracks_data[i], a);
    }
    array_list_deinit(&tracks, a);
  }

  return 0;
}

// main entry point preferring success path under if.
int main(int argc, char* argv[]) {
  int exit_code = 0;
  cli_args_t args = {};

  if (parse_arguments(argc, argv, &args)) {
    allocator_t a = allocator_get_default();
    trace_data_t* td = trace_loader_load_file(args.trace_file, a, nullptr);

    if (td) {
      string_t sub = string_from_cstr(args.subcommand);

      if (string_eq(sub, string_lit("summary"))) {
        exit_code = handle_summary(td, args.pretty, a);
      } else if (string_eq(sub, string_lit("tracks"))) {
        exit_code = handle_tracks(td, args.pretty, a);
      } else if (string_eq(sub, string_lit("heatmap"))) {
        exit_code = handle_heatmap(td, args.pretty, a);
      } else if (string_eq(sub, string_lit("histogram"))) {
        exit_code = handle_histogram(td, &args, a);
      } else {
        fprintf(stderr, "Error: Subcommand '%s' is not yet implemented.\n",
                args.subcommand);
        exit_code = 1;
      }

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
