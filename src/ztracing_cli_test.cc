#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct command_result {
  std::string output;
  int exit_code;
};

// Runs the CLI binary in a sandboxed subprocess and captures combined
// stdout/stderr and exit status.
static command_result run_cli(const std::string& args) {
  command_result result = {};
  std::array<char, 256> buffer;

  // Locate the binary in the Bazel sandbox runfiles
  std::string cmd = "src/ztracing " + args + " 2>&1";

  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("popen() failed to execute CLI binary!");
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result.output += buffer.data();
  }

  int status = pclose(pipe);
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else {
    result.exit_code = -1;
  }

  return result;
}

class ztracing_cli_test : public ::testing::Test {
 protected:
  std::vector<std::string> temp_files_;

  void TearDown() override {
    // Automatically clean up all temporary files created during the test
    for (const auto& path : temp_files_) {
      unlink(path.c_str());
    }
  }

  // Writes a hermetic trace file into the test's dedicated temporary directory.
  std::string write_temp_trace(const std::string& filename,
                               const std::string& json_content) {
    const char* test_tmpdir = getenv("TEST_TMPDIR");
    std::string path =
        test_tmpdir ? std::string(test_tmpdir) + "/" + filename : filename;

    std::ofstream f(path);
    EXPECT_TRUE(f.is_open()) << "Failed to create temp trace file at: " << path;
    f << json_content;
    f.close();

    temp_files_.push_back(path);
    return path;
  }

  // Compresses a string to gzip format and writes it to a hermetic temporary
  // file.
  std::string write_temp_gzip_trace(const std::string& filename,
                                    const std::string& json_content) {
    const char* test_tmpdir = getenv("TEST_TMPDIR");
    std::string path =
        test_tmpdir ? std::string(test_tmpdir) + "/" + filename : filename;

    // Initialize zlib deflate stream for gzip
    z_stream strm = {};
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS,
                     8, Z_DEFAULT_STRATEGY) != Z_OK) {
      throw std::runtime_error("deflateInit2 failed!");
    }

    strm.next_in = (Bytef*)json_content.data();
    strm.avail_in = (uInt)json_content.size();

    // Allocate a buffer for compressed output
    std::vector<unsigned char> out_buf(json_content.size() + 1024);
    strm.next_out = out_buf.data();
    strm.avail_out = (uInt)out_buf.size();

    // Compress
    int status = deflate(&strm, Z_FINISH);
    if (status != Z_STREAM_END) {
      deflateEnd(&strm);
      throw std::runtime_error("deflate failed to finish!");
    }

    size_t compressed_size = out_buf.size() - strm.avail_out;
    deflateEnd(&strm);

    // Write compressed bytes to file
    std::ofstream f(path, std::ios::binary);
    EXPECT_TRUE(f.is_open())
        << "Failed to create temp gzip trace file at: " << path;
    f.write((const char*)out_buf.data(), compressed_size);
    f.close();

    temp_files_.push_back(path);
    return path;
  }

  // Captures the CLI output and asserts that it matches the golden reference
  // file exactly.
  void assert_golden_output(const std::string& args,
                            const std::string& golden_name,
                            int expected_exit_code = 0) {
    command_result res = run_cli(args);
    EXPECT_EQ(res.exit_code, expected_exit_code)
        << "Exit status mismatch for args: '" << args << "'";

    std::string golden_path = "src/testdata/cli/" + golden_name;

    // Read golden file content
    std::ifstream golden_file(golden_path);
    std::string golden_content;
    if (golden_file.is_open()) {
      std::string line;
      bool first_line = true;
      while (std::getline(golden_file, line)) {
        if (first_line && !line.empty() && line[0] == '#') {
          first_line = false;
          continue;
        }
        first_line = false;
        golden_content += line + "\n";
      }
      golden_file.close();
    }

    const char* outputs_dir = getenv("TEST_UNDECLARED_OUTPUTS_DIR");
    std::string failed_path = "/tmp/" + golden_name + "_failed";
    bool is_bazel = (outputs_dir != nullptr);
    if (is_bazel) {
      failed_path = std::string(outputs_dir) + "/" + golden_name + "_failed";
    }

    if (res.output != golden_content) {
      // Sanitize args to remove absolute temp paths
      std::string sanitized_args = args;
      for (const auto& temp_path : temp_files_) {
        size_t pos = sanitized_args.find(temp_path);
        if (pos != std::string::npos) {
          size_t last_slash = temp_path.find_last_of('/');
          std::string filename = (last_slash == std::string::npos) ? temp_path : temp_path.substr(last_slash + 1);
          sanitized_args.replace(pos, temp_path.length(), filename);
        }
      }

      // Save failed output for the developer to inspect/diff
      std::ofstream failed_file(failed_path);
      if (failed_file.is_open()) {
        failed_file << "# ztracing " << sanitized_args << "\n";
        failed_file << res.output;
        failed_file.close();
      }

      std::string instructions =
          is_bazel ? "cp bazel-testlogs/src/ztracing_cli_test/test.outputs/" +
                         golden_name + "_failed " + golden_path
                   : "cp " + failed_path + " " + golden_path;

      FAIL() << "CLI output mismatch for args: '" << args
             << "' against golden '" << golden_path << "'!\n"
             << "Saved failed output to '" << failed_path << "'.\n"
             << "If the new output is correct, establish/update the golden "
                "reference using:\n"
             << "  " << instructions;
    }
  }
};

