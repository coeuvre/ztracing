# ztracing Full Rust Migration Plan

## 1. Purpose

This document defines the complete migration of ztracing from repository-owned C/C++ to idiomatic Rust while preserving all current behavior, build products, interfaces, tests, and package boundaries.

The migration is a correctness-preserving rewrite, not a redesign of the product. Internal APIs and ownership models should become idiomatic Rust, but externally observable behavior must remain unchanged.

The final migration will be submitted as one commit. Intermediate states may contain both implementations in the working tree while the old implementation is used as a behavioral oracle, but no intermediate migration commits are required or desired.

---

## 2. Decisions and hard constraints

The following decisions are fixed for this migration.

### 2.1 Build system

- Bazel remains the only build system.
- No Cargo workspace will be added.
- No `Cargo.toml` or `Cargo.lock` files will be added.
- `rules_rust` will be added as the Bazel rule set required to compile Rust.
- Bazel remains authoritative for:
  - source lists,
  - crate boundaries,
  - target selection,
  - native and WebAssembly linking,
  - test data/runfiles,
  - compiler flags,
  - CI,
  - release artifacts.
- rust-analyzer support should be provided through Bazel-generated `rust-project.json` metadata rather than a duplicate Cargo build graph.

Maintaining Cargo and Bazel simultaneously would duplicate native link settings, Emscripten settings, source lists, target features, test data, and build modes. It would be especially fragile around ImGui C++ linkage and `wasm32-unknown-emscripten`, so the project will use Bazel only.

### 2.2 Rust dependencies

- No third-party Rust libraries may be introduced unless separately approved.
- Production Rust code will use only the Rust standard library.
- Tests will also avoid third-party Rust test/helper libraries.
- All foreign interfaces will use handwritten Rust FFI declarations; bindgen will not be used.
- `rules_rust` is a Bazel build dependency, not a runtime Rust library.

### 2.3 Existing third-party dependencies

The current third-party dependencies remain unchanged in source and version:

- Dear ImGui remains C++.
- Emscripten remains the WebAssembly toolchain/runtime.
- zlib remains the gzip implementation.
- EGL and GLES remain system libraries for headless rendering.
- Existing Bazel C/C++ rules remain available for retained C++ bindings.
- GoogleTest may remain declared even though the final test code will no longer use it.

The owned-code migration requirement does not require rewriting external libraries in Rust.

### 2.4 Owned C/C++ allowlist

At the end of the migration, the only repository-owned C/C++ code allowed is the ImGui C binding and the ImGui platform/renderer bridges:

```text
src/imgui_c.cc
src/imgui_c.h
src/imgui_impl_wasm.cc
src/imgui_impl_wasm.h
src/imgui_impl_webgl.cc
src/imgui_impl_webgl.h
src/imgui_types.h          # May be merged into imgui_c.h.
```

`imgui_types.h` should preferably be merged into `imgui_c.h` if that simplifies the final ABI and reduces the allowlist.

The following do **not** remain as owned C/C++:

- core utilities,
- task scheduler,
- native logging,
- WebAssembly logging,
- native/headless platform code,
- Emscripten application entrypoint,
- EGL/headless context code,
- trace parser/data/analytics,
- loader and zlib integration,
- UI/application code,
- CLI,
- tests,
- benchmarks.

Browser-only bridge code may remain JavaScript. A new Emscripten JS library is allowed because the restriction concerns owned C/C++.

### 2.5 Package and crate structure

The old `core/` package will be replaced by a top-level `base/` Bazel package and Rust crate:

- package: `//base`
- crate name: `base`
- primary target: `//base:base`

This avoids conflict with Rust's built-in `core` crate.

The application remains under `//src`, and benchmarks remain under `//tools`.

The final repository will not retain a `core/` directory merely for aliases. Existing `//core:*` internal labels are allowed to disappear. User-facing build labels and artifact paths must remain stable.

### 2.6 Compatibility

The migration must not intentionally change behavior. It must preserve:

- CLI commands, options, defaults, output, errors, exit statuses, and golden files;
- WebAssembly exports and JavaScript integration;
- browser streaming and backpressure behavior;
- gzip handling;
- trace parsing behavior, including malformed/incomplete input behavior;
- task scheduling, stream serialization, cancellation, and completion behavior;
- theme and settings behavior;
- allocation telemetry behavior;
- UI interactions and rendering;
- screenshot golden results and tolerance;
- target artifact names and documented commands;
- the trace-analyzer skill's assumptions about the CLI.

### 2.7 Unsafe Rust

There is no crate-wide `deny(unsafe_code)` requirement. Unsafe Rust is nevertheless restricted by design to places where it has a concrete reason:

- C/C++ FFI calls;
- Emscripten FFI and exported WebAssembly ABI;
- zlib FFI;
- EGL/GLES FFI;
- raw buffers transferred to/from JavaScript;
- ImGui allocator callbacks;
- allocation accounting internals;
- narrowly scoped global entrypoint state where required by the C/JS ABI.

Every unsafe block must include a nearby `SAFETY:` comment that states its preconditions and why they hold. Safe wrappers must prevent FFI invariants from leaking into normal application code.

### 2.8 Rust version

- Use the latest stable Rust release available when implementation begins.
- Pin that exact release in Bazel for reproducibility.
- Use Rust edition 2024.
- Updating to a newer stable Rust after migration is a separate routine toolchain update.

### 2.9 Performance

