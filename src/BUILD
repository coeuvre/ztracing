load(":defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

package(default_visibility = ["//visibility:public"])

config_setting(
    name = "wasm_build",
    constraint_values = ["@@platforms//os:emscripten"],
)

cc_library(
    name = "platform",
    srcs = select({
        ":wasm_build": ["platform_wasm.cc"],
        "//conditions:default": ["platform_native.cc"],
    }),
    hdrs = ["platform.h"],
)

cc_library(
    name = "logging",
    srcs = select({
        ":wasm_build": ["logging_wasm.cc"],
        "//conditions:default": ["logging_native.cc"],
    }),
    hdrs = ["logging.h"],
)

cc_library(
    name = "allocator",
    srcs = ["allocator.cc"],
    hdrs = ["allocator.h"],
    deps = [":logging"],
)

cc_library(
    name = "str",
    hdrs = ["str.h"],
)

cc_library(
    name = "format",
    srcs = ["format.cc"],
    hdrs = ["format.h"],
)

cc_library(
    name = "array_list",
    hdrs = ["array_list.h"],
    deps = [":allocator"],
)

cc_test(
    name = "allocator_test",
    srcs = ["allocator_test.cc"],
    deps = [
        ":allocator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "str_test",
    srcs = ["str_test.cc"],
    deps = [
        ":str",
        "@googletest//:gtest_main",
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
    name = "array_list_test",
    srcs = ["array_list_test.cc"],
    deps = [
        ":array_list",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "trace_parser",
    srcs = ["trace_parser.cc"],
    hdrs = ["trace_parser.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":str",
    ],
)

cc_test(
    name = "trace_parser_test",
    srcs = ["trace_parser_test.cc"],
    deps = [
        ":trace_parser",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "imgui_impl_wasm",
    srcs = ["imgui_impl_wasm.cc"],
    hdrs = ["imgui_impl_wasm.h"],
    deps = [
        ":allocator",
        ":logging",
        "@imgui",
    ],
)

cc_library(
    name = "imgui_impl_webgl",
    srcs = ["imgui_impl_webgl.cc"],
    hdrs = ["imgui_impl_webgl.h"],
    deps = [
        ":allocator",
        ":logging",
        "@imgui",
    ],
)

cc_library(
    name = "colors",
    srcs = ["colors.cc"],
    hdrs = ["colors.h"],
    deps = ["@imgui"],
)

cc_library(
    name = "trace_data",
    srcs = ["trace_data.cc"],
    hdrs = ["trace_data.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":colors",
        ":hash_table",
        ":str",
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
    name = "hash_table",
    hdrs = ["hash_table.h"],
    deps = [":allocator"],
)

cc_test(
    name = "hash_table_test",
    srcs = ["hash_table_test.cc"],
    deps = [
        ":hash_table",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "track",
    srcs = ["track.cc"],
    hdrs = ["track.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":hash_table",
        ":trace_data",
    ],
)

cc_library(
    name = "track_renderer",
    srcs = ["track_renderer.cc"],
    hdrs = ["track_renderer.h"],
    deps = [
        ":array_list",
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
    name = "app",
    srcs = ["app.cc"],
    hdrs = ["app.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":colors",
        ":format",
        ":hash_table",
        ":imgui_impl_wasm",
        ":logging",
        ":platform",
        ":trace_data",
        ":trace_parser",
        ":track",
        ":track_renderer",
        "@imgui",
    ],
)

cc_binary(
    name = "ztracing",
    srcs = [
        "ztracing.h",
        "ztracing_wasm.cc",
    ],
    additional_linker_inputs = [
        "ztracing.js",
    ],
    linkopts = [
        "-sUSE_WEBGL2=1",
        "-sFULL_ES3=1",
        "-sWASM=1",
        "-sINVOKE_RUN=0",
        "-sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8','HEAPF32','UTF8ToString']",
        "-sEXPORTED_FUNCTIONS=['_ztracing_init','_ztracing_start','_ztracing_set_font_data','_ztracing_malloc','_ztracing_free','_ztracing_begin_session','_ztracing_handle_file_chunk','_ztracing_on_theme_changed']",
        "-sINITIAL_MEMORY=128MB",
        "-sMAXIMUM_MEMORY=4GB",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sASSERTIONS=1",
        "-sSTACK_OVERFLOW_CHECK=1",
        "-g",
        "--pre-js src/ztracing.js",
    ],
    deps = [
        ":app",
        ":imgui_impl_wasm",
        ":imgui_impl_webgl",
        ":logging",
        ":platform",
        "@imgui",
    ],
)

wasm_cc_binary(
    name = "ztracing_wasm",
    cc_target = ":ztracing",
)

exports_files(["shell.html"])
