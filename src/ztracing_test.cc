#include "src/ztracing.h"

#include <GLES3/gl3.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "src/app.h"
#include "src/platform.h"

// Declare the native-only test helper exported by ztracing_headless.cc
extern "C" app_t* ztracing_headless_get_app(void);

namespace {

// Helper to save a raw RGBA buffer as a 24-bit BMP image.
bool save_bmp(const std::string& filepath, int width, int height,
              const unsigned char* rgba) {
  std::ofstream f(filepath, std::ios::binary);
  if (!f) {
    std::cerr << "Failed to open BMP for writing: " << filepath << std::endl;
    return false;
  }

  unsigned char header[54] = {
      'B', 'M',        // Magic
      0,   0,   0, 0,  // Size (filled below)
      0,   0,   0, 0,  // Reserved
      54,  0,   0, 0,  // Offset to pixel data
      40,  0,   0, 0,  // Header size
      0,   0,   0, 0,  // Width (filled below)
      0,   0,   0, 0,  // Height (filled below)
      1,   0,          // Planes
      24,  0,          // Bits per pixel (24-bit BGR)
      0,   0,   0, 0,  // Compression
      0,   0,   0, 0,  // Image size
      0,   0,   0, 0,  // X pixels per meter
      0,   0,   0, 0,  // Y pixels per meter
      0,   0,   0, 0,  // Total colors
      0,   0,   0, 0   // Important colors
  };

  int row_size = (width * 3 + 3) & ~3;
  int size = 54 + row_size * height;

  header[2] = (unsigned char)(size);
  header[3] = (unsigned char)(size >> 8);
  header[4] = (unsigned char)(size >> 16);
  header[5] = (unsigned char)(size >> 24);

  header[18] = (unsigned char)(width);
  header[19] = (unsigned char)(width >> 8);
  header[20] = (unsigned char)(width >> 16);
  header[21] = (unsigned char)(width >> 24);

  header[22] = (unsigned char)(height);
  header[23] = (unsigned char)(height >> 8);
  header[24] = (unsigned char)(height >> 16);
  header[25] = (unsigned char)(height >> 24);

  f.write((char*)header, 54);

  std::vector<unsigned char> row(row_size, 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int r_idx = (y * width + x) * 4;
      row[x * 3 + 0] = rgba[r_idx + 2];  // B
      row[x * 3 + 1] = rgba[r_idx + 1];  // G
      row[x * 3 + 2] = rgba[r_idx + 0];  // R
    }
    f.write((char*)row.data(), row_size);
  }
  return true;
}

// Helper to load a 24-bit BMP image into an RGBA buffer.
bool load_bmp(const std::string& filepath, int& width, int& height,
              std::vector<unsigned char>& rgba) {
  std::ifstream f(filepath, std::ios::binary);
  if (!f) {
    std::cerr << "Failed to open BMP for reading: " << filepath << std::endl;
    return false;
  }

  unsigned char header[54];
  f.read((char*)header, 54);
  if (f.gcount() != 54 || header[0] != 'B' || header[1] != 'M') {
    std::cerr << "Invalid BMP header or magic: " << filepath << std::endl;
    return false;
  }

  width =
      header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
  height =
      header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
  int bits_per_pixel = header[28] | (header[29] << 8);

  if (bits_per_pixel != 24) {
    std::cerr << "Only 24-bit BMP is supported, got " << bits_per_pixel
              << " in " << filepath << std::endl;
    return false;
  }

  int row_size = (width * 3 + 3) & ~3;
  rgba.resize(width * height * 4);

  std::vector<unsigned char> row(row_size);
  for (int y = 0; y < height; ++y) {
    f.read((char*)row.data(), row_size);
    if (f.gcount() != row_size) {
      std::cerr << "Failed to read BMP row " << y << " from " << filepath
                << std::endl;
      return false;
    }
    for (int x = 0; x < width; ++x) {
      int r_idx = (y * width + x) * 4;
      rgba[r_idx + 0] = row[x * 3 + 2];  // R
      rgba[r_idx + 1] = row[x * 3 + 1];  // G
      rgba[r_idx + 2] = row[x * 3 + 0];  // B
      rgba[r_idx + 3] = 255;             // A
    }
  }
  return true;
}

// Compares two RGBA buffers with a small pixel difference tolerance threshold.
float compare_images(const unsigned char* img1, const unsigned char* img2,
                     int width, int height) {
  int diff_count = 0;
  int total_pixels = width * height;
  for (int i = 0; i < total_pixels; ++i) {
    int idx = i * 4;
    int dr = std::abs((int)img1[idx + 0] - (int)img2[idx + 0]);
    int dg = std::abs((int)img1[idx + 1] - (int)img2[idx + 1]);
    int db = std::abs((int)img1[idx + 2] - (int)img2[idx + 2]);
    if (dr > 2 || dg > 2 || db > 2) {
      diff_count++;
    }
  }
  return (float)diff_count / (float)total_pixels * 100.0f;
}

