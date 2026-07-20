# Rust Migration Functional Equivalence Review Checklist

This checklist pairs each migrated Rust component with its legacy C/C++ implementation and tests. Review the sections in order: later layers depend on behavior established by earlier ones.

## Mandatory review standard: functional identity

Every review must establish that the Rust implementation is **functionally identical** to the legacy C/C++ implementation. Similar structure, equivalent intent, passing existing tests, or generally correct behavior is not sufficient by itself.

For every checklist item, compare the old and new implementations directly and verify that they preserve:

- the same behavior for all valid inputs;
- the same behavior at boundary values and for malformed, incomplete, or invalid inputs;
- the same outputs, formatting, ordering, defaults, errors, and exit statuses;
- the same state transitions, lifecycle, cancellation, completion, and concurrency semantics;
- the same externally observable allocation, ownership, cleanup, and resource-lifetime behavior;
- the same native, headless, and WebAssembly behavior where applicable;
- the same ABI, exported symbols, buffer ownership, and JavaScript integration where applicable;
- the same rendering calculations, interactions, draw order, and golden-image results where applicable.

Any intentional or unavoidable difference must be documented explicitly and approved before the corresponding review item is marked complete. When existing tests do not prove functional identity, add focused differential or edge-case tests that execute equivalent scenarios against both implementations.

An important inventory caveat: `test_migration_inventory.md` names several Rust test files that do not physically exist. Many migrated tests are inline `#[cfg(test)]` modules. The actual locations are identified below.

## 1. Base memory and collections

### 1.1 Allocation and accounting — Complete

- Rust: `base/allocation.rs`
- Legacy: `core/allocator.c`, `core/allocator.h`, `core/counting_allocator.c`, `core/counting_allocator.h`
- Legacy tests: `core/allocator_test.cc`
- Review: zero-initialization, realloc semantics, alignment, overflow, accounting, and allocator pairing.

### 1.2 Arena compatibility — Complete

- Rust: removed after review because production uses Rust ownership/scopes rather than this compatibility arena.
- Legacy: `core/arena.c`, `core/arena.h`
- Legacy tests: `core/arena_test.cc`
- Review: allocation order, checkpoints, reset, growth, alignment, and peak accounting.
- Note: although the plan proposed removing arenas, the Rust migration preserves an arena implementation.

### 1.3 Dynamic arrays and hash tables — Complete

- Rust: removed after review because it contained only tests of standard-library collections.
- Legacy: `core/darray.c`, `core/darray.h`, `core/hash_table.c`, `core/hash_table.h`
- Legacy tests: `core/darray_test.cc`, `core/hash_table_test.cc`
- Review: growth, collision handling, deterministic iteration, clearing, indexing, and ownership.

### 1.4 Strings — Complete

- Rust: removed after review because its compatibility helpers were unused by production.
- Legacy: `core/string.c`, `core/string.h`
- Legacy tests: `core/string_test.cc`
- Review: byte versus UTF-8 behavior, slicing, formatting, buffer growth, and ownership transfer.

## 2. Base parsing and infrastructure

### 2.1 Incremental JSON reader — Complete

- Rust: `base/json.rs`
- Legacy: `core/json_reader.c`, `core/json_reader.h`
- Legacy tests: `core/json_reader_test.cc`
- Review: arbitrary chunk boundaries, malformed numbers, escapes, incomplete input, error offsets, and end-of-stream behavior.
- Approved behavior: partial tokens on non-final chunks return `NeedMore`; malformed or incomplete JSON at final EOF is reported as an error by parser and loader paths.

### 2.2 Logging — Complete

- Rust: `base/logging.rs`
- Legacy: `core/logging.h`, `core/logging_native.c`, `core/logging_wasm.cc`
- Review: native destinations, browser messages, formatting, severity behavior, and thread safety.
- Approved behavior: severity names follow Rust ecosystem conventions; WASM messages are dynamically sized rather than truncated to the legacy 1024-byte buffer.

### 2.3 Task scheduler — Complete

- Rust: `base/task.rs`
- Legacy: `core/task.c`, `core/task.h`
- Legacy tests: `core/task_test.cc`
- Review carefully: queue capacity, backpressure, stream serialization, worker affinity, cancellation races, completion delivery, shutdown, starvation, and inline execution.
- Approved Rust design: a generic bounded `TaskQueue` accepts an injected executor instead of creating threads. A fixed-size `ThreadPoolExecutor`, implemented using Rust standard-library primitives, supplies the production execution policy. Owned closures and a single typed completion stream replace user-data pointers, task arenas, separate peek/remove lifetimes, and per-task result channels. Streaming trace loading is one long-running cancellable job consuming a bounded chunk channel; searches share the same queue and use generation-based stale-result rejection. Native and WASM production use the same shared executor, and the threaded Emscripten build uses a pinned atomics-enabled Rust standard library built hermetically by Bazel.