// Standard mock trace content used across multiple tests.
const std::string STANDARD_MOCK_TRACE = R"([
  {"name": "task_A", "cat": "test", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
  {"name": "task_B", "cat": "test", "ph": "B", "ts": 2000, "pid": 1, "tid": 1},
  {"name": "task_B", "cat": "test", "ph": "E", "ts": 3000, "pid": 1, "tid": 1}
])";

// Verify that running with no arguments matches the golden help text.
TEST_F(ztracing_cli_test, no_arguments_matches_golden_help) {
  assert_golden_output("", "help.golden", 1);
}

// Verify that running help flag matches the golden help text.
TEST_F(ztracing_cli_test, help_flag_matches_golden_help) {
  assert_golden_output("--help", "help.golden", 1);
}

// Verify the 'summary' subcommand output.
TEST_F(ztracing_cli_test, summary_output_matches_golden) {
  std::string path =
      write_temp_trace("summary_standard.json", STANDARD_MOCK_TRACE);
  assert_golden_output("summary " + path, "summary.golden", 0);
}

// Verify the 'summary' subcommand output with gzip input.
TEST_F(ztracing_cli_test, summary_gzip_output_matches_golden) {
  std::string path =
      write_temp_gzip_trace("summary_standard.json.gz", STANDARD_MOCK_TRACE);
  assert_golden_output("summary " + path, "summary.golden", 0);
}

// Verify that running an unknown subcommand matches the golden error text.
TEST_F(ztracing_cli_test, unknown_subcommand_matches_golden_error) {
  std::string path = write_temp_trace("empty.json", "[]");
  assert_golden_output("unknown_subcommand " + path,
                       "error_unknown_subcommand.golden", 1);
}

// Verify that running with a non-existent trace file matches the golden
// error text.
TEST_F(ztracing_cli_test, non_existent_trace_file_matches_golden_error) {
  assert_golden_output("summary non_existent_file.json",
                       "error_non_existent_file.golden", 1);
}

// Verify that running with a corrupted gzip file prints a decompression
// error (inline assertion).
TEST_F(ztracing_cli_test, corrupted_gzip_file_errors) {
  // Write a corrupted gzip file: starts with correct gzip magic but followed by
  // garbage
  const char* test_tmpdir = getenv("TEST_TMPDIR");
  std::string path = test_tmpdir
                         ? std::string(test_tmpdir) + "/corrupted.json.gz"
                         : "corrupted.json.gz";

  std::ofstream f(path, std::ios::binary);
  ASSERT_TRUE(f.is_open());
  unsigned char bad_data[] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0xff, 0xff, 0xff, 0xff};
  f.write((const char*)bad_data, sizeof(bad_data));
  f.close();
  temp_files_.push_back(path);

  command_result res = run_cli("summary " + path);
  EXPECT_EQ(res.exit_code, 1);
  EXPECT_NE(res.output.find("Error: Gzip decompression failed"),
            std::string::npos);
}

// Verify the 'summary' subcommand output with --list-tracks.
TEST_F(ztracing_cli_test, summary_list_tracks_output_matches_golden) {
  std::string path =
      write_temp_trace("summary_list_tracks.json", STANDARD_MOCK_TRACE);
  assert_golden_output("summary " + path + " --list-tracks",
                       "summary_list_tracks.golden", 0);
}