class ztracing_test : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_EQ(ztracing_init(""), 0); }

  void TearDown() override {
    ztracing_deinit();
    EXPECT_EQ(ztracing_get_allocated_bytes(), 0u)
        << "Memory leak detected on teardown!";
  }

  // --- REUSABLE TEST HELPERS ---

  // Returns the app state pointer natively for assertions.
  app_t* get_app() {
    app_t* app = ztracing_headless_get_app();
    EXPECT_NE(app, nullptr);
    return app;
  }

  // Loads a mock Chrome Trace JSON asynchronously and waits for
  // parsing/indexing to complete.
  void load_trace(const char* json_content,
                  const char* filename = "test_trace.json") {
    size_t size = strlen(json_content);
    ztracing_begin_session(1, filename, (double)size);

    char* buf = (char*)ztracing_malloc((int)size + 1);
    memcpy(buf, json_content, size + 1);

    ztracing_handle_file_chunk(1, buf, (int)size, (double)size,
                               true);  // takes ownership

    double start = platform_get_now();
    while (ztracing_is_loading_active()) {
      ztracing_update();
      usleep(1000);
      if (platform_get_now() - start > 5000.0) {
        FAIL() << "Timeout waiting for trace to load";
        break;
      }
    }
    // Render a few frames to let ImGui dock layout settle with the new timeline
    ztracing_update();  // Frame 1: Init dock layout
    ztracing_update();  // Frame 2: Windows snap to dock nodes
    ztracing_update();  // Frame 3: Stable render
  }

  // Captures the current offscreen frame and asserts that it matches the golden
  // image. If it fails, it saves the failed frame and prints the exact copy
  // command.
  void assert_golden(const std::string& golden_name, float tolerance = 0.2f) {
    int width = 800;
    int height = 600;
    std::vector<unsigned char> captured_pixels(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                 captured_pixels.data());

    std::string golden_path = "src/testdata/" + golden_name + ".bmp";

    const char* outputs_dir = getenv("TEST_UNDECLARED_OUTPUTS_DIR");
    std::string failed_path = "/tmp/" + golden_name + "_failed.bmp";
    bool is_bazel = (outputs_dir != nullptr);
    if (is_bazel) {
      failed_path =
          std::string(outputs_dir) + "/" + golden_name + "_failed.bmp";
    }

    int golden_w = 0, golden_h = 0;
    std::vector<unsigned char> golden_pixels;
    bool golden_loaded =
        load_bmp(golden_path, golden_w, golden_h, golden_pixels);

    if (!golden_loaded) {
      ASSERT_TRUE(save_bmp(failed_path, width, height, captured_pixels.data()));
      std::string instructions =
          is_bazel ? "cp bazel-testlogs/src/ztracing_test/test.outputs/" +
                         golden_name + "_failed.bmp " + golden_path
                   : "cp " + failed_path + " " + golden_path;

      FAIL() << "Golden image '" << golden_path << "' could not be loaded. "
             << "Saved captured frame to '" << failed_path << "'.\n"
             << "If the captured image is correct, establish the golden "
                "reference using:\n"
             << "  " << instructions;
    }

    ASSERT_EQ(golden_w, width) << "Golden width mismatch";
    ASSERT_EQ(golden_h, height) << "Golden height mismatch";

    float diff_percent = compare_images(captured_pixels.data(),
                                        golden_pixels.data(), width, height);
    if (diff_percent > tolerance) {
      save_bmp(failed_path, width, height, captured_pixels.data());
      std::string instructions =
          is_bazel ? "cp bazel-testlogs/src/ztracing_test/test.outputs/" +
                         golden_name + "_failed.bmp " + golden_path
                   : "cp " + failed_path + " " + golden_path;

      FAIL()
          << "Visual mismatch detected for '" << golden_name
          << "'! Difference: " << diff_percent << "% (limit: " << tolerance
          << "%).\n"
          << "Saved failed frame to '" << failed_path << "'.\n"
          << "If the new image is correct, update the golden reference using:\n"
          << "  " << instructions;
    }
  }

  // --- REUSABLE IMGUI IO SIMULATION HELPERS ---

  // Simulates a single mouse click at screen coordinates (x, y).
  void simulate_click(float x, float y) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    ztracing_update();                // Let hover state settle!
    io.AddMouseButtonEvent(0, true);  // Left Mouse Button Down
    ztracing_update();
    io.AddMouseButtonEvent(0, false);  // Left Mouse Button Up
    ztracing_update();
  }

  // Simulates a double-click on the mouse at the specified screen coordinates.
  void simulate_double_click(float x, float y) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    ztracing_update();  // Let hover state settle!
    // First click
    io.AddMouseButtonEvent(0, true);
    ztracing_update();
    io.AddMouseButtonEvent(0, false);
    ztracing_update();
    // Second click
    io.AddMouseButtonEvent(0, true);
    ztracing_update();
    io.AddMouseButtonEvent(0, false);
    ztracing_update();
  }

  // Simulates a keyboard shortcut, optionally with a modifier (Ctrl, Shift,
  // etc.).
  void simulate_key_shortcut(ImGuiKey key, ImGuiKey modifier = ImGuiKey_None) {
    ImGuiIO& io = ImGui::GetIO();
    if (modifier != ImGuiKey_None) {
      io.AddKeyEvent(modifier, true);
    }
    io.AddKeyEvent(key, true);
    ztracing_update();
    io.AddKeyEvent(key, false);
    if (modifier != ImGuiKey_None) {
      io.AddKeyEvent(modifier, false);
    }
    ztracing_update();
  }

  // Simulates typing text into the currently focused ImGui input box.
  void simulate_text_input(const char* text) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8(text);
    ztracing_update();
  }

  // Simulates zooming in/out by scrolling the mouse wheel while holding Ctrl.
  void simulate_zoom(float x, float y, float wheel_delta) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddMouseWheelEvent(0.0f, wheel_delta);
    ztracing_update();
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddMouseWheelEvent(0.0f, 0.0f);
    ztracing_update();
  }

  // Simulates dragging the mouse (e.g., for panning or timeline selection).
  void simulate_drag(float start_x, float start_y, float end_x, float end_y) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(start_x, start_y);
    ztracing_update();  // Let hover state settle!
    io.AddMouseButtonEvent(0, true);
    ztracing_update();
    io.AddMousePosEvent(end_x, end_y);
    ztracing_update();
    ztracing_update();
    io.AddMouseButtonEvent(0, false);
    ztracing_update();
  }

  // Simulates dragging the mouse while holding down a keyboard modifier (e.g.
  // Shift for box selection).
  void simulate_drag_with_modifier(float start_x, float start_y, float end_x,
                                   float end_y, ImGuiKey modifier) {
    ImGuiIO& io = ImGui::GetIO();
    if (modifier != ImGuiKey_None) {
      io.AddKeyEvent(modifier, true);
    }
    io.AddMousePosEvent(start_x, start_y);
    ztracing_update();  // Let hover state settle!
    io.AddMouseButtonEvent(0, true);
    ztracing_update();
    io.AddMousePosEvent(end_x, end_y);
    ztracing_update();
    ztracing_update();
    io.AddMouseButtonEvent(0, false);
    if (modifier != ImGuiKey_None) {
      io.AddKeyEvent(modifier, false);
    }
    ztracing_update();
  }
};