### 2.4 Base crate wiring — Complete

- Rust: `base/lib.rs`, `base/BUILD`
- Legacy: `core/BUILD`, `core/assert.h`
- Review: public API exposure, conditional WASM behavior, panic/assertion substitutions, and build flags.
- Approved behavior: native Rust uses normal panic unwinding, including task-level panic reporting, rather than reproducing the legacy process-wide `abort()`. WASM uses `panic=abort`. Final Emscripten linker settings are owned by the Bazel C++ link-options target because it performs the final link.

## 3. Trace ingestion and storage

### 3.1 Trace parser — Complete

- Rust: `src/trace/parser.rs`
- Legacy: `src/trace_parser.c`, `src/trace_parser.h`
- Legacy tests: `src/trace_parser_test.cc`
- Rust tests: inline in `src/trace/parser.rs`
- Review: streaming boundaries, accepted document shapes, phase handling, numeric conversion, unknown fields, malformed input, and error compatibility.
- Approved behavior: parsing is intentionally best-effort rather than strict JSON validation. Malformed separators, trailing data, or malformed unknown fields may be accepted when the parser can still extract the trace events and all recognized fields it needs. Incomplete input or malformed required structure/recognized values still reports an error when extraction cannot complete.

### 3.2 Trace data and string interning — Complete

- Rust: `src/trace/data.rs`, `src/string_interner.rs`
- Legacy: `src/trace_data.c`, `src/trace_data.h`
- Legacy tests: `src/trace_data_test.cc`
- Rust tests: inline in `src/trace/data.rs` and `src/string_interner.rs`
- Review: event representation, string deduplication, begin/end matching, argument merging, timestamp/duration conversion, and metadata.
- Approved behavior: when an end event contains duplicate new argument keys, Rust keeps one argument with the last value rather than preserving duplicate entries.
- Approved behavior: an empty event name follows the same FNV-1a palette hashing rule as other names and therefore selects palette index 5; matching the legacy default index 0 is not required for this case.

### 3.3 Trace module composition — Complete

- Rust: `src/trace/mod.rs`
- Legacy counterpart: APIs spread across `src/trace_*.h`
- Rust tests: inline migrated tests in `src/trace/mod.rs`
- Review: exported API, ownership boundaries, and native/WASM conditional modules.
- Verified boundary: `LoadedTrace` and streamed loading remain platform-neutral in `src/trace/session.rs`; native filesystem/gzip loading is isolated in `src/trace/loader.rs`, which is excluded entirely from WASM together with its zlib link dependency.

## 4. Tracks and analytics

### 4.1 Track organization — Complete

- Rust: `src/trace/track.rs`
- Legacy: `src/track.c`, `src/track.h`
- Legacy tests: `src/track_test.cc`
- Rust tests: inline in `src/trace/track.rs`
- Review: sorting, depth assignment, counter organization, case-insensitive ordering, and visible-start lookup.
- Approved behavior: events with a missing phase are ordinary thread events. Unlike the legacy sentinel collision, the absence of interned `"C"` or `"M"` strings does not classify an empty phase as counter or metadata.
- Approved behavior: organizing an empty trace returns the explicit Rust range `(0, 0)` rather than preserving caller-owned output values. The viewer expands this empty range to its minimum viewport duration.

### 4.2 Histogram — Complete

- Rust: `src/trace/histogram.rs`
- Legacy: `src/trace_histogram.c`, `src/trace_histogram.h`
- Legacy tests: `src/trace_histogram_test.cc`
- Rust tests: inline in `src/trace/histogram.rs`
- Review: bucket boundaries, logarithmic mode, empty input, invalid indexes, and time filtering.

### 4.3 Heatmap — Complete

- Rust: `src/trace/heatmap.rs`
- Legacy: `src/trace_heatmap.c`, `src/trace_heatmap.h`
- Legacy tests: `src/trace_heatmap_test.cc`
- Rust tests: inline in `src/trace/heatmap.rs`
- Review: viewport clamping, counter tracks, zero-duration input, and cell indexing.

### 4.4 Concurrency analysis — Complete

- Rust: `src/trace/concurrency.rs`
- Legacy: `src/trace_concurrency.c`, `src/trace_concurrency.h`
- Legacy tests: `src/trace_concurrency_test.cc`
- Rust tests: inline in `src/trace/concurrency.rs`
- Review: interval boundary rules, simultaneous start/end events, and deterministic output.
- Approved behavior: when dominant events have equal accumulated durations, Rust orders them lexically by name. The legacy hash-table iteration and `qsort` tie order was unspecified.

