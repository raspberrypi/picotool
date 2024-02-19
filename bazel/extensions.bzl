load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def _libusb_impl(_ctx):
    # TODO: Make this configurable so versioning is supported.
    git_repository(
        name = "libusb",
        remote = "https://github.com/armandomontanez/rules_libusb.git",
        commit = "be0b87595cb660f162e54afce2efafc15ecbe301",
    )

libusb = module_extension(
    implementation = _libusb_impl,
)