// --- MOCK TRACES ---

const char* MOCK_STANDARD_TRACE =
    "["
    "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":"
    "\"TestProcess\"}},"
    "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":1,\"args\":{"
    "\"name\":\"MainThread\"}},"
    "{\"name\":\"EventA\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,\"tid\":1,"
    "\"ts\":1000},"
    "{\"name\":\"EventSub\",\"cat\":\"render\",\"ph\":\"B\",\"pid\":1,\"tid\":"
    "1,\"ts\":1200},"
    "{\"name\":\"EventSub\",\"cat\":\"render\",\"ph\":\"E\",\"pid\":1,\"tid\":"
    "1,\"ts\":1600},"
    "{\"name\":\"EventA\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,\"tid\":1,"
    "\"ts\":2500},"
    "{\"name\":\"EventB\",\"cat\":\"network\",\"ph\":\"B\",\"pid\":1,\"tid\":1,"
    "\"ts\":3000},"
    "{\"name\":\"EventB\",\"cat\":\"network\",\"ph\":\"E\",\"pid\":1,\"tid\":1,"
    "\"ts\":4500}"
    "]";

const char* MOCK_COUNTER_TRACE =
    "["
    "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":"
    "\"TestProcess\"}},"
    "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":1,\"args\":{"
    "\"name\":\"MainThread\"}},"
    "{\"name\":\"MyCounter\",\"ph\":\"C\",\"pid\":1,\"tid\":1,\"ts\":1000,"
    "\"args\":{\"Series1\":10,\"Series2\":20}},"
    "{\"name\":\"MyCounter\",\"ph\":\"C\",\"pid\":1,\"tid\":1,\"ts\":2000,"
    "\"args\":{\"Series1\":30,\"Series2\":15}},"
    "{\"name\":\"MyCounter\",\"ph\":\"C\",\"pid\":1,\"tid\":1,\"ts\":3000,"
    "\"args\":{\"Series1\":15,\"Series2\":40}},"
    "{\"name\":\"MyCounter\",\"ph\":\"C\",\"pid\":1,\"tid\":1,\"ts\":4000,"
    "\"args\":{\"Series1\":50,\"Series2\":10}}"
    "]";

const char* MOCK_OVERLAPPING_TRACE =
    "["
    "{\"name\":\"Event1\",\"cat\":\"cat\",\"ph\":\"X\",\"pid\":1,\"tid\":1,"
    "\"ts\":1000,\"dur\":1000},"
    "{\"name\":\"Event2\",\"cat\":\"cat\",\"ph\":\"X\",\"pid\":1,\"tid\":1,"
    "\"ts\":1500,\"dur\":1000},"
    "{\"name\":\"Event3\",\"cat\":\"cat\",\"ph\":\"X\",\"pid\":1,\"tid\":1,"
    "\"ts\":1800,\"dur\":500}"
    "]";

// --- TEST CASES ---

// 1. Welcome Screen Golden
TEST_F(ztracing_test, welcome_screen_golden) {
  ztracing_update();  // Frame 2: Windows snap to dock nodes
  ztracing_update();  // Frame 3: Stable render

  assert_golden("welcome_screen_golden");
}