// Verify the 'concurrency' subcommand output.
TEST_F(ztracing_cli_test, concurrency_output_matches_golden) {
  std::string path =
      write_temp_trace("concurrency_standard.json", STANDARD_MOCK_TRACE);
  assert_golden_output("concurrency " + path, "concurrency.golden", 0);
}

// Verify the 'histogram' subcommand output.
TEST_F(ztracing_cli_test, histogram_output_matches_golden) {
  std::string path =
      write_temp_trace("histogram_standard.json", STANDARD_MOCK_TRACE);
  assert_golden_output("histogram " + path, "histogram.golden", 0);
}

// Verify the 'histogram' subcommand with advanced filtering options (raw JSON).
TEST_F(ztracing_cli_test, histogram_filtered_output_matches_golden) {
  // Write a more complex mock trace to test filtering
  std::string complex_trace = R"([
    {"name": "render_frame", "cat": "gpu", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
    {"name": "network_request", "cat": "net", "ph": "X", "ts": 1500, "dur": 8000, "pid": 1, "tid": 2},
    {"name": "parse_json", "cat": "cpu", "ph": "X", "ts": 2000, "dur": 300, "pid": 1, "tid": 2},
    {"name": "render_frame", "cat": "gpu", "ph": "X", "ts": 3000, "dur": 600, "pid": 1, "tid": 1}
  ])";
  std::string path = write_temp_trace("histogram_filtered.json", complex_trace);

  // Substring match filter: "render" -> should only match the two render_frame
  // events!
  assert_golden_output("histogram " + path + " --match render",
                       "histogram_match_render.golden", 0);

  // Time filter: ts between 1200 and 2500 -> should only match network_request
  // and parse_json!
  assert_golden_output("histogram " + path + " --t-start 1200 --t-end 2500",
                       "histogram_time_filtered.golden", 0);
}

// Verify the 'inspect' subcommand output on a simple trace.
TEST_F(ztracing_cli_test, inspect_output_matches_golden) {
  std::string path =
      write_temp_trace("inspect_standard.json", STANDARD_MOCK_TRACE);
  assert_golden_output("inspect " + path + " --track \"\" --ts 1000",
                       "inspect.golden", 0);
}

// Verify the 'inspect' subcommand on nested events (parent, children, and
// self-time).
TEST_F(ztracing_cli_test, inspect_nested_hierarchy_matches_golden) {
  std::string nested_trace = R"([
    {"name": "parent_task", "cat": "test", "ph": "X", "ts": 1000, "dur": 1000, "pid": 1, "tid": 1},
    {"name": "child_task_A", "cat": "test", "ph": "X", "ts": 1100, "dur": 300, "pid": 1, "tid": 1},
    {"name": "child_task_B", "cat": "test", "ph": "X", "ts": 1500, "dur": 400, "pid": 1, "tid": 1}
  ])";
  std::string path = write_temp_trace("inspect_nested.json", nested_trace);

  // 1. Inspect the parent (should list child_task_A and child_task_B, self_time
  // = 300)
  assert_golden_output("inspect " + path + " --track \"\" --ts 1000",
                       "inspect_nested_parent.golden", 0);

  // 2. Inspect the first child (should list parent_task as parent, children
  // empty, self_time = 300)
  assert_golden_output("inspect " + path + " --track \"\" --ts 1100",
                       "inspect_nested_child.golden", 0);
}

// Verify the 'query' subcommand output.
TEST_F(ztracing_cli_test, query_output_matches_golden) {
  std::string path = write_temp_trace("query_standard.json", STANDARD_MOCK_TRACE);
  assert_golden_output("query " + path, "query.golden", 0);
}

