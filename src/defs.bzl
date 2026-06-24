load("@rules_cc//cc:defs.bzl", _cc_binary = "cc_binary", _cc_library = "cc_library", _cc_test = "cc_test")

COMMON_COPTS = [
    "-pthread",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wpedantic",
    "-Wshadow",
    "-Wconversion",
    "-Wunused-parameter",
    "-Wformat=2",
    "-Wno-missing-field-initializers",
    "-Wno-missing-designated-field-initializers",
]

WASM_SIMD_COPTS = select({
    "//src:wasm_build": ["-msimd128"],
    "//src:wasm_and_headless_build": ["-msimd128"],
    "//conditions:default": [],
})

def cc_library(name, **kwargs):
    _cc_library(
        name = name,
        copts = COMMON_COPTS + WASM_SIMD_COPTS + kwargs.pop("copts", []),
        conlyopts = ["-std=c23"] + kwargs.pop("conlyopts", []),
        cxxopts = ["-std=c++20"] + kwargs.pop("cxxopts", []),
        **kwargs
    )

def cc_binary(name, **kwargs):
    _cc_binary(
        name = name,
        copts = COMMON_COPTS + WASM_SIMD_COPTS + kwargs.pop("copts", []),
        conlyopts = ["-std=c23"] + kwargs.pop("conlyopts", []),
        cxxopts = ["-std=c++20"] + kwargs.pop("cxxopts", []),
        **kwargs
    )

def cc_test(name, **kwargs):
    _cc_test(
        name = name,
        copts = COMMON_COPTS + WASM_SIMD_COPTS + kwargs.pop("copts", []),
        conlyopts = ["-std=c23"] + kwargs.pop("conlyopts", []),
        cxxopts = ["-std=c++20"] + kwargs.pop("cxxopts", []),
        **kwargs
    )
