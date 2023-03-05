## Building

You need to set PICO_SDK_PATH in the environment, or pass it to cmake.

You also need to install `libusb-1.0`.

### Linux / macOS

Use your favorite package tool to install dependencies. For example, on Ubuntu:

```console
sudo apt install build-essential pkg-config libusb-1.0-0-dev cmake
```

On Linux you can add udev rules in order to run picotool without sudo:

```console
sudo cp udev/99-picotool.rules /etc/udev/rules.d/
```

### Windows

##### For Windows without MinGW

Download libUSB from here https://libusb.info/

set LIBUSB_ROOT environment variable to the install directory.
```console
mkdir build
cd build
cmake -G "NMake Makefiles" ..
nmake
```

##### For Windows with MinGW in WSL

Download libUSB from here https://libusb.info/

set LIBUSB_ROOT environment variable to the install directory.

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
MSYS2_ARG_CONV_EXCL=- cmake .. -G"MSYS Makefiles" -DCMAKE_INSTALL_PREFIX=$MINGW_PREFIX
make
make install DESTDIR=/  # optional
```

## Overview

`picotool` is a tool for inspecting RP2040 binaries, and interacting with RP2040 devices when they are in BOOTSEL mode. (As of version 1.1 of `picotool` it is also possible to interact with RP2040 devices that are not in BOOTSEL mode, but are using USB stdio support from the Raspberry Pi Pico SDK by using the `-f` argument of `picotool`).

Note for additional documentation see https://rptl.io/pico-get-started

```text
$ picotool help
PICOTOOL:
    Tool for interacting with a RP2040 device in BOOTSEL mode, or with a RP2040 binary

SYNOPSIS:
    picotool info [-b] [-p] [-d] [-l] [-a] [--bus <bus>] [--address <addr>] [-f] [-F]
    picotool info [-b] [-p] [-d] [-l] [-a] <filename> [-t <type>]
    picotool load [-n] [-N] [-u] [-v] [-x] <filename> [-t <type>] [-o <offset>] [--bus <bus>] [--address <addr>] [-f] [-F]
    picotool save [-p] [--bus <bus>] [--address <addr>] [-f] [-F] <filename> [-t <type>]
    picotool save -a [--bus <bus>] [--address <addr>] [-f] [-F] <filename> [-t <type>]
    picotool save -r <from> <to> [--bus <bus>] [--address <addr>] [-f] [-F] <filename> [-t <type>]
    picotool verify [--bus <bus>] [--address <addr>] [-f] [-F] <filename> [-t <type>] [-r <from> <to>] [-o <offset>]
    picotool reboot [-a] [-u] [--bus <bus>] [--address <addr>] [-f] [-F]
    picotool version [-s]
    picotool help [<cmd>]

COMMANDS:
    info      Display information from the target device(s) or file.
              Without any arguments, this will display basic information for all connected RP2040 devices in BOOTSEL mode
    load      Load the program / memory range stored in a file onto the device.
    save      Save the program / memory stored in flash on the device to a file.
    verify    Check that the device contents match those in the file.
    reboot    Reboot the device
    version   Display picotool version
    help      Show general help or help for a specific command

Use "picotool help <cmd>" for more info
```

Note commands that aren't acting on files require an RP2040 device in BOOTSEL mode to be connected.

## info

So there is now _Binary Information_ support in the SDK which allows for easily storing compact information that `picotool`
can find (See Binary Info section below). The info command is for reading this information.

The information can be either read from one or more connected RP2040 devices in BOOTSEL mode, or from 
a file. This file can be an ELF, a UF2 or a BIN file.

```text
$ picotool help info
INFO:
    Display information from the target device(s) or file.
    Without any arguments, this will display basic information for all connected RP2040 devices in BOOTSEL mode

SYNOPSIS:
    picotool info [-b] [-p] [-d] [-l] [-a] [--bus <bus>] [--address <addr>] [-f] [-F]
    picotool info [-b] [-p] [-d] [-l] [-a] <filename> [-t <type>]

OPTIONS:
    Information to display
        -b, --basic
            Include basic information. This is the default
        -p, --pins
            Include pin information
        -d, --device
            Include device information
        -l, --build
            Include build attributes
        -a, --all
            Include all information

