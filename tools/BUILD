load("//src:defs.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "trace_benchmark",
    srcs = ["trace_benchmark.cc"],
    deps = [
        "//src:allocator",
        "//src:trace_parser",
    ],
)
