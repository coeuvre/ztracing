# ztracing: Chrome Tracing Replacement

## Project Mandates

- **Language**: C-style C++. Avoid complex C++ abstractions.
- **C++ Standard**: C++20 (required for `__VA_OPT__` and other features).
- **Style**: Google C++ Style (modified: snake_case for all functions, SCREAMING_CASE for constants).
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

- `src/allocator`: Custom C-style allocator with `allocator_alloc`, `allocator_realloc`, and `allocator_free` helpers.
- `src/str`: Basic `Str` struct (buffer and length) for string views.
- `src/array_list`: Generic `ArrayList<T>` (vector) with explicit allocation and ZII.
- `src/trace_parser`: C-style streaming parser for the Chrome Trace Event Format. Parses names, categories, phases, timestamps, durations, and arguments.
- `src/trace_data`: Persistent storage for parsed events. Uses a string pool with offset-based addressing to prevent pointer invalidation during reallocations.
- `src/imgui_impl_webgl`: Handles WebGL 2.0 (GLES 3.0) rendering logic. Supports 32-bit indices for large traces.
- `src/imgui_impl_wasm`: Handles browser event loops and input mapping via `emscripten/html5.h`.
- `src/ztracing.js`: JavaScript side of the WASM/Web interop for file streaming and drag-and-drop.
- `src/app`: Pure application logic and UI logic (ImGui). Manages viewport state, track organization, and event selection.
- `src/ztracing_wasm.cc`: WASM-specific entry points, explicit lifecycle control, and platform orchestration.
- `src/ztracing.h`: Clean C API for the WASM-to-JS bridge.
- `src/platform`: Platform abstraction layer (e.g., high-resolution timestamps).
- `src/logging`: Simple logging utility with WASM console integration.

## Trace Viewport

- **Layout**: The "Trace Viewport" is the primary, full-screen background window. Other panels ("Status", "Details") are docked at the bottom by default using a custom dockspace.
- **Navigation**: Supports mouse-drag for panning and mouse-wheel for zooming.
- **Rendering Optimization**: Events are grouped into tracks by TID. Each frame, binary search is used to identify and render only the visible events in the current viewport.
- **32-bit Indices**: ImGui is patched via `MODULE.bazel` to use `unsigned int` for `ImDrawIdx`, allowing for more than 65,535 vertices (required for large traces).

## Trace Parser Integration

- **Streaming**: Trace files are read in chunks using the browser's `ReadableStream` API.
- **Decompression**: `ztracing.js` handles Gzip decompression on-the-fly using the `DecompressionStream` API if the `Content-Type` is `application/gzip` or `application/x-gzip`.
- **WASM Bridge**: `ztracing.js` handles both drag-and-drop and direct stream loading (via `ztracing_start` options).
- **Direct Loading**: `shell.html` parses the `trace` URL parameter and fetches the trace file automatically if present.
- **Responsiveness**: Parsing yields to the browser's event loop every 100ms during loading (via `setTimeout(0)` in JS). This prevents the microtask-based `ReadableStream` loop from starving the main thread, ensuring `requestAnimationFrame` can fire and keep the UI responsive.
- **Progress Feedback**: `app.cc` displays live parsing statistics (event count and MB loaded) while `trace_parser_active` is true.
- **WASM Exports**: `ztracing_malloc`, `ztracing_free`, `ztracing_begin_session`, and `ztracing_handle_file_chunk` are used for memory and data transfer.

## Memory Management

- **Allocator**: All backends must accept an `Allocator` struct during initialization.
- **Default**: Use `allocator_get_default()` for standard `malloc`/`free` behavior.
- **Signature**: `void* (*AllocFn)(void* ctx, void* ptr, size_t old_size, size_t new_size)`.

## Power-Save Mode

- **Description**: Redraws the UI only when events (input, resize, font updates) occur.
- **Implementation**:
    - `imgui_impl_wasm_request_update()`: Triggers 5 frames of rendering.
    - `imgui_impl_wasm_need_update()`: Returns true if frames are pending.
- **Startup**: Renders first 20 frames to ensure layout stability.
- **Toggle**: Controlled via `power_save_mode` in the `App` struct (enabled by default).

## Version Control

- **System**: Jujutsu (`jj`).
- **Flags**: Use `--no-pager` for all `jj` commands to avoid blocking on stdin.

## Logging

- **Macros**: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`.
- **Casing**: All log messages should be in **lowercase**.
- **Log Levels**: `LOG_LEVEL_DEBUG` (0), `LOG_LEVEL_INFO` (1), `LOG_LEVEL_WARN` (2), `LOG_LEVEL_ERROR` (3).
- **Defaults**:
    - Release builds (`-c opt`): `LOG_LEVEL_INFO` (hides `LOG_DEBUG`).
    - Debug builds: `LOG_LEVEL_DEBUG` (shows all).
- **Override**: Use `--copt="-DLOG_LEVEL=<LEVEL>"` (e.g., `--copt="-DLOG_LEVEL=LOG_LEVEL_WARN"`) to set the minimum log level at compile-time.