### 4.5 Aggregation — Complete

- Rust: `src/trace/aggregate.rs`
- Legacy: `src/trace_aggregate.c`, `src/trace_aggregate.h`
- Legacy tests: `src/trace_aggregate_test.cc`
- Rust tests: inline in `src/trace/aggregate.rs`
- Review: grouping keys, category handling, counts, totals, sorting, and tie-breaking.
- Approved behavior: aggregate entries with equal totals or counts are ordered lexically by key. The legacy `qsort` tie order was unspecified.

### 4.6 Diff — Complete

- Rust: `src/trace/diff.rs`
- Legacy: `src/trace_diff.c`, `src/trace_diff.h`
- Legacy tests: `src/trace_diff_test.cc`
- Rust tests: inline in `src/trace/diff.rs`
- Review: missing groups, signed deltas, sorting, category grouping, and floating-point output.

## 5. Loading and asynchronous operations

### 5.1 Raw and gzip loader — Complete

- Rust: `src/trace/loader.rs`
- Legacy: `src/trace_loader.c`, `src/trace_loader.h`
- Rust tests: inline in `src/trace/loader.rs`, with CLI integration coverage inline in `src/cli.rs`
- Review carefully: gzip detection, zlib return codes, truncated or corrupted streams, concatenated input, chunk sizing, and cleanup on errors.
- Approved behavior: Rust follows standard gzip behavior and decompresses concatenated gzip members as one input stream. The legacy direct `inflate()` loop stopped after the first member.

### 5.2 Loading session/task — Complete

- Rust: `src/trace/session.rs`
- Legacy: `src/trace_load_task.c`, `src/trace_load_task.h`
- Legacy tests: `src/trace_load_task_test.cc`
- Rust tests: inline in `src/trace/session.rs`
- Review: lifecycle, backpressure, cancellation, completion, partial input, cleanup, and allocation accounting.
- Approved design: one long-lived load job owns the streaming parser while a standard-library channel supplies chunks. This preserves ordered streaming without consuming one shared-executor task slot per chunk; producers use `buffered_bytes` for byte-based asynchronous backpressure rather than blocking submission by chunk count.
- Correctness requirement: review must ensure functional identity for production-observable loading behavior. Rust may use ownership and RAII internally, but successful completion, cancellation, progress, cleanup, and telemetry must remain equivalent unless an exception is explicitly approved.
- Verified fixes: EOF closes the submission side; cancellation propagates through `TaskHandle`/`CancelToken`; the worker polls cancellation while waiting; and each queued chunk owns an RAII byte-accounting guard so success, rejection, cancellation, and shutdown all return `buffered_bytes` to zero.
- Verified telemetry: streamed loads retain ingestion duration, throughput, parser-starvation time and percentage, organization duration, and total duration. The production log retains the legacy load-performance fields.
- Added Rust coverage: the two legacy success/cancellation cases plus byte-at-a-time streaming, cancellation before worker startup, cancellation while waiting, queue shutdown, EOF finality, malformed/incomplete EOF, and nonblocking submission while a worker is delayed.

### 5.3 WASM loading session — Complete

- Rust: shared threaded implementation in `src/trace/session.rs`
- Legacy: loading portions of `src/ztracing_wasm.c`, `src/platform_wasm.c`, and `src/trace_load_task.c`
- Review: raw-buffer ownership, buffered-byte reporting, repeated sessions, cancellation, and memory growth.
- Verified design: WASM transfers each valid positive-size allocation into one owned Rust `Vec<u8>`, reports live queued bytes after submission, and applies the 32 MiB producer limit asynchronously in JavaScript so the browser main thread is never blocked on parser throughput.
- Verified lifecycle: replacing or explicitly cancelling a session releases queued buffers, stream failures cancel the matching Rust session, and completions are adopted only when their task ID still belongs to the active session.
- Verified memory handling: JavaScript refreshes its view from the current WebAssembly memory buffer, checks allocation and bounds failures, and frees allocations on every path before ownership transfer. Rust accepts a null chunk only for zero-sized EOF.
- Added Rust coverage: strict raw-buffer validation and ownership transfer, delayed-worker submission and accounting, cancellation cleanup, stale chunks, matching-session cancellation, and an already-completed stale load that was queued before session replacement.
- Added JavaScript coverage with Node's built-in test framework: successful transfer and EOF, memory growth, allocation and copy failures, stream-error cancellation, and asynchronous producer backpressure.
- Added a hermetic Playwright/Chromium integration test installed through `aspect_rules_js`. It runs the production threaded WASM bundle with cross-origin isolation and covers multi-chunk loading, actual WASM memory growth, session replacement, cancellation, buffered-byte cleanup, and detection of thread-pool exhaustion, deadlocks, traps, and unexpected browser errors.

