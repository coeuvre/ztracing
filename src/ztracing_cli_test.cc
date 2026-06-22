#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

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
      while (std::getline(golden_file, line)) {
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
      // Save failed output for the developer to inspect/diff
      std::ofstream failed_file(failed_path);
      if (failed_file.is_open()) {
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

// 1. Verify that running with no arguments matches the golden help text.
TEST_F(ztracing_cli_test, no_arguments_matches_golden_help) {
  assert_golden_output("", "help.golden", 1);
}

// 2. Verify that running help flag matches the golden help text.
TEST_F(ztracing_cli_test, help_flag_matches_golden_help) {
  assert_golden_output("--help", "help.golden", 1);
}

// 3. Verify the 'summary' subcommand output in default minified mode.
TEST_F(ztracing_cli_test, summary_minified_output_matches_golden) {
  std::string trace = R"([
    {"name": "task_A", "cat": "test", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "test", "ph": "B", "ts": 2000, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "test", "ph": "E", "ts": 3000, "pid": 1, "tid": 1}
  ])";
  std::string path = write_temp_trace("summary_standard.json", trace);
  assert_golden_output("summary " + path, "summary_minified.golden", 0);
}

// 4. Verify the 'summary' subcommand output in pretty-printed mode.
TEST_F(ztracing_cli_test, summary_pretty_output_matches_golden) {
  std::string trace = R"([
    {"name": "task_A", "cat": "test", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "test", "ph": "B", "ts": 2000, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "test", "ph": "E", "ts": 3000, "pid": 1, "tid": 1}
  ])";
  std::string path = write_temp_trace("summary_pretty.json", trace);
  assert_golden_output("summary " + path + " --pretty", "summary_pretty.golden",
                       0);
}

// 5. Verify the 'summary' subcommand output with pretty flag placed globally.
TEST_F(ztracing_cli_test, pretty_flag_can_be_placed_globally) {
  std::string trace = R"([
    {"name": "task_A", "cat": "test", "ph": "X", "ts": 1000, "dur": 500, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "test", "ph": "B", "ts": 2000, "pid": 1, "tid": 1},
    {"name": "task_B", "cat": "test", "ph": "E", "ts": 3000, "pid": 1, "tid": 1}
  ])";
  std::string path = write_temp_trace("summary_global_pretty.json", trace);
  assert_golden_output("--pretty summary " + path, "summary_pretty.golden", 0);
}

// 6. Verify that running an unknown subcommand matches the golden error text.
TEST_F(ztracing_cli_test, unknown_subcommand_matches_golden_error) {
  std::string path = write_temp_trace("empty.json", "[]");
  assert_golden_output("unknown_subcommand " + path,
                       "error_unknown_subcommand.golden", 1);
}

// 7. Verify that running with a non-existent trace file matches the golden
// error text.
TEST_F(ztracing_cli_test, non_existent_trace_file_matches_golden_error) {
  assert_golden_output("summary non_existent_file.json",
                       "error_non_existent_file.golden", 1);
}

}  // namespace