// 2. Loading Screen Golden
TEST_F(ztracing_test, loading_screen_golden) {
  const double total_size = 10000.0;
  const double progress_50_percent = total_size * 0.5;

  // Begin a session with a known total size
  ztracing_begin_session(1, "large_trace.json", total_size);

  // Feed a small partial chunk without EOF so the loading state remains active
  const char* partial_chunk = "[{\"name\":\"Event\",";
  int chunk_size = (int)strlen(partial_chunk);
  char* buf = (char*)ztracing_malloc(chunk_size);
  memcpy(buf, partial_chunk, chunk_size);

  // Feed the chunk and report exactly 50% progress
  ztracing_handle_file_chunk(1, buf, chunk_size, progress_50_percent,
                             false);  // is_eof = false

  // Render a few frames to let ImGui dock layout settle
  ztracing_update();  // Frame 1: Init dock layout
  ztracing_update();  // Frame 2: Windows snap to dock nodes
  ztracing_update();  // Frame 3: Stable render

  assert_golden("loading_screen_golden");
}

// 3. Main Timeline Golden
TEST_F(ztracing_test, main_timeline_golden) {
  load_trace(MOCK_STANDARD_TRACE);
  assert_golden("main_timeline_golden");
}

// 4. Timeline Navigation Golden (Zoom & Pan)
TEST_F(ztracing_test, timeline_navigation_golden) {
  // Programmatically generate a large horizontal trace (100 events over
  // 100,000us) to allow substantial zooming and panning.
  std::string json = "[";
  json +=
      "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":"
      "\"TestProcess\"}},";
  json +=
      "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":1,\"args\":{"
      "\"name\":\"MainThread\"}}";
  for (int i = 0; i < 100; ++i) {
    json += ",{\"name\":\"Event_" + std::to_string(i) +
            "\",\"cat\":\"test\",\"ph\":\"X\",\"pid\":1,\"tid\":1,\"ts\":" +
            std::to_string(i * 1000) + ",\"dur\":500}";
  }
  json += "]";

  load_trace(json.c_str());

  // 1. Initial State (fully zoomed out, panning disabled)
  ztracing_update();
  assert_golden("timeline_navigation_initial");

  // 2. Zoomed State (zoomed in on the center, panning now enabled)
  // Hold Ctrl down for two frames to overlap with the delayed mouse wheel event
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(400.0f, 300.0f);
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  io.AddMouseWheelEvent(0.0f, 1.0f);

  ztracing_update();  // Frame 1: ctrl=1, wheel=0 (wheel event delayed)

  // Keep Ctrl down, but set wheel to 0 (we already injected the scroll)
  io.AddMouseWheelEvent(0.0f, 0.0f);
  ztracing_update();  // Frame 2: ctrl=1, wheel=1 -> ZOOM should occur here!

  // Release Ctrl in Frame 3
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  ztracing_update();  // Frame 3: settle

  assert_golden("timeline_navigation_zoomed");

  // 3. Panned State (dragged timeline left by moving mouse right (+100px))
  simulate_drag(400.0f, 300.0f, 500.0f, 300.0f);

  assert_golden("timeline_navigation_panned");
}

// 5. Event Selection & Details Panel Golden
TEST_F(ztracing_test, event_selection_golden) {
  load_trace(MOCK_STANDARD_TRACE);

  // EventA spans x: 61 to 324, y: 95 to 113. Click at (250, 104) to select.
  simulate_click(250.0f, 104.0f);

  // Step 3 frames to let the Details Panel docking layout settle completely
  ztracing_update();
  ztracing_update();
  ztracing_update();

  assert_golden("event_selection_golden");
}

