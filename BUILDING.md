## Building

You need to set PICO_SDK_PATH in the environment, or pass it to cmake with `-DPICO_SDK_PATH=/path/to/pico-sdk`. To use features such as signing or hashing, you will need to make sure the mbedtls submodule in the SDK is checked out - this can be done by running this from your SDK directory.

```console
git submodule update --init lib/mbedtls
```

You also need to install `libusb-1.0` if you want to use the USB functionality.

> If libusb-1.0 is not installed, picotool still builds, but it omits all options that deal with managing a pico via USB (load, save, erase, verify, reboot). Builds that do not include USB support can be identified by running `picotool version`, which will state `This version of picotool was compiled without USB support. Some commands are not available.`. Additionally, the unsupported commands won't appear in the output of the help command, and if you attempt to execute an invalid command you will get an error message. The build output message `libUSB is not found - no USB support will be built` also appears in the build logs.

### Linux / macOS

Use your favorite package tool to install dependencies. For example, on Ubuntu:

```console
sudo apt install build-essential pkg-config libusb-1.0-0-dev cmake
```

Then simply build like a normal CMake project:

```console
mkdir build
cd build
cmake ..
make
```

On Linux you can add udev rules in order to run picotool without sudo:

```console
sudo cp udev/60-picotool.rules /etc/udev/rules.d/
```

### Windows

##### For Windows without MinGW

Download libUSB from here https://libusb.info/

Set LIBUSB_ROOT environment variable to the install directory.
```console
mkdir build
cd build
cmake -G "NMake Makefiles" ..
nmake
```

##### For Windows with MinGW in WSL

Download libUSB from here https://libusb.info/

Set LIBUSB_ROOT environment variable to the install directory.

```console
mkdir build
cd build
cmake ..
make
```

##### For Windows with MinGW in MSYS2:

No need to download libusb separately or set `LIBUSB_ROOT`.

```console
pacman -S $MINGW_PACKAGE_PREFIX-{toolchain,cmake,libusb}
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$MINGW_PREFIX
cmake --build .
```

## Installing (so the Pico SDK can find it)

The Raspberry Pi Pico SDK ([pico-sdk](https://github.com/raspberrypi/pico-sdk)) version 2.0.0 and above uses `picotool` to do the ELF-to-UF2 conversion previously handled by the `elf2uf2` tool in the SDK. The SDK also uses `picotool` to hash and sign binaries.

Whilst the SDK can download picotool on its own per project, if you have multiple projects or build configurations, it is preferable to install a single copy of `picotool` locally. This can be done most simply with `make install` or `cmake --install .`, using `sudo` if required; the SDK will use this installed version by default.

> On some Linux systems, the `~/.local` prefix may be used for an install without `sudo`; from your build directory simply run
> ```console
> cmake -DCMAKE_INSTALL_PREFIX=~/.local ..
> make install
> ```
> This will only work if `~/.local/bin` is included in your `PATH`

### Custom Path Installation (eg if you can't use `sudo`)

Alternatively, you can install to a custom path via:

```console
cmake -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR -DPICOTOOL_FLAT_INSTALL=1 ..
make install
```

In order for the SDK to find `picotool` in this custom folder, you will usually need to set the `picotool_DIR` variable in your project. This can be achieved either by setting the `picotool_DIR` environment variable to `$MY_INSTALL_DIR/picotool`, by passing `-Dpicotool_DIR=$MY_INSTALL_DIR/picotool` to your `cmake` command, or by adding `set(picotool_DIR $MY_INSTALL_DIR/picotool)` to your CMakeLists.txt file.

> See the [find_package documentation](https://cmake.org/cmake/help/latest/command/find_package.html#config-mode-search-procedure) for more details
