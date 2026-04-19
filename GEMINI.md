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
- **Includes**: All internal headers must be included using their full project path (e.g., `#include "src/app.h"`).

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
- `src/hash_table`: Generic `HashTable<K, V>` with open addressing and linear probing. Supports stateful functors for hashing and equality, enabling complex key types (like string pool indices).
- `src/trace_parser`: C-style streaming parser for the Chrome Trace Event Format. Parses names, categories, phases, timestamps, durations, and arguments.
- `src/trace_data`: Persistent storage for parsed events. 
    - **String Table**: Uses a de-duplicated String Table with global hashing to minimize memory usage for repetitive trace data (e.g., event names, categories).
    - **StringRef**: Events and arguments store `StringRef` (indices) into the table rather than raw offsets, providing $O(1)$ access to both string data and length without `strlen` overhead.
- `src/imgui_impl_webgl`: Handles WebGL 2.0 (GLES 3.0) rendering logic. Supports 32-bit indices for large traces.
- `src/imgui_impl_wasm`: Handles browser event loops and input mapping via `emscripten/html5.h`.
- `src/ztracing.js`: JavaScript side of the WASM/Web interop for file streaming and drag-and-drop.
- `src/app`: Pure application logic and UI logic (ImGui). Manages viewport state, event selection, and theme orchestration.
- `src/colors`: Theme management system. Defines a `Theme` struct and provides standard Dark and Light theme implementations.
- `src/track`: Logic for organizing events into tracks, sorting, and depth calculation.
    - **Track Organization**: Implements a high-performance two-pass organization algorithm (`track_organize`) that uses a `HashTable` for $O(1)$ track discovery and a sequential cache for consecutive events. Decoupled from `App` for modularity and unit testing.
    - **Event Sorting**: Optimized for massive tracks using a cache-friendly temporary `SortKey` array to minimize cache misses during indirect data lookups.
- `src/format`: Human-readable time formatting (s, ms, us) and tick interval calculation.
- `src/ztracing_wasm.cc`: WASM-specific entry points, explicit lifecycle control, and platform orchestration.
- `src/ztracing.h`: Clean C API for the WASM-to-JS bridge.
- `src/platform`: Platform abstraction layer (e.g., high-resolution timestamps). Supports both WASM and native (for tests).
- `src/logging`: Simple logging utility with WASM console and native stdout integration.
- `src/track_renderer`: Standalone rendering module that implements performance optimizations like LOD and event coalescing. Decouples rendering calculations from ImGui-specific logic to allow for comprehensive unit testing.

## Trace Viewport

- **Layout**: The "Trace Viewport" is docked in the central area. Other panels ("Status", "Details") are docked at the bottom by default. The viewport window has no title bar or tabs for a cleaner look.
- **Time Ruler**: A persistent horizontal ruler at the top displays the current time range with adaptive units (s, ms, us) and nice tick intervals.
- **Vertical Scrolling**: Tracks are rendered within a scrollable child window. Mouse wheel scrolls the track list vertically. Individual tracks have variable heights based on their maximum nesting depth plus a dedicated header lane.
- **Contiguous Tracks**: Tracks follow each other with no gaps (`track_spacing = 0`). This creates a denser, more professional "Performance" view similar to modern browser profilers.
- **Track Headers**: 
    - **Structure**: Each track is divided into a **Header** (top lane) and **Content** (event lanes). A horizontal separator line clearly divides the two.
    - **Naming**: The header displays the **Thread Name** (or "Thread <TID>" fallback) at the left edge.
    - **Sticky Labels**: Headers use "sticky" positioning to keep the thread name visible while panning horizontally.
    - **Details Tooltip**: Hovering over the thread name label displays a detailed tooltip containing the **PID**, **TID**, and full **Name**.
- **Track Sorting**: Tracks are automatically organized using trace metadata. They are sorted primarily by **thread_sort_index** (ascending), with **PID** and **TID** as stable fallbacks.
- **Downwards Flame Graph**: Overlapping events within a track are organized into hierarchical lanes. An event is placed in a lower lane only if it is strictly contained within a parent event (Strict Containment Hierarchy). Non-strictly nested events move up to share the highest available lane, even if they temporally overlap. This creates a denser, containment-focused view similar to modern profilers.
- **Proportions**:
    - **Lane Height**: **30px** (increased from 20px for better legibility and more spacious interaction).
    - **Ruler Height**: **30px**.
