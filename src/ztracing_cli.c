#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/allocator.h"
#include "core/json_writer.h"
#include "core/darray.h"
#include "src/trace_data.h"
#include "src/trace_concurrency.h"
#include "src/trace_aggregate.h"
#include "src/trace_diff.h"
#include "src/cli_table.h"
#include "src/trace_histogram.h"
#include "src/trace_loader.h"
#include "src/trace_viewer.h"
#include "src/track.h"

// Print the global usage and exit.
static void print_usage(const char* prog_name) {
  fprintf(stderr, "Usage: %s <subcommand> <trace_file> [options]\n\n",
          prog_name);
  fprintf(stderr, "Subcommands:\n");
  fprintf(stderr,
          "  summary <trace_file>         Print high-level trace metadata "
          "(counts, duration).\n");
  fprintf(stderr,
          "                               Options: [--list-tracks]\n");
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
          "  concurrency <trace_file>     Visualize system load and concurrency.\n");
  fprintf(stderr,
          "                               Options: [--buckets <n>]\n");
  fprintf(stderr,
          "  aggregate <trace_file>       Aggregate event durations and counts.\n");
  fprintf(stderr,
          "                               Options: [--group-by name|category]\n");
  fprintf(stderr,
          "                                        [--sort duration|count]\n");
  fprintf(stderr,
          "                                        [--min-count <n>]\n");
  fprintf(stderr,
          "  diff <trace_1> <trace_2>     Compare two traces side-by-side.\n");
  fprintf(stderr,
          "                               Options: [--group-by name|category]\n");
  fprintf(stderr,
          "                                        [--sort dur-delta|count-delta]\n");
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
  const char* trace_file_2;
  bool list_tracks;

  // Histogram / Filtering options
  const char* track_filter;
  const char* match_filter;
  int64_t t_start;
  int64_t t_end;
  int max_depth;
  int limit;
  int concurrency_buckets;
  int min_count;
  string_view_t group_by;
  string_view_t sort_by;
  bool has_t_start;
  bool has_t_end;
  bool has_max_depth;
  bool has_limit;
  bool has_concurrency_buckets;
  bool has_min_count;
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
    if (string_view_eq(arg, SV("-h")) ||
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

  // If diff, we need a second trace file
  if (success && out_args->subcommand && strcmp(out_args->subcommand, "diff") == 0) {
    if (i < argc) {
      string_view_t arg = string_view_from_cstr(argv[i]);
      if (string_view_eq(arg, SV("-h")) || string_view_eq(arg, SV("--help"))) {
        success = false;
      } else {
        out_args->trace_file_2 = argv[i];
        i++;
      }
    } else {
      fprintf(stderr, "Error: Missing second trace file argument for diff.\n");
      success = false;
    }
  }

  // Parse remaining options
  while (success && i < argc) {
    string_view_t arg = string_view_from_cstr(argv[i]);
    if (string_view_eq(arg, SV("--ts"))) {
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
    } else if (string_view_eq(arg, SV("--list-tracks"))) {
      out_args->list_tracks = true;
    } else if (string_view_eq(arg, SV("--buckets"))) {
      if (i + 1 < argc) {
        out_args->concurrency_buckets = atoi(argv[i + 1]);
        out_args->has_concurrency_buckets = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--buckets'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--group-by"))) {
      if (i + 1 < argc) {
        out_args->group_by = string_view_from_cstr(argv[i + 1]);
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--group-by'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--sort"))) {
      if (i + 1 < argc) {
        out_args->sort_by = string_view_from_cstr(argv[i + 1]);
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--sort'\n");
        success = false;
      }
    } else if (string_view_eq(arg, SV("--min-count"))) {
      if (i + 1 < argc) {
        out_args->min_count = atoi(argv[i + 1]);
        out_args->has_min_count = true;
        i++;
      } else {
        fprintf(stderr, "Error: Missing value for option '--min-count'\n");
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
static int handle_summary(const trace_data_t* td, const darray_track_t* tracks,
                          int64_t min_ts, int64_t max_ts, bool list_tracks,
                          allocator_t* a) {
  (void)a; // Unused now since cli_table uses its own arena

  cli_table_t summary_table = {};
  cli_table_init(&summary_table);

  cli_table_add_column(&summary_table, SV("Metric"), CLI_ALIGN_LEFT, 25, true);
  cli_table_add_column(&summary_table, SV("Value"), CLI_ALIGN_LEFT, 15, true);

  cli_table_add_row(&summary_table);
  cli_table_set_cell(&summary_table, 0, SV("Event Count"));
  cli_table_set_cell_fmt(&summary_table, 1, "%zu", td->events.len);

  cli_table_add_row(&summary_table);
  cli_table_set_cell(&summary_table, 0, SV("Track Count"));
  cli_table_set_cell_fmt(&summary_table, 1, "%zu", tracks->len);

  cli_table_add_row(&summary_table);
  cli_table_set_cell(&summary_table, 0, SV("Min Timestamp (us)"));
  cli_table_set_cell_fmt(&summary_table, 1, "%ld", (long)min_ts);

  cli_table_add_row(&summary_table);
  cli_table_set_cell(&summary_table, 0, SV("Max Timestamp (us)"));
  cli_table_set_cell_fmt(&summary_table, 1, "%ld", (long)max_ts);

  cli_table_add_row(&summary_table);
  cli_table_set_cell(&summary_table, 0, SV("Duration (ms)"));
  cli_table_set_cell_fmt(&summary_table, 1, "%.3f", (double)(max_ts - min_ts) / 1000.0);

  cli_table_print(&summary_table);
  cli_table_deinit(&summary_table);

  if (list_tracks) {
    printf("\n");
    cli_table_t tracks_table = {};
    cli_table_init(&tracks_table);

    cli_table_add_column(&tracks_table, SV("Index"), CLI_ALIGN_RIGHT, 5, true);
    cli_table_add_column(&tracks_table, SV("Track Name"), CLI_ALIGN_LEFT, 20, true);
    cli_table_add_column(&tracks_table, SV("Type"), CLI_ALIGN_LEFT, 10, true);
    cli_table_add_column(&tracks_table, SV("PID"), CLI_ALIGN_RIGHT, 8, true);
    cli_table_add_column(&tracks_table, SV("TID"), CLI_ALIGN_RIGHT, 8, true);
    cli_table_add_column(&tracks_table, SV("Event Count"), CLI_ALIGN_RIGHT, 12, true);
    cli_table_add_column(&tracks_table, SV("Max Depth"), CLI_ALIGN_RIGHT, 10, true);

    track_t* tracks_data = tracks->ptr;
    for (size_t i = 0; i < tracks->len; i++) {
      const track_t* t = &tracks_data[i];
      string_view_t track_name = trace_data_get_string(td, t->name_ref);

      cli_table_add_row(&tracks_table);
      cli_table_set_cell_fmt(&tracks_table, 0, "%zu", i);
      cli_table_set_cell(&tracks_table, 1, track_name);
      cli_table_set_cell(&tracks_table, 2, t->type == TRACK_TYPE_THREAD ? SV("THREAD") : SV("COUNTER"));
      cli_table_set_cell_fmt(&tracks_table, 3, "%d", t->pid);
      cli_table_set_cell_fmt(&tracks_table, 4, "%d", t->tid);
      cli_table_set_cell_fmt(&tracks_table, 5, "%zu", t->event_indices.len);
      cli_table_set_cell_fmt(&tracks_table, 6, "%d", t->max_depth);
    }

    cli_table_print(&tracks_table);
    cli_table_deinit(&tracks_table);
  }

  return 0;
}

// Handles the 'concurrency' subcommand.
static int handle_concurrency(const trace_data_t* td, const darray_track_t* tracks,
                              int64_t min_ts, int64_t max_ts, const cli_args_t* args,
                              allocator_t* a) {
  size_t buckets = args->has_concurrency_buckets ? (size_t)args->concurrency_buckets : 16;

  darray_t(trace_concurrency_bucket_t) concurrency_buckets = {};
  darray_resize(&concurrency_buckets, buckets, a);
  trace_concurrency_bucket_t* buckets_ptr = concurrency_buckets.ptr;

  trace_concurrency_compute(tracks, td, min_ts, max_ts, (int)buckets, buckets_ptr, a);

  // Calculate bucket_width for formatting
  int bucket_width = 1;
  size_t temp_buckets = buckets;
  while (temp_buckets >= 10) {
    bucket_width++;
    temp_buckets /= 10;
  }

  cli_table_t table = {};
  cli_table_init(&table);

  cli_table_add_column(&table, SV("Bucket"), CLI_ALIGN_LEFT, 0, true);
  cli_table_add_column(&table, SV("Time Range (s)"), CLI_ALIGN_LEFT, 0, true);
  cli_table_add_column(&table, SV("Concurrency (Active Threads)"), CLI_ALIGN_LEFT, 0, true);
  cli_table_add_column(&table, SV("Dominant Events"), CLI_ALIGN_LEFT, 0, true);

  size_t thread_track_count = 0;
  track_t* tracks_data = tracks->ptr;
  for (size_t i = 0; i < tracks->len; i++) {
    if (tracks_data[i].type == TRACK_TYPE_THREAD) {
      thread_track_count++;
    }
  }

  for (size_t b = 0; b < buckets; b++) {
    const trace_concurrency_bucket_t* bucket = &buckets_ptr[b];
    double start_s = (bucket->start_ts - (double)min_ts) / 1000000.0;
    double end_s = (bucket->end_ts - (double)min_ts) / 1000000.0;

    // Calculate percentage
    double pct = 0.0;
    if (thread_track_count > 0) {
      pct = (bucket->average_concurrency / (double)thread_track_count) * 100.0;
    }

    // Build the visual bar (20 characters wide)
    int active_chars = (int)((pct / 100.0) * 20.0);
    if (active_chars < 0) active_chars = 0;
    if (active_chars > 20) active_chars = 20;

    cli_table_add_row(&table);

    // Col 0: Bucket
    cli_table_set_cell_fmt(&table, 0, "[%0*zu]", bucket_width, b);

    // Col 1: Time Range
    cli_table_set_cell_fmt(&table, 1, "%.1f - %.1f", start_s, end_s);

    // Col 2: Concurrency Bar
    string_t bar = {};
    string_append(&bar, SV("["), a);
    for (int i = 0; i < active_chars; i++) {
      string_append(&bar, SV("█"), a);
    }
    for (int i = active_chars; i < 20; i++) {
      string_append(&bar, SV("░"), a);
    }
    string_printf(&bar, a, "] %3.0f%%     ", pct);
    cli_table_set_cell(&table, 2, string_get_view(&bar));
    string_free(bar, a);

    // Col 3: Dominant Events
    string_t events_str = {};
    for (size_t i = 0; i < bucket->dominant_events_count; i++) {
      string_view_t name = trace_data_get_string(td, bucket->dominant_events[i]);
      string_append(&events_str, name, a);
      if (i < bucket->dominant_events_count - 1) {
        string_append(&events_str, SV(", "), a);
      }
    }
    cli_table_set_cell(&table, 3, string_get_view(&events_str));
    string_free(events_str, a);
  }

  cli_table_print(&table);
  cli_table_deinit(&table);

  darray_deinit(&concurrency_buckets, a);
  return 0;
}

// Handles the 'aggregate' subcommand.
static int handle_aggregate(const trace_data_t* td, const cli_args_t* args,
                            allocator_t* a) {
  string_view_t group_by = string_view_is_empty(args->group_by) ? SV("name") : args->group_by;
  string_view_t sort_by = string_view_is_empty(args->sort_by) ? SV("duration") : args->sort_by;

  if (!string_view_eq(group_by, SV("name")) && !string_view_eq(group_by, SV("category"))) {
    fprintf(stderr, "Error: Invalid value for --group-by: '%.*s'. Expected 'name' or 'category'.\n", (int)group_by.len, group_by.ptr);
    return 1;
  }
  if (!string_view_eq(sort_by, SV("duration")) && !string_view_eq(sort_by, SV("count"))) {
    fprintf(stderr, "Error: Invalid value for --sort: '%.*s'. Expected 'duration' or 'count'.\n", (int)sort_by.len, sort_by.ptr);
    return 1;
  }

  darray_trace_aggregate_entry_t entries = {};
  trace_aggregate_compute(td, group_by, sort_by, &entries, a);

  cli_table_t table = {};
  cli_table_init(&table);

  bool by_cat = string_view_eq(group_by, SV("category"));
  cli_table_add_column(&table, by_cat ? SV("Event Category") : SV("Event Name"), CLI_ALIGN_LEFT, 30, true);
  cli_table_add_column(&table, SV("Total Duration (s)"), CLI_ALIGN_RIGHT, 18, true);
  cli_table_add_column(&table, SV("Event Count"), CLI_ALIGN_RIGHT, 11, true);
  cli_table_add_column(&table, SV("Average Duration (ms)"), CLI_ALIGN_RIGHT, 20, true);

  int min_count = args->has_min_count ? args->min_count : 2;
  size_t skipped_count = 0;
  trace_aggregate_entry_t* entries_ptr = entries.ptr;
  for (size_t i = 0; i < entries.len; i++) {
    const trace_aggregate_entry_t* e = &entries_ptr[i];
    if ((int)e->count < min_count) {
      skipped_count++;
      continue;
    }
    string_view_t key_name = trace_data_get_string(td, e->key_ref);
    
    double total_dur_s = e->total_duration / 1000000.0;
    double avg_dur_ms = 0.0;
    if (e->count > 0) {
      avg_dur_ms = (e->total_duration / (double)e->count) / 1000.0;
    }

    cli_table_add_row(&table);
    cli_table_set_cell(&table, 0, key_name);
    cli_table_set_cell_fmt(&table, 1, "%.2f", total_dur_s);
    cli_table_set_cell_fmt(&table, 2, "%zu", e->count);
    cli_table_set_cell_fmt(&table, 3, "%.2f", avg_dur_ms);
  }

  cli_table_print(&table);
  cli_table_deinit(&table);

  if (skipped_count > 0) {
    if (min_count == 2) {
      printf("\n* Skipped %zu single-instance events (count = 1).\n", skipped_count);
    } else {
      printf("\n* Skipped %zu events with count < %d.\n", skipped_count, min_count);
    }
  }

  darray_deinit(&entries, a);
  return 0;
}

// Handles the 'diff' subcommand.
static int handle_diff(const trace_data_t* td_baseline, const trace_data_t* td_target,
                       const cli_args_t* args, allocator_t* a) {
  string_view_t group_by = string_view_is_empty(args->group_by) ? SV("name") : args->group_by;
  string_view_t sort_by = string_view_is_empty(args->sort_by) ? SV("dur-delta") : args->sort_by;

  if (!string_view_eq(group_by, SV("name")) && !string_view_eq(group_by, SV("category"))) {
    fprintf(stderr, "Error: Invalid value for --group-by: '%.*s'. Expected 'name' or 'category'.\n", (int)group_by.len, group_by.ptr);
    return 1;
  }
  if (!string_view_eq(sort_by, SV("dur-delta")) && !string_view_eq(sort_by, SV("count-delta"))) {
    fprintf(stderr, "Error: Invalid value for --sort: '%.*s'. Expected 'dur-delta' or 'count-delta'.\n", (int)sort_by.len, sort_by.ptr);
    return 1;
  }

  darray_trace_diff_entry_t entries = {};
  trace_diff_compute(td_baseline, td_target, group_by, sort_by, &entries, a);

  cli_table_t table = {};
  cli_table_init(&table);

  bool by_cat = string_view_eq(group_by, SV("category"));
  cli_table_add_column(&table, by_cat ? SV("Event Category") : SV("Event Name"), CLI_ALIGN_LEFT, 30, true);
  cli_table_add_column(&table, SV("Baseline Dur (s)"), CLI_ALIGN_RIGHT, 16, true);
  cli_table_add_column(&table, SV("Target Dur (s)"), CLI_ALIGN_RIGHT, 14, true);
  cli_table_add_column(&table, SV("Delta Dur (s)"), CLI_ALIGN_RIGHT, 14, true);
  cli_table_add_column(&table, SV("Delta Count"), CLI_ALIGN_RIGHT, 11, true);

  trace_diff_entry_t* entries_ptr = entries.ptr;
  for (size_t i = 0; i < entries.len; i++) {
    const trace_diff_entry_t* e = &entries_ptr[i];
    string_view_t key_name = e->key;

    double base_dur_s = e->baseline_duration / 1000000.0;
    double target_dur_s = e->target_duration / 1000000.0;
    double delta_dur_s = e->delta_duration / 1000000.0;

    cli_table_add_row(&table);
    cli_table_set_cell(&table, 0, key_name);
    cli_table_set_cell_fmt(&table, 1, "%.2f", base_dur_s);
    cli_table_set_cell_fmt(&table, 2, "%.2f", target_dur_s);
    cli_table_set_cell_fmt(&table, 3, "%+.2f", delta_dur_s);
    cli_table_set_cell_fmt(&table, 4, "%+ld", (long)e->delta_count);
  }

  cli_table_print(&table);
  cli_table_deinit(&table);

  darray_deinit(&entries, a);
  return 0;
}

// Handles the 'histogram' subcommand.
static int handle_histogram(const trace_data_t* td, const darray_track_t* tracks,
                            const cli_args_t* args, allocator_t* a) {
  // Gather all event indices matching the filters
  darray_int64_t selected_indices = {};
  const trace_event_persisted_t* events = td->events.ptr;

  bool has_track_filter = (args->track_filter != nullptr);

  if (has_track_filter) {
    string_view_t target_track = string_view_from_cstr(args->track_filter);
    track_t* tracks_data = tracks->ptr;
    for (size_t i = 0; i < tracks->len; i++) {
      const track_t* t = &tracks_data[i];
      string_view_t track_name = trace_data_get_string(td, t->name_ref);
      if (string_view_eq(track_name, target_track)) {
        const size_t* idx_ptr = t->event_indices.ptr;
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

            darray_push(&selected_indices, (int64_t)event_idx, a);
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

      darray_push(&selected_indices, (int64_t)i, a);
    }
  }

  // Compute histogram
  trace_histogram_t h = {};
  trace_histogram_compute(&selected_indices, td, &h);

  // Print summary
  const char* scale_str = "linear";
  if (h.has_non_zero_durations && (h.num_buckets > 0)) {
    int64_t width_first = h.buckets[0].max_dur - h.buckets[0].min_dur;
    int64_t width_last = h.buckets[h.num_buckets - 1].max_dur -
                         h.buckets[h.num_buckets - 1].min_dur;
    if (width_last > width_first * 2) {
      scale_str = "logarithmic";
    }
  }
  printf("Scale: %s, Total Events: %zu\n\n", scale_str, selected_indices.len);

  // Calculate bucket_width for formatting
  int bucket_width = 1;
  int temp_buckets = h.num_buckets;
  while (temp_buckets >= 10) {
    bucket_width++;
    temp_buckets /= 10;
  }

  cli_table_t table = {};
  cli_table_init(&table);

  cli_table_add_column(&table, SV("Bucket"), CLI_ALIGN_LEFT, 0, true);
  cli_table_add_column(&table, SV("Range (us)"), CLI_ALIGN_LEFT, 0, true);
  cli_table_add_column(&table, SV("Count"), CLI_ALIGN_RIGHT, 0, true);
  cli_table_add_column(&table, SV("Distribution"), CLI_ALIGN_LEFT, 0, true);

  for (int i = 0; i < h.num_buckets; i++) {
    const trace_histogram_bucket_t* b = &h.buckets[i];

    double pct = 0.0;
    if (selected_indices.len > 0) {
      pct = ((double)b->count / (double)selected_indices.len) * 100.0;
    }

    int active_chars = (int)((pct / 100.0) * 20.0);
    if (active_chars < 0) active_chars = 0;
    if (active_chars > 20) active_chars = 20;

    cli_table_add_row(&table);
    cli_table_set_cell_fmt(&table, 0, "[%0*d]", bucket_width, i);
    cli_table_set_cell_fmt(&table, 1, "%ld - %ld", (long)b->min_dur, (long)b->max_dur);
    cli_table_set_cell_fmt(&table, 2, "%u", b->count);

    string_t bar = {};
    string_append(&bar, SV("["), a);
    for (int j = 0; j < active_chars; j++) {
      string_append(&bar, SV("█"), a);
    }
    for (int j = active_chars; j < 20; j++) {
      string_append(&bar, SV("░"), a);
    }
    string_printf(&bar, a, "] %3.0f%%", pct);
    cli_table_set_cell(&table, 3, string_get_view(&bar));
    string_free(bar, a);
  }

  cli_table_print(&table);
  cli_table_deinit(&table);

  darray_deinit(&selected_indices, a);
  return 0;
}

// Handles the 'inspect' subcommand.
static int handle_inspect(const trace_data_t* td, const darray_track_t* tracks,
                          const cli_args_t* args, allocator_t* a) {
  (void)a;
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
  track_t* tracks_data = tracks->ptr;

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

  const size_t* event_indices = target_track->event_indices.ptr;
  const trace_event_persisted_t* events = td->events.ptr;

  // Use binary search to find the first event with ts >= target_ts
  size_t start_k = trace_data_events_lower_bound(
      event_indices, target_track->event_indices.len, events, target_ts);

  // Inspect all events starting at target_ts
  bool first_event = true;
  for (size_t k = start_k; k < target_track->event_indices.len; k++) {
    size_t event_idx = event_indices[k];
    const trace_event_persisted_t* e = &events[event_idx];
    if (e->ts != target_ts) {
      break;  // Since events are sorted by ts, we stop as soon as ts differs
    }

    if (!first_event) {
      printf("\n---\n\n");
    }
    first_event = false;

    cli_table_t details_table = {};
    cli_table_init(&details_table);

    cli_table_add_column(&details_table, SV("Property"), CLI_ALIGN_LEFT, 20, true);
    cli_table_add_column(&details_table, SV("Value"), CLI_ALIGN_LEFT, 30, true);

    cli_table_add_row(&details_table);
    cli_table_set_cell(&details_table, 0, SV("Name"));
    cli_table_set_cell(&details_table, 1, trace_data_get_string(td, e->name_ref));

    cli_table_add_row(&details_table);
    cli_table_set_cell(&details_table, 0, SV("Track"));
    cli_table_set_cell(&details_table, 1, target_track_name);

    cli_table_add_row(&details_table);
    cli_table_set_cell(&details_table, 0, SV("Timestamp (us)"));
    cli_table_set_cell_fmt(&details_table, 1, "%ld", (long)e->ts);

    cli_table_add_row(&details_table);
    cli_table_set_cell(&details_table, 0, SV("Duration (us)"));
    cli_table_set_cell_fmt(&details_table, 1, "%ld", (long)e->dur);

    if (target_track->type == TRACK_TYPE_THREAD) {
      const int64_t* self_durs = target_track->self_durs.ptr;
      const uint32_t* depths = target_track->depths.ptr;

      cli_table_add_row(&details_table);
      cli_table_set_cell(&details_table, 0, SV("Self Time (us)"));
      cli_table_set_cell_fmt(&details_table, 1, "%ld", (long)self_durs[k]);

      cli_table_add_row(&details_table);
      cli_table_set_cell(&details_table, 0, SV("Depth"));
      cli_table_set_cell_fmt(&details_table, 1, "%d", depths[k]);

      uint32_t depth_target = depths[k];

      // 1. Find Parent: nearest preceding event with depth == depth_target - 1
      const trace_event_persisted_t* parent_event = nullptr;
      for (int prev = (int)k - 1; prev >= 0; prev--) {
        if (depth_target > 0 && depths[prev] == depth_target - 1) {
          parent_event = &events[event_indices[prev]];
          break;
        }
      }

      if (parent_event) {
        string_view_t parent_name = trace_data_get_string(td, parent_event->name_ref);
        cli_table_add_row(&details_table);
        cli_table_set_cell(&details_table, 0, SV("Parent Name"));
        cli_table_set_cell(&details_table, 1, parent_name);

        cli_table_add_row(&details_table);
        cli_table_set_cell(&details_table, 0, SV("Parent TS (us)"));
        cli_table_set_cell_fmt(&details_table, 1, "%ld", (long)parent_event->ts);
      }
    }

    // Custom Arguments
    if (e->args_count > 0) {
      const trace_arg_persisted_t* args_ptr =
          (const trace_arg_persisted_t*)td->args.ptr + e->args_offset;
      for (uint32_t a_idx = 0; a_idx < e->args_count; a_idx++) {
        const trace_arg_persisted_t* arg = &args_ptr[a_idx];
        string_view_t key = trace_data_get_string(td, arg->key_ref);
        
        cli_table_add_row(&details_table);
        // Prefix argument keys to distinguish them
        cli_table_set_cell_fmt(&details_table, 0, "Arg: %.*s", (int)key.len, key.ptr);
        
        if (arg->val_ref != 0) {
          string_view_t val = trace_data_get_string(td, arg->val_ref);
          cli_table_set_cell(&details_table, 1, val);
        } else {
          cli_table_set_cell_fmt(&details_table, 1, "%f", arg->val_double);
        }
      }
    }

    cli_table_print(&details_table);
    cli_table_deinit(&details_table);

    // 2. Find and print Children
    if (target_track->type == TRACK_TYPE_THREAD) {
      const uint32_t* depths = target_track->depths.ptr;
      uint32_t depth_target = depths[k];
      
      // Count children first
      size_t child_count = 0;
      for (size_t next = k + 1; next < target_track->event_indices.len; next++) {
        uint32_t next_depth = depths[next];
        if (next_depth <= depth_target) {
          break;
        }
        if (next_depth == depth_target + 1) {
          child_count++;
        }
      }

      if (child_count > 0) {
        printf("\nChildren:\n");
        cli_table_t children_table = {};
        cli_table_init(&children_table);

        cli_table_add_column(&children_table, SV("Child Name"), CLI_ALIGN_LEFT, 20, true);
        cli_table_add_column(&children_table, SV("Timestamp (us)"), CLI_ALIGN_RIGHT, 15, true);
        cli_table_add_column(&children_table, SV("Duration (us)"), CLI_ALIGN_RIGHT, 15, true);

        for (size_t next = k + 1; next < target_track->event_indices.len; next++) {
          uint32_t next_depth = depths[next];
          if (next_depth <= depth_target) {
            break;
          }
          if (next_depth == depth_target + 1) {
            const trace_event_persisted_t* child_event = &events[event_indices[next]];
            string_view_t child_name = trace_data_get_string(td, child_event->name_ref);

            cli_table_add_row(&children_table);
            cli_table_set_cell(&children_table, 0, child_name);
            cli_table_set_cell_fmt(&children_table, 1, "%ld", (long)child_event->ts);
            cli_table_set_cell_fmt(&children_table, 2, "%ld", (long)child_event->dur);
          }
        }

        cli_table_print(&children_table);
        cli_table_deinit(&children_table);
      }
    }
  }

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
static int handle_query(const trace_data_t* td, const darray_track_t* tracks,
                        const cli_args_t* args, allocator_t* a) {
  track_t* tracks_data = tracks->ptr;

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
  darray_t(query_match_t) matches = {};

  const trace_event_persisted_t* events = td->events.ptr;

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
    const size_t* event_indices = t->event_indices.ptr;
    const uint32_t* depths = t->depths.ptr;

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
        int depth = (t->type == TRACK_TYPE_THREAD) ? (int)depths[k] : 0;
        if (depth > args->max_depth) {
          continue;
        }
      }

      // We have a match!
      query_match_t m_val = {
          .event_idx = event_idx,
          .track = t,
          .depth = (t->type == TRACK_TYPE_THREAD) ? (int)depths[k] : 0,
          .ts = e->ts,
      };
      darray_push(&matches, m_val, a);
    }
  }

  // Sort matches chronologically
  if (matches.len > 0) {
    qsort(matches.ptr, matches.len, sizeof(query_match_t),
          compare_query_matches);
  }

  cli_table_t table = {};
  cli_table_init(&table);

  cli_table_add_column(&table, SV("Event Name"), CLI_ALIGN_LEFT, 30, true);
  cli_table_add_column(&table, SV("Track"), CLI_ALIGN_LEFT, 20, true);
  cli_table_add_column(&table, SV("Start Time (us)"), CLI_ALIGN_RIGHT, 17, true);
  cli_table_add_column(&table, SV("Duration (us)"), CLI_ALIGN_RIGHT, 15, true);
  cli_table_add_column(&table, SV("Depth"), CLI_ALIGN_RIGHT, 5, true);

  size_t limit = args->has_limit ? (size_t)args->limit : matches.len;
  size_t print_count = (matches.len < limit) ? matches.len : limit;

  const query_match_t* matches_data = (const query_match_t*)matches.ptr;
  for (size_t i = 0; i < print_count; i++) {
    const query_match_t* m = &matches_data[i];
    const trace_event_persisted_t* e = &events[m->event_idx];

    cli_table_add_row(&table);
    cli_table_set_cell(&table, 0, trace_data_get_string(td, e->name_ref));
    cli_table_set_cell(&table, 1, trace_data_get_string(td, m->track->name_ref));
    cli_table_set_cell_fmt(&table, 2, "%ld", (long)e->ts);
    cli_table_set_cell_fmt(&table, 3, "%ld", (long)e->dur);
    cli_table_set_cell_fmt(&table, 4, "%d", m->depth);
  }

  cli_table_print(&table);
  cli_table_deinit(&table);

  darray_deinit(&matches, a);

  return 0;
}

// main entry point preferring success path under if.
int main(int argc, char* argv[]) {
  int exit_code = 0;
  cli_args_t args = {};

  if (parse_arguments(argc, argv, &args)) {
    allocator_t* a = c_allocator();
    darray_track_t tracks = {};
    int64_t min_ts = 0;
    int64_t max_ts = 0;
    trace_data_t* td =
        trace_loader_load_file(args.trace_file, a, nullptr, &tracks, &min_ts,
                               &max_ts, nullptr, nullptr);

    if (td) {
      string_view_t sub = string_view_from_cstr(args.subcommand);

      if (string_view_eq(sub, SV("summary"))) {
        exit_code =
            handle_summary(td, &tracks, min_ts, max_ts, args.list_tracks, a);
      } else if (string_view_eq(sub, SV("concurrency"))) {
        exit_code = handle_concurrency(td, &tracks, min_ts, max_ts, &args, a);
      } else if (string_view_eq(sub, SV("aggregate"))) {
        exit_code = handle_aggregate(td, &args, a);
      } else if (string_view_eq(sub, SV("diff"))) {
        darray_track_t tracks_2 = {};
        int64_t min_ts_2 = 0;
        int64_t max_ts_2 = 0;
        trace_data_t* td2 =
            trace_loader_load_file(args.trace_file_2, a, nullptr, &tracks_2, &min_ts_2,
                                   &max_ts_2, nullptr, nullptr);
        if (td2) {
          exit_code = handle_diff(td, td2, &args, a);
          
          // Clean up td2
          track_t* tracks_data_2 = tracks_2.ptr;
          for (size_t i = 0; i < tracks_2.len; i++) {
            track_deinit(&tracks_data_2[i], a);
          }
          darray_deinit(&tracks_2, a);
          trace_data_release(td2, a);
        } else {
          exit_code = 1;
        }
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
      track_t* tracks_data = tracks.ptr;
      for (size_t i = 0; i < tracks.len; i++) {
        track_deinit(&tracks_data[i], a);
      }
      darray_deinit(&tracks, a);
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
