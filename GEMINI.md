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
