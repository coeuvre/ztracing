# ztracing: Chrome Tracing Replacement

## Project Mandates

- **Language**: C-style C++. Avoid complex C++ abstractions.
- **Style**: Google C++ Style.
- **UI Framework**: Dear ImGui (v1.92.7-docking).
- **Backend**: Custom WebGL 2.0 (Rendering) and Custom Emscripten HTML5 (Platform).
- **Build System**: Bazel with Bzlmod.
- **Target**: WASM/Browser via Emscripten.

## Development Workflow

- **Build**: `bazel build //:ztracing`
- **Output**: `bazel-bin/ztracing/` contains `index.html`, `ztracing.js`, and `ztracing.wasm`.
- **Run**:
  1. `cd bazel-bin/ztracing/`
  2. `python3 -m http.server 8000`
  3. Open `http://localhost:8000/index.html` in a browser.

## Architecture

- `src/imgui_impl_webgl`: Handles WebGL 2.0 (GLES 3.0) rendering logic.
- `src/imgui_impl_wasm`: Handles browser event loops and input mapping via `emscripten/html5.h`.
- `src/main_wasm.cc`: Orchestrates the initialization and frame loop.
- `src/logging`: Simple logging utility with WASM console integration.

## Logging

- **Macros**: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`.
- **Casing**: All log messages should be in **lowercase**.
- **Log Levels**: `DEBUG` (0), `INFO` (1), `WARN` (2), `ERROR` (3).
- **Defaults**:
    - Release builds (`-c opt`): `INFO` (hides `LOG_DEBUG`).
    - Debug builds: `DEBUG` (shows all).
- **Override**: Use `--copt="-DLOG_LEVEL=<LEVEL>"` (e.g., `--copt="-DLOG_LEVEL=WARN"`) to set the minimum log level at compile-time.
