load(":defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "logging",
    srcs = ["logging_wasm.cc"],
    hdrs = ["logging.h"],
)

cc_library(
    name = "allocator",
    srcs = ["allocator.cc"],
    hdrs = ["allocator.h"],
)

cc_library(
    name = "str",
    hdrs = ["str.h"],
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

cc_binary(
    name = "ztracing",
    srcs = [
        "main_wasm.cc",
    ],
    additional_linker_inputs = [
        "wasm_bridge.js",
    ],
    linkopts = [
        "-sUSE_WEBGL2=1",
        "-sFULL_ES3=1",
        "-sWASM=1",
        "-sINVOKE_RUN=0",
        "-sEXPORTED_RUNTIME_METHODS=['ccall','callMain','HEAPU8']",
        "-sEXPORTED_FUNCTIONS=['_main','_ztracing_set_font_data','_ztracing_malloc','_ztracing_free','_ztracing_handle_file_chunk']",
        "--pre-js src/wasm_bridge.js",
    ],
    deps = [
        ":allocator",
        ":array_list",
        ":imgui_impl_wasm",
        ":imgui_impl_webgl",
        ":logging",
        ":trace_parser",
        "@imgui",
    ],
)

wasm_cc_binary(
    name = "ztracing_wasm",
    cc_target = ":ztracing",
)

exports_files(["shell.html"])