- **Navigation**: 
    - **Zoom**: `Ctrl + MouseWheel` to zoom in/out horizontally around the mouse cursor. Requires modern ImGui modifier checks (`ImGui::IsKeyDown`).
    - **Pan (Horizontal)**: `Shift + MouseWheel` or horizontal scroll wheel.
    - **Pan (Dual-Axis)**: Left-mouse drag within the track viewport allows for simultaneous horizontal and vertical panning.
- **Rendering Optimization**:
    - **Visibility Culling (Horizontal)**: Events are grouped into tracks. Each track maintains a `max_dur` (maximum event duration) and sorted `event_indices`. Binary search is used to find the first potentially visible event at `viewport_start - max_dur`, ensuring partially visible events are correctly rendered.
    - **Visibility Culling (Vertical)**: Tracks outside the vertical scroll area are skipped entirely.
    - **Level of Detail (LOD)**: To handle massive traces (10M+ events), the renderer skips "tiny" events (typically < 1.0 pixel wide) that fall into the same pixel range as a previously drawn block in the same lane.
    - **Event Coalescing**: Consecutive events that are very close to each other (typically within 0.5 - 3.0 pixels) are merged into a single rendering block. 
        - **Color Agnostic**: Different colored events can be merged when they are too small to be visually distinguished, significantly reducing ImGui primitives in dense areas.
        - **Bucket Limit**: Merged blocks are capped at a maximum width (typically 3.0 pixels) to preserve a reasonably accurate representation of event density.
    - **Selection & Hover**:
        - **Hit-Testing**: Hover detection perfectly aligns with the visual representation, including the minimum rendering width (3.0px). If an event is visible, it is interactive.
        - **Visual Priority & Z-Order**: When multiple events overlap in a lane (due to the "move up" rule) or are too small to distinguish, the UI prioritizes the event that was **drawn last** for both highlighting and tooltips.
        - **Tooltips**: Hovering over an event displays a rich tooltip with `10.0f` padding:
            - **Single Events**: Shows full name, category, relative start time (from trace start), duration, and all associated arguments.
            - **Merged Blocks**: Shows the exact number of coalesced events (e.g., "5 merged events").
        - **Deselection**: Clicking on an empty area of the track viewport deselects the current event.
        - **Drag Protection**: Selection and deselection only trigger on a clean click (mouse release without exceeding the `MouseDragThreshold`), preventing accidental changes while panning.
    - **Text Rendering**: Optimized via CPU-side clipping using the `ImDrawList::AddText` overload with a `cpu_fine_clip_rect`. This avoids draw call splits from `PushClipRect` and is only applied when text actually exceeds the available area.
    - **Event Names**: Names are vertically centered within event blocks. Horizontal padding (`6.0f`) is applied to both sides. "Sticky" positioning is used to keep names visible at the left edge of the viewport when an event's start is off-screen.
- **Theming**:
    - **Theme Struct**: A centralized `Theme` struct in `src/colors.h` holds all UI colors, including backgrounds, ruler elements, and event palettes.
    - **Dark Theme**: Muted, professional palette with dark grey tracks and solid black background.
    - **Light Theme**: Based on "MRS. L'S CLASSROOM" palette with brightened green/teal for optimal legibility of black text.
    - **Dynamic Switching**: Themes can be toggled via the "Status" window, automatically updating both application-specific colors and ImGui's built-in styles.
    - **Auto Mode (Default)**: Tracks the system's preferred color scheme.
    - **Event-Driven Updates**: Uses `matchMedia.addEventListener` in `ztracing.js` to notify the application of system theme changes via the `ztracing_on_theme_changed` WASM export, avoiding unnecessary polling.
- **Event Coloring**:
    - **cname Support**: Standard Chrome Trace `cname` values are mapped to specific theme-appropriate colors.
    - **Name Hashing**: Consistent fallback coloring using FNV-1a hash of the event name to select from a balanced palette.
    - **Caching**: Colors are pre-computed and cached in `TraceEventPersisted` during parsing to maximize rendering performance.
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
- **OOM Handling**: The default allocator is configured to log a `LOG_ERROR` and `abort()` immediately if an allocation fails. This simplifies the codebase as other components do not need to check for NULL pointers after allocation.
- **WASM Limits**: The application is configured with `INITIAL_MEMORY=128MB` and `MAXIMUM_MEMORY=4GB` (the 32-bit WASM limit) with `ALLOW_MEMORY_GROWTH=1`.
- **ArrayList Growth**: For large arrays (>1M elements), `ArrayList` uses a conservative growth strategy (25% or 1M elements, whichever is larger) to minimize peak memory pressure during reallocations.
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
