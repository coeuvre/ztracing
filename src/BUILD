load("//rules:cc.bzl", "cc_library")
load("//rules:rust.bzl", "rust_binary", "rust_library", "rust_test")
load("@aspect_rules_js//js:defs.bzl", "js_test")
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

package(default_visibility = ["//visibility:public"])

rust_library(
    name = "ztracing_lib",
    srcs = glob(["**/*.rs"], exclude = ["**/*_test.rs"]),
    crate_name = "ztracing",
    crate_root = "lib.rs",
    deps = ["//base:base"],
    link_deps = [":imgui_c"] + select({
        "//rules:wasm_build": [],
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
    deps = [
        "//base:base",
        ":ztracing_lib",
    ],
    link_deps = ["@zlib//:zlib"],
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
    srcs = ["ztracing_wasm.rs"],
    crate_name = "ztracing_wasm",
    crate_root = "ztracing_wasm.rs",
    experimental_use_cc_common_link = 1,
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

cc_library(
    name = "imgui_c",
    srcs = ["imgui_c.cc"],
    hdrs = ["imgui_c.h"],
    deps = [
        "@imgui",
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

wasm_cc_binary(
    name = "ztracing_wasm",
    cc_target = ":ztracing_wasm_entrypoint",
    tags = ["wasm"],
    threads = "emscripten",
)

exports_files([
    "shell.html",
    "coi-serviceworker.js",
])