// 6. Search Highlights Golden
TEST_F(ztracing_test, search_highlights_golden) {
  // Programmatically generate a comprehensive multi-track trace with 20 tracks
  // to exceed the viewport height (approx 565px), forcing vertical scrolling.
  std::string json = "[";
  json +=
      "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":"
      "\"TestProcess\"}}";

  // Generate 20 threads
  for (int tid = 1; tid <= 20; ++tid) {
    json += ",{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":" +
            std::to_string(tid) + ",\"args\":{\"name\":\"Thread_" +
            std::to_string(tid) + "\"}}";

    // Add standard non-matching events to all threads to populate the timeline
    json +=
        ",{\"name\":\"OtherEvent\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,"
        "\"tid\":" +
        std::to_string(tid) + ",\"ts\":1200}";
    json +=
        ",{\"name\":\"OtherEvent\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,"
        "\"tid\":" +
        std::to_string(tid) + ",\"ts\":2200}";
  }

  // Inject our specific SearchTarget events at strategic locations:
  // 1. Thread 1 (On-screen track): On-screen event, duration 1000us
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,"
      "\"tid\":1,\"ts\":1000}";
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,"
      "\"tid\":1,\"ts\":2000}";

  // 2. Thread 2 (On-screen track): On-screen event, different duration (1500us)
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,"
      "\"tid\":2,\"ts\":3000}";
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,"
      "\"tid\":2,\"ts\":4500}";

  // 3. Thread 3 (On-screen track): Horizontally OFF-SCREEN event (ts 10000)
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,"
      "\"tid\":3,\"ts\":10000}";
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,"
      "\"tid\":3,\"ts\":11000}";

  // 4. Thread 18 (Vertically OFF-SCREEN track): On-screen event (ts 2000),
  // duration 1200us
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,"
      "\"tid\":18,\"ts\":2000}";
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,"
      "\"tid\":18,\"ts\":3200}";

  // 5. Thread 19 (Vertically OFF-SCREEN track): Horizontally OFF-SCREEN event
  // (ts 10000)
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,"
      "\"tid\":19,\"ts\":10000}";
  json +=
      ",{\"name\":\"SearchTarget\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,"
      "\"tid\":19,\"ts\":11000}";

  json += "]";

  load_trace(json.c_str());

  // 1. Zoom in on the left side to push the ts:10000 events horizontally
  // off-screen. Thread 18 & 19 remain vertically off-screen at the bottom.
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(250.0f, 300.0f);  // Hover over the left side of tracks
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  io.AddMouseWheelEvent(0.0f, 2.0f);  // Scroll wheel up to zoom in

  ztracing_update();  // Frame 1: ctrl=1, wheel=0 (delayed)

  io.AddMouseWheelEvent(0.0f, 0.0f);
  ztracing_update();  // Frame 2: ctrl=1, wheel=2 -> ZOOM occurs here

  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  ztracing_update();  // Frame 3: settle

  // 2. First Ctrl+F to open the Details panel
  simulate_key_shortcut(ImGuiKey_F, ImGuiMod_Ctrl);

  // Step 3 frames to let the Details window docking layout settle
  ztracing_update();
  ztracing_update();
  ztracing_update();

  // 3. Second Ctrl+F to focus the search input now that the layout is settled
  simulate_key_shortcut(ImGuiKey_F, ImGuiMod_Ctrl);

  // 4. Type search query "SearchTarget"
  simulate_text_input("SearchTarget");

  app_t* app = get_app();
  ASSERT_STREQ((const char*)app->trace_viewer.search_query.ptr, "SearchTarget")
      << "Search query was not typed into the input box!";

  // 5. Wait for background search job to complete
  double start = platform_get_now();
  while (app->trace_viewer.search.is_searching) {
    ztracing_update();
    usleep(1000);
    if (platform_get_now() - start > 5000.0) {
      FAIL() << "Timeout waiting for search to complete";
    }
  }

  assert_golden("search_highlights_golden");
}

// 7. Counter Tracks Golden
TEST_F(ztracing_test, counter_tracks_golden) {
  load_trace(MOCK_COUNTER_TRACE);

  // Render a stable frame
  ztracing_update();

  assert_golden("counter_tracks_golden");
}

// 8. Overlapping Events (Multi-Lane) Golden
TEST_F(ztracing_test, multi_lane_golden) {
  load_trace(MOCK_OVERLAPPING_TRACE);

  // Render a stable frame
  ztracing_update();

  assert_golden("multi_lane_golden");
}

// 9. Shortcuts Cheatsheet Modal Golden
TEST_F(ztracing_test, shortcuts_cheatsheet_golden) {
  load_trace(MOCK_STANDARD_TRACE);

  // Press '?' (Shift + /) to toggle the cheatsheet modal
  simulate_key_shortcut(ImGuiKey_Slash, ImGuiMod_Shift);

  assert_golden("shortcuts_cheatsheet_golden");
}

// 10. Vertical Minimap Heatmap Golden
TEST_F(ztracing_test, vertical_minimap_golden) {
  // Programmatically generate a massive multi-thread trace (40 threads) to
  // force vertical scrollbar/minimap
  std::string json = "[";
  for (int i = 1; i <= 40; ++i) {
    if (i > 1) json += ",";
    json += "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":" +
            std::to_string(i) + ",\"args\":{\"name\":\"Thread_" +
            std::to_string(i) + "\"}},";
    json += "{\"name\":\"Event_" + std::to_string(i) +
            "\",\"cat\":\"test\",\"ph\":\"X\",\"pid\":1,\"tid\":" +
            std::to_string(i) + ",\"ts\":1000,\"dur\":2000}";
  }
  json += "]";

  load_trace(json.c_str());

  // Render a stable frame. The minimap scrollbar should render on the right.
  ztracing_update();

  assert_golden("vertical_minimap_golden");
}

// 11. Timeline Range Selection Golden (Ruler Drag)
TEST_F(ztracing_test, timeline_selection_golden) {
  load_trace(MOCK_STANDARD_TRACE);

  // Drag on the timeline ruler (Y=28px is inside the Y:19-38px ruler range)
  // to select a range from X=250px to X=450px
  simulate_drag(250.0f, 28.0f, 450.0f, 28.0f);

  // Step a frame to settle the selection overlay rendering
  ztracing_update();

  assert_golden("timeline_selection_golden");
}

