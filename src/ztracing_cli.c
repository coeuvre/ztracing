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
#include "src/trace_loader.h"
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
}

typedef struct cli_args {
  const char* subcommand;
  const char* trace_file;
  bool pretty;
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
