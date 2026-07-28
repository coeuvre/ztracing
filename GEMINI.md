# ztracing: Chrome Tracing Replacement

## Project Mandates

- **Language Standards**: Rust 2024 edition for all core modules, domain models, trace parsing, UI state, rendering math, CLI, and test runners. C++20 strictly limited to Dear ImGui and WebGL platform bridge files.
- **UI Framework**: Dear ImGui (v1.92.7-docking) via safe Rust wrapper.
- **Backend**: Custom WebGL 2.0 (Rendering) and Emscripten HTML5 (Platform).
- **Build System**: Bazel with Bzlmod and `rules_rust`.
- **Targets**: Native CLI (`//src:ztracing`) and WASM/Browser (`//src:ztracing_wasm` / `//:ztracing`).

## Development Workflow

- **Build Native**: `bazel build //src:ztracing`
- **Build WASM**: `bazel build //src:ztracing_wasm //:ztracing`
- **Test All**: `bazel test //...`
- **Format Code**: `./format.sh`
- **Run Web App**:
  1. `bazel build //:ztracing`
  2. `./tools/serve.py`
  3. Open `http://localhost:8000/index.html` in browser.

## Core Rules & Constraints

1. **C/C++ Allowlist**: C++ is strictly restricted to Dear ImGui and WebGL platform bridge files in `src/` (`imgui_c.cc/h`, `imgui_impl_wasm.cc/h`, `imgui_impl_webgl.cc/h`). Do NOT introduce C/C++ code elsewhere.
2. **VCS Commands**: Use Jujutsu (`jj`) with `--no-pager` for version control operations.
3. **Code Formatting**: Run `./format.sh` after editing any source files.

## CLI Tool (`ztracing`)

Native CLI trace analyzer supporting terminal-width aware formatted tables:
- `summary`: Trace metadata and track listings.
- `inspect`: Event inspection with parent/child containment hierarchy.
- `concurrency`: Active thread concurrency computation over time buckets.
- `aggregate`: Event duration aggregation grouped by name or category.
- `diff`: Side-by-side comparative trace alignment and delta analysis.
- `query`: Chronological search with track, time, depth, and regex filters.
- `histogram`: Duration distribution histograms with visual ASCII bar charts.
