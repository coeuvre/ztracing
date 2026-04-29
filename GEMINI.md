# ztracing: Chrome Tracing Replacement

## Project Mandates

- **Language**: C-style C++. Avoid complex C++ abstractions.
- **C++ Standard**: C++20 (required for `__VA_OPT__` and other features).
- **Style**: Google C++ Style (modified: snake_case for all functions, SCREAMING_CASE for constants).
- **Include Guards**: Must follow the pattern `ZTRACING_SRC_<FILE>_H_`.
- **Warnings**: Strict warnings are enabled for all local code (`-Wall -Wextra -Werror` etc.) via macros in `src/defs.bzl`.
- **ZII & Initialization**: 
    - **Pattern**: Prefer Zero-Is-Initialization (ZII) via `{}` or designated initializers (C++20).
    - **No `memset(0)`**: Never use `memset(ptr, 0, sizeof(T))` for struct initialization.
    - **Designated Initializers**: Use the concise `.field = value` pattern for "ZII + setting" operations. Redundant `= 0` or `= nullptr` initializers are omitted.
    - **Non-Aggregates**: For types with `std::atomic` or `std::thread` (non-aggregates), use placement new (`new (ptr) T()`) to ensure correct value-initialization.
- **UI Framework**: Dear ImGui (v1.92.7-docking).
- **Backend**: Custom WebGL 2.0 (Rendering) and Custom Emscripten HTML5 (Platform).
- **Build System**: Bazel with Bzlmod.
- **Target**: WASM/Browser via Emscripten.
- **Includes**: All internal headers must be included using their full project path (e.g., `#include "src/app.h"`).

## Development Workflow

- **Build**: `bazel build //:ztracing`
- **Output**: `bazel-bin/ztracing/` contains `index.html`, `ztracing.js`, and `ztracing.wasm`.
- **Run**:
  1. `bazel build //:ztracing`
  2. `./tools/serve.py`
  3. Open `http://localhost:8000/index.html` in a browser.

## Architecture

- `src/allocator`: Custom C-style allocator with `allocator_alloc`, `allocator_realloc`, and `allocator_free` helpers. Supports ZII via designated initializers.
    - **CountingAllocator**: A thread-safe decorator that tracks total allocated bytes using `std::atomic<size_t>`. It utilizes `memory_order_relaxed` for high-performance counter updates across the main UI thread and background parser threads.
    - **ImGui Integration**: Dear ImGui is configured to use the `CountingAllocator` for all internal allocations. A specialized wrapper handles the size-tracking requirement by prepending a 16-byte header to every ImGui-requested block, ensuring accurate memory reporting in the UI.
- `src/str`: (Removed) Migrated to `std::string_view`. String-to-number utilities have been moved to their respective usage locations (e.g., `src/trace_parser.cc`) and now utilize `std::from_chars` for improved performance.
- `src/array_list`: Generic `ArrayList<T>` (vector) with explicit allocation and ZII support via `{}`.
- `src/hash_table`: Generic `HashTable<K, V>` with open addressing and linear probing. 
    - **ZII Support**: Fully Zero-Is-Initialization compatible. Internal storage is lazily allocated upon the first `put` operation.
    - **Hash Caching**: Stores the precomputed hash in each entry to accelerate lookups by avoiding equality checks when hashes differ.
    - **Fast Resizing**: Uses cached hashes during table expansion to eliminate redundant recomputations.
    - **Stateful Functors**: Supports stateful functors for hashing and equality, enabling complex key types.
- `src/trace_parser`: C-style streaming parser for the Chrome Trace Event Format. Parses names, categories, phases, timestamps, durations, and arguments. Includes support for the `id` field and numeric argument pre-parsing.
    - **ZII Support**: Fully Zero-Is-Initialization compatible. Initialization is performed via `{}`.
    - **Explicit Allocation**: The stored `Allocator` has been removed. All parser functions (`trace_parser_deinit`, `trace_parser_feed`, `trace_parser_next`) now accept an `Allocator` as an explicit argument.
    - **Progress Tracking**: `trace_parser_feed` returns the number of discarded bytes when shifting the internal buffer, allowing callers to accurately track cumulative parsing progress across multiple chunks.