- Correctness is the migration gate.
- Existing performance-oriented architecture such as streaming, compact data, and background work should be retained where practical.
- Benchmark tools will be migrated so they continue to build and run.
- Performance comparison, SIMD restoration, and optimization are deferred until after migration.
- A benchmark regression alone does not block this migration unless it reveals a functional issue such as inability to load realistically large traces.
- Performance work after the correctness migration is described in [Section 32](#32-future-performance-improvement-plan).

---

## 3. Current system inventory

The current repository contains approximately:

- 12.4k lines of production C/C++;
- 11.7k lines of C++ tests;
- 23 Bazel test targets;
- 40 C/C++ binary/library targets;
- a native CLI;
- a browser application built with Emscripten, WebGL2, and pthreads;
- a Linux headless EGL/GLES screenshot harness;
- a custom incremental JSON parser;
- a custom allocator/arena/dynamic-array/hash-table layer;
- a custom task scheduler with serialized streams and cancellation;
- streaming trace ingestion with gzip support and backpressure;
- trace organization, rendering, search, histogram, heatmap, aggregate, concurrency, and diff logic.

The highest-risk migration areas are:

1. linking Rust, C++, Dear ImGui, zlib, and Emscripten into one WebAssembly product;
2. preserving the task scheduler's concurrency and cancellation semantics;
3. preserving parser behavior across arbitrary chunk boundaries;
4. preserving pixel-level UI output;
5. preserving raw pointer ownership across JavaScript/WebAssembly;
6. preserving deterministic CLI output without depending on randomized map iteration.

---

## 4. Target repository layout

The exact module split may be adjusted while implementing, but the intended final layout is:

```text
BUILD
MODULE.bazel
MODULE.bazel.lock
README.md
format.sh
rust_migration_plan.md

base/
  BUILD
  lib.rs
  allocation.rs
  json.rs
  logging.rs
  task.rs
  tests/
    allocation_test.rs
    json_test.rs
    task_test.rs

src/
  BUILD
  lib.rs
  app.rs
  cli.rs
  cli_table.rs
  colors.rs
  format.rs
  loading_screen.rs
  platform.rs
  welcome_screen.rs

  trace/
    mod.rs
    aggregate.rs
    concurrency.rs
    data.rs
    diff.rs
    heatmap.rs
    histogram.rs
    load_task.rs
    loader.rs
    parser.rs
    search_task.rs
    track.rs

  viewer/
    mod.rs
    renderer.rs
    input.rs
    layout.rs

  ffi/
    mod.rs
    imgui.rs
    zlib.rs
    egl.rs
    gles.rs
    emscripten.rs

  ztracing_headless.rs
  ztracing_wasm.rs

  imgui_c.cc
  imgui_c.h
  imgui_impl_wasm.cc
  imgui_impl_wasm.h
  imgui_impl_webgl.cc
  imgui_impl_webgl.h

  ztracing.js
  emscripten_bridge.js
  shell.html
  coi-serviceworker.js

  tests/
    cli_table_test.rs
    format_test.rs
    trace_aggregate_test.rs
    trace_concurrency_test.rs
    trace_data_test.rs
    trace_diff_test.rs
    trace_heatmap_test.rs
    trace_histogram_test.rs
    trace_load_task_test.rs
    trace_parser_test.rs
    trace_search_task_test.rs
    trace_viewer_test.rs
    track_renderer_test.rs
    track_test.rs
    ztracing_cli_test.rs
    ztracing_test.rs

  testdata/
    ... unchanged golden files ...

tools/
  BUILD
  generate_mock_trace.py
  serve.py
  trace_benchmark.rs
  trace_renderer_benchmark.rs
```

The final source tree must not contain stale C headers for migrated Rust modules.

---

## 5. Bazel design

## 5.1 Module dependencies

`MODULE.bazel` will:

- retain the current `rules_cc`, Emscripten, zlib, and ImGui setup;
- add `rules_rust`;
- register a pinned latest-stable Rust toolchain;
- make native and `wasm32-unknown-emscripten` Rust standard libraries available;
- retain the existing Emscripten version and C/C++ toolchain setup.

No crate repository rules such as `crate_universe` are needed because there are no external Rust crates.

## 5.2 `//base` targets

At minimum:

```text
//base:base
//base:allocation_test
//base:json_test
//base:task_test
```

Small tests can be grouped if that produces simpler Bazel definitions, but target names should remain descriptive.

## 5.3 `//src` targets

The intended target graph is:

```text
//src:ztracing_lib             Rust library containing domain and app logic
//src:ztracing                 Native Rust CLI binary
//src:ztracing_wasm_entrypoint Rust static library for Emscripten linkage
//src:ztracing_wasm_cc         Emscripten C++ link target
//src:ztracing_wasm            Existing wasm_cc_binary wrapper
//src:ztracing_headless        Rust headless integration library/test support
//src:imgui_c                  Retained C++ binding
//src:imgui_impl_wasm          Retained C++ platform backend
//src:imgui_impl_webgl         Retained C++ renderer backend
```

The root `//:ztracing` bundle remains responsible for producing:

```text
bazel-bin/ztracing/index.html
bazel-bin/ztracing/ztracing.js
bazel-bin/ztracing/ztracing.wasm
bazel-bin/ztracing/coi-serviceworker.js
```

The native CLI remains:

```text
bazel-bin/src/ztracing
```

## 5.4 Rust/C++ linkage

The preferred WebAssembly link path is:

1. Compile `src/ztracing_wasm.rs` and its Rust dependencies as a Rust static library for `wasm32-unknown-emscripten`.
2. Expose that archive to Bazel as `CcInfo` through `rust_static_library`.
3. Link it into the existing Emscripten `cc_binary` together with Dear ImGui and the retained C++ backends.
4. Let Emscripten perform the final link and JS glue generation.
5. Keep the existing `wasm_cc_binary` wrapper and root bundling process.

This link path must be proven before broad source migration because it is the largest build-system risk. The proof must verify:

- Rust exports are retained from the static archive;
- C++ can call Rust callbacks;
- Rust can call C++ binding functions;
- Rust `std` works with Emscripten pthreads;
- the final module exports the current symbol list;
- memory growth and pthread settings remain compatible;
- no second allocator/runtime is accidentally introduced in a way that breaks ownership.

If `rules_rust` requires explicit target configuration, Bazel transitions/selects must ensure that the Rust archive uses `wasm32-unknown-emscripten` whenever the Emscripten C++ target is selected.

## 5.5 Native linkage

Native Rust targets will link against C++ and system libraries through Bazel:

- Dear ImGui and `imgui_c`;
- `imgui_impl_webgl`;
- EGL;
- GLESv2/GLES3 as currently configured;
- zlib;
- the C++ standard library as required by Dear ImGui.

## 5.6 Build modes

Preserve the current distinctions:

- native CLI;
- native/headless test build selected by `--define=headless=true`;
- Emscripten browser build;
- Emscripten tags excluded from normal native `bazel test //...`.

Rust code should use Bazel-provided `--cfg` values or separate crate roots rather than broad runtime conditionals where target-specific code differs significantly.

## 5.7 Developer tooling

- Update `format.sh` to detect changed `.rs` files and invoke pinned `rustfmt`.
- Continue `clang-format` only for the retained C++/header allowlist.
- Add a Bazel-supported way to generate `rust-project.json` for rust-analyzer.
- Add Rust formatting and compilation to CI.
- Do not require Cargo for formatting, tests, IDE metadata, or builds.

---

## 6. Base crate design

The `base` crate contains reusable infrastructure but should not mechanically reproduce C APIs that Rust no longer needs.

## 6.1 Allocation

### Goals

- Use normal Rust ownership in application code.
- Preserve allocation telemetry exposed to the UI and tests.
- Support raw buffers handed to JavaScript.
- Support the ImGui allocator callbacks.
- Avoid a general-purpose C-style allocator argument throughout Rust APIs.

### Design

Most Rust values use `Vec`, `String`, `Box`, `Arc`, and standard collections. Allocation tracking is isolated in `base::allocation`.

The accounting design must distinguish app-owned allocations from unrelated test harness or process allocations. A proposed implementation is:

- a process allocator wrapper backed by `std::alloc::System`;
- an application allocation scope/generation;
- an allocation header recording size, alignment, and whether the block is attributed to the active application;
- atomic counters for attributed live bytes;
- propagation of the application allocation scope into worker jobs;
- explicit use by ImGui allocation callbacks and JavaScript buffer allocation.

The exact accounting mechanism may be simplified if tests prove that a less invasive implementation preserves the current observable semantics. It must still support:

- process-wide allocation telemetry for the in-application memory display;
- returning to the pre-initialization allocation baseline after complete headless teardown;
- ownership-correct `ztracing_malloc`/`ztracing_free`;
- no freeing through a different allocator than the one that allocated a block.

### Unsafe justification

Raw allocation and deallocation require unsafe code because `std::alloc` returns raw pointers and because C++/JavaScript callbacks do not carry Rust lifetimes. The unsafe code remains in this module and exposes safe owned wrappers elsewhere.

## 6.2 Removal of arena allocation

The C arena implementation will not be ported as a public allocator unless a concrete algorithm requires it.

Replacement patterns:

- task submission payloads become owned structs;
- task-local scratch data becomes local `Vec`/`String` values;
- batch cleanup happens naturally when a submission/completion value is dropped;
- temporary algorithm buffers are reused by owning structs where beneficial;
- checkpoints become ordinary scoped values or saved vector lengths.

This preserves lifetime behavior while eliminating manual arena lifetime rules.

## 6.3 Dynamic arrays

Every `darray_t(T)` becomes an appropriate Rust type:

- mutable growable storage: `Vec<T>`;
- immutable final storage: `Box<[T]>` where useful;
- byte buffers: `Vec<u8>`;
- optional ownership transfer: moving the `Vec`, not clearing pointers manually;
- queue storage: `VecDeque<T>` or fixed-capacity vectors depending on required semantics.

All capacity-sensitive behavior that affects memory/backpressure must be tested.

## 6.4 Hashing and maps

Use standard `HashMap` where iteration order is not observable. Because there are no external crates:

- use `std::collections::HashMap`;
- provide a small deterministic `BuildHasher` in `base` where reproducibility or current hashing behavior matters;
- never emit user-visible results directly in map iteration order;
- explicitly sort output with complete tie-breakers;
- use indexed lookup structures where string-pool offsets need to avoid duplicate string ownership.

## 6.5 Strings

Replace `string_t` and `string_view_t` with:

- `&str` when input is valid UTF-8 and textual semantics are required;
- `&[u8]` when trace bytes may not be valid UTF-8 or exact byte behavior matters;
- `String` and `Vec<u8>` for owned buffers;
- a type-safe `StringId(u32)` for trace string-pool identifiers;
- explicit formatting through `write!` into `String` where current code uses printf-style construction.

The parser should operate on bytes first. Conversion to UTF-8 must not introduce new rejection behavior if the C implementation accepted arbitrary bytes.

## 6.6 Logging

Provide a small standard-library-only logging facade:

- native sink: stderr;
- WebAssembly sink: imported functions implemented by `emscripten_bridge.js` or the retained Emscripten bridge layer;
- levels: debug, info, warning, error;
- preserve compile-time debug/release filtering closely enough that normal output does not change;
- avoid variadic FFI.

## 6.7 Incremental JSON reader

Implement a safe byte-oriented tokenizer preserving the current token model:

- object/array delimiters;
- strings;
- integer and floating-point numbers;
- booleans;
- null;
- colon and comma;
- EOF versus parse error;
- token source ranges for values whose original text must be retained.

Correctness requirements include:

- arbitrary chunk boundaries;
- escaped quotes and backslashes;
- incomplete strings/numbers/containers;
- signed integers and floating-point syntax currently accepted;
- overflow behavior matching current tests;
- nested object/array source slices used for trace argument values;
- no out-of-bounds scanning.

The first Rust implementation is scalar and safe. SSE/NEON/WASM SIMD is deferred.

## 6.8 Task scheduler

### Required behavior

The Rust scheduler must retain the semantics tested by `core/task_test.cc`:

- bounded submission and completion capacity;
- preparation of one or more submissions before batch submission;
- stream `0` tasks are independent and may run in parallel;
- nonzero stream tasks run sequentially;
- same-stream tasks should continue on the same worker execution path where the current scheduler guarantees/cache-optimizes this;
- cancellation by stream;
- cancellation by submission identity;
- active tasks receive a cooperative abort flag;
- pending cancelled tasks complete as cancelled without running;
- failure of a serialized task cancels later tasks in that stream;
- cancellation cascades across a serialized stream;
- completion ordering remains compatible;
- completion queue backpressure blocks workers when full;
- owner-thread completion APIs retain their restrictions;
- wait and timed-wait behavior remains compatible;
- destruction requires no active jobs and frees all pending/completed payloads;
- the owner thread must not deadlock trying to post a completion into its own full queue.

### Rust model

Use:

- `Arc<QueueInner>` for shared state;
- `Mutex<QueueState>` for scheduler transitions;
- `Condvar` for completion availability and completion-space availability;
- `AtomicBool` for cooperative cancellation on active execution;
- `VecDeque` for prepared, pending, and completion queues;
- typed task closures or a trait-object task representation with owned `Send + 'static` payloads;
- an execution record for stream, cancellation, status, and completion identity;
- an owner `ThreadId` captured during queue creation.

Task payloads and outputs should be typed at the application layer. The base scheduler should avoid `void*`-style identity, but it must provide a stable submission ID so application code can cancel a specific search/load operation.

### Worker pool

The platform worker pool remains two workers with bounded job submission. It should use standard Rust threads on native and Emscripten pthread support on WebAssembly. The queue-full fallback must not block the Emscripten browser main thread on an operation equivalent to `Atomics.wait`; yielding/spinning behavior must preserve the current safety constraint.

---

## 7. Trace parser migration

## 7.1 Data types

Use a transient borrowed event representation conceptually equivalent to:

```rust
struct TraceArg<'a> {
    key: &'a [u8],
    value: TraceArgValue<'a>,
}

enum TraceArgValue<'a> {
    Text(&'a [u8]),
    Number(f64),
}

struct TraceEvent<'a> {
    name: &'a [u8],
    category: &'a [u8],
    phase: &'a [u8],
    color_name: &'a [u8],
    id: &'a [u8],
    timestamp: i64,
    duration: i64,
    process_id: i32,
    thread_id: i32,
    args: &'a [TraceArg<'a>],
}
```

Exact types may use byte ranges into the parser buffer to make borrowing straightforward.

## 7.2 Parser state

Preserve the current states:

- initial;
- looking for `traceEvents` in an object wrapper;
- inside trace event array;
- complete.

Preserve support for:

- a root array of events;
- an object containing `traceEvents`;
- unknown top-level properties;
- unknown event properties;
- nested argument objects and arrays represented by their original JSON slice;
- integer and floating-point timestamps/durations;
- `pid`/`tid` clamping to `i32`;
- string and numeric event IDs;
- incremental feeding and parser-buffer compaction.

## 7.3 Borrowing API

The parser should expose events whose lifetime is tied to a mutable parser borrow. This statically prevents feeding or advancing the parser while a transient event view is still in use.

If returning a directly borrowed event proves awkward because argument metadata is stored separately, acceptable safe alternatives are:

- return ranges and provide accessors tied to the parser borrow;
- invoke a caller callback with a borrowed event;
- construct a short-lived owned argument vector while keeping string slices borrowed.

Unsafe self-referential storage is not acceptable for parser convenience.

## 7.4 Error compatibility

The existing parser API often reports “no next event” for both incomplete input and malformed input. The Rust internals may use a richer enum, but callers must preserve current behavior unless tests demonstrate a distinction that is already observable.

---

## 8. Trace data and string interning

## 8.1 String references

Define:

```rust
#[repr(transparent)]
#[derive(Clone, Copy, Eq, PartialEq, Hash, Default)]
struct StringId(u32);
```

Semantics:

- `0` means missing/empty;
- nonzero values index the string table using the same conceptual one-based convention;
- accessors return borrowed bytes/string views tied to `TraceData`;
- invalid references are rejected or treated as empty according to current behavior.

## 8.2 String pool

Retain compact pooled storage:

- one byte buffer containing string data;
- one table of offset/length/hash entries;
- a deterministic hash lookup from hash to candidate string references;
- byte comparison against the pooled buffer to resolve collisions;
- fast-path cached references for common category/phase/color/argument keys if still useful.

Avoid storing every interned string twice merely to satisfy `HashMap<String, _>`.

## 8.3 Persisted events

Port all fields exactly:

- name/category/phase/color/id references;
- palette index;
- timestamp and duration;
- process/thread IDs;
- argument offset/count.

Use typed indices/newtypes where they prevent accidental mixing without changing stored numeric widths that affect memory or FFI.

## 8.4 Begin/end matching

Preserve:

- matching by combined process/thread identity;
- independent stacks per thread;
- nested begin/end events;
- duration completion;
- behavior for unmatched begin or end events;
- argument persistence;
- color selection.

Use a Rust map from thread identity to a `Vec` stack. No manual deinitialization is needed.

## 8.5 Shared ownership

Replace manual atomic reference counting with `Arc<TraceData>` once construction is complete.

During loading:

- mutable parser/data state is owned by the serialized load stream;
- no shared immutable references exist until final adoption, or synchronization is explicit;
- the completed trace is wrapped in `Arc` and shared with viewer/search tasks;
- cancellation drops owned state naturally.

---

## 9. Tracks and organization

Port `track.c` into `src/trace/track.rs` while preserving:

- thread versus counter track classification;
- process/thread IDs;
- counter ID and series identity;
- metadata event handling;
- process/thread names;
- process/thread sort indices;
- event-index sorting;
- track ordering;
- stable behavior for equal sort keys;
- thread depth computation;
- inclusive and self durations;
- maximum track duration;
- block maximum duration summaries using `TRACK_BLOCK_SIZE = 1024`;
- counter palette indices;
- counter maximum totals;
- minimum/maximum trace timestamps;
- visible start lower-bound lookup.

All sorts that affect CLI or UI output must specify complete ordering, including tie-breakers. Do not rely on `HashMap` iteration order or unstable equal-key ordering.

The existing track tests form the behavioral specification and must be ported before deleting C code.

---

## 10. Rendering calculations

Port `track_renderer.c` into a safe renderer-calculation module independent of the raw ImGui FFI.

Preserve:

- `TRACK_MIN_EVENT_WIDTH = 3.0`;
- thread event block generation;
- counter block generation;
- bucket state by depth;
- blocked-until behavior;
- event merge blocks;
- viewport clipping;
- block/event index mapping;
- depth behavior;
- timestamp-to-pixel conversions;
- floating-point operation order where it affects screenshots;
- output order.

The renderer calculation layer should produce typed Rust render blocks. A separate UI layer emits ImGui draw-list calls. This allows most renderer tests to remain pure safe Rust without initializing ImGui.

---

## 11. Analytics migration

## 11.1 Histogram

Preserve:

- maximum 32 bins;
- linear/logarithmic behavior;
- min/max duration handling;
- empty input behavior;
- bucket boundaries;
- event filtering;
- count and selected bucket behavior;
- CLI formatting expectations.

## 11.2 Heatmap

Preserve:

- 16 heatmap buckets;
- per-track density calculations;
- viewport/time normalization;
- counter and thread behavior;
- minimap integration.

## 11.3 Concurrency

Preserve:

- requested/default bucket count;
- active thread overlap calculation;
- average concurrency;
- dominant event selection;
- maximum three dominant events;
- output ordering and ASCII bars.

## 11.4 Aggregate

Preserve:

- grouping by name or category;
- count, total duration, and average duration;
- minimum-count filtering;
- sort by duration or count;
- deterministic tie handling;
- skipped-entry footnote.

## 11.5 Diff

Preserve:

- alignment by actual string value rather than pool reference;
- baseline/target totals and counts;
- duration/count deltas;
- grouping options;
- sort options and tie handling.

---

## 12. Loader and gzip integration

## 12.1 zlib

Continue using the current Bazel zlib dependency through a handwritten `src/ffi/zlib.rs`.

The binding will define only the required ABI:

- `z_stream` and callback types;
- constants currently used by the loader;
- initialization, inflate, and cleanup functions;
- zlib integer and byte pointer types.

Binding definitions must use `#[repr(C)]`, fixed-width/platform-correct C types, and ABI layout tests where practical. No generated bindings and no Rust gzip crate will be used.

## 12.2 Native synchronous loader

Preserve:

- binary file opening;
- gzip magic detection (`1f 8b`);
- 256 KiB compressed input buffer;
- 1 MiB decompressed/output chunk buffer;
- streaming inflate;
- raw JSON streaming;
- 32 MiB in-flight backpressure threshold;
- asynchronous parse submissions even for the synchronous CLI loader;
- completion draining;
- output decompressed size;
- ingestion/organization telemetry;
- cleanup on initialization, read, parse, cancellation, or inflate failure;
- existing error messages.

Rust file I/O uses `std::fs::File` and `Read`. Only decompression crosses zlib FFI.

## 12.3 Browser loading

Browser gzip decompression remains in `src/ztracing.js` using `DecompressionStream`, exactly as today. Rust receives decompressed chunks and must retain:

- session IDs;
- cumulative raw input progress;
- EOF signaling with a null/empty chunk;
- stale-session cancellation;
- buffered-byte return values;
- ownership transfer for allocated chunk buffers.

---

## 13. Loading task migration

Replace pointer/reference-counted `trace_load_task_t` with typed shared state.

Suggested model:

```text
LoadSession
  session/stream ID
  cancellation state
  atomic buffered byte count
  parser and matcher state owned by serialized work
  cumulative progress
  timing/telemetry
  final TraceData and tracks
```

Each submitted chunk owns a `Vec<u8>` and progress metadata. Completion contains:

- parsed event count;
- processed bytes;
- input consumed bytes;
- EOF marker;
- status;
- final data/tracks/timestamps/telemetry on successful EOF.

Required semantics:

- adding a chunk increments buffered bytes before dispatch;
- all success, failure, and cancellation paths decrement it exactly once;
- chunks in one load session execute sequentially;
- parser state remains on the same serialized execution chain;
- an EOF completion transfers final ownership once;
- cancelled/stale sessions cannot replace current app data;
- partial final state is dropped on failure;
- the UI can query buffered bytes concurrently.

---

## 14. Search task migration

Replace the raw search task struct with an owned submission/result pair:

- owned query bytes/string;
- `Arc<TraceData>`;
- include-thread/include-counter flags;
- cancellation token;
- result event indices;
- computed histogram.

The UI stores a stable submission ID and cancels the previous search when input changes. A completed result is adopted only if it corresponds to the still-active search.

Preserve case-insensitive matching behavior exactly, including ASCII/non-ASCII behavior currently implemented.

---

## 15. Platform layer

## 15.1 Common Rust API

Provide safe platform operations for:

- monotonic milliseconds;
- dark-mode preference;
- macOS detection;
- opening the file dialog;
- settings persistence;
- main-thread detection;
- background job submission;
- worker teardown.

## 15.2 Native/headless

Use the standard library for:

- `Instant`-based monotonic timing;
- `std::thread::ThreadId` main-thread identity captured during initialization;
- worker threads and synchronization.

Preserve defaults:

- native/headless dark mode defaults to dark;
- native settings remain no-op/not found;
- headless file dialog remains a no-op;
- native macOS detection remains compile-target based.

## 15.3 WebAssembly

Use handwritten Emscripten imports and JS-library imports for:

- browser main-thread detection;
- time/device pixel ratio as needed by the Rust entrypoint;
- dark-mode query;
- macOS/user-agent query;
- file picker;
- local storage settings;
- browser logging.

No owned `platform_wasm.c` remains.

---

## 16. ImGui C ABI cleanup

The retained C++ binding is the only general bridge between Rust UI code and Dear ImGui.

## 16.1 ABI principles

- Use `extern "C"` functions.
- Use fixed-width integer types at the boundary.
- Use explicit opaque pointer declarations.
- Use `#[repr(C)]` Rust mirrors for value structs.
- Avoid exposing C++ layout except behind opaque handles.
- Avoid C variadics in Rust call sites.
- Prefer pointer-plus-length text APIs.
- Prefer integer flags with static C++ assertions against ImGui values.
- Normalize booleans at the ABI where useful to avoid invalid Rust `bool` values from foreign code.

## 16.2 Handwritten Rust binding

`src/ffi/imgui.rs` will manually declare:

- vector/color/theme structs;
- opaque draw-list/font/table/clipper/viewport/dock-node/draw-data handles;
- flags and key constants actually used;
- allocator callback signatures;
- context/frame lifecycle;
- IO access;
- window/table/popup/menu/widget functions;
- input state;
- draw-list operations;
- text measurement/rendering;
- theme application;
- docking functions.

No bindgen output is checked in or generated.

## 16.3 Safe facade

A safe Rust facade will provide:

- context ownership;
- typed flags/newtypes where useful;
- `WindowToken`, `ChildToken`, `TableToken`, `PopupToken`, and style-stack guards;
- `Drop`-based balancing of begin/end and push/pop operations;
- safe clipper ownership;
- checked draw-list methods;
- pointer-plus-length text rendering;
- temporary label/ID buffers with explicit NUL handling;
- safe input text buffer resizing.

ImGui is main-thread-only. Safe wrappers should avoid claiming `Send` or `Sync` for context-bound handles.

## 16.4 Binding changes

The C++ binding may be changed internally to support safe Rust without changing UI behavior. Expected changes include:

- add non-variadic text entrypoints;
- add length-aware text entrypoints;
- move the `Theme` ABI definition into the binding header;
- remove dependencies on `src/colors.h`;
- ensure callback user-data ownership is explicit;
- keep static assertions for all mirrored flag values;
- expose only operations actually used by Rust.

---

## 17. ImGui backends

## 17.1 `imgui_impl_webgl.cc`

This file remains C++ but must stop depending on migrated core code.

Replace:

- `allocator_t` with ImGui's configured allocation functions or local C++ RAII;
- `darray_t` staging buffers with ImGui-compatible/local C++ storage;
- core logging macros with a tiny backend-local logging mechanism or bridge callback.

Preserve:

- shader source and compilation;
- vertex/index staging and one-upload behavior;
- GL state setup;
- scissor calculations;
- draw command iteration;
- texture handling;
- vertex offset support;
- font texture creation/destruction;
- renderer error return codes.

Its public init API should no longer accept a deleted `allocator_t`. The ImGui allocator is configured before backend initialization.

## 17.2 `imgui_impl_wasm.cc`

This file remains C++ because it is an ImGui/Emscripten platform binding.

It must stop depending on migrated core/platform headers. Preserve:

- software-renderer detection;
- DPI behavior;
- canvas sizing;
- cursor updates;
- mouse, wheel, keyboard, blur, and resize callbacks;
- browser-shortcut filtering;
- clipboard behavior;
- key release behavior;
- update-frame countdown/power-saving support;
- macOS behavior.

It may continue to contain Emscripten-specific bridge code because it is explicitly allowed, but generic application platform operations should move to the Rust/JS bridge.

Allocation should use ImGui's configured allocator or C++ RAII, not deleted core allocators.

---

## 18. Colors and themes

Port `colors.c/.h` to Rust:

- define the full theme structure in Rust with `#[repr(C)]` for passing to `imgui_c`;
- preserve all packed RGBA values exactly;
- preserve the eight-color event palette;
- preserve light and dark constants;
- preserve color-name palette selection;
- preserve packed color channel order;
- add layout/size tests against constants exported by the C++ binding if needed.

The C++ binding header owns only the ABI shape, while Rust owns theme values and selection logic.

---

## 19. Viewer migration

`trace_viewer.c` is the largest individual migration and should be decomposed without changing behavior.

## 19.1 State model

Convert the current monolithic C struct into Rust structs for:

- viewport;
- timeline selection;
- box selection;
- snapping;
- track view information;
- render caches;
- focused/selected event state;
- details panel state;
- search state;
- histogram selection;
- vertical minimap state;
- interaction transient state.

Keep public app ownership simple: `App` owns one `TraceViewer`.

## 19.2 Pure calculations versus ImGui calls

Separate:

- pure layout and interaction calculations;
- data filtering and sorting;
- render block generation;
- ImGui command emission.

This allows the large existing viewer test suite to exercise pure Rust without unsafe FFI.

## 19.3 Behavior to preserve

Preserve all current interactions and layouts, including:

- reset and initial viewport;
- panning and zooming;
- mouse wheel and horizontal wheel behavior;
- double-click focus/zoom;
- event hover and tooltips;
- focused event;
- multi-selection;
- box selection;
- selection range creation and endpoint resizing;
- snapping and thresholds;
- details panel content;
- parent/child navigation;
- search filtering and sorting;
- histogram bucket selection;
- counter/thread filtering;
- vertical minimap density, selection markers, and scrolling;
- track lane heights and clipping;
- keyboard shortcuts;
- viewport and timeline formatting;
- request-update behavior.

## 19.4 Floating-point compatibility

For screenshot stability:

- preserve constants exactly;
- retain `f32` versus `f64` choices;
- preserve operation order where practical;
- avoid introducing iterator transformations that reorder accumulation;
- use the same rounding/truncation behavior for pixel coordinates;
- preserve sort stability.

---

## 20. App migration

Port `app.c/.h` to an owning Rust `App`.

The app owns:

- allocation accounting context;
- theme mode and active theme;
- power-save and window flags;
- task queue/worker integration;
- active loading session;
- active search submission;
- loading progress state;
- optional `Arc<TraceData>`;
- `TraceViewer`.

Preserve lifecycle order:

1. initialize allocation accounting;
2. initialize task infrastructure;
3. initialize viewer/UI state;
4. apply persisted settings/theme;
5. accept loading/search work;
6. poll completions before rendering;
7. cancel jobs before worker teardown;
8. drop viewer/data/backend state in an allocator-safe order.

Completion processing must preserve:

- stale session/search rejection;
- load progress updates;
- final trace adoption;
- final track/minimap setup;
- telemetry logging;
- cancellation cleanup;
- redraw requests.

---

## 21. Native CLI migration

Port `ztracing_cli.c` to a standard-library-only Rust binary.

## 21.1 Argument parsing

Argument parsing remains handwritten to preserve exact behavior. Keep:

- global help behavior;
- unknown global option errors;
- required subcommand and trace path checks;
- second path requirement for `diff`;
- all current subcommand options;
- option-value validation;
- defaults;
- handling of duplicated options as currently observed;
- stdout/stderr selection;
- exit statuses.

Do not introduce a CLI crate because it is unapproved and could alter output.

## 21.2 Commands

Preserve:

- `summary` and `--list-tracks`;
- `inspect --track --ts`;
- `query` filters;
- `concurrency --buckets`;
- `aggregate` grouping/sorting/minimum count;
- `diff` grouping/sorting;
- `histogram` filters;
- any currently accepted aliases/global flags present in tests.

## 21.3 Table formatting

Port `cli_table.c` while preserving:

- left/right alignment;
- width calculation;
- row and column behavior;
- formatted cells;
- separators/borders;
- empty values;
- duration formatting;
- ASCII bar generation;
- trailing newlines and footnotes.

Golden output must match byte-for-byte.

---

## 22. WebAssembly entrypoint and JS bridge

## 22.1 Removal of `ztracing_wasm.c`

`src/ztracing_wasm.c` will be deleted. Application entrypoint logic moves to `src/ztracing_wasm.rs`.

## 22.2 Exported ABI

Rust must export the same symbols with the same C ABI:

```text
ztracing_init
ztracing_start
ztracing_update
ztracing_is_loading_active
ztracing_malloc
ztracing_free
ztracing_set_font_data
ztracing_begin_session
ztracing_set_error
ztracing_handle_file_chunk
ztracing_get_buffered_bytes
ztracing_on_theme_changed
```

The linker export list and JS call sites remain compatible.
`ztracing_begin_session` returns a nonzero acceptance status so the browser
producer can stop immediately when the shared worker queue cannot accept the
loader job.
`ztracing_set_error` routes browser-side failures into the initialized Rust
UI. The JavaScript `onError` callback is reserved for failures that prevent UI
initialization.

The legacy `ztracing_deinit` WASM export was a no-op, and
`ztracing_get_allocated_bytes` was not consumed by the production JavaScript.
They are intentionally omitted from the Rust production ABI. Rust frontends
use ownership for teardown, the UI reads process-wide allocation telemetry
directly, and the headless lifecycle test checks that allocations return to
their baseline.

Because edition 2024 treats export attributes as unsafe attributes, exported functions will use the required syntax while keeping implementation unsafe code narrowly scoped.

## 22.3 Global app state

The C/JS ABI is process-global, so the entrypoint needs global state. Requirements:

- explicit uninitialized/initialized state;
- reject or safely handle calls in invalid lifecycle order according to current behavior;
- main-thread ownership for UI state;
- synchronization only for data read from workers;
- no `static mut` references escaping into safe code;
- native/headless and WebAssembly entrypoints share lifecycle logic where possible.

A locked/global holder, raw opaque pointer with strict wrapper, or another carefully audited mechanism may be used. Unsafe access must be centralized.

## 22.4 Raw buffer ownership

`ztracing_malloc(size)` returns WebAssembly memory writable by JavaScript.

`ztracing_handle_file_chunk` takes ownership exactly as today. The Rust implementation must:

- accept null only when size is zero/EOF;
- validate negative/overflowing integer inputs before conversion;
- reconstruct the exact allocated buffer using stored allocation metadata;
- move bytes into the load submission without double-free;
- ensure every stale-session/error path frees the buffer;
- keep memory valid while JavaScript copies data into it.

`ztracing_free` must release buffers that JavaScript allocated but did not transfer.

## 22.5 Emscripten API bindings

`src/ffi/emscripten.rs` will hand-declare only APIs required by Rust, potentially including:

- main loop registration;
- time;
- browser-main-thread detection;
- WebGL context attribute initialization/create/current;
- canvas/device pixel operations if not wholly retained in the ImGui backend.

Any mirrored structs must use `#[repr(C)]` and be validated against the pinned Emscripten headers.

## 22.6 `emscripten_bridge.js`

Use an Emscripten `--js-library` input for browser operations that are better represented in JavaScript than as C `EM_JS` macros:

- dark mode query;
- macOS/user-agent query;
- open file dialog;
- settings get/set;
- logging;
- any small browser operation required by the Rust platform layer.

Rust imports these functions through handwritten `extern "C"` declarations. String arguments use UTF-8 pointers and explicit output capacities matching current behavior.

The existing `ztracing.js` public API remains intact.

## 22.7 Main loop and rendering

Preserve frame order:

1. poll task completions;
2. consume app redraw requests;
3. request backend update frames;
4. skip frame in power-save mode when no update is needed;
5. start WebGL backend frame;
6. start ImGui WASM backend frame;
7. start ImGui frame;
8. update/draw app;
9. render ImGui;
10. set viewport and clear framebuffer;
11. render draw data.

Preserve current initialization return codes:

- `0`: success;
- `1`: WebGL2 context creation failure;
- `2`: renderer initialization failure.

## 22.8 Browser behavior

Smoke-test and preserve:

- initial font fetch/upload;
- query-string trace loading;
- drag and drop;
- file dialog;
- raw stream loading;
- gzip `DecompressionStream` loading;
- progress based on raw input bytes;
- backpressure polling;
- browser event-loop yielding;
- session cancellation;
- theme media-query updates;
- clipboard copy/paste;
- keyboard/browser shortcut behavior;
- mouse and wheel behavior;
- HiDPI and software-renderer fallback;
- pthread workers;
- cross-origin isolation/service worker behavior.

---

## 23. Headless EGL/GLES migration

Delete `headless_gl_linux.c/.h` and provide Rust wrappers.

## 23.1 Handwritten FFI

`src/ffi/egl.rs` and `src/ffi/gles.rs` declare only used symbols and constants. No external Rust OpenGL crate is added.

## 23.2 Safe `HeadlessGlContext`

The wrapper owns:

- EGL display;
- EGL context;
- pbuffer surface;
- framebuffer object;
- color renderbuffer;
- width and height.

Initialization preserves the current sequence:

1. set surfaceless EGL platform behavior;
2. get display;
3. initialize EGL;
4. choose RGBA8 ES3 pbuffer configuration;
5. create ES3 context;
6. create pbuffer surface;
7. make current;
8. create/bind framebuffer and renderbuffer;
9. verify framebuffer completeness.

Failure at any step cleans up all resources created by previous steps. `Drop` performs final cleanup in reverse order.

Setting `EGL_PLATFORM` may require a direct `setenv` FFI call or edition-2024 unsafe standard operation. It must happen before worker threads start, matching the current lifecycle.

## 23.3 Headless app harness

Port `ztracing_headless.c` to Rust while preserving:

- 800x600 default context;
- ImGui allocator installation;
- context/config flags;
- renderer initialization;
- fixed 1/60 delta time;
- completion polling before each frame;
- framebuffer rendering;
- font upload and texture recreation;
- load/session APIs;
- orderly cancellation, worker join, backend shutdown, and context destruction.

---

## 24. Test migration strategy

Tests are the primary behavioral specification. The migration should port tests alongside each component and keep the old tests runnable until the corresponding Rust behavior is established.

## 24.1 Test framework

- Use built-in Rust `#[test]`.
- No third-party assertion, snapshot, temp-file, image, or subprocess library.
- Implement small local helpers where needed.
- Use Bazel runfiles to locate binaries and golden data.
- Keep tests deterministic.
- Serialize tests using global ImGui/EGL/application state through a global test mutex and/or Bazel test arguments.

## 24.2 Core test mapping

| Current target/file | Rust destination | Coverage to preserve |
|---|---|---|
| `allocator_test.cc` | `base/tests/allocation_test.rs` | zeroing, alignment, realloc, page behavior, OOM assumptions where testable |
| `arena_test.cc` | mostly removed/recast | lifetime/reset behavior covered by owned-task tests; retain algorithmically relevant checkpoint tests only |
| `darray_test.cc` | removed/recast | vector growth/ownership behavior covered at callers; no need to test `Vec` itself |
| `hash_table_test.cc` | deterministic map/interner tests | collision, replacement, clear, growth behavior relevant to callers |
| `json_reader_test.cc` | `base/tests/json_test.rs` | all token and malformed input behavior |
| `string_test.cc` | module-specific formatting/string tests | preserve custom formatting semantics, do not retest `String` |
| `task_test.cc` | `base/tests/task_test.rs` | full queue/stream/cancellation/concurrency contract |

Every individual test case in every existing `.cc` test file must be represented by a Rust test before that file is deleted. Tests for deleted generic C containers may assert the equivalent replacement behavior at the owning-module level rather than mechanically testing Rust's standard library, but no test case may be silently omitted. A generated test inventory will track each original suite/name, its Rust destination, and its passing status.

## 24.3 Source test mapping

| Current target/file | Rust destination |
|---|---|
| `format_test.cc` | `src/tests/format_test.rs` |
| `trace_parser_test.cc` | `src/tests/trace_parser_test.rs` |
| `trace_data_test.cc` | `src/tests/trace_data_test.rs` |
| `track_test.cc` | `src/tests/track_test.rs` |
| `track_renderer_test.cc` | `src/tests/track_renderer_test.rs` |
| `trace_heatmap_test.cc` | `src/tests/trace_heatmap_test.rs` |
| `trace_histogram_test.cc` | `src/tests/trace_histogram_test.rs` |
| `trace_concurrency_test.cc` | `src/tests/trace_concurrency_test.rs` |
| `trace_aggregate_test.cc` | `src/tests/trace_aggregate_test.rs` |
| `trace_diff_test.cc` | `src/tests/trace_diff_test.rs` |
| `trace_load_task_test.cc` | `src/tests/trace_load_task_test.rs` |
| `trace_search_task_test.cc` | `src/tests/trace_search_task_test.rs` |
| `trace_viewer_test.cc` | `src/tests/trace_viewer_test.rs` |
| `cli_table_test.cc` | `src/tests/cli_table_test.rs` |
| `ztracing_cli_test.cc` | `src/tests/ztracing_cli_test.rs` |
| `ztracing_test.cc` | `src/tests/ztracing_test.rs` |

## 24.4 CLI golden tests

Implement standard-library helpers for:

- locating `//src:ztracing` in runfiles;
- creating unique temporary files/directories;
- writing raw JSON traces;
- creating gzip test input through the same zlib FFI or retaining static compressed fixtures;
- invoking subprocesses;
- capturing stdout/stderr/status;
- byte-for-byte comparison with existing `.golden` files.

Do not regenerate goldens merely because formatting differs. Differences indicate a migration bug unless explicitly approved.

## 24.5 Screenshot tests

Port the BMP and image-comparison helpers directly to Rust:

- 24-bit BMP read/write;
- RGBA framebuffer conversion;
- row padding;
- channel order;
- current per-channel threshold;
- current percentage threshold;
- diagnostic image output on failure if currently available/useful.

Preserve all current screenshot scenarios:

- welcome and loading screens;
- main timeline;
- event selection/focus;
- details panel;
- light theme;
- counter tracks;
- multi-lane rendering;
- search filtering and highlights;
- shortcuts window;
- navigation, panning, and zoom;
- selection creation/resizing;
- box selection;
- hover tooltips;
- vertical minimap and scrolling.

The retained C++ renderer and identical ImGui calls should keep images stable. Any difference must be investigated for state, ordering, float, font, or lifecycle changes before updating a golden.

## 24.6 ABI tests

Add tests for all shared Rust/C++ layouts:

- vector structs;
- theme struct;
- callback signatures where testable;
- relevant Emscripten structs;
- zlib stream struct;
- primitive widths and alignments.

The C++ binding can expose compile-time/static assertions or tiny size/alignment query functions used by native Rust tests.

## 24.7 Concurrency tests

Port all stress/death-style task tests. C++ death tests should become one of:

- direct `#[should_panic]` tests for safe precondition failures;
- subprocess tests where abort behavior must be verified;
- normal result/error tests if the Rust API makes invalid construction unrepresentable.

Do not weaken coverage of races, cancellation, queue-full behavior, completion waits, teardown, or worker affinity.

---

## 25. Benchmark migration

Port:

- `tools/trace_benchmark.cc` to `tools/trace_benchmark.rs`;
- `tools/trace_renderer_benchmark.cc` to `tools/trace_renderer_benchmark.rs`.

Preserve:

- accepted arguments;
- trace generation/loading path;
- measured phases;
- output fields;
- allocation reporting where applicable.

The benchmarks need to compile and run as smoke tests. No numeric performance gate is part of this migration.

---

## 26. File-by-file disposition

## 26.1 Existing `core/`

| Existing file | Final disposition |
|---|---|
| `allocator.c/.h` | replaced by Rust ownership and `base/allocation.rs` |
| `counting_allocator.c/.h` | replaced by scoped allocation accounting |
| `arena.c/.h` | deleted; lifetimes represented by ownership/scopes |
| `darray.c/.h` | deleted; use `Vec`/`Box` |
| `hash_table.c/.h` | deleted; use safe Rust maps/indexes |
| `string.c/.h` | deleted; use Rust strings/bytes/formatting |
| `json_reader.c/.h` | replaced by `base/json.rs` |
| `task.c/.h` | replaced by `base/task.rs` |
| `assert.h` | deleted; Rust assertions/results |
| `logging.h` | replaced by `base/logging.rs` |
| `logging_native.c` | replaced by native Rust sink |
| `logging_wasm.cc` | replaced by Rust-to-JS logging bridge |
| all `*_test.cc` | replaced by Rust tests or caller-level tests |
| `core/BUILD` | removed with directory; replaced by `base/BUILD` |

After migration, `core/` is removed entirely.

## 26.2 Existing `src/` production files

| Existing file | Final disposition |
|---|---|
| `app.c/.h` | `src/app.rs` |
| `cli_table.c/.h` | `src/cli_table.rs` |
| `colors.c/.h` | `src/colors.rs`; ABI shape moved into ImGui binding |
| `format.c/.h` | `src/format.rs` |
| `headless_gl_linux.c/.h` | Rust EGL/GLES FFI and safe context |
| `loading_screen.c/.h` | `src/loading_screen.rs` |
| `platform_common.c` | Rust worker/platform implementation |
| `platform_native.c` | native branch of `src/platform.rs` |
| `platform_headless.c` | headless branch of `src/platform.rs` |
| `platform_wasm.c` | Rust Emscripten FFI plus JS library |
| `platform.h` | deleted |
| `trace_aggregate.c/.h` | `src/trace/aggregate.rs` |
| `trace_concurrency.c/.h` | `src/trace/concurrency.rs` |
| `trace_data.c/.h` | `src/trace/data.rs` |
| `trace_diff.c/.h` | `src/trace/diff.rs` |
| `trace_heatmap.c/.h` | `src/trace/heatmap.rs` |
| `trace_histogram.c/.h` | `src/trace/histogram.rs` |
| `trace_load_task.c/.h` | `src/trace/load_task.rs` |
| `trace_loader.c/.h` | `src/trace/loader.rs` plus handwritten zlib FFI |
| `trace_parser.c/.h` | `src/trace/parser.rs` |
| `trace_search_task.c/.h` | `src/trace/search_task.rs` |
| `trace_viewer.c/.h` | `src/viewer/*` |
| `track.c/.h` | `src/trace/track.rs` |
| `track_renderer.c/.h` | `src/viewer/renderer.rs` |
| `welcome_screen.c/.h` | `src/welcome_screen.rs` |
| `ztracing_cli.c` | `src/cli.rs` binary crate root/module |
| `ztracing_headless.c` | `src/ztracing_headless.rs` |
| `ztracing_wasm.c` | `src/ztracing_wasm.rs`; C file deleted |
| `ztracing.h` | deleted; ABI declared by Rust exports and JS contract |
| `imgui_c.cc/.h` | retained and adapted |
| `imgui_impl_wasm.cc/.h` | retained and decoupled from migrated C core |
| `imgui_impl_webgl.cc/.h` | retained and decoupled from migrated C core |
| `imgui_types.h` | retained or merged into `imgui_c.h` |

## 26.3 Tests and tools

- Every `core/*_test.cc` and `src/*_test.cc` is deleted after its Rust equivalent passes.
- `tools/trace_benchmark.cc` and `tools/trace_renderer_benchmark.cc` are replaced by `.rs` binaries.
- Python and JavaScript tooling remains unchanged except where build integration requires updates.

---

## 27. Implementation sequence

These are working phases, not commits. The final result is committed once.

## Phase 0: Freeze the behavioral baseline

1. Run all current native tests.
2. Build the current WebAssembly bundle.
3. Record current Bazel labels and artifact paths.
4. Save current CLI golden results.
5. Confirm screenshot tests pass in the development environment.
6. Exercise raw and gzip browser loading manually.
7. Record current WebAssembly export names.
8. Ensure the working tree is clean before migration work begins.

Exit condition: the existing implementation is a trustworthy oracle.

## Phase 1: Prove Bazel/Rust/native/WASM linkage

1. Add `rules_rust` and pin latest stable Rust.
2. Create minimal `//base:base`.
3. Create a minimal native Rust target.
4. Hand-bind one harmless `imgui_c` function and verify Rust-to-C++ linkage.
5. Hand-bind one Rust callback called from C++ and verify C++-to-Rust linkage.
6. Build a minimal Rust static archive for `wasm32-unknown-emscripten`.
7. Link it with Dear ImGui through Emscripten.
8. Export and invoke a Rust function from generated JavaScript.
9. Verify pthread and memory-growth configuration remains valid.

Exit condition: there is no unresolved build architecture risk.

## Phase 2: Migrate safe base primitives

1. Add logging.
2. Add deterministic hashing helpers.
3. Add allocation accounting/raw buffer wrapper.
4. Port JSON reader and tests.
5. Implement Rust task scheduler and port task tests.
6. Port only project-specific allocator/interner tests that remain meaningful.

Keep the C core available to the old application during this phase.

Exit condition: `//base/...` tests pass independently.

## Phase 3: Migrate parser and trace data

1. Port trace parser with all tests.
2. Port string pool and persisted data.
3. Port begin/end matching.
4. Differentially feed identical chunks to old/new parsers during development where useful.
5. Compare event fields and pooled strings.

Exit condition: parser and trace-data Rust tests cover and match all existing cases.

## Phase 4: Migrate tracks and analytics

1. Port track organization.
2. Port renderer calculations.
3. Port histogram and heatmap.
4. Port concurrency, aggregate, and diff.
5. Port all associated tests.
6. Verify deterministic ordering.

Exit condition: all pure trace-processing tests pass in Rust.

## Phase 5: Migrate loader and asynchronous tasks

1. Add handwritten zlib bindings.
2. Port native raw/gzip loader.
3. Port loading task/session state.
4. Port search task.
5. Integrate with Rust scheduler/worker pool.
6. Port load/search tests.
7. Verify cancellation and leak behavior.

Exit condition: native Rust can load all current test traces and produce expected tracks/results.

## Phase 6: Migrate CLI

1. Port formatter and CLI table.
2. Port argument parser and commands.
3. Port CLI integration/golden tests.
4. Keep old and new binaries available under temporary labels for differential testing.
5. Compare stdout, stderr, and exit status for every golden case.
6. Move the final `//src:ztracing` label to Rust.

Exit condition: all CLI goldens match byte-for-byte.

## Phase 7: Stabilize ImGui FFI

1. Define the final binding ABI.
2. Add handwritten Rust declarations.
3. Add safe RAII wrappers.
4. Move theme ABI into the binding.
5. Remove variadic requirements from Rust call sites.
6. Decouple retained backends from deleted core headers.
7. Add ABI tests.

Exit condition: a native Rust smoke UI can initialize, render, and shut down without leaks.

## Phase 8: Migrate viewer and app

1. Port pure viewer/layout state first.
2. Port viewer tests incrementally.
3. Port ImGui draw emission.
4. Port welcome/loading screens.
5. Port app state and completion handling.
6. Preserve redraw/power-save behavior.

Exit condition: viewer unit tests pass and native headless app reaches stable frames.

## Phase 9: Migrate headless rendering and screenshots

1. Add EGL/GLES bindings.
2. Implement safe headless context.
3. Port headless app entrypoint.
4. Port BMP/image helpers.
5. Port screenshot scenarios.
6. Investigate every golden difference without updating expected images by default.

Exit condition: all screenshot goldens pass at the current tolerance.

## Phase 10: Migrate WebAssembly entrypoint

1. Add handwritten Emscripten bindings.
2. Add `emscripten_bridge.js`.
3. Port global app lifecycle and exported functions to Rust.
4. Port JS buffer ownership.
5. Remove `ztracing_wasm.c` from the build.
6. Verify all current exports.
7. Exercise browser behavior manually.

Exit condition: `bazel build //:ztracing` succeeds and the browser application behaves unchanged.

## Phase 11: Migrate benchmarks and clean the tree

1. Port both benchmark binaries.
2. Delete all migrated `.c`, `.cc`, and `.h` files outside the allowlist.
3. Remove `core/` and create final `base/` structure.
4. Remove obsolete C/C++ targets and test dependencies from BUILD files.
5. Update formatting, CI, README, and developer instructions.
6. Run the owned C/C++ allowlist scan.
7. Run the complete validation matrix.
8. Review unsafe blocks and FFI ownership.
9. Commit the complete migration once.

---

## 28. Validation matrix

## 28.1 Required Bazel commands

At minimum, the final tree must pass:

```bash
bazel test //...
bazel build //src:ztracing
bazel build //:ztracing
bazel build //tools:trace_benchmark
bazel build //tools:trace_renderer_benchmark
```

Also run focused native/headless tests as needed under the same configuration currently supplied by `.bazelrc`.

## 28.2 CLI validation

For every golden case, verify:

- command line;
- exit status;
- stdout bytes;
- stderr bytes;
- line endings;
- spacing;
- numeric formatting;
- deterministic ordering.

Test both raw and gzip traces.

## 28.3 WebAssembly validation

Verify:

- expected `.js` and `.wasm` artifacts exist;
- no unexpected `main` invocation occurs;
- all expected exports exist;
- initial memory, maximum memory, and growth settings remain;
- pthread pool size remains two;
- WebGL2/full ES3 settings remain;
- JavaScript can allocate, write, transfer, and free Rust-owned buffers;
- no stale typed-array view issue after memory growth;
- raw and gzip streams load;
- backpressure works;
- repeated sessions cancel old work;
- theme, input, clipboard, file dialog, drag/drop, and font upload work;
- power-save redraw logic works.

## 28.4 Native/headless validation

Verify:

- EGL initializes in CI without X11/Wayland;
- every screenshot passes;
- context teardown is clean;
- workers terminate;
- app allocation count returns to zero after teardown;
- repeated test initialization does not leak global ImGui/backend state.

## 28.5 Source allowlist validation

Run a repository-owned source scan equivalent to:

```bash
find . -type f \
  \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o \
     -name '*.h' -o -name '*.hpp' \) \
  -not -path './.git/*'
```

Every result must be one of:

- the approved `src/imgui_*` binding/backend files;
- files originating from external dependencies rather than repository-owned source.

No old headers may remain solely for convenience.

## 28.6 Dependency validation

- No `Cargo.toml` or `Cargo.lock` exists.
- No external Rust crate is fetched.
- No bindgen step exists.
- Existing ImGui, zlib, and Emscripten versions are unchanged.
- Bazel lockfile changes are limited to the required `rules_rust`/toolchain additions and normal dependency resolution.

## 28.7 Unsafe review

Produce a final list of all `unsafe` blocks and verify each is in an approved category. Review:

- pointer nullability;
- pointer length and alignment;
- ownership transfer;
- allocator pairing;
- C string termination;
- callback lifetime;
- thread affinity;
- `Send`/`Sync` assumptions;
- FFI struct layout;
- panic/unwind behavior across ABI boundaries;
- global initialization and teardown.

---

## 29. CI and documentation changes

## 29.1 CI

Update test/deploy workflows to:

- rely on Bazel's pinned Rust toolchain;
- retain the native compiler and EGL/GLES packages required by ImGui/headless tests;
- run `bazel test //...`;
- build the WebAssembly bundle;
- optionally verify the C/C++ allowlist;
- continue deploying the same root bundle artifact.

GCC 14 may no longer be required for migrated application code, but a C++ compiler remains required for Dear ImGui and retained bindings. Do not remove compiler setup until the retained C++ build is confirmed on CI.

## 29.2 README

Update implementation/build prerequisites to mention Rust is fetched/pinned through Bazel, while preserving user commands:

```bash
bazel build //:ztracing
bazel build //src:ztracing
bazel test //...
```

Do not document Cargo commands.

## 29.3 Trace analyzer skill

The CLI interface and output remain unchanged, so the skill should require no behavioral rewrite. Validate every command documented in `.agents/skills/trace-analyzer/SKILL.md` against the Rust CLI.

## 29.4 Style/formatting

- Rust uses `rustfmt` from the pinned toolchain.
- Retained C++ continues to use the current clang-format style.
- New Rust modules should favor explicit ownership, typed domain values, exhaustive matches, and small safe interfaces over transliterated C naming/patterns.

---

## 30. Idiomatic Rust requirements

The migration must not be a line-by-line C transliteration. The following practices are expected:

- ownership instead of manual init/deinit pairs;
- `Drop` for EGL, ImGui tokens, and other resources;
- `Arc` instead of handwritten object reference counts;
- `Vec`/`String` instead of custom dynamic arrays;
- enums with data instead of parallel phase/value fields where appropriate;
- newtypes for string/event/track/submission IDs;
- `Option` instead of sentinel pointers/boolean-plus-value pairs;
- `Result` internally for fallible operations;
- explicit conversion at compatibility boundaries where legacy APIs return integer status codes;
- iterators where they do not obscure ordering or alter floating-point accumulation;
- no self-referential/raw-pointer structures when borrowing or owned ranges can express the same behavior;
- no global mutable state outside unavoidable ABI entrypoints;
- no unsafe optimizations during correctness migration;
- no broad FFI exposure of Rust internals.

At the same time, idiomatic cleanup must not silently alter behavior. Existing tests and output take precedence over aesthetic API changes.

---

## 31. Risk register and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Rust/Emscripten/C++ link incompatibility | blocks browser build | prove minimal full-direction linkage in Phase 1 before broad migration |
| `wasm32-unknown-emscripten` std/pthread mismatch | browser workers fail | pin compatible Rust/Emscripten, run threaded smoke test early |
| task semantic drift | races, deadlocks, stale results | port full task test suite and model state transitions explicitly |
| parser chunk-boundary drift | corrupt/incomplete traces | differential tests over many split points and current parser corpus |
| string/number semantic drift | wrong trace metadata | preserve byte slices and exact casts/clamping; avoid serde-like reinterpretation |
| nondeterministic map order | CLI golden failures | deterministic hashers plus explicit complete sorting |
| UI float/order drift | screenshot changes | retain constants/types/operation order and unchanged C++ renderer |
| ImGui begin/end imbalance on early return | corrupt frame state | RAII tokens in safe wrapper |
| JS/Rust buffer double-free/leak | memory corruption | explicit allocation headers and ownership-transfer API tests |
| FFI struct layout mismatch | native/WASM corruption | handwritten minimal bindings plus layout assertions/tests |
| panic crosses C ABI | undefined behavior/abort surprises | panic-abort configuration and non-panicking callbacks |
| global test state races | flaky screenshots | serialized global-state tests and explicit teardown |
| allocation metric drift | UI/test regression | attributed allocation accounting and teardown assertions |
| accidental Rust dependency introduction | violates constraint | dependency audit and no Cargo/crate-universe setup |
| stale owned C/C++ remains | incomplete migration | final automated allowlist scan |

---

## 32. Future performance improvement plan

Performance optimization is a separate follow-up project after the correctness-preserving Rust migration has landed. It must not be mixed into the migration commit because simultaneous language and algorithm changes would make regressions harder to diagnose.

### 32.1 Benchmark foundation

The primary ingestion benchmark is the migrated Bazel target backed by `tools/trace_benchmark.rs`:

```bash
bazel build //tools:trace_benchmark
bazel-bin/tools/trace_benchmark <trace-file>
```

This is the direct Rust replacement for `tools/trace_benchmark.cc` and must remain usable with representative small, medium, and multi-gigabyte traces. It should report the same major phases and telemetry as the current tool, including input size, event count, ingestion time, organization time, total time, throughput, and tracked allocation usage.

The renderer benchmark remains available as:

```bash
bazel build //tools:trace_renderer_benchmark
bazel-bin/tools/trace_renderer_benchmark [arguments]
```

Use `tools/trace_benchmark` for parser, ingestion, gzip, allocation, scheduler, and track-organization work. Use `tools/trace_renderer_benchmark` for viewport queries, render-block generation, event merging, and track-renderer work.

Before making any post-migration optimization:

1. Select a documented benchmark corpus covering raw JSON, gzip, root-array, object-wrapped, deeply nested, counter-heavy, and multi-threaded traces.
2. Record file size, decompressed size, event count, track count, and machine/toolchain details.
3. Run a warm-up before collecting measurements.
4. Collect multiple samples and report median plus a spread such as minimum/maximum or percentiles.
5. Build with the same optimized Bazel configuration for every comparison.
6. Separate cold filesystem-cache measurements from warm-cache measurements where I/O is relevant.
7. Record peak/live allocation metrics in addition to wall-clock time.
8. Compare native and WebAssembly separately; an optimization is not assumed to help both targets.
9. Preserve benchmark command lines and raw output with the change under review.

Microbenchmarks may be added for isolated hot paths, but `tools/trace_benchmark` remains the end-to-end authority for trace ingestion improvements.

### 32.2 Profiling before optimization

Optimization candidates must be selected from measurements rather than assumptions. The follow-up should profile at least:

- JSON token scanning and number parsing;
- parser buffer compaction and chunk copying;
- string hashing, interning, and collision lookup;
- persisted argument/event insertion;
- begin/end matching;
- track organization and sorting;
- depth and self-duration calculation;
- gzip inflate time;
- scheduler idle/starvation time and queue contention;
- render-block generation and visible-range lookup;
- ImGui command generation and WebGL staging copies;
- allocation count, allocation size distribution, and peak live bytes.

Native profiling can use platform profilers and generated trace data. Browser profiling should use browser performance tooling and Emscripten-compatible sampling. The ztracing CLI and trace-analyzer workflow may be used to inspect generated Chrome Trace profiles where applicable.

### 32.3 Parser and JSON scanning

After the safe scalar implementation is stable, evaluate:

1. Restoring 16-byte quote/backslash scanning for x86 SSE2, ARM NEON, and WASM SIMD128.
2. Using target intrinsics only in a small module with scalar fallback.
3. Guarding every vector load so it remains within the input slice.
4. Faster whitespace and structural-character scans.
5. Reducing repeated token construction and bounds checks in validated inner loops.
6. Parsing common integer timestamp/duration forms without generic floating-point conversion.
7. Avoiding rescans when an event spans input chunks.
8. Tuning parser compaction thresholds and buffer growth.

Any unsafe SIMD implementation must have differential tests against the scalar parser over arbitrary data lengths, alignments, escape patterns, malformed data, and every possible chunk split. Fuzz-like deterministic test generation can be implemented locally without introducing a Rust dependency.

Every parser optimization must be evaluated with `tools/trace_benchmark`, not only a token-level microbenchmark.

### 32.4 Trace storage and allocation

Measure and consider:

- reserving event/argument/string capacities from early observed density;
- reducing allocation-header/accounting overhead;
- reusing parser argument and scratch buffers;
- replacing temporary owned strings with ranges or interned references;
- compacting completed vectors into boxed slices when that lowers retained capacity;
- reducing duplicate hashes and string comparisons for common fields;
- preserving fast caches for repeated category, phase, color, and argument keys;
- using narrower index types where trace-size limits make them safe;
- grouping hot event fields for cache locality without making the data model unsafe;
- avoiding zero-initialization when Rust initialization already writes every field;
- reducing copies between JavaScript buffers, load submissions, parser storage, and final storage.

Memory improvements must preserve the ability to load large traces and must report both throughput and peak live bytes from `tools/trace_benchmark`.

### 32.5 Hashing and string interning

Evaluate the deterministic standard-library-compatible hasher and interner with realistic traces:

- compare hash quality and cost;
- size collision candidate storage appropriately;
- reserve lookup capacity;
- avoid hashing the same string more than once per insertion;
- optimize equality checks by length/hash before bytes;
- retain deterministic behavior and explicit output sorting;
- consider specialized lookup for the small set of very common metadata strings.

Do not switch to an external hash-map or hashing crate without explicit approval.

### 32.6 Scheduler and loading pipeline

Use ingestion telemetry and `tools/trace_benchmark` to examine:

- worker starvation;
- queue lock contention;
- completion queue backpressure;
- chunk size;
- number of in-flight chunks;
- serialization overhead for load streams;
- unnecessary data copies;
- cancellation-check frequency;
- gzip producer versus parser consumer balance;
- native and Emscripten worker behavior.

Potential improvements include adaptive chunk sizing, reduced lock hold time, batched completion handling, buffer reuse, and direct ownership transfer. Any scheduler change must continue to pass the full concurrency/cancellation suite before benchmark gains are considered valid.

The browser main thread must never gain a blocking wait that maps to a forbidden `Atomics.wait` operation.

### 32.7 Track organization and analytics

Profile and optimize:

- event index sorting;
- metadata lookup;
- depth-stack maintenance;
- self-duration accumulation;
- block maximum-duration construction;
- counter-series aggregation;
- histogram and heatmap scans;
- aggregate/diff map allocation and sorting;
- query filtering and case-insensitive matching.

Possible techniques include capacity reservation, single-pass calculations, scratch-buffer reuse, partitioning independent tracks across workers, and avoiding repeated string resolution. Parallelization must be deterministic and should only be introduced when end-to-end measurements justify its complexity.

### 32.8 Viewer and renderer

Use `tools/trace_renderer_benchmark` to evaluate:

- binary-search and visible-range calculations;
- render-block cache reuse;
- event merging at low pixel density;
- per-depth temporary state reset;
- counter staging;
- minimap recomputation;
- selected/hovered event lookup;
- repeated text measurement and formatting;
- WebGL vertex/index staging copies.

Potential improvements include dirty-region/cache tracking, buffer reuse, reserving render-block capacity, reducing redundant ImGui calls, and avoiding full minimap/search recomputation when inputs have not changed.

Screenshot tests remain mandatory for every rendering optimization. Pixel changes require an explicit visual review rather than automatic golden regeneration.

### 32.9 WebAssembly-specific improvements

Measure browser behavior independently and consider:

- WASM SIMD128 parser paths;
- memory growth frequency and initial-memory tuning;
- JavaScript-to-WASM chunk copy count;
- worker startup and task dispatch overhead;
- pthread pool utilization;
- redraw frequency in power-save mode;
- high-DPI framebuffer cost;
- software-renderer behavior;
- WebGL staging and upload sizes;
- browser decompression versus Rust/zlib trade-offs, without changing dependencies unless approved.

Changes to initial/maximum memory, pthread count, or browser stream behavior are user-visible deployment changes and require explicit evidence and compatibility testing.

### 32.10 Performance acceptance for follow-up changes

Each future optimization should satisfy all of the following:

1. All correctness, CLI golden, concurrency, ABI, and screenshot tests pass.
2. `tools/trace_benchmark` or `tools/trace_renderer_benchmark` demonstrates a repeatable end-to-end gain on at least one documented representative workload.
3. Important workloads do not show an unexplained material regression.
4. Peak memory does not materially regress unless the speed/memory trade-off is explicit and approved.
5. Native and WebAssembly effects are reported separately.
6. New unsafe code is localized, documented, and tested against a safe reference path.
7. Benchmark methodology and results are included with the change.
8. Complexity remains proportionate to the measured gain.

The initial post-migration benchmark results become the Rust baseline. Historical C/C++ results may be retained for context, but future optimization decisions should compare against the latest accepted Rust baseline under the same machine and build configuration.

---

## 33. Definition of done

The migration is complete only when all of the following are true:

1. `base/` exists as the `//base:base` Rust crate.
2. `core/` is removed.
3. All application, platform, loader, trace, CLI, headless, test, and benchmark owned code is Rust.
4. `ztracing_wasm.c` is removed and all WebAssembly application exports are implemented in Rust.
5. Browser-only generic platform operations use handwritten Rust imports backed by JavaScript bridge functions.
6. Only approved ImGui binding/backend C++ files remain.
7. Retained C++ files no longer depend on deleted C core headers.
8. No unapproved Rust libraries are used.
9. No Cargo build files exist.
10. Existing third-party dependency versions/sources are unchanged except for adding the Rust Bazel toolchain dependency.
11. Native CLI output and status behavior match all current goldens.
12. All screenshot goldens pass without unapproved updates.
13. Raw and gzip trace loading works natively and in the browser.
14. Task scheduling/cancellation/backpressure behavior passes the migrated test suite.
15. The current WebAssembly export list and JS API remain compatible.
16. User-facing Bazel commands and artifact paths remain unchanged.
17. `bazel test //...` passes.
18. Native CLI, WebAssembly bundle, and both benchmark targets build.
19. Every unsafe block is justified and reviewed.
20. The complete result is delivered as one commit.
