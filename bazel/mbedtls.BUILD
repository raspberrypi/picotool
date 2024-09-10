package(default_visibility = ["//visibility:public"])

cc_library(
    name = "mbedtls",
    srcs = glob(["library/*.c"]),
    hdrs = glob(
        include = [
            "include/**/*.h",
            "library/*.h",
        ],
    ),
    includes = ["include"],
    linkopts = select({
        "@rules_cc//cc/compiler:msvc-cl": ["-DEFAULTLIB:AdvAPI32.Lib"],
        "//conditions:default": [],
    }),
    deps = ["@picotool//lib:mbedtls_config"],
)
