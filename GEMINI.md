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
- `src/str`: Basic `Str` struct (buffer and length) for string views. Includes high-performance string-to-number utilities (`str_to_int64`, `str_to_double`) with fast-path integer parsing.
- `src/array_list`: Generic `ArrayList<T>` (vector) with explicit allocation and ZII.
- `src/hash_table`: Generic `HashTable<K, V>` with open addressing and linear probing. 
    - **Hash Caching**: Stores the precomputed hash in each entry to accelerate lookups by avoiding equality checks when hashes differ.
    - **Fast Resizing**: Uses cached hashes during table expansion to eliminate redundant recomputations.
    - **Stateful Functors**: Supports stateful functors for hashing and equality, enabling complex key types.
- `src/trace_parser`: C-style streaming parser for the Chrome Trace Event Format. Parses names, categories, phases, timestamps, durations, and arguments. Includes support for the `id` field and numeric argument pre-parsing.
- `src/trace_data`: Persistent storage for parsed events. 
    - **String Table**: Uses a de-duplicated String Table with global hashing to minimize memory usage for repetitive trace data (e.g., event names, categories).
    - **Hash Caching**: Each `StringEntry` stores a persistent hash, computed once during insertion. This makes subsequent lookups for the same string (which occur frequently during trace ingestion) extremely efficient.
    - **StringRef**: Events and arguments store `StringRef` (indices) into the table rather than raw offsets, providing $O(1)$ access to both string data and length without `strlen` overhead.
    - **Pre-parsed Numbers**: Numeric arguments are pre-parsed into `double` values during ingestion to eliminate conversion overhead during rendering.
- `src/imgui_impl_webgl`: Handles WebGL 2.0 (GLES 3.0) rendering logic.
    - **Single-Upload Strategy**: To minimize WASM-JS bridge overhead, all vertex and index data for a frame are concatenated on the CPU and uploaded in just two `glBufferData` calls.
    - **Persistent VAO**: Reuses a single Vertex Array Object to capture and restore vertex attribute state, eliminating per-frame setup costs.
    - **Index Adjustment**: Indices are adjusted on the CPU to be absolute, allowing for a single `glVertexAttribPointer` setup per frame.
    - **32-bit Indices**: Supports more than 65,535 vertices (required for large traces).
- `src/imgui_impl_wasm`: Handles browser event loops and input mapping via `emscripten/html5.h`.
- `src/ztracing.js`: JavaScript side of the WASM/Web interop for file streaming and drag-and-drop.
- `src/app`: Application shell and state management. Orchestrates transitions between scenes (Welcome, Loading, Trace Viewer) and handles file streaming and data parsing.
- `src/trace_viewer`: Logic for rendering the trace viewer scene, including tracks, ruler, and the "Details" window (event properties and arguments).
- `src/loading_screen`: Specialized scene for displaying parsing progress and filename during trace loading.
- `src/welcome_screen`: Initial "drop file" landing scene.
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

## Main Viewport

