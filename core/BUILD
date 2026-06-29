load("//src:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "logging",
    srcs = select({
        "//src:wasm_build": ["logging_wasm.cc"],
        "//conditions:default": ["logging_native.c"],
    }),
    hdrs = ["logging.h"],
    copts = select({
        "//src:wasm_build": ["-Wno-dollar-in-identifier-extension"],
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
    name = "allocator",
    srcs = ["allocator.c"],
    hdrs = ["allocator.h"],
    deps = [":logging"],
)

cc_library(
    name = "counting_allocator",
    srcs = ["counting_allocator.c"],
    hdrs = ["counting_allocator.h"],
    deps = [":allocator"],
)

cc_library(
    name = "string",
    srcs = ["string.c"],
    hdrs = ["string.h"],
    deps = [
        ":allocator",
        ":assert",
    ],
)

cc_library(
    name = "arena",
    srcs = ["arena.c"],
    hdrs = ["arena.h"],
    deps = [
        ":allocator",
        ":assert",
    ],
)

cc_library(
    name = "darray",
    srcs = ["darray.c"],
    hdrs = ["darray.h"],
    deps = [
        ":allocator",
    ],
)

cc_test(
    name = "darray_test",
    srcs = ["darray_test.cc"],
    deps = [
        ":darray",
        ":allocator",
        ":arena",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "task",
    srcs = ["task.c"],
    hdrs = ["task.h"],
    deps = [
        ":allocator",
        ":arena",
        ":assert",
        ":logging",
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
    name = "arena_test",
    srcs = ["arena_test.cc"],
    deps = [
        ":arena",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "task_test",
    srcs = ["task_test.cc"],
    deps = [
        ":task",
        ":allocator",
        ":counting_allocator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "string_test",
    srcs = ["string_test.cc"],
    deps = [
        ":string",
        ":allocator",
        ":arena",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "json_reader",
    srcs = ["json_reader.c"],
    hdrs = ["json_reader.h"],
    deps = [
        ":string",
    ],
)

cc_library(
    name = "json_writer",
    srcs = ["json_writer.c"],
    hdrs = ["json_writer.h"],
    deps = [
        ":allocator",
        ":darray",
        ":string",
    ],
)

cc_test(
    name = "json_reader_test",
    srcs = ["json_reader_test.cc"],
    deps = [
        ":json_reader",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "json_writer_test",
    srcs = ["json_writer_test.cc"],
    deps = [
        ":json_writer",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hash_table",
    srcs = ["hash_table.c"],
    hdrs = ["hash_table.h"],
    deps = [
        ":allocator",
        ":assert",
        ":logging",
    ],
)

cc_test(
    name = "hash_table_test",
    srcs = ["hash_table_test.cc"],
    deps = [
        ":hash_table",
        ":allocator",
        "@googletest//:gtest_main",
    ],
)