### 5.4 Search task — Complete

- Rust: `src/trace/search.rs`
- Legacy: `src/trace_search_task.c`, `src/trace_search_task.h`
- Legacy tests: `src/trace_search_task_test.cc`
- Rust tests: inline in `src/trace/search.rs`
- Review: matching semantics, filters, highlighted results, cancellation, and result ordering.
- Verified matching: Rust preserves ASCII case-insensitive substring matching across event names, categories, and string argument values; thread/counter filters retain the legacy metadata exception; scan results remain in event-index order; and cancellation is polled every 2,048 events.
- Verified lifecycle: search results are adopted only when both task identity and generation match the active search. Starting a new trace invalidates and cancels the old search, including the already-completed-but-not-yet-polled race.
- Verified UI behavior: search input grows dynamically, Enter resubmits the current query, and only active or pending full-query scans display the legacy search status. Name/category/start/duration sorting and histogram filtering run in display-only background jobs without displaying the search status and produce a separate list while the timeline retains its index-sorted highlight set. Match indices use shared ownership, so submitting a display update is O(1) and does not rescan the trace.
- Added coverage: all matching fields and filter combinations, empty queries, cancellation during scanning and display derivation, result ordering in both directions, background duration filtering, stale and superseded completions, highlight ordering under background table sorting, progress-status behavior for full scans versus sorting and duration filtering, inputs beyond the initial capacity, Enter resubmission, deterministic search completion in golden tests, and active-search session replacement.

## 6. Formatting and CLI

### 6.1 General formatting — Complete

- Rust: `src/format.rs`
- Legacy: `src/format.c`, `src/format.h`
- Legacy tests: `src/format_test.cc`
- Review: units, rounding, negative durations, tick intervals, and boundary values.
- Verified equivalence: finite duration formatting retains the legacy interval-selected units, two-decimal rounding with trailing-zero removal, negative values, and zero representation. Tick intervals retain the clamped `1/2/5 × 10^n` algorithm and minimum interval.
- Added coverage: every legacy assertion plus exact unit transitions, negative intervals, exact mantissa thresholds, sub-microsecond clamping, negative dimensions, and large finite values.

### 6.2 CLI table rendering — Complete

- Rust: `src/cli_table.rs`
- Legacy: `src/cli_table.c`, `src/cli_table.h`
- Legacy tests: `src/cli_table_test.cc`
- Review: UTF-8 display width, truncation, spacing, terminal width, and empty cells.
- Verified rendering: dynamic and fixed columns, proportional shrinking, minimum widths, left/right alignment, Unicode-scalar truncation, ellipsis rules, separators, and absent cells retain the legacy output behavior.
- Verified terminal sizing: `COLUMNS` remains the highest-priority override, redirected output remains unlimited, supported native TTYs query their actual window width, and an unavailable native size falls back to 80 columns.
- Approved behavior: `COLUMNS` must be a complete positive integer. Rust rejects whitespace, numeric prefixes, zero, and negative values instead of retaining `atoi` partial parsing.
- Added coverage: all legacy cases plus right-aligned truncation, missing and empty cells, multiple dynamic columns, extremely narrow terminals, zero-to-three-character truncation, Unicode boundaries, and invalid `COLUMNS`.

### 6.3 Native CLI — Complete

- Rust: `src/cli.rs`
- Legacy: `src/ztracing_cli.c`
- Legacy tests: `src/ztracing_cli_test.cc`
- Rust tests: inline in `src/cli.rs`
- Golden data: `src/testdata/cli/*.golden`
- Review carefully: arguments, defaults, commands, errors, stdout/stderr separation, exit status, deterministic ordering, and raw/gzip behavior.
- Verified commands and I/O: every legacy CLI golden scenario is migrated for summary, inspect, query, concurrency, aggregate, diff, and histogram. Raw and gzip loading, errors, exit statuses, deterministic equal-timestamp query ordering, and strict stdout/stderr separation are covered.
- Verified legacy edge behavior: help flags in required filename positions print usage; histogram accepts a missing track as an empty result, combines duplicate track names, and ignores the query-only `--max-depth` option.
- Approved behavior: integer options use complete, checked Rust parsing instead of `atoi`/`atoll`. Malformed values and overflow are errors; bucket counts must be positive; limits, maximum depth, and minimum counts must be non-negative; timestamps may remain negative.