// Verify the 'query' subcommand with advanced filtering options.
TEST_F(ztracing_cli_test, query_filtered_output_matches_golden) {
  // Write a nested and multi-threaded mock trace with named threads to test
  // complex filters
  std::string complex_trace = R"([
    {"name": "thread_name", "ph": "M", "pid": 1, "tid": 1, "args": {"name": "main_thread"}},
    {"name": "thread_name", "ph": "M", "pid": 1, "tid": 2, "args": {"name": "worker_thread"}},
    {"name": "parent_task", "cat": "cpu", "ph": "X", "ts": 1000, "dur": 1000, "pid": 1, "tid": 1},
    {"name": "child_task_A", "cat": "cpu", "ph": "X", "ts": 1100, "dur": 300, "pid": 1, "tid": 1},
    {"name": "network_request", "cat": "net", "ph": "X", "ts": 1500, "dur": 800, "pid": 1, "tid": 2},
    {"name": "render_frame", "cat": "gpu", "ph": "X", "ts": 2000, "dur": 500, "pid": 1, "tid": 1}
  ])";
  std::string path = write_temp_trace("query_filtered.json", complex_trace);

  // 1. Substring match and max-depth filter: "task" at depth <= 0 -> should
  // only match parent_task!
  assert_golden_output("query " + path + " --match task --max-depth 0",
                       "query_match_depth.golden", 0);

  // 2. Time-window and limit filter: ts >= 1200, limit to 1 match -> should
  // only return network_request!
  assert_golden_output("query " + path + " --t-start 1200 --limit 1",
                       "query_time_limit.golden", 0);

  // 3. Track filter: only scan the named "worker_thread"
  assert_golden_output("query " + path + " --track \"worker_thread\"",
                       "query_track_filter.golden", 0);
}

// Verify the 'aggregate' subcommand output.
TEST_F(ztracing_cli_test, aggregate_output_matches_golden) {
  std::string complex_trace = R"([
    {"name": "task_A", "cat": "cpu", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "gpu", "ph": "X", "ts": 1500, "dur": 1000, "pid": 1, "tid": 1},
    {"name": "task_A", "cat": "cpu", "ph": "X", "ts": 2000, "dur": 300, "pid": 1, "tid": 1}
  ])";
  std::string path = write_temp_trace("aggregate.json", complex_trace);

  // 1. Default (group by name, sort by duration descending)
  assert_golden_output("aggregate " + path, "aggregate_default.golden", 0);

  // 2. Group by category, sort by duration descending
  assert_golden_output("aggregate " + path + " --group-by category",
                       "aggregate_by_category.golden", 0);

  // 3. Group by name, sort by count descending
  assert_golden_output("aggregate " + path + " --sort count",
                       "aggregate_sort_by_count.golden", 0);
}

// Verify the 'aggregate' subcommand with --min-count option.
TEST_F(ztracing_cli_test, aggregate_min_count_matches_golden) {
  std::string complex_trace = R"([
    {"name": "task_A", "cat": "cpu", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "gpu", "ph": "X", "ts": 1500, "dur": 1000, "pid": 1, "tid": 1},
    {"name": "task_A", "cat": "cpu", "ph": "X", "ts": 2000, "dur": 300, "pid": 1, "tid": 1}
  ])";
  std::string path = write_temp_trace("aggregate_min_count.json", complex_trace);

  // 1. Show all (min-count = 1) -> should show task_A and task_B, no footnote
  assert_golden_output("aggregate " + path + " --min-count 1",
                       "aggregate_min_count_1.golden", 0);

  // 2. Filter out all (min-count = 5) -> should show empty table and footnote
  assert_golden_output("aggregate " + path + " --min-count 5",
                       "aggregate_min_count_5.golden", 0);
}

// Verify the 'diff' subcommand output.
TEST_F(ztracing_cli_test, diff_output_matches_golden) {
  std::string baseline_trace = R"([
    {"name": "task_A", "cat": "cpu", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "gpu", "ph": "X", "ts": 1500, "dur": 1000, "pid": 1, "tid": 1}
  ])";
  std::string target_trace = R"([
    {"name": "task_A", "cat": "cpu", "ph": "X", "ts": 1000, "dur": 800, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "gpu", "ph": "X", "ts": 1500, "dur": 900, "pid": 1, "tid": 1},
    {"name": "task_C", "cat": "cpu", "ph": "X", "ts": 2500, "dur": 300, "pid": 1, "tid": 1}
  ])";
  std::string path_base = write_temp_trace("diff_base.json", baseline_trace);
  std::string path_target = write_temp_trace("diff_target.json", target_trace);

  // 1. Default (group by name, sort by dur-delta descending)
  assert_golden_output("diff " + path_base + " " + path_target, "diff_default.golden", 0);

  // 2. Group by category, sort by dur-delta descending
  assert_golden_output("diff " + path_base + " " + path_target + " --group-by category",
                       "diff_by_category.golden", 0);

  // 3. Group by name, sort by count-delta descending
  assert_golden_output("diff " + path_base + " " + path_target + " --sort count-delta",
                       "diff_sort_by_count_delta.golden", 0);
}

}  // namespace
