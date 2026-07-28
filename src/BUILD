load(":defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@aspect_rules_js//js:defs.bzl", "js_test")
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")
load("@rules_rust//rust:defs.bzl", "rust_binary", "rust_library", "rust_test")

package(default_visibility = ["//visibility:public"])

rust_library(
    name = "ztracing_lib",
    srcs = glob(["**/*.rs"], exclude = ["**/*_test.rs"]),
    crate_name = "ztracing",
    crate_root = "lib.rs",
    edition = "2024",
    rustc_flags = select({
        ":wasm_build": [
            "-C", "panic=abort",
            "-C", "target-feature=+atomics,+bulk-memory,+mutable-globals",
            "-C", "debuginfo=2",
            "-C", "symbol-mangling-version=v0",
        ],
        "//conditions:default": [],
    }),
    deps = ["//base:base"],
    link_deps = [":imgui_c"] + select({
        ":wasm_build": [],
        "//conditions:default": ["@zlib//:zlib"],
    }),
)

alias(
    name = "ztracing_headless",
    actual = ":ztracing_lib",
    target_compatible_with = ["@@platforms//os:linux"],
)

cc_library(
    name = "headless_system_libraries",
    linkopts = ["-lEGL", "-lGLESv2"],
    target_compatible_with = ["@@platforms//os:linux"],
)

rust_test(
    name = "ztracing_test",
    crate = ":ztracing_lib",
    data = glob(["testdata/*.bmp"]),
    link_deps = select({
        "@@platforms//os:linux": [
            ":headless_system_libraries",
            ":imgui_impl_webgl",
        ],
        "//conditions:default": [],
    }),
)

rust_test(
    name = "headless_lifecycle_test",
    srcs = ["headless_lifecycle_test.rs"],
    crate_name = "headless_lifecycle_test",
    crate_root = "headless_lifecycle_test.rs",
    deps = [
        "//base:base",
        ":ztracing_lib",
    ],
    link_deps = [
        ":headless_system_libraries",
        ":imgui_impl_webgl",
    ],
    target_compatible_with = ["@@platforms//os:linux"],
)

js_test(
    name = "ztracing_js_test",
    entry_point = "ztracing_test.js",
    data = [
        "ztracing.js",
        "ztracing_bridge.js",
    ],
)

js_test(
    name = "ztracing_e2e_test",
    data = [
        "//:node_modules/playwright",
        "//:ztracing",
    ],
    entry_point = "ztracing_e2e_test.js",
    timeout = "long",
)

rust_binary(
    name = "ztracing",
    srcs = ["cli.rs"],
    crate_name = "ztracing_cli",
    crate_root = "cli.rs",
    edition = "2024",
    deps = [
        "//base:base",
        ":ztracing_lib",
    ],
)

rust_test(
    name = "ztracing_cli_test",
    srcs = ["cli.rs"],
    crate_name = "ztracing_cli_test",
    crate_root = "cli.rs",
    data = [":ztracing"] + glob(["testdata/cli/*"]),
    edition = "2024",
    deps = [
        "//base:base",
        ":ztracing_lib",
    ],
    link_deps = ["@zlib//:zlib"],
)

rust_test(
    name = "ffi_buffer_test",
    srcs = ["ffi_buffer.rs"],
    crate_name = "ffi_buffer_test",
    crate_root = "ffi_buffer.rs",
    edition = "2024",
)

cc_library(
    name = "wasm_link_options",
    additional_linker_inputs = [
        "ztracing.js",
        "ztracing_bridge.js",
    ],
    linkopts = [
        "--js-library", "$(location ztracing_bridge.js)",
        "--pre-js", "$(location ztracing.js)",
        "--profiling",
        "-g2",
        "-sUSE_WEBGL2=1",
        "-sFULL_ES3=1",
        "-sUSE_PTHREADS=1",
        "-sPTHREAD_POOL_SIZE=2",
        "-sPTHREAD_POOL_SIZE_STRICT=2",
        "-fwasm-exceptions",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sINITIAL_MEMORY=128MB",
        "-sMAXIMUM_MEMORY=4GB",
        "-sASSERTIONS=1",
        "-sSTACK_OVERFLOW_CHECK=1",
        "-sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8','HEAPF32','UTF8ToString','wasmMemory']",
        "-sEXPORTED_FUNCTIONS=['_ztracing_init','_ztracing_start','_ztracing_update','_ztracing_set_font_data','_ztracing_malloc','_ztracing_free','_ztracing_begin_session','_ztracing_set_error','_ztracing_handle_file_chunk','_ztracing_get_buffered_bytes','_ztracing_is_loading_active','_ztracing_on_theme_changed']",
    ],
    target_compatible_with = ["@@platforms//os:emscripten"],
)