## 7. Viewer calculations and rendering

### 7.1 Viewer state and interaction model — Complete

- Rust: `src/viewer/model.rs`
- Legacy: `src/trace_viewer.c`, `src/trace_viewer.h`
- Legacy tests: `src/trace_viewer_test.cc`
- Rust tests: inline in `src/viewer/model.rs`
- Review carefully: pan/zoom, selection, focus, coordinate conversion, minimap, filtering, and keyboard/mouse behavior.
- Fixed interaction lifecycle and isolation: track-boundary drags now terminate on release, releases outside the timeline content do not mutate focus/selection, and zero-width selections do not constrain zoom.
- Restored legacy minimap interaction: clicking outside the slider jumps to a track, clicking inside captures without jumping, dragging scrolls proportionally, and the slider retains its minimum size and boundary alignment.
- Restored direct two-axis track navigation: left-drag continues to pan time horizontally and now also scrolls the track child vertically, matching the legacy draw-phase behavior.
- Fixed selection equivalence: box selection is lane-aware, ignores culled tracks and counter headers, includes long overlapping events, refreshes histogram/table state, and schedules table filtering/sorting on the background executor.
- Matched selection edge cases: proximity uses the legacy strict five-pixel boundary, resolves overlapping handles to the closest edge, and box selection completes whenever the mouse is no longer down so a lost release event cannot leave it stuck.
- Restored legacy reset and double-click behavior: resetting the viewport preserves interaction state, adopting a new trace clears it explicitly, and only thread events zoom on double-click.
- Restored production UI feedback: selection boundaries use the resize cursor, track headers expose PID/TID tooltips, counter blocks receive hover shading, and snapping uses the legacy red three-pixel guide.
- Counter hover, selection, and focus highlights use the same merged interval bounds.
- Restored platform modifiers: Command is the primary modifier on macOS and Ctrl is used elsewhere for viewer zoom and search shortcuts.
- Removed per-frame work that scales with all events: minimap heatmaps and selected-track markers are cached, and renderer selection state is rebuilt only when the selected-event snapshot changes.
- Migrated focused coverage for hit testing, focus, pan/zoom constraints, exact selection proximity, snapping, drag lifecycle, box selection, track layout/names, ruler/selection layout, zero-duration events, and minimap behavior. Headless goldens now cover direct vertical dragging, track-header tooltips, counter hover feedback, and minimap slider dragging through the production renderer and input path.

### 7.2 Track renderer — Complete

- Rust: `src/viewer/renderer.rs`
- Legacy: `src/track_renderer.c`, `src/track_renderer.h`
- Legacy tests: `src/track_renderer_test.cc`
- Rust tests: inline in `src/viewer/renderer.rs`
- Review carefully: bucketing, event visibility, counters, focused events, floating-point thresholds, and draw order.
- Restored legacy counter bucketing: carried-value gap buckets retain their stable three-pixel boundaries, so hover, selection, and focus highlights have matching dimensions.
- Restored counter drawing details: every series retains the legacy one-pixel minimum visual height, including zero, negative, and very small values.
- Restored renderer scaling behavior: spanning-thread scans skip irrelevant 1,024-event blocks using cached maximum durations, per-bucket thread scratch is reused, and counter peaks use one flat buffer rather than one allocation per rendered block.
- Migrated all 38 legacy `track_renderer_test.cc` scenarios, including counter viewport boundaries, gap state, peak preservation, stable panning, threshold jitter, spanning-event correctness/performance, and focused-event retention.
- Approved Rust safety behavior: timestamp-end arithmetic saturates instead of overflowing, invalid selected-event indices are ignored, and non-positive viewport dimensions return no blocks.

### 7.3 Viewer module API — Complete

- Rust: `src/viewer/mod.rs`
- Legacy: `src/trace_viewer.h`, `src/track_renderer.h`
- Review: public surface and state ownership.
- Replaced the implementation-shaped `viewer::model` and `viewer::renderer` public modules with a facade that exposes `viewer::Viewer`, `viewer::TrackRenderer`, and the renderer result types.
- Made mutable viewer state and supporting layout/input types private to `Viewer`. `App`, as the viewer's production user, can no longer mutate tracks, viewport state, selections, or renderer-dependent caches independently.
- Exposed the App-facing `Viewer` operations as public methods. Production search adoption, filtering, clearing, focus requests, details visibility, and release suppression use those methods so coupled state updates remain owned by the viewer.
- Preserved the legacy ownership model with Rust lifecycle safety: `App` owns one viewer, the viewer owns tracks and render/minimap scratch storage, trace data is borrowed while stepping/drawing, and `Drop` replaces explicit initialization/deinitialization.
- Kept the renderer directly available under the explicit `TrackRenderer` name for `tools/trace_renderer_benchmark` without exposing the internal source-file module structure.
- Removed copied-but-unused input/layout fields (`click_y`, `mouse_delta_y`, ruler relative timestamps, and minimap width). Production drawing and headless interaction tests share the slider bounds computed by `Viewer` instead of reconstructing geometry from exposed layout dimensions.