- `src/trace_data`: Persistent storage for parsed events. 
    - **ZII Support**: Fully ZII compatible via `{}`. Internal functors are lazily linked to the current instance address during the first string push to avoid dangling pointers during moves/copies.
    - **String Table**: Uses a de-duplicated String Table with global hashing to minimize memory usage for repetitive trace data (e.g., event names, categories).
    - **Hash Caching**: Each `StringEntry` stores a persistent hash, computed once during insertion. This makes subsequent lookups for the same string (which occur frequently during trace ingestion) extremely efficient.
    - **StringRef**: Events and arguments store `StringRef` (indices) into the table rather than raw offsets, providing $O(1)$ access to both string data and length without `strlen` overhead.
    - **Pre-parsed Numbers**: Numeric arguments are pre-parsed into `double` values during ingestion to eliminate conversion overhead during rendering.
- `src/imgui_impl_webgl`: Handles WebGL 2.0 (GLES 3.0) rendering logic.
    - **Manual Attribute Binding**: Bypasses Vertex Array Objects (VAOs) in favor of manual `glVertexAttribPointer` calls each frame. This improves compatibility and performance on software-rendering paths (like SwiftShader) that may have slower VAO implementations.
    - **Manual BaseVertex**: Since WebGL 2.0 lacks `glDrawElementsBaseVertex`, the renderer manually offsets `glVertexAttribPointer` calls using `ImDrawCmd::VtxOffset`. This allows for more than 65,535 vertices while still using 16-bit indices.
    - **Single-Upload Strategy**: To minimize WASM-JS bridge overhead, all vertex and index data for a frame are concatenated on the CPU and uploaded in just two `glBufferData` calls.
    - **32-bit Indices Support**: While 16-bit indices are the default and preferred for performance, the renderer automatically detects `ImDrawIdx` size and uses `GL_UNSIGNED_INT` if 32-bit indices are enabled in `imconfig.h`.
    - **Optimized Blending**: Uses `glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` to match Dear ImGui's requirements and avoid expensive alpha-normalization in compositors.
- `src/imgui_impl_wasm`: Handles browser event loops and input mapping via `emscripten/html5.h`.
    - **Keyboard Mapping**: Implements a custom mapping from browser `KeyboardEvent.code` to `ImGuiKey`. Includes explicit support for the **Slash** key to enable the **?** hotkey.
    - **Software Renderer Detection**: Automatically detects software rendering paths (SwiftShader, llvmpipe) via the `WEBGL_debug_renderer_info` extension.
    - **HiDPI Optimization**: Disables HiDPI scaling (forces 1.0x) when a software renderer is detected. This reduces the number of pixels processed by the CPU by 4x on 2x DPI displays, drastically lowering "Commit" latency.
- `src/ztracing.js`: JavaScript side of the WASM/Web interop for file streaming and drag-and-drop. Handles the orchestration of font loading and trace data streaming.
- `src/app`: Application shell and state management. Orchestrates transitions between scenes (Welcome, Loading, Trace Viewer). Utilizes a background `TraceLoadingState` to handle multi-threaded file streaming and data parsing without blocking the UI.
    - **Initialization**: Initialized by `app_init` returning an `App` by value. Self-referencing pointers (e.g., wiring `loading.trace_data`) are established post-construction in `app_begin_session` or the platform entry point.
    - **Thread Safety**: Access to `TraceData` from the main thread (e.g., for theme updates) is strictly prohibited while `loading.active` is true to avoid data races with the background parser.
