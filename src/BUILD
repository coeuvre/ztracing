load(":defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

package(default_visibility = ["//visibility:public"])

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
    name = "logging",
    srcs = select({
        ":wasm_build": ["logging_wasm.cc"],
        "//conditions:default": ["logging_native.c"],
    }),
    hdrs = ["logging.h"],
    copts = select({
        ":wasm_build": ["-Wno-dollar-in-identifier-extension"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "assert",
    hdrs = ["assert.h"],
    deps = [
        ":logging",
    ],
)

cc_library(
    name = "headless_gl",
    srcs = ["headless_gl_linux.c"],
    hdrs = ["headless_gl.h"],
    linkopts = ["-lEGL", "-lGLESv2"],
    deps = [
        ":logging",
    ],
)

cc_library(
    name = "allocator",
    srcs = ["allocator.c"],
    hdrs = ["allocator.h"],
    deps = [":logging"],
)

cc_library(
    name = "string",
    hdrs = ["string.h"],
)

cc_library(
    name = "format",
    srcs = ["format.c"],
    hdrs = ["format.h"],
)

cc_library(
    name = "array_list",
    hdrs = ["array_list.h"],
    deps = [":allocator"],
)

cc_library(
    name = "channel",
    srcs = ["channel.c"],
    hdrs = ["channel.h"],
    deps = [
        ":allocator",
        ":assert",
    ],
)

cc_library(
    name = "app_msg",
    srcs = ["app_msg.c"],
    hdrs = ["app_msg.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":assert",
        ":channel",
        ":trace_data",
        ":trace_viewer",
    ],
)

cc_library(
    name = "trace_load_task",
    srcs = ["trace_load_task.c"],
    hdrs = ["trace_load_task.h"],
    deps = [
        ":allocator",
        ":app_msg",
        ":array_list",
        ":assert",
        ":channel",
        ":colors",
        ":platform",
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
        ":allocator",
        ":app_msg",
        ":array_list",
        ":assert",
        ":channel",
        ":platform",
    ],
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

cc_test(
    name = "channel_test",
    srcs = ["channel_test.cc"],
    deps = [
        ":channel",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "trace_load_task_test",
    srcs = ["trace_load_task_test.cc"],
    deps = [
        ":app_msg",
        ":channel",
        ":trace_data",
        ":trace_load_task",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "trace_search_task_test",
    srcs = ["trace_search_task_test.cc"],
    deps = [
        ":app_msg",
        ":channel",
        ":trace_load_task",
        ":trace_search_task",
        ":trace_viewer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "app_msg_test",
    srcs = ["app_msg_test.cc"],
    deps = [
        ":app_msg",
        ":channel",
        ":trace_load_task",
        "@googletest//:gtest_main",
    ],
)


cc_library(
    name = "trace_parser",
    srcs = ["trace_parser.c"],
    hdrs = ["trace_parser.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":string",
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
    deps = [
        ":allocator",
        ":logging",
        ":platform",
        "@imgui",
    ],
)

cc_library(
    name = "imgui_impl_webgl",
    srcs = ["imgui_impl_webgl.cc"],
    hdrs = ["imgui_impl_webgl.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":logging",
        "@imgui",
    ],
)

cc_library(
    name = "colors",
    srcs = ["colors.c"],
    hdrs = ["colors.h"],
    deps = [
        "@imgui",
        ":imgui_c",
    ],
)

cc_library(
    name = "trace_data",
    srcs = ["trace_data.c"],
    hdrs = ["trace_data.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":assert",
        ":colors",
        ":hash_table",
        ":string",
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
    srcs = ["track.c"],
    hdrs = ["track.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":colors",
        ":hash_table",
        ":trace_data",
    ],
)

cc_library(
    name = "track_renderer",
    srcs = ["track_renderer.c"],
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
    name = "trace_viewer",
    srcs = ["trace_viewer.c"],
    hdrs = ["trace_viewer.h"],
    deps = [
        ":allocator",
        ":array_list",
        ":colors",
        ":format",
        ":imgui_c",
        ":logging",
        ":platform",
        ":trace_data",
        ":trace_parser",
        ":track",
        ":track_renderer",
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
        ":allocator",
        ":app_msg",
        ":array_list",
        ":channel",
        ":colors",
        ":format",
        ":imgui_c",
        ":loading_screen",
        ":logging",
        ":platform",
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
    name = "ztracing",
    srcs = [
        "ztracing.h",
        "ztracing_wasm.c",
    ],
    tags = ["wasm"],
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
        "-sEXPORTED_FUNCTIONS=['_ztracing_init','_ztracing_start','_ztracing_set_font_data','_ztracing_malloc','_ztracing_free','_ztracing_begin_session','_ztracing_handle_file_chunk','_ztracing_on_theme_changed','_imgui_impl_wasm_set_clipboard_text_from_js','_imgui_impl_wasm_trigger_paste_event']",
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
        ":assert",
        ":imgui_c",
        ":imgui_impl_wasm",
        ":imgui_impl_webgl",
        ":logging",
        ":platform",
    ],
)

wasm_cc_binary(
    name = "ztracing_wasm",
    cc_target = ":ztracing",
    tags = ["wasm"],
)

cc_library(
    name = "ztracing_headless",
    srcs = [
        "ztracing_headless.c",
    ],
    hdrs = [
        "ztracing.h",
    ],
    deps = [
        ":app",
        ":headless_gl",
        ":imgui_c",
        ":imgui_impl_webgl",
        ":logging",
        ":platform",
        "@imgui",
    ],
)

cc_test(
    name = "ztracing_test",
    srcs = ["ztracing_test.cc"],
    data = glob(["testdata/*.bmp"]),
    deps = [
        ":ztracing_headless",
        "@googletest//:gtest_main",
        "@imgui",
    ],
)

exports_files([
    "shell.html",
    "coi-serviceworker.js",
])
