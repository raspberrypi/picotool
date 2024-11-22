## Building

You need to set PICO_SDK_PATH in the environment, or pass it to cmake with `-DPICO_SDK_PATH=/path/to/pico-sdk`. To use features such as signing or hashing, you will need to make sure the mbedtls submodule in the SDK is checked out - this can be done by running this from your SDK directory.

```console
git submodule update --init lib/mbedtls
```

You also need to install `libusb-1.0`.

### Linux / macOS

Use your favorite package tool to install dependencies. For example, on Ubuntu:

```console
sudo apt install build-essential pkg-config libusb-1.0-0-dev cmake
```

> If libusb-1.0-0-dev is not installed, picotool still builds, but it omits all options that deal with managing a pico via USB (load, save, erase, verify, reboot). Builds that do not include USB support can be recognized because these commands also do not appear in the help command. The build output message 'libUSB is not found - no USB support will be built' also appears in the build logs.

Then simply build like a normal CMake project:

```console
mkdir build
cd build
cmake ..
make
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
cmake .. -DCMAKE_INSTALL_PREFIX=$MINGW_PREFIX
cmake --build .
```

## Usage by the Raspberry Pi Pico SDK

The Raspberry Pi Pico SDK ([pico-sdk](https://github.com/raspberrypi/pico-sdk)) version 2.0.0 and above uses `picotool` to do the ELF-to-UF2 conversion previously handled by the `elf2uf2` tool in the SDK. The SDK also uses `picotool` to hash and sign binaries.

Whilst the SDK can download picotool on its own per project, if you have multiple projects or build configurations, it is preferable to install a single copy of `picotool` locally. This can be done most simply with `make install` or `cmake --install .`, using `sudo` if required; the SDK will use this installed version by default.

> On some Linux systems, the `~/.local` prefix may be used for an install without `sudo`; from your build directory simply run
> ```console
> cmake -DCMAKE_INSTALL_PREFIX=~/.local ..
> make install
> ```
> This will only work if `~/.local` is included in your `PATH`

Alternatively, you can install to a custom path via:

```console
cmake -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR -DPICOTOOL_FLAT_INSTALL=1 ..
make install
```

In order for the SDK to find `picotool` in this custom folder, you will usually need to set the `picotool_DIR` variable in your project. This can be achieved either by setting the `picotool_DIR` environment variable to `$MY_INSTALL_DIR/picotool`, by passing `-Dpicotool_DIR=$MY_INSTALL_DIR/picotool` to your `cmake` command, or by adding `set(picotool_DIR $MY_INSTALL_DIR/picotool)` to your CMakeLists.txt file.

> See the [find_package documentation](https://cmake.org/cmake/help/latest/command/find_package.html#config-mode-search-procedure) for more details

## Overview

`picotool` is a tool for working with RP2040/RP2350 binaries, and interacting with RP2040/RP2350 devices when they are in BOOTSEL mode. (As of version 1.1 of `picotool` it is also possible to interact with devices that are not in BOOTSEL mode, but are using USB stdio support from the Raspberry Pi Pico SDK by using the `-f` argument of `picotool`).

Note for additional documentation see https://rptl.io/pico-get-started

```
$ picotool help
PICOTOOL:
    Tool for interacting with RP-series device(s) in BOOTSEL mode, or with an RP-series binary

SYNOPSIS:
    picotool info [-b] [-m] [-p] [-d] [--debug] [-l] [-a] [device-selection]
    picotool info [-b] [-m] [-p] [-d] [--debug] [-l] [-a] <filename> [-t <type>]
    picotool config [-s <key> <value>] [-g <group>] [device-selection]
    picotool config [-s <key> <value>] [-g <group>] <filename> [-t <type>]
    picotool load [--ignore-partitions] [--family <family_id>] [-p <partition>] [-n] [-N] [-u] [-v] [-x] <filename> [-t <type>] [-o <offset>] [device-selection]
    picotool encrypt [--quiet] [--verbose] [--hash] [--sign] <infile> [-t <type>] [-o <offset>] <outfile> [-t <type>] <aes_key> [-t <type>] [<signing_key>] [-t <type>]
    picotool seal [--quiet] [--verbose] [--hash] [--sign] [--clear] <infile> [-t <type>] [-o <offset>] <outfile> [-t <type>] [<key>] [-t <type>] [<otp>] [-t <type>] [--major <major>] [--minor <minor>] [--rollback <rollback> [<rows>..]]
    picotool link [--quiet] [--verbose] <outfile> [-t <type>] <infile1> [-t <type>] <infile2> [-t <type>] [<infile3>] [-t <type>] [-p] <pad>
    picotool save [-p] [-v] [--family <family_id>] [device-selection]
    picotool save -a [-v] [--family <family_id>] [device-selection]
    picotool save -r <from> <to> [-v] [--family <family_id>] [device-selection]
    picotool erase [-a] [device-selection]
    picotool erase -p <partition> [device-selection]
    picotool erase -r <from> <to> [device-selection]
    picotool verify [device-selection]
    picotool reboot [-a] [-u] [-g <partition>] [-c <cpu>] [device-selection]
    picotool otp list|get|set|load|dump|permissions|white-label
    picotool partition info|create
    picotool uf2 info|convert
    picotool version [-s] [<version>]
    picotool coprodis [--quiet] [--verbose] <infile> [-t <type>] <outfile> [-t <type>]
    picotool help [<cmd>]

COMMANDS:
    info        Display information from the target device(s) or file.
                Without any arguments, this will display basic information for all connected RP-series devices in BOOTSEL mode
    config      Display or change program configuration settings from the target device(s) or file.
    load        Load the program / memory range stored in a file onto the device.
    encrypt     Encrypt the program.
    seal        Add final metadata to a binary, optionally including a hash and/or signature.
    link        Link multiple binaries into one block loop.
    save        Save the program / memory stored in flash on the device to a file.
    erase       Erase the program / memory stored in flash on the device.
    verify      Check that the device contents match those in the file.
    reboot      Reboot the device
    otp         Commands related to the RP2350 OTP (One-Time-Programmable) Memory
    partition   Commands related to RP2350 Partition Tables
    uf2         Commands related to UF2 creation and status
    version     Display picotool version
    coprodis    Post-process coprocessor instructions in disassembly files.
    help        Show general help or help for a specific command

Use "picotool help <cmd>" for more info
```

Note commands that aren't acting on files require a device in BOOTSEL mode to be connected.

## info

The is _Binary Information_ support in the SDK which allows for easily storing compact information that `picotool`
can find (See Binary Info section below). The info command is for reading this information.

The information can be either read from one or more connected devices in BOOTSEL mode, or from 
a file. This file can be an ELF, a UF2 or a BIN file.

```
$ picotool help info
INFO:
    Display information from the target device(s) or file.
    Without any arguments, this will display basic information for all connected RP-series devices in BOOTSEL mode

SYNOPSIS:
    picotool info [-b] [-m] [-p] [-d] [--debug] [-l] [-a] [device-selection]
    picotool info [-b] [-m] [-p] [-d] [--debug] [-l] [-a] <filename> [-t <type>]

OPTIONS:
    Information to display
        -b, --basic
            Include basic information. This is the default
        -m, --metadata
            Include all metadata blocks
        -p, --pins
            Include pin information
        -d, --device
            Include device information
        --debug
            Include device debug information
        -l, --build
            Include build attributes
        -a, --all
            Include all information

TARGET SELECTION:
    To target one or more connected RP-series device(s) in BOOTSEL mode (the default)
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            RPI-RP2 drive mounted
    To target a file
        <filename>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
```

Note the -f arguments vary slightly for Windows vs macOS / Unix platforms.

e.g.


$ picotool info
Program Information
 name:      hello_world
 features:  stdout to UART
```


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


$ picotool info -bp
Program Information
 name:      hello_world
 features:  stdout to UART

Fixed Pin Information
 20:  UART1 TX
 21:  UART1 RX
```


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

## config

Config allows you to configure the binary info on a device, if it is configurable. Specifically, you can configure `bi_ptr_int32` and `bi_ptr_string`.

```text
$ picotool help config
CONFIG:
    Display or change program configuration settings from the target device(s) or file.

SYNOPSIS:
    picotool config [-s <key> <value>] [-g <group>] [device-selection]
    picotool config [-s <key> <value>] [-g <group>] <filename> [-t <type>]

OPTIONS:
        <key>
            Variable name
        <value>
            New value
        -g <group>
            Filter by feature group

TARGET SELECTION:
    To target one or more connected RP-series device(s) in BOOTSEL mode (the default)
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            RPI-RP2 drive mounted
    To target a file
        <filename>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
```

```text
$ picotool config
n = 5
name = "Billy"
nonconst_pins:
 default_pin = 3
 default_name = "My First Pin"
```

```text
$ picotool config -g nonconst_pins
nonconst_pins:
 default_pin = 3
 default_name = "My First Pin"
```

```text
$ picotool config -s name Jane
name = "Billy"
setting name -> "Jane"

$ picotool config
n = 5
name = "Jane"
nonconst_pins:
 default_pin = 3
 default_name = "My First Pin"
```

## load

`load` allows you to write data from a file onto the device (either writing to flash, or to RAM)

```text
$ picotool help load
LOAD:
    Load the program / memory range stored in a file onto the device.

SYNOPSIS:
    picotool load [--ignore-partitions] [--family <family_id>] [-p <partition>] [-n] [-N] [-u] [-v] [-x] <filename> [-t <type>] [-o
                <offset>] [device-selection]

OPTIONS:
    Post load actions
        --ignore-partitions
            When writing flash data, ignore the partition table and write to absolute space
        --family
            Specify the family ID of the file to load
        <family_id>
            family ID to use for load
        -p, --partition
            Specify the partition to load into
        <partition>
            partition to load into
        -n, --no-overwrite
            When writing flash data, do not overwrite an existing program in flash. If picotool cannot determine the size/presence of the
            program in flash, the command fails
        -N, --no-overwrite-unsafe
            When writing flash data, do not overwrite an existing program in flash. If picotool cannot determine the size/presence of the
            program in flash, the load continues anyway
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
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            RPI-RP2 drive mounted
```

e.g.

```text
$ picotool load blink.uf2
Loading into Flash: [==============================]  100%
```

## save

`save` allows you to save a range of RAM, the program in flash, or an explicit range of flash from the device to a BIN file or a UF2 file.

```text
$ picotool help save
SAVE:
    Save the program / memory stored in flash on the device to a file.

SYNOPSIS:
    picotool save [-p] [-v] [--family <family_id>] [device-selection]
    picotool save -a [-v] [--family <family_id>] [device-selection]
    picotool save -r <from> <to> [-v] [--family <family_id>] [device-selection]

OPTIONS:
    Selection of data to save
        -p, --program
            Save the installed program only. This is the default
        -a, --all
            Save all of flash memory
        -r, --range
            Save a range of memory. Note that UF2s always store complete 256 byte-aligned blocks of 256 bytes, and the range is expanded
            accordingly
        <from>
            The lower address bound in hex
        <to>
            The upper address bound in hex
    Other
        -v, --verify
            Verify the data was saved correctly
        --family
            Specify the family ID to save the file as
        <family_id>
            family id to save file as
    Source device selection
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            RPI-RP2 drive mounted
    File to save to
        <filename>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
```

e.g. first looking at what is on the device...

```text
$ picotool info
Program Information
name:      lcd_1602_i2c
web site:  https://github.com/raspberrypi/pico-examples/tree/HEAD/i2c/lcd_1602_i2c
```

... saving it to a file ... 
```text
$ picotool save spoon.uf2
Saving file: [==============================]  100%
Wrote 51200 bytes to spoon.uf2
```

... and looking at the file:
```text
$ picotool info spoon.uf2
File spoon.uf2:
Program Information
name:      lcd_1602_i2c
web site:  https://github.com/raspberrypi/pico-examples/tree/HEAD/i2c/lcd_1602_i2c
```

## erase

`erase` allows you to erase all of flash, a partition of flash, or an explicit range of flash on the device.
It defaults to erasing all of flash.

```text
$ picotool help erase
ERASE:
    Erase the program / memory stored in flash on the device.

SYNOPSIS:
    picotool erase [-a] [device-selection]
    picotool erase [-p <partition>] [device-selection]
    picotool erase -r <from> <to> [device-selection]

OPTIONS:
    Selection of data to erase
        -a, --all
            Erase all of flash memory. This is the default
        -p, --partition
            Erase a partition
        <partition>
            Partition number to erase
        -r, --range
            Erase a range of memory. Note that erases must be 4096 byte-aligned, so the range is expanded accordingly
        <from>
            The lower address bound in hex
        <to>
            The upper address bound in hex
    Source device selection
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            RPI-RP2 drive mounted
```

e.g. first looking at what is on the device...

```text
$ picotool info
Partition 0
 Program Information
  none

Partition 1
 Program Information
  name:          blink
  web site:      https://github.com/raspberrypi/pico-examples/tree/HEAD/blink
  features:      UART stdin / stdout
  binary start:  0x10000000
  binary end:    0x1000a934
  target chip:   RP2350
  image type:    ARM Secure
```

... then erase partition 1 ...
```text
$ picotool erase -p 1
Erasing partition 1:
  0007f000->000fc000
Erasing: [==============================]  100%
Erased 512000 bytes
```

... and looking at the device again:
```text
$ picotool info
Partition 0
 Program Information
  none

Partition 1
 Program Information
  none
```

## seal

`seal` allows you to sign and/or hash a binary to run on RP2350.

By default, it will just sign the binary, but this can be configured with the `--hash` and `--no-sign` arguments.

Your signing key must be for the _secp256k1_ curve, in PEM format. You can create a .PEM file with:

```bash
openssl ecparam -name secp256k1 -genkey -out private.pem
```

```text
$ picotool help seal
SEAL:
    Add final metadata to a binary, optionally including a hash and/or signature.

SYNOPSIS:
    picotool seal [--quiet] [--verbose] [--hash] [--sign] [--clear] <infile> [-t <type>] [-o <offset>] <outfile> [-t <type>] [<key>] [-t
                <type>] [<otp>] [-t <type>] [--major <major>] [--minor <minor>] [--rollback <rollback> [<rows>..]]

OPTIONS:
        --quiet
            Don't print any output
        --verbose
            Print verbose output
        --major <major>
            Add Major Version
        --minor <minor>
            Add Minor Version
        --rollback <rollback> [<rows>..]
            Add Rollback Version
    Configuration
        --hash
            Hash the file
        --sign
            Sign the file
        --clear
            Clear all of SRAM on load
    File to load from
        <infile>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
    BIN file options
        -o, --offset
            Specify the load address for a BIN file
        <offset>
            Load offset (memory address; default 0x10000000)
    File to save to
        <outfile>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
    Key file
        <key>
            The file name
        -t <type>
            Specify file type (pem) explicitly, ignoring file extension
    File to save OTP to (will edit existing file if it exists)
        <otp>
            The file name
        -t <type>
            Specify file type (json) explicitly, ignoring file extension
```

## encrypt

`encrypt` allows you to encrypt and sign a binary for use on the RP2350. By default, it will sign the encrypted binary, but that can be configured similarly to `picotool sign`.

The encrypted binary will have the following structure:

- First metadata block (5 words)
- IV (4 words)
- Encrypted Binary
- Padding to ensure the encrypted length is a multiple of 4 words
- Signature metadata block

The AES key must be provided as a .bin file of the 256 bit AES key to be used for encryption.

```text
$ picotool help encrypt
ENCRYPT:
    Encrypt the program.

SYNOPSIS:
    picotool encrypt [--quiet] [--verbose] [--hash] [--sign] <infile> [-t <type>] [-o <offset>] <outfile> [-t <type>] <aes_key> [-t <type>]
                [<signing_key>] [-t <type>]

OPTIONS:
        --quiet
            Don't print any output
        --verbose
            Print verbose output
    Signing Configuration
        --hash
            Hash the encrypted file
        --sign
            Sign the encrypted file
    File to load from
        <infile>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
    BIN file options
        -o, --offset
            Specify the load address for a BIN file
        <offset>
            Load offset (memory address; default 0x10000000)
    File to save to
        <outfile>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
    AES Key
        <aes_key>
            The file name
        -t <type>
            Specify file type (bin) explicitly, ignoring file extension
    Signing Key file
        <signing_key>
            The file name
        -t <type>
            Specify file type (pem) explicitly, ignoring file extension
```

## partition

The `partition` commands allow you to interact with the partition tables on RP2350 devices, and also create them.

### info

```text
$ picotool help partition info
PARTITION INFO:
    Print the device's partition table.

SYNOPSIS:
    picotool partition info -m <family_id> [device-selection]

OPTIONS:
        -m <family_id> [device-selection]
```

```text
$ picotool partition info
un-partitioned_space :  S(rw) NSBOOT(rw) NS(rw), uf2 { absolute }
partitions:
  0(A)       00002000->00201000 S(rw) NSBOOT(rw) NS(rw), id=0000000000000000, "A", uf2 { rp2350-arm-s, rp2350-riscv }, arm_boot 1, riscv_boot 1
  1(B w/ 0)  00201000->00400000 S(rw) NSBOOT(rw) NS(rw), id=0000000000000001, "B", uf2 { rp2350-arm-s, rp2350-riscv }, arm_boot 1, riscv_boot 1
```

```text
$ picotool partition info -m rp2350-arm-s
un-partitioned_space :  S(rw) NSBOOT(rw) NS(rw), uf2 { absolute }
partitions:
  0(A)       00002000->00201000 S(rw) NSBOOT(rw) NS(rw), id=0000000000000000, "A", uf2 { rp2350-arm-s, rp2350-riscv }, arm_boot 1, riscv_boot 1
  1(B w/ 0)  00201000->00400000 S(rw) NSBOOT(rw) NS(rw), id=0000000000000001, "B", uf2 { rp2350-arm-s, rp2350-riscv }, arm_boot 1, riscv_boot 1
Family ID 'rp2350-arm-s' can be downloaded in partition 0:
  00002000->00201000
```

### create

This command allows you to create partition tables, and additionally embed them into the block loop if ELF files (for example, for bootloaders).
By default, all partition tables are hashed, and you can also sign them. The schema for this JSON file is [here](json/schemas/partition-table-schema.json).

```text
$ picotool help partition create
PARTITION CREATE:
    Create a partition table from json

SYNOPSIS:
    picotool partition create [--quiet] [--verbose] <infile> [-t <type>] <outfile> [-t <type>] [[-o <offset>] [--family <family_id>]]
                [<bootloader>] [-t <type>] [[--sign <keyfile>] [-t <type>] [--no-hash] [--singleton]] [[--abs-block] [<abs_block_loc>]]

OPTIONS:
        --quiet
            Don't print any output
        --verbose
            Print verbose output
    partition table JSON
        <infile>
            The file name
        -t <type>
            Specify file type (json) explicitly, ignoring file extension
    output file
        <outfile>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
    UF2 output options
        -o, --offset
            Specify the load address for UF2 file output
        <offset>
            Load offset (memory address; default 0x10000000)
        --family
            Specify the family if for UF2 file output
        <family_id>
            family ID for UF2 (default absolute)
    embed partition table into bootloader ELF
        <bootloader>
            The file name
        -t <type>
            Specify file type (elf) explicitly, ignoring file extension
    Partition Table Options
        --sign <keyfile>
            The file name
        -t <type>
            Specify file type (pem) explicitly, ignoring file extension
        --no-hash
            Don't hash the partition table
        --singleton
            Singleton partition table
    Errata RP2350-E10 Fix
        --abs-block
            Enforce support for an absolute block
        <abs_block_loc>
            absolute block location (default to 0x10ffff00)
```

## uf2

The `uf2` commands allow for creation of UF2s, and can provide information if a UF2 download has failed.

### convert

This command replaces the elf2uf2 functionality that was previously in the Raspberry Pi Pico SDK. It will attempt to auto-detect the family ID, but if this fails you can specify one manually with the `--family` argument.

```text
picotool help uf2 convert
UF2 CONVERT:
    Convert ELF/BIN to UF2.

SYNOPSIS:
    picotool uf2 convert [--quiet] [--verbose] <infile> [-t <type>] <outfile> [-t <type>] [-o <offset>] [--family <family_id>]
                [[--abs-block] [<abs_block_loc>]]

OPTIONS:
        --quiet
            Don't print any output
        --verbose
            Print verbose output
    File to load from
        <infile>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
    File to save UF2 to
        <outfile>
            The file name
        -t <type>
            Specify file type (uf2) explicitly, ignoring file extension
    Packaging Options
        -o, --offset
            Specify the load address
        <offset>
            Load offset (memory address; default 0x10000000 for BIN file)
    UF2 Family options
        <family_id>
            family ID for UF2
    Errata RP2350-E10 Fix
        --abs-block
            Add an absolute block
        <abs_block_loc>
            absolute block location (default to 0x10ffff00)
```

### info

This command reads the information on a device about why a UF2 download has failed. It will only give information if the most recent download has failed.

```text
$ picotool help uf2 info
UF2 INFO:
    Print info about UF2 download.

SYNOPSIS:
    picotool uf2 info [device-selection]

OPTIONS:
    Target device selection
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            RPI-RP2 drive mounted

```

## otp

The `otp` commands are for interacting with the RP2350 OTP Memory. They are not available on  RP2040 devices, as RP2040 has no OTP.

Note that the OTP Memory is One-Time-Programmable, which means that once a bit has been changed from 0 to 1, it cannot be changed back.
Therefore, caution should be used when using these commands, as they risk bricking your RP2350 device. For example, if you set SECURE_BOOT_ENABLE but don't set a boot key, and disable the PICOBOOT interface, then your device will be unusable.

For the `list`, `set`, `get` and `load` commands, you can define your own OTP layout in a JSON file and pass that in with the `-i` argument. These rows will be added to the default rows when parsing. The schema for this JSON file is [here](json/schemas/otp-contents-schema.json)

```text
$ picotool help otp
OTP:
    Commands related to the RP2350 OTP (One-Time-Programmable) Memory

SYNOPSIS:
    picotool otp list [-p] [-n] [-f] [-i <filename>] [<selector>..]
    picotool otp get [-c <copies>] [-r] [-e] [-n] [-i <filename>] [device-selection] [-z] [<selector>..]
    picotool otp set [-c <copies>] [-r] [-e] [-s] [-i <filename>] [-z] <selector> <value> [device-selection]
    picotool otp load [-r] [-e] [-s <row>] [-i <filename>] <filename> [-t <type>] [device-selection]
    picotool otp dump [-r] [-e] [device-selection]
    picotool otp permissions <filename> [-t <type>] [--led <pin>] [--hash] [--sign] [<key>] [-t <type>] [device-selection]
    picotool otp white-label -s <row> <filename> [-t <type>] [device-selection]

SUB COMMANDS:
    list          List matching known registers/fields
    get           Get the value of one or more OTP registers/fields (RP2350 only)
    set           Set the value of an OTP row/field (RP2350 only)
    load          Load the row range stored in a file into OTP and verify. Data is 2 bytes/row for ECC, 4 bytes/row for raw. (RP2350 only)
    dump          Dump entire OTP (RP2350 only)
    permissions   Set the OTP access permissions (RP2350 only)
    white-label   Set the white labelling values in OTP (RP2350 only)

```

### set/get

These commands will set/get specific rows of OTP. By default, they will write/read all redundant rows, but this can be overridden with the `-c` argument

### load

This command allows loading of a range of OTP rows onto the device. The source can be a binary file, or a JSON file such as the one output by `picotool sign`. The schema for this JSON file is [here](json/schemas/otp-schema.json)
For example, if you wish to sign a binary and then test secure boot with it, you can run the following set of commands:
```text
$ picotool sign hello_world.elf hello_world.signed.elf private.pem otp.json
$ picotool load hello_world.signed.elf
$ picotool otp load otp.json
$ picotool reboot
```

### white-label

This command allows for OTP white-labelling, which sets the USB configuration used by the device in BOOTSEL mode.
This can be configured from a JSON file, an example of which is in [sample-wl.json](json/sample-wl.json). The schema for this JSON file is [here](json/schemas/whitelabel-schema.json)

```text
$ picotool help otp white-label
OTP WHITE-LABEL:
    Set the white labelling values in OTP

SYNOPSIS:
    picotool otp white-label -s <row> <filename> [-t <type>] [device-selection]

OPTIONS:
    File with white labelling values
        <filename>
            The file name
        -t <type>
            Specify file type (json) explicitly, ignoring file extension
    Target device selection
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            RPI-RP2 drive mounted

```

```text
$ picotool otp white-label -s 0x100 sample-wl.json 
Setting attributes 20e0
0x2e8b, 0x000e, 0x0215, 0x0c09, 0x1090, 0x200c, 0x2615, 0x20e0, 0x310b, 0x3706, 0x3a04, 0x3c04, 0x3e21, 0x4f15, 0x5a0a, 0x5f0a, 0x007a, 0x00df, 0x6c34, 0xd83c, 0xdf4c, 0x0020, 0x0054, 0x0065, 0x0073, 0x0074, 0x0027, 0x0073,
0x0020, 0x0050, 0x0069, 0x0073, 0x6554, 0x7473, 0x5220, 0x3250, 0x3533, 0x3f30, 0x6f6e, 0x6e74, 0x6365, 0x7365, 0x6173, 0x6972, 0x796c, 0x6e61, 0x6d75, 0x6562, 0x0072, 0x6554, 0x7473, 0x6950, 0x4220, 0x6f6f, 0x0074, 0x6554,
0x7473, 0x6950, 0x794d, 0x6950, 0x3876, 0x3739, 0x7468, 0x7074, 0x3a73, 0x2f2f, 0x7777, 0x2e77, 0x6172, 0x7073, 0x6562, 0x7272, 0x7079, 0x2e69, 0x6f63, 0x2f6d, 0x656e, 0x7377, 0x002f, 0x6f53, 0x656d, 0x4e20, 0x7765, 0x2073,
0x6241, 0x756f, 0x2074, 0x7453, 0x6675, 0x0066, 0x794d, 0x5420, 0x7365, 0x2074, 0x6950, 0x5054, 0x2d49, 0x5052, 0x3332, 0x3035,
$ picotool reboot -u
$ lsusb -v -s 1:102
Bus 001 Device 102: ID 2e8b:000e zß水🍌 Test's Pis Test RP2350?
Device Descriptor:
  bLength                18
  bDescriptorType         1
  bcdUSB               2.10
  bDeviceClass            0 
  bDeviceSubClass         0 
  bDeviceProtocol         0 
  bMaxPacketSize0        64
  idVendor           0x2e8b 
  idProduct          0x000e 
  bcdDevice            2.15
  iManufacturer           1 zß水🍌 Test's Pis
  iProduct                2 Test RP2350?
  iSerial                 3 notnecessarilyanumber
  bNumConfigurations      1
  Configuration Descriptor:
    bLength                 9
    bDescriptorType         2
    wTotalLength       0x0037
    bNumInterfaces          2
    bConfigurationValue     1
    iConfiguration          0 
    bmAttributes         0xc0
      Self Powered
    MaxPower               64mA
...
```

### permissions

This command will run a binary on your device in order to set the OTP permissions, as these are not directly accessible from `picotool` on due to the default permissions settings required to fix  errata XXX on RP2350. 
Because it runs a binary, the binary needs to be sign it if secure boot is enabled. The binary will print what it is doing over uart, which
can be configured using the UART Configuration arguments. You can define your OTP permissions in a json file, an example of which
is in [sample-permissions.json](json/sample-permissions.json). The schema for this JSON file is [here](json/schemas/permissions-schema.json)

```text
$ picotool help otp permissions
OTP PERMISSIONS:
    Set the OTP access permissions

SYNOPSIS:
    picotool otp permissions <filename> [-t <type>] [--led <pin>] [--hash] [--sign] [<key>] [-t <type>] [device-selection]

OPTIONS:
    File to load permissions from
        <filename>
            The file name
        -t <type>
            Specify file type (json) explicitly, ignoring file extension
        --led <pin>
            LED Pin to flash; default 25
    Signing Configuration
        --hash
            Hash the executable
        --sign
            Sign the executable
    Key file
        <key>
            The file name
        -t <type>
            Specify file type (pem) explicitly, ignoring file extension
    Target device selection
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            RPI-RP2 drive mounted
```

```text
$ picotool otp permissions --sign private.pem --tx 46 sample-permissions.json 
Picking file ./xip_ram_perms.elf
page10
page10 = 0
setting page10 -> 4063233
page11
page11 = 0
setting page11 -> 4128781
page12
page12 = 0
setting page12 -> 4128781
tx_pin = 0
setting tx_pin -> 46
Loading into XIP RAM: [==============================]  100%
>>> using flash update boot of 13ffc000

The device was rebooted to start the application.
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

### Configuration

This is very handy if you want to be able to modify parameters in a binary, without having to recompile it.

```text
$ picotool config -s name Jane
name = "Billy"
setting name -> "Jane"
```

### Including Binary information

Binary information is declared in the program by macros (vile warped macros); for the pins example:

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

For the configuration example, you put the line

```c
bi_decl(bi_ptr_string(0x1111, 0x3333, name, "Billy", 128));
```

into your code, which will then create the name variable for you to subsequently print.
The parameters are the tag, the ID, variable name, default value, and maximum string length.

```c
printf("Name is %s\n", name);
```

### Details

Things are designed to waste as little space as possible, but you can turn everything off with preprocessor variable `PICO_NO_BINARY_INFO=1`. Additionally,
any SDK code that inserts binary info can be separately excluded by its own preprocessor variable.

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

### Forced Reboots

Running commands with `-f/F` requires compatible code to be running on the device. The definition of compatible code for the
purposes of binaries compiled using the [pico-sdk](https://github.com/raspberrypi/pico-sdk) is code that
- Is still running -
If your code has returned then rebooting with `-f/F` will not work - instead you can set the compile definition `PICO_ENTER_USB_BOOT_ON_EXIT`
to reboot and be accessible to picotool once your code has finished execution, for example with
`target_compile_definitions(<yourTargetName> PRIVATE PICO_ENTER_USB_BOOT_ON_EXIT=1)`
- Uses stdio_usb -
If your binary calls `stdio_init_all()` and you have `pico_enable_stdio_usb(<yourTargetName> 1)` in your CMakeLists.txt file then you meet
this requirement (see the [hello_usb](https://github.com/raspberrypi/pico-examples/tree/master/hello_world/usb) example)

### Issues

If you ctrl+c out of the middle of a long operation, then libusb seems to get a bit confused, which means we aren't able
to unlock our lockout of USB MSD writes (we have turned them off so the user doesn't step on their own toes). Simply running
`picotool info` again will unlock it properly the next time (or you can reboot the device).
