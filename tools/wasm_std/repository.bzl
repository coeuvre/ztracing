def _wasm_std_repository_impl(ctx):
    if ctx.os.name == "linux" and ctx.os.arch == "amd64":
        cargo = Label(ctx.attr.cargo_linux_x86_64)
        rustc = Label(ctx.attr.rustc_linux_x86_64)
    elif ctx.os.name == "mac os x" and ctx.os.arch == "aarch64":
        cargo = Label(ctx.attr.cargo_macos_aarch64)
        rustc = Label(ctx.attr.rustc_macos_aarch64)
    else:
        fail("threaded WASM builds are unsupported on {} {}".format(ctx.os.name, ctx.os.arch))

    ctx.download_and_extract(
        url = "https://static.rust-lang.org/dist/rust-src-1.97.0.tar.xz",
        sha256 = "f2530acd6b4da25a21151e3560d908b31f082edd6968f96d2682c8c2c15c66b7",
        stripPrefix = "rust-src-1.97.0/rust-src/lib/rustlib/src/rust",
        output = "rust-src",
    )
    ctx.file("Cargo.toml", """\
[package]
name = "wasm-atomic-std-bootstrap"
version = "0.0.0"
edition = "2024"

[lib]
path = "bootstrap.rs"

[profile.release]
panic = "abort"
""")
    ctx.file("bootstrap.rs", "#![no_std]\n")
    result = ctx.execute(
        [
            ctx.path(cargo),
            "build",
            "--target",
            "wasm32-unknown-emscripten",
            "--release",
            "-Z",
            "build-std=std,panic_abort",
        ],
        environment = {
            "__CARGO_TESTS_ONLY_SRC_ROOT": str(ctx.path("rust-src/library")),
            "CARGO_HOME": str(ctx.path("cargo-home")),
            "CARGO_TARGET_DIR": str(ctx.path("target")),
            "RUSTC": str(ctx.path(rustc)),
            "RUSTC_BOOTSTRAP": "1",
            "RUSTFLAGS": "-Ctarget-feature=+atomics,+bulk-memory,+mutable-globals -Cpanic=abort",
        },
        quiet = False,
        timeout = 1200,
    )
    if result.return_code:
        fail("failed to build threaded WASM Rust standard library:\n{}\n{}".format(result.stdout, result.stderr))
    ctx.file("BUILD.bazel", """\
load("@rules_rust//rust:toolchain.bzl", "rust_stdlib_filegroup")

rust_stdlib_filegroup(
    name = "rust_std",
    srcs = glob([
        "target/wasm32-unknown-emscripten/release/deps/*.rlib",
        "target/wasm32-unknown-emscripten/release/deps/*.rmeta",
    ]),
    visibility = ["//visibility:public"],
)
""")

wasm_std_repository = repository_rule(
    implementation = _wasm_std_repository_impl,
    attrs = {
        "cargo_linux_x86_64": attr.string(mandatory = True),
        "cargo_macos_aarch64": attr.string(mandatory = True),
        "rustc_linux_x86_64": attr.string(mandatory = True),
        "rustc_macos_aarch64": attr.string(mandatory = True),
    },
)