// 12. Light Theme Timeline Golden (Theme Switching)
TEST_F(ztracing_test, light_theme_timeline_golden) {
  load_trace(MOCK_STANDARD_TRACE);

  // Switch theme mode to Light using the public API
  ztracing_on_theme_changed(false);

  // Step a frame to propagate color changes across all tracks and events
  ztracing_update();

  assert_golden("light_theme_timeline_golden");

  // Restore theme mode to Dark to avoid polluting subsequent tests
  ztracing_on_theme_changed(true);
  ztracing_update();
}

// 13. Timeline Double-Click to Zoom to Event Golden (Initial and Zoomed)
TEST_F(ztracing_test, timeline_double_click_zoom_golden) {
  // Programmatically generate a large-scale trace to make zooming dramatic.
  // Total span: 1000us to 50000us (49000us total).
  // Target EventA is in the middle (ts: 24000-26000), width ~30px on screen.
  std::string json = "[";
  json +=
      "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":"
      "\"TestProcess\"}},";
  json +=
      "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":1,\"args\":{"
      "\"name\":\"MainThread\"}},";
  json +=
      "{\"name\":\"EventStart\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,\"tid\":"
      "1,\"ts\":1000},";
  json +=
      "{\"name\":\"EventStart\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,\"tid\":"
      "1,\"ts\":2000},";
  json +=
      "{\"name\":\"EventA\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,\"tid\":1,"
      "\"ts\":24000},";
  json +=
      "{\"name\":\"EventA\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,\"tid\":1,"
      "\"ts\":26000},";
  json +=
      "{\"name\":\"EventEnd\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,\"tid\":1,"
      "\"ts\":49000},";
  json +=
      "{\"name\":\"EventEnd\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,\"tid\":1,"
      "\"ts\":50000}";
  json += "]";

  load_trace(json.c_str());

  // 1. Click on EventA to select it and open the Details Panel (at its initial
  // position X=368px)
  simulate_click(368.0f, 104.0f);

  // Settle the docking layout (Details Panel docks right, tracks width shrinks
  // to 494px stably)
  ztracing_update();
  ztracing_update();
  ztracing_update();

  // 2. Capture the initial state (Details open, EventA is at [234.40, 251.20])
  assert_golden("timeline_double_click_zoom_initial");

  // 3. Double-click on EventA (stably centered at X=242.8px, Y=104px)
  simulate_double_click(242.8f, 104.0f);

  // Step a frame to settle the zoomed viewport layout (EventA should now fill
  // ~90% of the screen)
  ztracing_update();

  assert_golden("timeline_double_click_zoom_zoomed");
}

// 14. Vertical Minimap Scroll-by-Click Golden (Initial and Scrolled)
TEST_F(ztracing_test, vertical_minimap_scroll_golden) {
  // Programmatically generate a massive multi-track trace with 300 tracks
  // to fully fill the vertical minimap scrollbar and make the slider very
  // small.
  std::string json = "[";
  json +=
      "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":"
      "\"TestProcess\"}}";
  for (int tid = 1; tid <= 300; ++tid) {
    json += ",{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":" +
            std::to_string(tid) + ",\"args\":{\"name\":\"Thread_" +
            std::to_string(tid) + "\"}}";
    json +=
        ",{\"name\":\"OtherEvent\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,"
        "\"tid\":" +
        std::to_string(tid) + ",\"ts\":1200}";
    json +=
        ",{\"name\":\"OtherEvent\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,"
        "\"tid\":" +
        std::to_string(tid) + ",\"ts\":2200}";
  }
  json += "]";

  load_trace(json.c_str());

  // 1. Render stable frame and capture the initial state (scrolled to top)
  ztracing_update();
  assert_golden("vertical_minimap_scroll_initial");

  // 2. Click on the lower part of the vertical minimap (X=768px, Y=400px)
  // to scroll the track list vertically.
  simulate_click(768.0f, 400.0f);

  // Step a frame to settle the scrolled tracks layout
  ztracing_update();

  // 3. Capture the scrolled state
  assert_golden("vertical_minimap_scroll_scrolled");
}

// 15. Timeline Selection Boundary Resize Golden (Initial and Resized)
TEST_F(ztracing_test, timeline_selection_resized_golden) {
  load_trace(MOCK_STANDARD_TRACE);

  // 1. First create an active selection from X=250px to X=450px
  simulate_drag(250.0f, 28.0f, 450.0f, 28.0f);
  ztracing_update();

  // Capture the initial selection state (before resize)
  assert_golden("timeline_selection_resized_initial");

  // 2. Hover near the left selection boundary (X=250px) and drag it left to
  // X=150px
  simulate_drag(250.0f, 28.0f, 150.0f, 28.0f);

  // Step a frame to settle the resized selection overlay rendering
  ztracing_update();

  // Capture the resized selection state (after resize)
  assert_golden("timeline_selection_resized_resized");
}