## 8. UI, platform, and FFI

### 8.1 Colors and themes — Complete

- Rust: `src/colors.rs`
- Legacy: `src/colors.c`, `src/colors.h`
- Review: exact color values, theme changes, ImGui style assignments, and ABI layout.
- Preserved the packed RGBA channel order, dark/light derivation, status color conversion, and eight-color event palettes.
- Restored the `View > Theme` Auto/Dark/Light choices and browser persistence. Auto follows operating-system changes while forced modes ignore them.
- Routed theme changes through `Runtime`, which applies the shared ImGui style after the active frame ends and schedules the follow-up frame.
- Kept the existing C++ ImGui style assignments as the single production implementation and added matching Rust/C++ ABI size, alignment, and offset checks.
- Added Rust coverage for theme-mode behavior and a threaded-WASM browser test for restoring and rendering persisted themes.

### 8.2 ImGui binding and safe wrapper — Complete

- Rust: `src/imgui.rs`
- Retained C++: `src/imgui_c.cc`, `src/imgui_c.h`, `src/imgui_types.h`
- Review carefully: `repr(C)` layouts, enum values, pointer lifetimes, string termination, variadic replacements, begin/end pairing, and allocator callbacks.
- Added Rust and C++ ABI checks for shared vector layouts and compile-time checks for all ImGui enum and flag values used by Rust.
- Replaced Rust variadic text calls with explicit pointer-and-length C++ entry points. Display APIs accept `&str`, preserve embedded NUL bytes where ImGui supports explicit ranges, and use valid pointers for empty strings.
- Changed editable text fields to accept `&mut String` while keeping ImGui's resizable byte buffer and UTF-8 validation internal to the wrapper.
- Bound borrowed viewport, draw-list, and list-clipper handles to the active frame and made `Context` retain font bytes for the full lifetime ImGui may reference them.
- Enforced a single active ImGui context and replaced manual begin/end, style push/pop, tooltip, group, clipper, and draw-list flag management with scoped drop guards.
- Kept ImGui allocations routed through the paired `CountingAllocator::foreign_alloc` and `foreign_free` callbacks.
- Added coverage for shared ABI layouts, label termination and hidden IDs, empty and embedded-NUL text, editable buffer resizing and termination, and invalid UTF-8 repair.

### 8.3 Platform layer — Complete

- Rust: `src/platform.rs`
- Legacy: `src/platform_common.c`, `src/platform_native.c`, `src/platform_headless.c`, `src/platform_wasm.c`, `src/platform.h`
- Review: worker creation, time, clipboard, browser callbacks, redraw scheduling, thread behavior, and conditional compilation.
- Preserved monotonic timing, native/headless dark-theme defaults and settings stubs, compile-target macOS detection, and browser queries for time, main-thread identity, color scheme, and platform.
- Kept native main-thread identity tied to explicit application initialization so a worker cannot register itself through the query path.
- Preserved browser settings under the `ztracing_` local-storage namespace and restored legacy file-picker validation and error reporting for missing or failed stream loaders.
- Kept the approved shared two-worker Rust executor for loading and search. Native uses standard threads, threaded WASM uses the pinned atomics-enabled standard library and Emscripten pthread pool, and teardown is automatic through Rust ownership.
- Added Rust and JavaScript coverage for clock behavior, primary-modifier selection, native/worker and browser/pthread identity, local-storage round trips, and file-picker failure handling.
- Added production-browser coverage that selects and loads a real trace through the file picker, verifies automatic color-scheme changes, and continues to detect worker-pool exhaustion, deadlocks, traps, and unexpected browser errors.

### 8.4 Headless OpenGL context — Complete

