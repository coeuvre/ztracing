load(":defs.bzl", "cc_binary", "cc_library")
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
    linkopts = [
        "-sUSE_WEBGL2=1",
        "-sFULL_ES3=1",
        "-sWASM=1",
        "-sINVOKE_RUN=0",
        "-sEXPORTED_RUNTIME_METHODS=['ccall','callMain']",
        "-sEXPORTED_FUNCTIONS=['_main','_ztracing_set_font_data']",
    ],
    deps = [
        ":allocator",
        ":imgui_impl_wasm",
        ":imgui_impl_webgl",
        ":logging",
        "@imgui",
    ],
)

wasm_cc_binary(
    name = "ztracing_wasm",
    cc_target = ":ztracing",
)

exports_files(["shell.html"])