- `src/trace_viewer`: Logic for rendering the trace viewer scene, including tracks, ruler, and the "Details" window (event properties and arguments).
    - **Architecture**: Decouples interaction and layout logic from ImGui rendering via a pure `trace_viewer_step` function and a `TraceViewerInput` struct. This enables comprehensive unit testing of viewport navigation, hit-testing, selection, and layout without an ImGui context.
    - **Independent States**: Maintains a single `focused_event_idx` (for single clicks) and an `ArrayList<int64_t> selected_event_indices` (for multi-selection) as independent states, allowing a focused event to exist within or outside of a box selection.
    - **Unified Inspection UI**: Centralizes argument and property rendering into modular helper functions (`trace_viewer_draw_args_table`, `trace_viewer_draw_tooltip`). This ensures a consistent, professional, and zero-redundancy experience across all interactive elements.
    - **Table-Based Layout**: Utilizes structured ImGui tables for all multi-key data (Counter series, Event arguments), providing perfect vertical alignment and high legibility.
    - **Zero-Redundancy Logic**: Automatically filters and hides redundant fields (e.g., hiding track names in tooltips, hiding internal PH codes, hiding duration for instant events) to maintain a high signal-to-noise ratio.
    - **Precision & Formatting**: Consistently applies 2-decimal precision (`%.2f`) to all numeric data and utilizes relative, human-readable timestamps (e.g., "Start: 1.2s") for temporal analysis.
    - **Layout Pre-computation**: `trace_viewer_step` computes all layout-dependent state (track Y-offsets, heights, visibility, header names, ruler ticks, and selection overlay dimensions) into dedicated layout structures (`TrackViewInfo`, `RulerTick`, `SelectionOverlayLayout`). This ensures the drawing phase is "dumb" and strictly consumes pre-computed values.
    - **Unit Tests**: Logic is extensively verified in `src/trace_viewer_test.cc`, covering zoom/pan, event hit-testing/selection, timeline selection/snapping, and layout calculations (including culling and naming).
    - **ZII Support**: Fully Zero-Is-Initialization compatible.
- `src/loading_screen`: Specialized scene for displaying parsing progress and filename during trace loading.
    - **Progress Bar**: Displays a visual completion percentage based on the raw input bytes processed. The bar is styled using the theme's default aesthetics and automatically hides if the total file size is unknown.
- `src/welcome_screen`: Initial "drop file" landing scene.
- `src/colors`: Theme management system. Defines a `Theme` struct and provides standard Dark and Light theme implementations.
- `src/track`: Logic for organizing events into tracks, sorting, and depth calculation. Supports ZII.
    - **Track Organization**: Implements a high-performance two-pass organization algorithm (`track_organize`) that uses a `HashTable` for $O(1)$ track discovery and a sequential cache for consecutive events. Decoupled from `App` for modularity and unit testing.
    - **Coloring**: Provides `track_update_colors` to update counter track colors based on the current theme. This is used both during initial organization and when switching themes dynamically.
    - **Event Sorting**: Optimized for massive tracks using a cache-friendly temporary `SortKey` array to minimize cache misses during indirect data lookups.
    - **Block Summaries**: Computes `block_max_durs` for each track, storing the maximum event duration for every 1024 events. This enables efficient skipping of invisible events during rendering.
- `src/format`: Human-readable time formatting (s, ms, us) and tick interval calculation.
- `src/ztracing_wasm.cc`: WASM-specific entry points, explicit lifecycle control, and platform orchestration.
    - **Performance Attributes**: Configures WebGL context with `alpha: false`, `antialias: false`, `depth: false`, and `premultipliedAlpha: false` to minimize compositor workload.
- `src/ztracing.h`: Clean C API for the WASM-to-JS bridge.
- `src/platform`: Platform abstraction layer (e.g., high-resolution timestamps). Supports both WASM and native (for tests).
- `src/logging`: Simple logging utility with WASM console and native stdout integration.
- `src/track_renderer`: Standalone rendering module that implements performance optimizations like LOD and event coalescing. 
    - **Allocation-Free Rendering**: Uses a persistent `TrackRendererState` (Zero-Is-Initialization compatible) to host temporary buffers (`thread_bucket_states`, `counter_current_values`, etc.), eliminating per-frame heap allocations during rendering.
    - **Block-Based Optimization**: Utilizes `block_max_durs` to achieve $O(\text{Blocks} + \text{VisibleEvents})$ rendering complexity. This ensures high performance even when zoomed into microsecond-level details on massive traces by instantly skipping irrelevant event blocks and identifying spanning events without a full trace scan.
    - **Decoupling**: Separates rendering calculations from ImGui-specific logic to allow for comprehensive unit testing.

## Multi-threading & PThreads