TARGET SELECTION:
    To target one or more connected RP2040 device(s) in BOOTSEL mode (the default)
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing
            the command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing
            the command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but
            without the RPI-RP2 drive mounted
    To target a file
        <filename>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
```

Note the -f arguments vary slightly for Windows vs macOS / Unix platforms.

e.g.

```text
$ picotool info
Program Information
 name:      hello_world
 features:  stdout to UART
```

```text
$ picotool info -a
Program Information
 name:          hello_world
 features:      stdout to UART
 binary start:  0x10000000
 binary end:    0x1000606c

Fixed Pin Information
 20:  UART1 TX
 21:  UART1 RX

Build Information
 build date:        Dec 31 2020
 build attributes:  Debug build

Device Information
 flash size:   2048K
 ROM version:  2
```

```text
$ picotool info -bp
Program Information
 name:      hello_world
 features:  stdout to UART

Fixed Pin Information
 20:  UART1 TX
 21:  UART1 RX
```

```text
$ picotool info -a lcd_1602_i2c.uf2
File lcd_1602_i2c.uf2:

Program Information
 name:          lcd_1602_i2c
 web site:      https://github.com/raspberrypi/pico-examples/tree/HEAD/i2c/lcd_1602_i2c
 binary start:  0x10000000
 binary end:    0x10003c1c

Fixed Pin Information
 4:  I2C0 SDA
 5:  I2C0 SCL

Build Information
 build date:  Dec 31 2020
```

## load

Load allows you to write data from a file into flash

```text
$ picotool help load
LOAD:
    Load the program / memory range stored in a file onto the device.

SYNOPSIS:
    picotool load [-n] [-N] [-u] [-v] [-x] <filename> [-t <type>] [-o <offset>] [--bus <bus>] [--address <addr>] [-f] [-F]

OPTIONS:
    Post load actions
        -n, --no-overwrite
            When writing flash data, do not overwrite an existing program in flash. If picotool cannot determine the size/presence
            of the program in flash, the command fails
        -N, --no-overwrite-unsafe
            When writing flash data, do not overwrite an existing program in flash. If picotool cannot determine the size/presence
            of the program in flash, the load continues anyway
        -u, --update
            Skip writing flash sectors that already contain identical data
        -v, --verify
            Verify the data was written correctly
        -x, --execute
            Attempt to execute the downloaded file as a program after the load
    File to load from
        <filename>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
    BIN file options
        -o, --offset
            Specify the load address for a BIN file
        <offset>
            Load offset (memory address; default 0x10000000)
    Target device selection
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing
            the command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing
            the command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but
            without the RPI-RP2 drive mounted
```

e.g.

```text
$ picotool load blink.uf2
Loading into Flash: [==============================]  100%
```

## save

Save allows you to save a range of memory or a program or the whole of flash from the device to a BIN file or a UF2 file

```text
$ picotool help save
SAVE:
    Save the program / memory stored in flash on the device to a file.

SYNOPSIS:
    picotool save [-p] [--bus <bus>] [--address <addr>] [-f] [-F] <filename> [-t <type>]
    picotool save -a [--bus <bus>] [--address <addr>] [-f] [-F] <filename> [-t <type>]
    picotool save -r <from> <to> [--bus <bus>] [--address <addr>] [-f] [-F] <filename> [-t <type>]

OPTIONS:
    Selection of data to save
        -p, --program
            Save the installed program only. This is the default
        -a, --all
            Save all of flash memory
        -r, --range
            Save a range of memory. Note that UF2s always store complete 256 byte-aligned blocks of 256 bytes, and the range is
            expanded accordingly
        <from>
            The lower address bound in hex
        <to>
            The upper address bound in hex
    Source device selection
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing
            the command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing
            the command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but
            without the RPI-RP2 drive mounted
    File to save to
        <filename>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