- **Global Menu Bar**: A persistent menu bar at the top provides access to:
    - **View**: Reset View, Power-save Mode toggle, Details Panel toggle, and Theme selection (Auto, Dark, Light).
    - **Tools**: Access to "Metrics/Debugger" (ImGui's built-in debugger).
    - **Help**: "About Dear ImGui" information.
- **Layout**: The "Main Viewport" is docked in the central area. The "Details" panel is docked at the bottom by default and can be toggled via the View menu. The viewport window has no title bar or tabs.
- **Time Ruler**: A persistent horizontal ruler at the top displays the current time range with adaptive units (s, ms, us) and nice tick intervals.
- **Vertical Scrolling**: Tracks are rendered within a scrollable child window. Mouse wheel scrolls the track list vertically. Individual tracks have variable heights based on their maximum nesting depth plus a dedicated header lane.
- **Contiguous Tracks**: Tracks follow each other with no gaps (`track_spacing = 0`). This creates a denser, more professional "Performance" view similar to modern browser profilers.
- **Track Headers**: 
    - **Structure**: Each track is divided into a **Header** (top lane) and **Content** (event lanes or chart). A horizontal separator line clearly divides the two.
    - **Naming**: The header displays the **Thread Name** (or "Thread <TID>" fallback) or the **Counter Name** (including the `id` if present).
    - **Sticky Labels**: Headers use "sticky" positioning to keep the name visible while panning horizontally.
    - **Details Tooltip**: Hovering over the header label displays a detailed tooltip containing the **PID**, **TID** (for threads), **Name**, and **ID** (for counters).
- **Track Sorting**: Tracks are automatically organized using trace metadata. They are sorted primarily by **thread_sort_index** (ascending), then by **PID**. Within a process, **Counter Tracks** always appear before **Thread Tracks**. Thread tracks are further sorted by **TID**, while counter tracks are sorted alphabetically by **Name** (case-insensitive) and then by **ID**.
- **Downwards Flame Graph**: Overlapping events within a thread track are organized into hierarchical lanes. An event is placed in a lower lane only if it is strictly contained within a parent event (Strict Containment Hierarchy). Non-strictly nested events move up to share the highest available lane, even if they temporally overlap. This creates a denser, containment-focused view similar to modern profilers.
- **Counter Tracks**:
    - **Stacked Area Chart**: Renders multiple data series as a stacked area chart using a step-function approach.
    - **Step Lines**: A continuous step line is drawn on top of each filled area using the theme's border color, providing sharp definition between series.
    - **Stable Time-Based Bucketing**: To prevent flickering during dragging, buckets are aligned to absolute timestamps (multiples of the time equivalent of 3 pixels).
    - **Performance**: Complexity is $O(W \log N)$ via binary search jumping, where $W$ is the viewport width and $N$ is the event count, making rendering performance independent of total event count.
    - **Hover Highlighting**: Hovering over a counter track highlights the active bucket/block with a semi-transparent overlay.
    - **Interactive Tooltip**: Displays values for all series at the hovered bucket's timestamp, including a cumulative total for multi-series counters.
    - **Coloring**: Each series is assigned a unique color from the theme's event palette based on the hash of its key.
- **Proportions**:
    - **Lane Height**: Dynamically matches the menu bar height using `ImGui::GetFrameHeight()`.
    - **Ruler Height**: Dynamically matches the menu bar height using `ImGui::GetFrameHeight()`.
    - **Counter Height**: Counter tracks have a fixed height equivalent to 2 lanes.
- **Navigation**: 
    - **Zoom**: `Ctrl + MouseWheel` to zoom in/out horizontally around the mouse cursor. Requires modern ImGui modifier checks (`ImGui::IsKeyDown`).
    - **Pan (Horizontal)**: `Shift + MouseWheel` or horizontal scroll wheel.
    - **Pan (Dual-Axis)**: Left-mouse drag within the track viewport allows for simultaneous horizontal and vertical panning.
- **Rendering Optimization**:
    - **Visibility Culling (Horizontal)**: Events are grouped into tracks. Each track maintains a `max_dur` (maximum event duration) and sorted `event_indices`. Binary search is used to find the first potentially visible event at `viewport_start - max_dur`, ensuring partially visible events are correctly rendered.
    - **Visibility Culling (Vertical)**: Tracks outside the vertical scroll area are skipped entirely.
    - **Level of Detail (LOD)**: To handle massive traces (10M+ events):
        - **Tiny Events**: Skips rendering of events < 1.0 pixel wide that fall into the same pixel range as a previously drawn block.
        - **Event Borders**: Borders are only drawn for selected events or those wider than 5.0 pixels. For dense areas, this reduces the triangle count from 10 to 2 per event, significantly lowering GPU load.
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
    - **Event Names**: Names are centered both vertically and horizontally within event blocks. Horizontal padding (`6.0f`) is applied to both sides. "Sticky" positioning for names is disabled to prioritize centering.
- **Theming**:
    - **Theme Struct**: A centralized `Theme` struct in `src/colors.h` holds all UI colors, including backgrounds, ruler elements, and event palettes.
    - **Dark Theme**: Muted, professional palette with dark grey tracks and solid black background.
    - **Light Theme**: Based on "MRS. L'S CLASSROOM" palette with brightened green/teal for optimal legibility of black text.
    - **Dynamic Switching**: Themes can be toggled via the global menu bar, automatically updating both application-specific colors and ImGui's built-in styles.
    - **Auto Mode (Default)**: Tracks the system's preferred color scheme.
    - **Event-Driven Updates**: Uses `matchMedia.addEventListener` in `ztracing.js` to notify the application of system theme changes via the `ztracing_on_theme_changed` WASM export, avoiding unnecessary polling.
- **Event Coloring**:
    - **cname Support**: Standard Chrome Trace `cname` values are mapped to specific theme-appropriate colors.
    - **Name Hashing**: Consistent fallback coloring using FNV-1a hash of the event name to select from a balanced palette.
    - **Caching**: Colors are pre-computed and cached in `TraceEventPersisted` during parsing to maximize rendering performance.
- **32-bit Indices**: ImGui is patched via `MODULE.bazel` to use `unsigned int` for `ImDrawIdx`, allowing for more than 65,535 vertices (required for large traces).

## Details Panel

- **Visibility**: Can be toggled via the "View" menu. Docked at the bottom by default.
- **Behavior**:
    - **Content**: Displays detailed information for the currently selected event (Name, Category, PH, Timestamp, Duration, PID, TID, and all Arguments).
    - **Selection Prompt**: Displays a "Select an event to see details" prompt when no event is selected.
    - **Padding**: Uses `10.0f` window padding for better legibility.

## Trace Parser Integration

- **Streaming**: Trace files are read in chunks using the browser's `ReadableStream` API.
- **Decompression**: `ztracing.js` handles Gzip decompression on-the-fly using the `DecompressionStream` API if the `Content-Type` is `application/gzip` or `application/x-gzip`.
- **WASM Bridge**: `ztracing.js` handles both drag-and-drop and direct stream loading (via `ztracing_start` options).
- **Direct Loading**: `shell.html` parses the `trace` URL parameter and fetches the trace file automatically if present.
- **Responsiveness**: Parsing yields to the browser's event loop every 100ms during loading (via `setTimeout(0)` in JS). This prevents the microtask-based `ReadableStream` loop from starving the main thread, ensuring `requestAnimationFrame` can fire and keep the UI responsive.
- **Progress Feedback**: Displays live parsing statistics (event count and MB loaded) and the filename within the `LoadingScreen` while `trace_parser_active` is true.
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
