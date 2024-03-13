## Prerequisites

To build picotool using Bazel, you will first and foremost need to [get Bazel](https://github.com/bazelbuild/bazel/releases/latest).

### Linux

Use your favorite package tool to install dependencies. For example, on Ubuntu:

```console
sudo apt install build-essential libudev-dev
```

On Linux you can add udev rules in order to run picotool without sudo:

```console
sudo cp udev/99-picotool.rules /etc/udev/rules.d/
```

### macOS

To build on macOS, you'll need to ensure Xcode is installed.

```console
xcode-select --install
```

### Windows

To build on Windows, you must install [Visual Studio for Desktop Development With C++](https://visualstudio.microsoft.com/vs/features/cplusplus/).

## Building picotool

From the root of the picotool repository, run Bazel with the following command:

```console
bazel build //:picotool
```

## Running picotool

To run picotool, run the binary built by Bazel:

```console
./bazel-bin/picotool
```