rust_binary(
    name = "ztracing_wasm_entrypoint",
    srcs = [
        "ffi_buffer.rs",
        "ztracing_wasm.rs",
    ],
    crate_name = "ztracing_wasm",
    crate_root = "ztracing_wasm.rs",
    edition = "2024",
    experimental_use_cc_common_link = 1,
    rustc_flags = [
        "-C", "panic=abort",
        "-C", "target-feature=+atomics,+bulk-memory,+mutable-globals",
        "-C", "debuginfo=2",
        "-C", "symbol-mangling-version=v0",
    ],
    target_compatible_with = ["@@platforms//os:emscripten"],
    deps = [
        "//base:base",
        ":ztracing_lib",
    ],
    link_deps = [
        ":imgui_impl_wasm",
        ":imgui_impl_webgl",
        ":wasm_link_options",
    ],
    tags = ["wasm"],
)

alias(
    name = "ztracing_wasm_cc",
    actual = ":ztracing_wasm_entrypoint",
    target_compatible_with = ["@@platforms//os:emscripten"],
)

config_setting(
    name = "wasm_build",
    constraint_values = ["@@platforms//os:emscripten"],
)

config_setting(
    name = "headless_build",
    values = {"define": "headless=true"},
)

config_setting(
    name = "wasm_and_headless_build",
    constraint_values = ["@@platforms//os:emscripten"],
    values = {"define": "headless=true"},
)

cc_library(
    name = "platform",
    srcs = ["platform_common.c"] + select({
        ":wasm_build": ["platform_wasm.c"],
        ":wasm_and_headless_build": ["platform_wasm.c"],
        ":headless_build": ["platform_headless.c"],
        "//conditions:default": ["platform_native.c"],
    }),
    hdrs = ["platform.h"],
)

cc_library(
    name = "headless_gl",
    srcs = ["headless_gl_linux.c"],
    hdrs = ["headless_gl.h"],
    linkopts = ["-lEGL", "-lGLESv2"],
    deps = [
        "//core:logging",
    ],
    target_compatible_with = ["@@platforms//os:linux"] + select({
        ":headless_build": [],
        "//conditions:default": ["@@platforms//:incompatible"],
    }),
)

cc_library(
    name = "format",
    srcs = ["format.c"],
    hdrs = ["format.h"],
)


cc_library(
    name = "trace_load_task",
    srcs = ["trace_load_task.c"],
    hdrs = ["trace_load_task.h"],
    deps = [
        "//core:allocator",
        "//core:arena",
        "//core:darray",
        "//core:assert",
        ":colors",
        ":platform",
        "//core:task",
        ":trace_data",
        ":trace_parser",
        ":track",
    ],
)

cc_library(
    name = "trace_search_task",
    srcs = ["trace_search_task.c"],
    hdrs = ["trace_search_task.h"],
    deps = [
        "//core:allocator",
        "//core:darray",
        "//core:assert",
        "//core:task",
        ":trace_data",
        ":trace_histogram",
        ":trace_viewer",
    ],
)

