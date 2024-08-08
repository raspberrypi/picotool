#!/usr/bin/env python3
#
# Copyright (c) 2024 Raspberry Pi (Trading) Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause
"""Generate a header that provides a JSON file as a string."""

import argparse
from pathlib import Path
import sys


_BYTES_PER_LINE = 32


def _parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__,
    )
    parser.add_argument(
        "data",
        type=argparse.FileType("rb"),
        help="Path to data file to generate a header for",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=argparse.FileType("wb"),
        default=sys.stdout.buffer,
        help="Output file path. Defaults to stdout.",
    )
    return parser.parse_args()


def generate_header(data, output):
    var_name = Path(output.name).stem.replace(".", "_")
    include_guard = f"_{var_name.upper()}_H"
    prefix_lines = (
        f"#ifndef {include_guard}",
        f"#define {include_guard}",
        "",
        "#include <string>",
        "",
        f'const std::string {var_name} = R"""(',
    )
    output.write("\n".join(prefix_lines).encode())

    output.write(data.read())

    suffix_lines = (
        ')""";',
        "",
        f"#endif  // {include_guard}",
        "",
    )
    output.write("\n".join(suffix_lines).encode())


if __name__ == "__main__":
    sys.exit(generate_header(**vars(_parse_args())))
