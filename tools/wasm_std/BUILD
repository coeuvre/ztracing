load("@rules_rust//rust:toolchain.bzl", "rust_toolchain")

rust_toolchain(
    name = "wasm_atomic_toolchain_linux_x86_64_impl",
    rust_doc = "@rust_host_tools_linux_x86_64//:rustdoc",
    rust_std = "@wasm_atomic_std//:rust_std",
    rustc = "@rust_host_tools_linux_x86_64//:rustc",
    linker = "@rust_host_tools_linux_x86_64//:rust-lld",
    linker_type = "direct",
    rust_objcopy = "@rust_host_tools_linux_x86_64//:rust-objcopy",
    cargo = "@rust_host_tools_linux_x86_64//:cargo",
    clippy_driver = "@rust_host_tools_linux_x86_64//:clippy_driver_bin",
    cargo_clippy = "@rust_host_tools_linux_x86_64//:cargo_clippy_bin",
    llvm_cov = "@rust_host_tools_linux_x86_64//:llvm_cov_bin",
    llvm_profdata = "@rust_host_tools_linux_x86_64//:llvm_profdata_bin",
    llvm_lib = "@rust_host_tools_linux_x86_64//:llvm_lib",
    rustc_lib = "@rust_host_tools_linux_x86_64//:rustc_lib",
    binary_ext = ".js",
    staticlib_ext = ".js",
    dylib_ext = ".js",
    stdlib_linkflags = [],
    default_edition = "2024",
    exec_triple = "x86_64-unknown-linux-gnu",
    target_triple = "wasm32-unknown-emscripten",
    version = "1.97.0",
    channel = "stable",
)

toolchain(
    name = "wasm_atomic_toolchain_linux_x86_64",
    exec_compatible_with = [
        "@@platforms//cpu:x86_64",
        "@@platforms//os:linux",
    ],
    target_compatible_with = [
        "@@platforms//cpu:wasm32",
        "@@platforms//os:emscripten",
    ],
    target_settings = ["@rules_rust//rust/toolchain/channel:stable"],
    toolchain = ":wasm_atomic_toolchain_linux_x86_64_impl",
    toolchain_type = "@rules_rust//rust:toolchain",
)

rust_toolchain(
    name = "wasm_atomic_toolchain_macos_aarch64_impl",
    rust_doc = "@rust_host_tools_macos_aarch64//:rustdoc",
    rust_std = "@wasm_atomic_std//:rust_std",
    rustc = "@rust_host_tools_macos_aarch64//:rustc",
    linker = "@rust_host_tools_macos_aarch64//:rust-lld",
    linker_type = "direct",
    rust_objcopy = "@rust_host_tools_macos_aarch64//:rust-objcopy",
    cargo = "@rust_host_tools_macos_aarch64//:cargo",
    clippy_driver = "@rust_host_tools_macos_aarch64//:clippy_driver_bin",
    cargo_clippy = "@rust_host_tools_macos_aarch64//:cargo_clippy_bin",
    llvm_cov = "@rust_host_tools_macos_aarch64//:llvm_cov_bin",
    llvm_profdata = "@rust_host_tools_macos_aarch64//:llvm_profdata_bin",
    llvm_lib = "@rust_host_tools_macos_aarch64//:llvm_lib",
    rustc_lib = "@rust_host_tools_macos_aarch64//:rustc_lib",
    binary_ext = ".js",
    staticlib_ext = ".js",
    dylib_ext = ".js",
    stdlib_linkflags = [],
    default_edition = "2024",
    exec_triple = "aarch64-apple-darwin",
    target_triple = "wasm32-unknown-emscripten",
    version = "1.97.0",
    channel = "stable",
)

toolchain(
    name = "wasm_atomic_toolchain_macos_aarch64",
    exec_compatible_with = [
        "@@platforms//cpu:aarch64",
        "@@platforms//os:osx",
    ],
    target_compatible_with = [
        "@@platforms//cpu:wasm32",
        "@@platforms//os:emscripten",
    ],
    target_settings = ["@rules_rust//rust/toolchain/channel:stable"],
    toolchain = ":wasm_atomic_toolchain_macos_aarch64_impl",
    toolchain_type = "@rules_rust//rust:toolchain",
)