cc_test(
    name = "format_test",
    srcs = ["format_test.cc"],
    deps = [
        ":format",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "trace_load_task_test",
    srcs = ["trace_load_task_test.cc"],
    deps = [
        ":trace_data",
        ":trace_load_task",
        "//core:counting_allocator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "trace_search_task_test",
    srcs = ["trace_search_task_test.cc"],
    deps = [
        "//core:allocator",
        "//core:counting_allocator",
        ":platform",
        "//core:task",
        ":trace_data",
        ":trace_histogram",
        ":trace_search_task",
        "@googletest//:gtest_main",
    ],
)


cc_library(
    name = "trace_parser",
    srcs = ["trace_parser.c"],
    hdrs = ["trace_parser.h"],
    deps = [
        "//core:allocator",
        "//core:darray",
        "//core:string",
        "//core:json_reader",
    ],
)

cc_test(
    name = "trace_parser_test",
    srcs = ["trace_parser_test.cc"],
    deps = [
        ":trace_parser",
        "//core:counting_allocator",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "imgui_types",
    hdrs = ["imgui_types.h"],
    deps = [
        "@imgui",
    ],
)

cc_library(
    name = "imgui_c",
    srcs = ["imgui_c.cc"],
    hdrs = ["imgui_c.h"],
    deps = [
        "@imgui",
        ":imgui_types",
    ],
)

cc_library(
    name = "imgui_impl_wasm",
    srcs = ["imgui_impl_wasm.cc"],
    hdrs = ["imgui_impl_wasm.h"],
    tags = ["wasm"],
    target_compatible_with = ["@@platforms//os:emscripten"],
    deps = ["@imgui"],
)

cc_library(
    name = "imgui_impl_webgl",
    srcs = ["imgui_impl_webgl.cc"],
    hdrs = ["imgui_impl_webgl.h"],
    deps = ["@imgui"],
)

cc_library(
    name = "colors",
    srcs = ["colors.c"],
    hdrs = ["colors.h"],
    deps = [
        "@imgui",
        ":imgui_types",
    ],
)

cc_library(
    name = "trace_data",
    srcs = ["trace_data.c"],
    hdrs = ["trace_data.h"],
    deps = [
        "//core:allocator",
        "//core:darray",
        "//core:assert",
        ":colors",
        "//core:hash_table",
        "//core:string",
        ":trace_parser",
    ],
)

cc_test(
    name = "trace_data_test",
    srcs = ["trace_data_test.cc"],
    deps = [
        ":trace_data",
        "@googletest//:gtest_main",
    ],
)


cc_library(
    name = "track",
    srcs = ["track.c"],
    hdrs = ["track.h"],
    deps = [
        "//core:allocator",
        "//core:arena",
        "//core:darray",
        ":colors",
        "//core:hash_table",
        ":trace_data",
    ],
)

cc_library(
    name = "track_renderer",
    srcs = ["track_renderer.c"],
    hdrs = ["track_renderer.h"],
    deps = [
        "//core:darray",
        ":track",
        ":trace_data",
    ],
)

cc_test(
    name = "track_renderer_test",
    srcs = ["track_renderer_test.cc"],
    deps = [
        ":track_renderer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "track_test",
    srcs = ["track_test.cc"],
    deps = [
        ":track",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "trace_viewer",
    srcs = ["trace_viewer.c"],
    hdrs = ["trace_viewer.h"],
    deps = [
        "//core:allocator",
        "//core:darray",
        ":colors",
        ":format",
        ":imgui_c",
        "//core:logging",
        ":platform",
        ":trace_data",
        ":trace_parser",
        ":track",
        ":track_renderer",
        ":trace_heatmap",
        ":trace_histogram",
    ],
)

cc_test(
    name = "trace_viewer_test",
    srcs = ["trace_viewer_test.cc"],
    deps = [
        ":trace_viewer",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "loading_screen",
    srcs = ["loading_screen.c"],
    hdrs = ["loading_screen.h"],
    deps = [
        ":colors",
        ":imgui_c",
    ],
)

cc_library(
    name = "welcome_screen",
    srcs = ["welcome_screen.c"],
    hdrs = ["welcome_screen.h"],
    deps = [
        ":colors",
        ":platform",
        ":imgui_c",
    ],
)

cc_library(
    name = "app",
    srcs = ["app.c"],
    hdrs = ["app.h"],
    deps = [
        "//core:allocator",
        "//core:counting_allocator",
        "//core:darray",
        ":colors",
        ":format",
        ":imgui_c",
        ":loading_screen",
        "//core:logging",
        ":platform",
        "//core:task",
        ":trace_data",
        ":trace_load_task",
        ":trace_parser",
        ":trace_search_task",
        ":trace_viewer",
        ":track",
        ":welcome_screen",
    ],
)

cc_binary(
    name = "ztracing_wasm_legacy_cc",
    srcs = [
        "ztracing.h",
        "ztracing_wasm.c",
    ],
    tags = ["wasm"],
    target_compatible_with = ["@@platforms//os:emscripten"],
    additional_linker_inputs = [
        "ztracing.js",
    ],
    linkopts = [
        "-sUSE_WEBGL2=1",
        "-sFULL_ES3=1",
        "-sWASM=1",
        "-sINVOKE_RUN=0",
        "-sUSE_PTHREADS=1",
        "-sPTHREAD_POOL_SIZE=2",
        "-sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8','HEAPF32','UTF8ToString','wasmMemory']",
        "-sEXPORTED_FUNCTIONS=['_ztracing_init','_ztracing_start','_ztracing_set_font_data','_ztracing_malloc','_ztracing_free','_ztracing_begin_session','_ztracing_handle_file_chunk','_ztracing_get_buffered_bytes','_ztracing_on_theme_changed','_imgui_impl_wasm_set_clipboard_text_from_js','_imgui_impl_wasm_trigger_paste_event']",
        "-sINITIAL_MEMORY=128MB",
        "-sMAXIMUM_MEMORY=4GB",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sASSERTIONS=1",
        "-sSTACK_OVERFLOW_CHECK=1",
        "--pre-js src/ztracing.js",
    ],
    deps = [
        ":app",
        "//core:assert",
        "//core:darray",
        ":imgui_c",
        ":imgui_impl_wasm",
        ":imgui_impl_webgl",
        "//core:logging",
        ":platform",
        "//core:counting_allocator",
    ],
)

wasm_cc_binary(
    name = "ztracing_wasm",
    cc_target = ":ztracing_wasm_cc",
    tags = ["wasm"],
    threads = "emscripten",
)

cc_library(
    name = "trace_heatmap",
    srcs = ["trace_heatmap.c"],
    hdrs = ["trace_heatmap.h"],
    deps = [
        "//core:darray",
        ":trace_data",
        ":track",
    ],
)

cc_test(
    name = "trace_heatmap_test",
    srcs = ["trace_heatmap_test.cc"],
    deps = [
        ":trace_heatmap",
        "//core:allocator",
        "//core:counting_allocator",
        ":track",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "trace_histogram",
    srcs = ["trace_histogram.c"],
    hdrs = ["trace_histogram.h"],
    deps = [
        "//core:darray",
        ":trace_data",
    ],
)

cc_test(
    name = "trace_histogram_test",
    srcs = ["trace_histogram_test.cc"],
    deps = [
        ":trace_histogram",
        "//core:allocator",
        "//core:counting_allocator",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "trace_loader",
    srcs = ["trace_loader.c"],
    hdrs = ["trace_loader.h"],
    deps = [
        "//core:allocator",
        "//core:darray",
        ":platform",
        "//core:task",
        ":trace_data",
        ":trace_load_task",
        ":track",
        "@zlib//:zlib",
    ],
)

cc_library(
    name = "trace_concurrency",
    srcs = ["trace_concurrency.c"],
    hdrs = ["trace_concurrency.h"],
    deps = [
        "//core:darray",
        "//core:hash_table",
        ":trace_data",
        ":track",
    ],
)

cc_library(
    name = "trace_aggregate",
    srcs = ["trace_aggregate.c"],
    hdrs = ["trace_aggregate.h"],
    deps = [
        "//core:darray",
        "//core:hash_table",
        ":trace_data",
    ],
)

cc_library(
    name = "trace_diff",
    srcs = ["trace_diff.c"],
    hdrs = ["trace_diff.h"],
    deps = [
        "//core:darray",
        "//core:hash_table",
        ":trace_data",
        ":trace_aggregate",
    ],
)

cc_library(
    name = "cli_table",
    srcs = ["cli_table.c"],
    hdrs = ["cli_table.h"],
    deps = [
        "//core:darray",
        "//core:string",
        "//core:allocator",
        "//core:arena",
    ],
)

cc_binary(
    name = "ztracing_cc",
    srcs = [
        "ztracing_cli.c",
    ],
    deps = [
        ":app",
        "//core:darray",
        ":track",
        ":trace_loader",
        "//core:logging",
        ":platform",
        ":trace_concurrency",
        ":trace_aggregate",
        ":trace_diff",
        ":cli_table",
    ],
)

cc_library(
    name = "ztracing_headless_cc",
    srcs = [
        "ztracing_headless.c",
    ],
    hdrs = [
        "ztracing.h",
    ],
    deps = [
        ":app",
        "//core:darray",
        ":headless_gl",
        ":imgui_c",
        ":imgui_impl_webgl",
        "//core:logging",
        ":platform",
        "@imgui",
        "//core:counting_allocator",
    ],
    tags = ["legacy"],
    target_compatible_with = ["@@platforms//os:linux"] + select({
        ":headless_build": [],
        "//conditions:default": ["@@platforms//:incompatible"],
    }),
)

cc_test(
    name = "ztracing_cc_test",
    srcs = ["ztracing_test.cc"],
    data = glob(["testdata/*.bmp"]),
    deps = [
        ":ztracing_headless_cc",
        "@googletest//:gtest_main",
        "@imgui",
    ],
    tags = ["legacy"],
    target_compatible_with = ["@@platforms//os:linux"] + select({
        ":headless_build": [],
        "//conditions:default": ["@@platforms//:incompatible"],
    }),
)

exports_files([
    "shell.html",
    "coi-serviceworker.js",
])

cc_test(
    name = "ztracing_cli_cc_test",
    srcs = ["ztracing_cli_test.cc"],
    data = [":ztracing"] + glob(["testdata/cli/*"]),
    deps = [
        "@googletest//:gtest_main",
        "@zlib//:zlib",
    ],
)

cc_test(
    name = "cli_table_test",
    srcs = ["cli_table_test.cc"],
    deps = [
        ":cli_table",
        "//core:allocator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "trace_concurrency_test",
    srcs = ["trace_concurrency_test.cc"],
    deps = [
        ":trace_concurrency",
        ":trace_data",
        ":track",
        "//core:allocator",
        "//core:arena",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "trace_aggregate_test",
    srcs = ["trace_aggregate_test.cc"],
    deps = [
        ":trace_aggregate",
        ":trace_data",
        "//core:allocator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "trace_diff_test",
    srcs = ["trace_diff_test.cc"],
    deps = [
        ":trace_diff",
        ":trace_data",
        "//core:allocator",
        "@googletest//:gtest_main",
    ],
)
