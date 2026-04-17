# ztracing: Chrome Tracing Replacement

## Project Mandates

- **Language**: C-style C++. Avoid complex C++ abstractions.
- **C++ Standard**: C++20 (required for `__VA_OPT__` and other features).
- **Style**: Google C++ Style.
- **Include Guards**: Must follow the pattern `ZTRACING_SRC_<FILE>_H_`.
- **Warnings**: Strict warnings are enabled for all local code (`-Wall -Wextra -Werror` etc.) via macros in `src/defs.bzl`.
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

- `src/allocator`: Custom C-style allocator with `Alloc`, `Realloc`, and `Free` helpers.
- `src/imgui_impl_webgl`: Handles WebGL 2.0 (GLES 3.0) rendering logic.
- `src/imgui_impl_wasm`: Handles browser event loops and input mapping via `emscripten/html5.h`.
- `src/main_wasm.cc`: Orchestrates the initialization and frame loop.
- `src/logging`: Simple logging utility with WASM console integration.

## Memory Management

- **Allocator**: All backends must accept an `Allocator` struct during initialization.
- **Default**: Use `DefaultAllocator()` for standard `malloc`/`free` behavior.
- **Signature**: `void* (*AllocFn)(void* ctx, void* ptr, size_t old_size, size_t new_size)`.

## Power-Save Mode

- **Description**: Redraws the UI only when events (input, resize, font updates) occur.
- **Implementation**:
    - `ImGui_ImplWasm_RequestUpdate()`: Triggers 5 frames of rendering.
    - `ImGui_ImplWasm_NeedUpdate()`: Returns true if frames are pending.
- **Startup**: Renders first 20 frames to ensure layout stability.
- **Toggle**: Controlled via `g_power_save_mode` in `main_wasm.cc` (enabled by default).

## Logging

- **Macros**: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`.
- **Casing**: All log messages should be in **lowercase**.
- **Log Levels**: `DEBUG` (0), `INFO` (1), `WARN` (2), `ERROR` (3).
- **Defaults**:
    - Release builds (`-c opt`): `INFO` (hides `LOG_DEBUG`).
    - Debug builds: `DEBUG` (shows all).
- **Override**: Use `--copt="-DLOG_LEVEL=<LEVEL>"` (e.g., `--copt="-DLOG_LEVEL=WARN"`) to set the minimum log level at compile-time.
