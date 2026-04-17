load("@rules_cc//cc:defs.bzl", _cc_binary = "cc_binary", _cc_library = "cc_library")

COMMON_COPTS = [
    "-std=c++20",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wpedantic",
    "-Wshadow",
    "-Wconversion",
    "-Wunused-parameter",
    "-Wformat=2",
]

def cc_library(name, **kwargs):
    _cc_library(
        name = name,
        copts = COMMON_COPTS + kwargs.pop("copts", []),
        **kwargs
    )

def cc_binary(name, **kwargs):
    _cc_binary(
        name = name,
        copts = COMMON_COPTS + kwargs.pop("copts", []),
        **kwargs
    )