- Rust: `src/headless_gl.rs`
- Legacy: `src/headless_gl_linux.c`, `src/headless_gl.h`
- Review: EGL/GLES constants and signatures, initialization sequence, framebuffer behavior, teardown, and error handling.
- Verified the handwritten Linux EGL/GLES signatures and constants against the system headers and preserved the RGBA8 ES3 pbuffer, framebuffer, and color-renderbuffer initialization sequence.
- Preserved an explicitly configured `EGL_PLATFORM`; otherwise context creation selects the legacy surfaceless default before application worker threads start.
- Made the safe wrapper exclusively own one active context, with private GL handles and dimensions, automatic reservation release after failed construction, and dimension getters for the headless harness.
- Preserved reverse lifecycle cleanup and captured EGL errors before cleanup calls can alter diagnostics. Incomplete-framebuffer errors now include the reported GL status.
- Added focused coverage for invalid dimensions and exclusive context lifetime. The shared Rust golden suite exercises successful creation, rendering, RGBA readback, teardown, and repeated context recreation.

### 8.5 Headless test harness and golden images — Complete

- Rust: `src/headless.rs`, `src/golden.rs`
- Legacy: `src/ztracing_headless.c`, `src/ztracing_test.cc`
- Goldens: `src/testdata/*.bmp`
- Review: scenario setup, frame stabilization, pixel capture, BMP encoding, tolerance, teardown, and leaks.
- Migrated the missing welcome-screen scenario and verified that the Rust and legacy harnesses exercise equivalent production rendering paths and golden scenarios.
- Added bounded configurable waits for asynchronous loading and search stabilization, while leaving production loading unconstrained by test timeouts or chunk counts.
- Hardened BMP decoding, encoding, framebuffer-dimension validation, buffer-length checks, failed-image output, and the shared 0.2% pixel-difference threshold.
- Restored the original counter-hover scenario and golden, tightened session-replacement assertions, and added malformed-image and non-default-dimension coverage.
- Added process-allocation accounting around repeated headless application creation, rendering, and teardown to detect retained Rust allocations.
- Verified the complete Rust golden suite, the dedicated lifecycle test, and the retained legacy golden suite.

### 8.6 Application UI — Complete

- Rust: `src/app.rs`
- Legacy: `src/app.c`, `src/app.h`, `src/loading_screen.c`, `src/loading_screen.h`, `src/welcome_screen.c`, `src/welcome_screen.h`
- Review: application states, task completion, loading/welcome screens, settings, redraw decisions, and shutdown.
- Preserved loading, welcome, timeline, theme-mode, power-save, redraw, stale-completion, cancellation, session-replacement, and Rust ownership-based shutdown behavior.
- Made load-session creation non-blocking so a full shared task queue cannot deadlock the UI thread, with deterministic one-slot queue-exhaustion coverage.
- Restored keyboard focus for the `Tools > Search Events` action and suppressed application shortcuts while an ImGui text input is active.
- Replaced unbounded application-test polling with bounded waits that protect tiny fixtures without constraining production loading.
- Verified the Rust unit and golden suites, headless lifecycle coverage, retained legacy suite, production WASM build, JavaScript bridge tests, and browser end-to-end tests.

### 8.7 Crate root — Complete

- Rust: `src/lib.rs`
- Legacy counterpart: public declarations in `src/ztracing.h` and related headers
- Review: module exports, feature/platform selection, and initialization boundaries.
- Kept native file loading excluded from Emscripten, Linux headless modules and goldens gated to Linux, and the reviewed viewer and string-interner module boundaries intact.
- Kept production allocation accounting opt-in at the binary level while the library selects `CountingAllocator` only for its own tests.
- Intentionally omitted the unused legacy `ztracing_deinit` no-op and `ztracing_get_allocated_bytes` function from the Rust production ABI; Rust ownership handles teardown and allocation telemetry is consumed directly.
- Documented `ztracing.h` as a legacy C/C++ comparison header that remains only while the retained implementation and tests require it.
- Made Linux EGL/GLES dependencies conditional so portable Rust tests remain buildable on macOS, and marked the dedicated headless lifecycle test Linux-only.
- Verified native Rust and retained legacy tests, the production WASM build, formatting, and whitespace checks.

## 9. WebAssembly integration

### 9.1 WASM entrypoint — Complete

- Rust: `src/ztracing_wasm.rs`
- Legacy: `src/ztracing_wasm.c`, relevant parts of `src/ztracing.h`
- JavaScript: `src/ztracing_bridge.js`, `src/ztracing.js`
- Review very carefully: exported names and signatures, global state, allocation/free contract, chunk ownership, font upload, session replacement, theme updates, and main-loop behavior.
- Verified ABI and lifecycle: production exports and linker symbols agree; initialization and animation-loop startup are idempotent; calls made before initialization are handled safely.
- Verified ownership and streaming: JavaScript allocations transfer exactly once, failed copies are reclaimed, stale sessions release their buffers, browser readers are cancelled immediately on replacement, and raw/gzip streams preserve progress and backpressure behavior.
- Verified UI integration: font and theme updates request redraws, initialization failures alone use JavaScript `onError`, and all later browser, parser, queue, and search failures are presented by the Rust error popup with golden coverage.
- Verified tests: Rust unit/golden tests, JavaScript bridge tests, and the production threaded-WASM browser E2E suite pass.

