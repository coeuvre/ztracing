load("@rules_rust//rust:defs.bzl", _rust_binary = "rust_binary", _rust_library = "rust_library", _rust_test = "rust_test")

WASM_RUSTC_FLAGS = select({
    "//rules:wasm_build": [
        "-C", "panic=abort",
        "-C", "target-feature=+atomics,+bulk-memory,+mutable-globals",
        "-C", "debuginfo=2",
        "-C", "symbol-mangling-version=v0",
    ],
    "//conditions:default": [],
})

def rust_library(name, **kwargs):
    _rust_library(
        name = name,
        edition = kwargs.pop("edition", "2024"),
        rustc_flags = WASM_RUSTC_FLAGS + kwargs.pop("rustc_flags", []),
        **kwargs
    )

def rust_binary(name, **kwargs):
    _rust_binary(
        name = name,
        edition = kwargs.pop("edition", "2024"),
        rustc_flags = WASM_RUSTC_FLAGS + kwargs.pop("rustc_flags", []),
        **kwargs
    )

def rust_test(name, **kwargs):
    _rust_test(
        name = name,
        edition = kwargs.pop("edition", "2024"),
        **kwargs
    )