- **Background Parsing**: Trace parsing and track organization are offloaded to a background Web Worker (using Emscripten PThreads). This ensures the UI remains responsive (60 FPS) during heavy data ingestion.
- **Communication**: Chunks are streamed from the main thread to the worker via a thread-safe `ChunkQueue`.
- **Backpressure**: To prevent excessive memory usage, the JS bridge monitors the `ChunkQueue` size. If the total queued data exceeds **32MB**, the loader yields to the browser's event loop via `setTimeout(10)` until the worker thread has cleared enough space.
- **Atomics**: Progress metrics (event count, bytes loaded) are updated using C++20 atomics to provide live feedback on the loading screen.
- **COOP/COEP Headers**: To enable PThreads in the browser (via `SharedArrayBuffer`), the web environment must be "cross-origin isolated".
- **Client-Side Solution (Service Worker)**: For deployment on platforms that do not support custom headers (like GitHub Pages), the project includes `src/coi-serviceworker.js`. This service worker intercepts requests and injects the necessary `COOP` and `COEP` headers. It reloads the page once on the first visit to establish the isolated environment.
- **Local Development**: 
    - When using `python3 -m http.server`, the Service Worker will handle the headers automatically.
    - A standard HTTP server script (`tools/serve.py`) is also provided which serves the project artifacts without any custom headers, relying on the Service Worker for environment isolation.

## Startup Optimization

- **Parallel Initialization**: The application parallelizes the fetching of the custom font and the trace data stream.
- **Async Loading API**: The `ztracing_start` bridge accepts an async `getFont` function (returning an `ArrayBuffer`) and an async `getTrace` function (returning a stream object).
- **Fast First Frame**: To minimize "Commit" latency (blank screen), the application begins fetching the trace data as soon as possible. The WASM main loop only awaits the `getFont` promise before starting, ensuring that the `LoadingScreen` appears instantly once the essential UI assets are ready.

## Software Rendering Optimizations

To maintain a smooth 60 FPS even on systems without hardware acceleration (e.g., Linux software paths), the following strategies are employed:
- **DPI Capping**: HiDPI is automatically disabled for software renderers. On a 4K display with 2x scaling, this reduces the fragment shader workload from 8M pixels to 2M pixels per frame.
- **Bridge Call Minimization**: Redundant WebGL state changes (like `glViewport`, `glUseProgram`, and projection matrix updates) are strictly avoided.
- **Manual Attributes**: Using manual attribute pointers instead of VAOs avoids driver-level synchronization stalls common in software WebGL implementations.
- **Precision Balancing**: Uses `highp` for the vertex shader to ensure coordinate stability during massive pans, but `mediump` for the fragment shader to maximize pixel fill-rate on the CPU.
- **Opaque Canvas**: Disabling `alpha` and `premultipliedAlpha` at the context level allows the browser compositor to perform a fast opaque blit instead of expensive per-pixel blending.

## Main Viewport