// 16. Spatial Box Selection Golden (Dragging and Selected)
TEST_F(ztracing_test, timeline_box_selection_golden) {
  load_trace(MOCK_STANDARD_TRACE);

  ImGuiIO& io = ImGui::GetIO();

  // --- PHASE 1: DRAGGING ---
  // Hold Shift and press mouse button down at (100, 80)
  io.AddKeyEvent(ImGuiMod_Shift, true);
  io.AddMousePosEvent(100.0f, 80.0f);
  io.AddMouseButtonEvent(0, true);
  ztracing_update();

  // Drag to (700, 130) while keeping mouse button down and Shift held
  io.AddMousePosEvent(700.0f, 130.0f);
  ztracing_update();
  ztracing_update();  // Settle box drawing

  // Capture the state WHILE DRAGGING (should show the selection box overlay)
  assert_golden("timeline_box_selection_dragging");

  // --- PHASE 2: SELECTED ---
  // Release mouse button and Shift to complete selection
  io.AddMouseButtonEvent(0, false);
  io.AddKeyEvent(ImGuiMod_Shift, false);

  // Step a frame to let spatial query complete and Details Panel open
  ztracing_update();

  // Capture the state AFTER RELEASE (should show the selected events and
  // Details Panel)
  assert_golden("timeline_box_selection_selected");
}

// 17. Thread Event Hover Tooltip Golden
TEST_F(ztracing_test, timeline_hover_tooltip_thread_golden) {
  load_trace(MOCK_STANDARD_TRACE);

  // Settle layout first
  ztracing_update();

  // Hover mouse over EventA on the MainThread (X=200px, Y=104px)
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(200.0f, 104.0f);
  ztracing_update();
  ztracing_update();
  ztracing_update();

  assert_golden("timeline_hover_tooltip_thread_golden");
}

// 18. Counter Track Hover Tooltip Golden
TEST_F(ztracing_test, timeline_hover_tooltip_counter_golden) {
  load_trace(MOCK_COUNTER_TRACE);

  // Settle layout first
  ztracing_update();

  // Hover mouse over the Counter track content area (X=360px, Y=76px)
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(360.0f, 76.0f);
  ztracing_update();
  ztracing_update();
  ztracing_update();

  assert_golden("timeline_hover_tooltip_counter_golden");
}

// 19. Details Selection Table Click-to-Focus Golden (Horizontal Scroll
// Auto-Focus)
TEST_F(ztracing_test, details_click_to_focus_golden) {
  // Build a custom 15-thread trace:
  // Thread 1: EventA (matched)
  // Thread 2-14: 2 Noise events each (not matched, spread across timeline)
  // Thread 15: EventB (matched)
  std::string json = "[";
  json +=
      "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":"
      "\"TestProcess\"}},";

  // Thread metadata
  json +=
      "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":1,\"args\":{"
      "\"name\":\"MainThread\"}},";
  for (int t = 1; t <= 13; t++) {
    json += "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":" +
            std::to_string(t + 1) + ",\"args\":{\"name\":\"NoiseThread" +
            std::to_string(t) + "\"}},";
  }
  json +=
      "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":15,\"args\":{"
      "\"name\":\"TargetThread\"}},";

  // EventA on MainThread (tid=1): ts=1000, dur=10000 (wide, easy to hit)
  json +=
      "{\"name\":\"EventA\",\"cat\":\"ui\",\"ph\":\"X\",\"pid\":1,\"tid\":1,"
      "\"ts\":1000,\"dur\":10000},";

  // Noise events on NoiseThread1 to NoiseThread13 (tid=2..14)
  for (int t = 1; t <= 13; t++) {
    int tid = t + 1;
    int ts1 = 20000 + t * 2000;
    int ts2 = 80000 + t * 2000;
    json +=
        "{\"name\":\"Noise\",\"cat\":\"noise\",\"ph\":\"X\",\"pid\":1,"
        "\"tid\":" +
        std::to_string(tid) + ",\"ts\":" + std::to_string(ts1) +
        ",\"dur\":1000},";
    json +=
        "{\"name\":\"Noise\",\"cat\":\"noise\",\"ph\":\"X\",\"pid\":1,"
        "\"tid\":" +
        std::to_string(tid) + ",\"ts\":" + std::to_string(ts2) +
        ",\"dur\":1000},";
  }

  // EventB on TargetThread (tid=15): ts=112000, dur=10000 (matched, far
  // off-screen)
  json +=
      "{\"name\":\"EventB\",\"cat\":\"ui\",\"ph\":\"X\",\"pid\":1,\"tid\":15,"
      "\"ts\":112000,\"dur\":10000}";
  json += "]";

  load_trace(json.c_str());

  // Settle layout first
  ztracing_update();
  ztracing_update();

  // 1. Perform 2D box selection to select all events on all 16 tracks
  // Track 0 starts at Y=38, Track 15 (TargetThread) ends at Y=646.
  // Drag diagonally from top-left of Track 0 to bottom-right of Track 15 (Y=640
  // is off-screen!).
  simulate_drag_with_modifier(5.0f, 45.0f, 730.0f, 640.0f, ImGuiMod_Shift);
  ztracing_update();
  ztracing_update();

  // 2. Double-click EventA on Track 1 (X=58px, Y=104.5px) to zoom in on it.
  // This pushes EventB (at ts=112000 on Track 15) way off-screen horizontally
  // and vertically!
  simulate_double_click(58.0f, 104.5f);

  // 3. Click the Details Panel search box (X=650px, Y=75px) and type "Event"
  // to filter out the 30 Noise events and show only EventA and EventB.
  simulate_click(650.0f, 75.0f);
  simulate_text_input("Event");

  // Wait for Details search filter job to complete
  app_t* app = get_app();
  double start = platform_get_now();
  while (app->trace_viewer.search.is_searching) {
    ztracing_update();
    usleep(1000);
    if (platform_get_now() - start > 5000.0) {
      break;
    }
  }
  ztracing_update();
  ztracing_update();
  ztracing_update();

  // Capture the BEFORE image (zoomed on EventA, EventB is off-screen, Details
  // table has 2 rows)
  assert_golden("details_click_to_focus_before_golden");

  printf("FOCUSED EVENT BEFORE CLICK: %ld\n",
         app->trace_viewer.has_focused_event
             ? (long)app->trace_viewer.focused_event_idx
             : -1);

  // 4. Click on Row 2 (EventB) in the Details table (X=650px, Y=298px)
  // This should trigger auto-zoom/scroll to center EventB in the timeline!
  simulate_click(650.0f, 298.0f);

  // Settle the zoom/scroll animation
  ztracing_update();
  ztracing_update();
  ztracing_update();

  printf("FOCUSED EVENT AFTER CLICK: %ld\n",
         app->trace_viewer.has_focused_event
             ? (long)app->trace_viewer.focused_event_idx
             : -1);

  // Capture the AFTER image (timeline centered on EventB, EventB is focused)
  assert_golden("details_click_to_focus_after_golden");
}

