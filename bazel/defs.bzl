load("@bazel_skylib//rules:run_binary.bzl", "run_binary")

def picotool_binary_data_header(name, src, out, **kwargs):
    run_binary(
        name = name,
        srcs = [src],
        outs = [out],
        args = [
            "$(location {})".format(src),
            "-o=$(location {})".format(out),
        ],
        tool = "@picotool//bazel:binh",
        **kwargs
    )

def otp_header_parse(name, src, out, **kwargs):
    json_path = out + ".json"
    run_binary(
        name = name + "_json",
        srcs = [src],
        outs = [json_path],
        args = [
            "$(location {})".format(src),
            "$(location {})".format(json_path),
        ],
        tool = "@picotool//otp_header_parser:otp_header_parser",
        **kwargs
    )

    run_binary(
        name = name,
        srcs = [json_path],
        outs = [out],
        args = [
            "$(location {})".format(json_path),
            "-o=$(location {})".format(out),
        ],
        tool = "@picotool//bazel:jsonh",
        **kwargs
    )