- **Global Menu Bar**: A persistent menu bar at the top provides access to:
    - **View**: Reset View, Power-save Mode toggle, Details Panel toggle, and Theme selection (Auto, Dark, Light).
    - **Tools**: Access to "Metrics/Debugger" (ImGui's built-in debugger).
    - **Help**: Access to the "Shortcuts" cheatsheet and "About Dear ImGui" information.
- **Shortcuts Cheatsheet**:
    - **Interaction**: A global modal dialog accessible via the **Help** menu or the **?** hotkey (Shift + /).
    - **Closing**: Can be dismissed by clicking the "Close" button or the background "blur" area. Background clicks are automatically consumed to prevent accidental interaction with underlying tracks.
    - **Design**: A structured, two-column cheatsheet layout with themed grid backgrounds, 1px category separators, and top-aligned sections (**GENERAL**, **NAVIGATION**, **SELECTION**).
    - **Aesthetics**: Fully theme-aware, using viewport-integrated background colors and high-contrast text for optimal legibility in both Light and Dark modes.
- **Layout**: The "Main Viewport" is docked in the central area. The "Details" panel is docked at the bottom by default and can be toggled via the View menu. The viewport window has no title bar or tabs, and docking other windows directly into it is disabled (though splitting the area is allowed).
- **Time Ruler**: A persistent horizontal ruler at the top displays the current time range with adaptive units (s, ms, us) and nice tick intervals.
    - **Full-Width Rendering**: The ruler background and border are rendered across the entire viewport width (including the area above the vertical scrollbar), ensuring a consistent visual appearance even when the track list is scrollable.
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
    - **Step Lines**: A continuous step line is drawn on top of each filled area. These lines are rendered without anti-aliasing to ensure pixel-perfect sharpness.
    - **Minimum Visual Height**: Every series in the stack is guaranteed a minimum visual height of 1.0 pixel, ensuring that even series with 0.0 value are visible as thin lines.
    - **Stable Time-Based Bucketing**: To prevent flickering during dragging, buckets are aligned to absolute timestamps (multiples of the time equivalent of 3 pixels).
    - **Stub-Free Peaks**: When a value drop occurs within a bucket, the renderer prioritizes new event values over carry-overs for peak calculation. This eliminates high-value "stubs" in the selection highlight for drop events while still capturing upward spikes as peaks.
    - **Data Clamping**: Rendering is strictly limited to the range between the first and last events in the track. "Gap" blocks before the first event or after the last event are not generated, providing a cleaner view.
    - **Performance**: Complexity is $O(W \log N)$ via binary search jumping, where $W$ is the viewport width and $N$ is the event count. Internal calculations use pre-calculated visual offsets ($O(W \cdot S)$) to minimize per-frame overhead.
    - **Hover Highlighting**: Hovering over a counter track highlights the active bucket/block with a semi-transparent overlay.
    - **Selection Highlighting**: Selecting a counter event highlights all horizontal step lines in the active bucket with the theme's selection color and increased thickness.
    - **Focus Highlighting**: Clicking a counter event applies a **3px thick** border in the theme's focus color (Electric Cyan/Deep Navy) and a distinct semi-transparent vertical overlay (`event_focused_bg`) to the entire bucket. Vertical connection lines remain neutral.
    - **Interactive Tooltip**: Displays values for all series at the hovered bucket's timestamp, including a cumulative total for multi-series counters. Supports both string and numeric arguments.
    - **Coloring**: Each series is assigned a unique color from the theme's event palette based on the hash of its key.
- **Proportions**:
    - **Lane Height**: Dynamically matches the menu bar height using `ImGui::GetFrameHeight()`.
    - **Ruler Height**: Dynamically matches the menu bar height using `ImGui::GetFrameHeight()`.
    - **Counter Height**: Counter tracks have a fixed height equivalent to 3 lanes.
- **Navigation**: 
    - **Zoom**: `Ctrl + MouseWheel` to zoom in/out horizontally around the mouse cursor. Requires modern ImGui modifier checks (`ImGui::IsKeyDown`).
        - **Max Zoom-out**: Limited to 1.2x the trace's total duration.
        - **Min Zoom-in**: Limited to 1000us (1ms).
    - **Pan (Horizontal)**: `Shift + MouseWheel` or horizontal scroll wheel.
    - **Pan (Dual-Axis)**: Left-mouse drag within the track viewport allows for simultaneous horizontal and vertical panning.
    - **Initial View & Reset**: Upon loading a trace or selecting "Reset View", the viewport is centered and zoomed to the maximum zoom-out level (1.2x duration).
- **Double-click to Zoom**: Double-clicking (LMB) any thread event instantly focuses the viewport on that event.
    - **Context Padding**: Adds a **5% padding** to both sides of the event duration to provide temporal context.
    - **Automatic Focus**: Automatically sets the event as **focused** (3px vibrant border), creates a **timeline selection** for the event's exact range, and opens the **Details** panel.
    - **Selection Override**: Works even if the event is outside a currently active timeline selection area.
    - **Stability Shield**: Employs a frame-aware release shield (`ignore_next_release`) to prevent viewport shifts from causing accidental deselection during the trailing mouse-up event of a double-click.
- **Timeline Selection**:
    - **Interaction**: Dragging on the timeline ruler creates a time range selection. Every new drag on the ruler starts a fresh selection from the click point.
    - **Rectangle Selection**: Holding **Shift** and dragging with the **Left Mouse Button** in the tracks area creates a spatial rectangle selection.
    - **Selection Threshold**: New selections in the ruler area require a **5-pixel drag** before becoming active. This ensures that a simple press or click on the ruler does not destroy or re-create the selection prematurely.
    - **Draggable Boundaries**: Vertical boundaries can be adjusted by dragging them within the tracks area. The mouse cursor changes to `ew-resize` when hovering over or dragging a boundary (unless in `BOX_SELECT` mode).
    - **Snapping**: Dragging selection boundaries (both in the ruler and tracks) snaps to the edges of visible thread event blocks within a **5-pixel threshold**. Snapping is disabled during rectangle selection (`BOX_SELECT`).
    - **Visuals**: Displays two vertical lines marking the range boundaries, a semi-transparent dimmed overlay for areas outside the selection, and a duration label with horizontal arrows positioned **1/3 from the top** of the tracks area. The label features a themed background and a 1px border matching the selection color for improved legibility.
    - **Edge Alignment**: Boundaries are perfectly aligned between the ruler and track areas. The right boundary line is always drawn with a **-10f offset** from its calculated position to prevent clipping at the viewport's right edge, ensuring it remains visible during panning.
    - **Dimming Consistency**: The dimmed overlay in the ruler area spans the full viewport width, matching the ruler's background rendering.
    - **Snapping Highlight**: When a boundary is snapped during a drag, the specific edge of the event block is highlighted with a **3-pixel wide red vertical line**.
    - **Interaction Gating**:
        - Hovering and clicking on events or tracks is disabled within the dimmed areas (outside the selection).
        - Hovering and clicking on events is disabled while dragging boundaries or when the mouse is over a boundary handle.
        - Panning (horizontal and vertical) is disabled while a boundary is being dragged.
    - **Zoom/Pan Constraints**: When a selection is active, the viewport is constrained to keep the selection visible. Panning is clamped so selection boundaries can reach but not exceed viewport edges.
    - **Deselection**:
        - **Ruler Click**: A simple click on the timeline ruler (without dragging) clears the active timeline selection.
        - **Viewport Click**: Clicking on an empty area of the track viewport clears the currently **focused event** but **preserves** the multi-event selection.
        - **Manual Clear**: The multi-event selection can be explicitly cleared using the **"Clear"** button in the Details panel.
    - **Refactored Interaction Logic**: Selection and snapping logic are consolidated directly into `TraceViewer`, with per-frame updates handled in `trace_viewer_step`. Dimming areas are calculated locally during rendering to ensure perfect alignment with viewport bounds.
    - **Multi-Selection Storage**: `TraceViewer` maintains an `ArrayList<int64_t> selected_event_indices` to support multiple simultaneous selections. The list is sorted for efficient $O(\log N)$ rendering checks.
    - **Spatial Hit-Testing**: Rectangle selection performs spatial intersection tests against all visible events. It utilizes track `max_dur` to correctly identify long events that start before the selection box and ignores counter track headers to prevent accidental selections.
    - **Comprehensive Tests**: Logic is verified by multi-frame simulation tests in `src/trace_viewer_test.cc`, covering precise click starts, snapped dragging, interaction gating, zoom clamping, and rectangle selection (including long events and counter gating).
    - **Stationary Mapping**: All time-to-pixel conversions use a consistent stationary origin (`tv->last_tracks_x`) and width (`tv->last_inner_width`) to ensure perfect horizontal alignment between the ruler and tracks.
- **Rendering Optimization**:
    - **Visibility Culling (Horizontal)**: Events are grouped into tracks. Each track maintains a `max_dur` (maximum event duration) and sorted `event_indices`. Binary search is used to find the first potentially visible event at `viewport_start - max_dur`, ensuring partially visible events are correctly rendered.
    - **Visibility Culling (Vertical)**: Tracks outside the vertical scroll area are skipped entirely.
    - **Level of Detail (LOD)**: To handle massive traces (10M+ events):
        - **Tiny Events**: Skips rendering of events < 1.0 pixel wide that fall into the same pixel range as a previously drawn block. **Focused events** always bypass this optimization.
        - **Event Borders**: Borders are only drawn for focused/selected events or those wider than `TRACK_MIN_EVENT_WIDTH`. A **0.01f epsilon** is applied to the threshold to prevent floating-point jitter from causing borders to flicker during panning.
    - **Event Coalescing & Bucketing**: To maintain performance and visual stability in dense areas:
        - **Stable Bucketing**: Both thread and counter tracks utilize a stable bucketing system. Viewports are divided into buckets aligned to absolute timestamps (multiples of the time equivalent of 3.0 pixels).
        - **Panning Stability**: For thread tracks, bucketing starts from a stable timestamp that covers all potentially visible events (accounting for `max_dur`). Each event is strictly processed only within the bucket iteration corresponding to its start time. This eliminates "dancing" or flickering of merged blocks during horizontal panning.
        - **Thread-Track Merging**: For thread tracks, sub-pixel events within a bucket are merged at each depth. The event with the longest duration is chosen as the "representative," providing the color, name, and tooltip for the merged block.
        - **Bucket Limit**: Merged blocks are capped at a maximum width (typically 3.0 pixels) to preserve a reasonably accurate representation of event density.
        - **High-Priority Bypass**: Events wider than a bucket, **focused events**, or currently selected events bypass the bucketing logic and are rendered with full precision. A **0.01f epsilon** is applied to the width threshold to prevent floating-point jitter from flipping events between 'large' and 'tiny' states during panning.
    - **Selection & Hover**:
        - **Hit-Testing**: Hover detection perfectly aligns with the visual representation, including the minimum rendering width (3.0px). If an event is visible, it is interactive.
        - **Block-Based Interaction**: Both highlighting and selection are driven by the same `TrackRenderBlock` data structure. Each block carries an `event_idx`, ensuring that the event highlighted by the mouse is always the one selected when clicking. This eliminates discrepancies between the visual feedback and the actual selection.
        - **Visual Priority & Z-Order**: When multiple events overlap in a lane (due to the "move up" rule) or are too small to distinguish, the UI prioritizes the event that was **drawn last** for both highlighting and tooltips.
        - **Tooltips**: Hovering over an event displays a rich tooltip with `10.0f` padding:
            - **Single Events**: Shows full name, category, relative start time (from trace start), duration, and all associated arguments.
            - **Merged Blocks**: Shows the exact number of coalesced events (e.g., "5 merged events").
        - **Selection Indicators**: Selection is indicated by a 1px high-contrast border (`event_border_selected`).
        - **Focus Indicators**: Focused event is indicated by a **3px thick** vibrant border (`event_border_focused`) and uses the same original event colors to maintain category visibility.
        - **Deselection**:
            - Clicking on an empty area of the track viewport clears the currently focused event but **preserves** the multi-event selection.
            - The multi-event selection can be explicitly cleared using the **"Clear"** button in the Details panel.
        - **Drag Protection**: Selection and deselection only trigger on a clean click (mouse release without exceeding the `MouseDragThreshold`), preventing accidental changes while panning.
    - **Text Rendering**: Optimized via CPU-side clipping using the `ImDrawList::AddText` overload with a `cpu_fine_clip_rect`. This avoids draw call splits from `PushClipRect` and is only applied when text actually exceeds the available area.
    - **Event Names**: Names are centered both vertically and horizontally within event blocks if they fit. If the name is larger than the available area, it starts rendering from the beginning of the block (with padding). Horizontal padding (`6.0f`) is applied to both sides. "Sticky" positioning for names is disabled to prioritize centering.
- **Theming**:
    - **Theme Struct**: A centralized `Theme` struct in `src/colors.h` holds all UI colors, including backgrounds, ruler elements, and event palettes.
    - **Dark Theme**: Muted, professional palette with dark grey tracks and solid black background (rendered within the canvas). White 1px selection borders.
    - **Light Theme**: Based on "MRS. L'S CLASSROOM" palette with brightened green/teal for optimal legibility of black text. Black 1px selection borders.
    - **Dynamic Switching**: Themes can be toggled via the global menu bar, automatically updating both application-specific colors and ImGui's built-in styles.
    - **Color Updates**: Switching themes triggers a full re-computation of counter track series colors (`track_update_colors`) to maintain visual consistency.
    - **Auto Mode (Default)**: Tracks the system's preferred color scheme.
    - **Event-Driven Updates**: Uses `matchMedia.addEventListener` in `ztracing.js` to notify the application of system theme changes via the `ztracing_on_theme_changed` WASM export, avoiding unnecessary polling.
- **Event Coloring**:
    - **cname Support**: Standard Chrome Trace `cname` values are mapped to specific theme-appropriate colors.
    - **Name Hashing**: Consistent fallback coloring using FNV-1a hash of the event name to select from a balanced palette.
    - **Caching**: Colors are pre-computed and cached in `TraceEventPersisted` during parsing to maximize rendering performance.

## Details Panel

- **Visibility**: Can be toggled via the "View" menu. Docked at the bottom by default.
- **Behavior**:
    - **Closed by Default**: The panel is initially closed to maximize the viewport area.
    - **Auto-Open**: Automatically opens when one or more events are selected.
    - **Focus Management**: When automatically opened, it does not steal focus (`ImGuiWindowFlags_NoFocusOnAppearing`), ensuring the user can continue navigating the viewport uninterrupted.
    - **Content**: 
        - **Focused Event**: Displays detailed information for the focused event (Name, Category, PH, Timestamp, Duration, PID, TID, and all Arguments).
        - **Selection**: Displays a summary (count) and a high-performance, scrollable table listing each selected event's Name, Category, Start time, and Duration.
        - **Concurrent Display**: If both a focused event and a multi-selection exist, both sections are displayed simultaneously, separated by a visual divider, with the focused event details prioritized at the top.
            - **Click to Focus**: Clicking a row in the table instantly focuses, zooms, and scrolls to the corresponding event in the track viewport. The track is automatically centered vertically.
            - **Performance**: Utilizes `ImGuiListClipper` for the selection table to achieve $O(\text{VisibleRows})$ rendering and formatting complexity.
            - **Sticky Headers**: Implements a sticky header row via `ImGuiTableFlags_ScrollY` and `ImGui::TableSetupScrollFreeze(0, 1)`.
            - **Alignment & Legibility**: All data is left-aligned using `ImGuiTableFlags_SizingFixedFit`. Row backgrounds and borders are enabled to improve row tracking.
    - **Arguments**: Supports both string and numeric arguments (`val_double`), ensuring counter values are correctly displayed.
    - **Selection Prompt**: Displays a "Select an event to see details" prompt when no event is selected.
    - **Padding**: Uses `10.0f` window padding for better legibility.

## Trace Parser Integration

- **Streaming**: Trace files are read in chunks using the browser's `ReadableStream` API.
- **Decompression**: `ztracing.js` handles Gzip decompression on-the-fly using the `DecompressionStream` API if the `Content-Type` is `application/gzip` or `application/x-gzip`.
- **Zero-Copy Streaming**: Trace chunks are allocated on the WASM heap by the JS bridge and ownership is transferred directly to the application. This eliminates redundant copies and minimizes garbage collection pressure.
- **Dynamic Memory Growth**: To safely handle heap growth (especially with `SharedArrayBuffer` in PThreads mode), the JS bridge utilizing a dedicated `setWasmMemory` helper that creates a fresh `Uint8Array` view of `wasmMemory.buffer` immediately before every write. This ensures the view length always matches the current buffer size.
- **Responsiveness**: Parsing yields to the browser's event loop every 100ms during loading (via `setTimeout(0)` in JS). This prevents the microtask-based `ReadableStream` loop from starving the main thread, ensuring `requestAnimationFrame` can fire and keep the UI responsive.
- **Progress Feedback**: Displays live parsing statistics (event count and MB processed) and the filename within the `LoadingScreen`. Progress is deferred until the worker thread actually processes the data, providing a more accurate representation of system state.
- **WASM Exports**: `ztracing_malloc`, `ztracing_free`, `ztracing_begin_session`, and `ztracing_handle_file_chunk` are used for memory and data transfer. `wasmMemory` is explicitly exported to allow the JS bridge to maintain consistent memory views.
- **Error Handling**: `ztracing_init` returns specific error codes (1 for WebGL context creation failure, 2 for renderer initialization failure). The `ztracing_start` JS bridge accepts an `onError(errorCode, errorMessage)` callback to display custom error pages in the DOM.

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
