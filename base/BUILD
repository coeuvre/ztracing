load("//rules:rust.bzl", "rust_library", "rust_test")

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
)

rust_test(
    name = "base_test",
    args = ["--test-threads=1"],
    crate = ":base",
)