// 20. Search Filter Checkboxes Golden (Threads vs. Counters Highlight Culling)
TEST_F(ztracing_test, search_filter_counters_only_golden) {
  // Programmatically generate a trace containing:
  // 1. Thread Event named "EventA"
  // 2. Counter Event named "EventA"
  std::string json = "[";
  json +=
      "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":"
      "\"TestProcess\"}},";
  json +=
      "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":1,\"args\":{"
      "\"name\":\"MainThread\"}},";
  json +=
      "{\"name\":\"EventA\",\"cat\":\"ui\",\"ph\":\"B\",\"pid\":1,\"tid\":1,"
      "\"ts\":1000},";
  json +=
      "{\"name\":\"EventA\",\"cat\":\"ui\",\"ph\":\"E\",\"pid\":1,\"tid\":1,"
      "\"ts\":2000},";
  json +=
      "{\"name\":\"EventA\",\"ph\":\"C\",\"pid\":1,\"tid\":1,\"ts\":1200,"
      "\"args\":{\"val\":10}},";
  json +=
      "{\"name\":\"EventA\",\"ph\":\"C\",\"pid\":1,\"tid\":1,\"ts\":1500,"
      "\"args\":{\"val\":30}},";
  json +=
      "{\"name\":\"EventA\",\"ph\":\"C\",\"pid\":1,\"tid\":1,\"ts\":1800,"
      "\"args\":{\"val\":20}}";
  json += "]";

  load_trace(json.c_str());
  ztracing_update();

  // 0. First Ctrl+F to open the Details panel
  simulate_key_shortcut(ImGuiKey_F, ImGuiMod_Ctrl);

  // Step 3 frames to let the Details window docking layout settle
  ztracing_update();
  ztracing_update();
  ztracing_update();

  // 1. Second Ctrl+F to focus the search input now that the layout is settled,
  // then type "EventA"
  simulate_key_shortcut(ImGuiKey_F, ImGuiMod_Ctrl);
  simulate_text_input("EventA");

  // Wait for search to complete
  app_t* app = get_app();
  double start = platform_get_now();
  while (app->trace_viewer.search.is_searching) {
    ztracing_update();
    usleep(1000);
    if (platform_get_now() - start > 5000.0) {
      break;
    }
  }
  // Step 3 frames to make sure search results are integrated and selection
  // bitset is updated
  ztracing_update();
  ztracing_update();
  ztracing_update();
  assert_golden("search_filter_counters_only_before_golden");

  // 2. Click the "Threads" checkbox to exclude thread events.
  // Click target is X=580.0f, Y=96.0f (inside the checkbox box)
  simulate_click(580.0f, 96.0f);

  // Wait for the new search job (triggered by filter change) to complete
  start = platform_get_now();
  while (app->trace_viewer.search.is_searching) {
    ztracing_update();
    usleep(1000);
    if (platform_get_now() - start > 5000.0) {
      break;
    }
  }

  // Step 3 frames to make sure search results are integrated and selection
  // bitset is updated
  ztracing_update();
  ztracing_update();
  ztracing_update();

  assert_golden("search_filter_counters_only_after_golden");
}

// 21. Shortcuts Cheatsheet Background Dismissal Golden
TEST_F(ztracing_test, shortcuts_cheatsheet_dismissal_golden) {
  load_trace(MOCK_STANDARD_TRACE);

  // 1. Press '?' (Shift + /) to toggle the cheatsheet modal
  simulate_key_shortcut(ImGuiKey_Slash, ImGuiMod_Shift);
  ztracing_update();

  // 2. Click outside the modal in the dark background area (X=50px, Y=300px)
  simulate_click(50.0f, 300.0f);
  ztracing_update();

  assert_golden("shortcuts_cheatsheet_dismissed_golden");
}

}  // namespace
