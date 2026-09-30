load("@bazel_skylib//rules:run_binary.bzl", "run_binary")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

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

def _provision_boards_header_impl(ctx):
    # A board's cc_library carries all the board headers (plus a few others)
    headers = [
        f
        for f in ctx.attr.board[CcInfo].compilation_context.headers.to_list()
        if "include/boards/" in f.path and f.basename.endswith(".h")
    ]
    args = ctx.actions.args()
    args.add(ctx.outputs.out)
    args.add_all(headers)
    ctx.actions.run(
        executable = ctx.executable._tool,
        arguments = [args],
        inputs = headers,
        outputs = [ctx.outputs.out],
        mnemonic = "ProvisionBoards",
        progress_message = "Generating %{output}",
    )
    return [DefaultInfo(files = depset([ctx.outputs.out]))]

# Generates the provision connect --board pin table from the SDK board headers
provision_boards_header = rule(
    implementation = _provision_boards_header_impl,
    attrs = {
        "board": attr.label(providers = [CcInfo]),
        "out": attr.output(mandatory = True),
        "_tool": attr.label(
            default = "@picotool//board_header_parser:board_header_parser",
            executable = True,
            cfg = "exec",
        ),
    },
)