```

e.g.

```text
$ picotool info
Program Information
name:      lcd_1602_i2c
web site:  https://github.com/raspberrypi/pico-examples/tree/HEAD/i2c/lcd_1602_i2c
```
```text
$ picotool save spoon.uf2
Saving file: [==============================]  100%
Wrote 51200 bytes to spoon.uf2
```
```text
$ picotool info spoon.uf2
File spoon.uf2:
Program Information
name:      lcd_1602_i2c
web site:  https://github.com/raspberrypi/pico-examples/tree/HEAD/i2c/lcd_1602_i2c
```

## Binary Information

Binary information is machine locatable and generally machine consumable. I say generally because anyone can
include any information, and we can tell it from ours, but it is up to them whether they make their data self describing.

Note that we will certainly add more binary info over time, but I'd like to get a minimum core set included
in most binaries from launch!!

### Basic Information

This information is really handy when you pick up a Pico and don't know what is on it!

Basic information includes

- program name
- program description
- program version string
- program build date
- program url
- program end address
- program features - this is a list built from individual strings in the binary, that can be displayed (e.g. we will have one for UART stdio and one for USB stdio) in the SDK
- build attributes - this is a similar list of strings, for things pertaining to the binary itself (e.g. Debug Build)

The binary information is self-describing/extensible, so programs can include information picotool is not aware of (e.g. MicroPython includes a list of in-built libraries)

### Pins

This is certainly handy when you have an executable called 'hello_world.elf' but you forgot what board it is built for...

Static (fixed) pin assignments can be recorded in the binary in very compact form:

```text
$ picotool info --pins sprite_demo.elf
File sprite_demo.elf:

Fixed Pin Information
0-4:    Red 0-4
6-10:   Green 0-4
11-15:  Blue 0-4
16:     HSync
17:     VSync
18:     Display Enable
19:     Pixel Clock
20:     UART1 TX
21:     UART1 RX
```

### Including Binary information

Binary information is declared in the program by macros (vile warped macros); for the previous example:

```text
$ picotool info --pins sprite_demo.elf
File sprite_demo.elf:

Fixed Pin Information
0-4:    Red 0-4
6-10:   Green 0-4
11-15:  Blue 0-4
16:     HSync
17:     VSync
18:     Display Enable
19:     Pixel Clock
20:     UART1 TX
21:     UART1 RX
```

... there is one line in the `setup_default_uart` function:

```c
bi_decl_if_func_used(bi_2pins_with_func(PICO_DEFAULT_UART_RX_PIN, PICO_DEFAULT_UART_TX_PIN, GPIO_FUNC_UART));
```


The two pin numbers, and the function UART are stored, then decoded to their actual function names (UART1 TX etc) by picotool.
The `bi_decl_if_func_used` makes sure the binary information is only included if the containing function is called.

Equally, the video code contains a few lines like this:

```c
bi_decl_if_func_used(bi_pin_mask_with_name(0x1f << (PICO_SCANVIDEO_COLOR_PIN_BASE + PICO_SCANVIDEO_DPI_PIXEL_RSHIFT), "Red 0-4"));
```

### Details

Things are designed to waste as little space as possible, but you can turn everything off with preprocessor var `PICO_NO_BINARY_INFO=1`. Additionally
any SDK code that inserts binary info can be separately excluded by its own preprocesor var.

You need
```c
#include "pico/binary_info.h"
```

Basically you either use `bi_decl(bi_blah(...))` for unconditional inclusion of the binary info blah, or
`bi_decl_if_func_used(bi_blah(...))` for binary information that may be stripped if the enclosing function
is not included in the binary by the linker (think `--gc-sections`)

There are a bunch of bi_ macros in the headers

```c
#define bi_binary_end(end) ...
#define bi_program_name(name) ...
#define bi_program_description(description) ...
#define bi_program_version_string(version_string) ...
#define bi_program_build_date_string(date_string) ...
#define bi_program_url(url) ...
#define bi_program_feature(feature) ...
#define bi_program_build_attribute(attr) ...
#define bi_1pin_with_func(p0, func) ...
#define bi_2pins_with_func(p0, p1, func) ...
#define bi_3pins_with_func(p0, p1, p2, func) ...
#define bi_4pins_with_func(p0, p1, p2, p3, func) ...
#define bi_5pins_with_func(p0, p1, p2, p3, p4, func) ...
#define bi_pin_range_with_func(plo, phi, func) ...
#define bi_pin_mask_with_name(pmask, label) ...
#define bi_pin_mask_with_names(pmask, label) ...
#define bi_1pin_with_name(p0, name) ...
#define bi_2pins_with_names(p0, name0, p1, name1) ...
#define bi_3pins_with_names(p0, name0, p1, name1, p2, name2) ...
#define bi_4pins_with_names(p0, name0, p1, name1, p2, name2, p3, name3) ... 
```

which make use of underlying macros, e.g.
```c
#define bi_program_url(url) bi_string(BINARY_INFO_TAG_RASPBERRY_PI, BINARY_INFO_ID_RP_PROGRAM_URL, url)
```

NOTE: It is easy to forget to enclose these in `bi_decl` etc., so an effort has been made (at the expense of a lot of kittens)
to make the build fail with a _somewhat_ helpful error message if you do so.

For example, trying to compile

```c
bi_1pin_with_name(0, "Toaster activator");
```

gives

```
/home/graham/dev/mu/pico_sdk/src/common/pico_binary_info/include/pico/binary_info/code.h:17:55: error: '_error_bi_is_missing_enclosing_decl_261' undeclared here (not in a function)
17 | #define __bi_enclosure_check_lineno_var_name __CONCAT(_error_bi_is_missing_enclosing_decl_,__LINE__)
|                                                       ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
... more macro call stack of doom
```

## Setting common fields from CMake

You can use 

```cmake
pico_set_program_name(foo "not foo") # as "foo" would be the default
pico_set_program_description(foo "this is a foo")
pico_set_program_version(foo "0.00001a")
pico_set_program_url(foo "www.plinth.com/foo")
```

Note all of these are passed as command line arguments to the compilation, so if you plan to use
quotes, newlines etc you may have better luck defining via bi_decl in the code.

## Additional binary information/picotool features

### Block devices

MicroPython and CircuitPython, eventually the SDK and others may support one or more storage devices in flash. We already
have macros to define these although picotool doesn't do anything with them yet... but backup/restore/file copy and even fuse mount
in the future might be interesting.

I suggest we tag these now... 

This is what I have right now off the top of my head (at the time)
```c
#define bi_block_device(_tag, _name, _offset, _size, _extra, _flags)
```
with the data going into
```c
typedef struct __packed _binary_info_block_device {
        struct _binary_info_core core;
        bi_ptr_of(const char) name; // optional static name (independent of what is formatted)
        uint32_t offset;
        uint32_t size;
        bi_ptr_of(binary_info_t) extra; // additional info
        uint16_t flags;
} binary_info_block_device_t;
```
and
```c
enum {
    BINARY_INFO_BLOCK_DEV_FLAG_READ = 1 << 0, // if not readable, then it is basically hidden, but tools may choose to avoid overwriting it
    BINARY_INFO_BLOCK_DEV_FLAG_WRITE = 1 << 1,
    BINARY_INFO_BLOCK_DEV_FLAG_REFORMAT = 1 << 2, // may be reformatted..

    BINARY_INFO_BLOCK_DEV_FLAG_PT_UNKNOWN = 0 << 4, // unknown free to look
    BINARY_INFO_BLOCK_DEV_FLAG_PT_MBR = 1 << 4, // expect MBR
    BINARY_INFO_BLOCK_DEV_FLAG_PT_GPT = 2 << 4, // expect GPT
    BINARY_INFO_BLOCK_DEV_FLAG_PT_NONE = 3 << 4, // no partition table
};
```
### USB device descriptors

Seems like tagging these might be nice (we just need to store the pointer to it assuming - as is often the case -
the descriptor is just a linear chunk of memory) ... I assume there is a tool out there to prettify such a thing if picotool dumps the descriptor
in binary.

### Issues

If you ctrl+c out of the middle of a long operation, then libusb seems to get a bit confused, which means we aren't able
to unlock our lockout of USB MSD writes (we have turned them off so the user doesn't step on their own toes). Simply running
`picotool info` again will unlock it properly the next time (or you can reboot the device).
