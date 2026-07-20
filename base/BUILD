load("@rules_rust//rust:defs.bzl", "rust_library", "rust_test")

package(default_visibility = ["//visibility:public"])

rust_library(
    name = "base",
    srcs = [
        "allocation.rs",
        "json.rs",
        "lib.rs",
        "logging.rs",
        "task.rs",
    ],
    crate_name = "base",
    crate_root = "lib.rs",
    edition = "2024",
    rustc_flags = select({
        "//src:wasm_build": [
            "-C", "panic=abort",
            "-C", "target-feature=+atomics,+bulk-memory,+mutable-globals",
        ],
        "//conditions:default": [],
    }),
)

rust_test(
    name = "base_test",
    args = ["--test-threads=1"],
    crate = ":base",
)
