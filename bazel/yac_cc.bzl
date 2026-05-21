load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

YAC_COPTS = [
    "-iquote",
    "src",
] + select({
    "@platforms//os:macos": [
        "-fexperimental-library",
        "-mmacosx-version-min=13.3",
    ],
    "//conditions:default": [],
})

YAC_LINKOPTS = select({
    "@platforms//os:macos": [
        "-fexperimental-library",
        "-mmacosx-version-min=13.3",
    ],
    "//conditions:default": [],
})

def yac_cc_library(name, copts = [], linkopts = [], **kwargs):
    cc_library(
        name = name,
        copts = YAC_COPTS + copts,
        linkopts = YAC_LINKOPTS + linkopts,
        **kwargs
    )

def yac_cc_binary(name, copts = [], linkopts = [], **kwargs):
    cc_binary(
        name = name,
        copts = YAC_COPTS + copts,
        linkopts = YAC_LINKOPTS + linkopts,
        **kwargs
    )

def yac_cc_test(name, copts = [], linkopts = [], **kwargs):
    cc_test(
        name = name,
        copts = YAC_COPTS + copts,
        linkopts = YAC_LINKOPTS + linkopts,
        **kwargs
    )