### 9.2 Retained backends — Complete

- Retained C++: `src/imgui_impl_wasm.cc`, `src/imgui_impl_wasm.h`, `src/imgui_impl_webgl.cc`, `src/imgui_impl_webgl.h`
- Rust callers: `src/imgui.rs`, `src/ztracing_wasm.rs`, `src/headless.rs`
- Review: ABI compatibility, context/thread assumptions, and initialization/shutdown ordering.
- Verified ABI compatibility: retained C/C++ declarations and Rust callers use matching fixed signatures and integer status values.
- Verified lifecycle: initialization is transactional, partial failures release renderer and WebGL resources, shutdown unregisters WASM callbacks and listeners, and repeated headless lifecycles return allocations to their baseline.
- Verified context and ordering assumptions: WebGL is current before renderer initialization, the platform backend is initialized afterward, and failure cleanup runs before destroying the ImGui context.
- Verified tests and builds: Rust and legacy native tests, JavaScript bridge tests, the production threaded-WASM browser E2E suite, and both migrated and legacy Emscripten builds pass.

## 10. Benchmarks and build graph

### 10.1 Parser benchmark — Complete

- Rust: `tools/trace_benchmark.rs`
- Legacy: `tools/trace_benchmark.cc`
- Review: workload, iterations, input data, and reported units.
- Verified workload and input parity: native files are streamed through the production `LoadSession` parser on a standard executor with bounded backpressure, while raw and gzip inputs retain magic-based detection and one measured iteration.
- Verified measurement parity: the compression probe reads only its two-byte prefix, compaction is included in ingestion time, and disk/decompressed throughput, event rate, organization time, total time, and consumed memory use the legacy units and labels.
- Verified coverage and build: raw and gzip report tests cover fields and units, the prefix-read test prevents full-file probing, the benchmark target builds, and the repository-wide test suite passes.

### 10.2 Renderer benchmark — Complete

- Rust: `tools/trace_renderer_benchmark.rs`
- Legacy: `tools/trace_renderer_benchmark.cc`
- Review: generated trace, viewport, rendering workload, warmup, and measurements.
- Verified viewport parity: up to 25 contiguous tracks are selected using the first maximum-event block, with the full trace time range, a 1000-pixel width, zero canvas offset, and no focused event; empty traces are rejected explicitly.
- Verified workload and warmup: one untimed production-style frame stabilizes the shared renderer buffers before 100 measured thread/counter render-block frames.
- Verified measurements and coverage: total and average frame times retain millisecond units, and tests cover tie-breaking, warmup count, empty traces, mixed track dispatch, viewport reporting, and output units.
- Verified build and integration: the benchmark target builds and the repository-wide test suite passes.

### 10.3 Bazel integration — Complete

- Files: `MODULE.bazel`, `MODULE.bazel.lock`, `base/BUILD`, `src/BUILD`, `tools/BUILD`, root `BUILD`
- Review: stable target labels, source selection, native/WASM flags, pthreads, memory limits, exports, zlib and ImGui linkage, artifacts, and test data.
- Verified target graph and naming: production labels select Rust implementations without language-specific suffixes, retained legacy targets use `_cc`, and no legacy application target is present in the production dependency graph.
- Verified platform support: threaded WASM uses a custom atomic standard library and host-specific Rust toolchains for Linux x86-64 and macOS ARM64; incompatible WASM and legacy headless targets are excluded from ordinary native configurations.
- Verified linkage and runtime settings: native and WASM dependency selection preserves zlib and ImGui linkage, pthread configuration, memory limits, exports, assertions, and stack-overflow checks.
- Verified artifacts and coverage: native and optimized WASM builds produce the stable CLI and four-file browser bundle, `bazel build //...` succeeds, and all 32 repository tests pass.

## Recommended high-risk review focus

The following components deserve the most intensive review:

1. `base/task.rs`
2. `base/json.rs`
3. `src/trace/parser.rs`
4. `src/trace/loader.rs`
5. `src/trace/session.rs`
6. `src/viewer/model.rs`
7. `src/viewer/renderer.rs`
8. `src/imgui.rs`
9. `src/platform.rs`
10. `src/ztracing_wasm.rs`

For each comparison, establish functional identity by verifying observable behavior, edge cases, ownership and cleanup, concurrency behavior, deterministic ordering, error behavior, and coverage parity before marking the item complete.
