load("//src:defs.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "trace_benchmark",
    srcs = ["trace_benchmark.cc"],
    deps = [
        "//src:allocator",
        "//src:trace_parser",
        "//src:trace_data",
        "//src:track",
        "//src:colors",
    ],
)

cc_binary(
    name = "trace_renderer_benchmark",
    srcs = ["trace_renderer_benchmark.cc"],
    deps = [
        "//src:allocator",
        "//src:trace_parser",
        "//src:trace_data",
        "//src:track",
        "//src:colors",
        "//src:track_renderer",
    ],
)
