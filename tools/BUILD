load("//rules:rust.bzl", "rust_binary", "rust_test")

package(default_visibility = ["//visibility:public"])

rust_binary(
    name = "trace_benchmark",
    srcs = ["trace_benchmark.rs"],
    crate_name = "trace_benchmark",
    crate_root = "trace_benchmark.rs",
    deps = [
        "//base:base",
        "//src:ztracing_lib",
    ],
)

rust_test(
    name = "trace_benchmark_test",
    srcs = ["trace_benchmark.rs"],
    crate_name = "trace_benchmark_test",
    crate_root = "trace_benchmark.rs",
    deps = [
        "//base:base",
        "//src:ztracing_lib",
    ],
)

rust_binary(
    name = "trace_renderer_benchmark",
    srcs = ["trace_renderer_benchmark.rs"],
    crate_name = "trace_renderer_benchmark",
    crate_root = "trace_renderer_benchmark.rs",
    deps = [
        "//base:base",
        "//src:ztracing_lib",
    ],
    link_deps = select({
        "@@platforms//os:linux": [
            "//src:headless_system_libraries",
            "//src:imgui_impl_webgl",
        ],
        "//conditions:default": [],
    }),
    target_compatible_with = ["@@platforms//os:linux"],
)

rust_test(
    name = "trace_renderer_benchmark_test",
    srcs = ["trace_renderer_benchmark.rs"],
    crate_name = "trace_renderer_benchmark_test",
    crate_root = "trace_renderer_benchmark.rs",
    deps = [
        "//base:base",
        "//src:ztracing_lib",
    ],
    link_deps = select({
        "@@platforms//os:linux": [
            "//src:headless_system_libraries",
            "//src:imgui_impl_webgl",
        ],
        "//conditions:default": [],
    }),
    target_compatible_with = ["@@platforms//os:linux"],
)
