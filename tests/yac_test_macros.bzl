load("//bazel:yac_cc.bzl", "yac_cc_test")

COMMON_DEPS = [
    "//src:app",
    "//src:presentation",
    "//src:service",
    "@catch2//:catch2_main",
]

AWS_DEPS = [
    "@aws_sdk_bedrock//:bedrock-runtime",
]

def yac_test(name, srcs, deps = [], data = [], defines = [], tags = [], common_deps = COMMON_DEPS, **kwargs):
    yac_cc_test(
        name = name,
        srcs = srcs + [":all_test_headers"],
        data = data,
        defines = defines,
        includes = ["."],
        tags = tags,
        deps = common_deps + deps,
        **kwargs
    )

def yac_aws_test(name, srcs, deps = [], **kwargs):
    yac_test(
        name = name,
        srcs = srcs,
        deps = AWS_DEPS + deps,
        **kwargs
    )

def yac_test_suite(name, extra_tests = []):
    native.test_suite(
        name = name,
        tests = [
            ":" + rule_name
            for rule_name in native.existing_rules().keys()
            if rule_name.startswith("yac_test_") and rule_name != "yac_test_e2e_runner"
        ] + extra_tests,
    )
