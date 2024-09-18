cmake_minimum_required(VERSION 3.12)

if (NOT USE_PRECOMPILED)
    set(PICO_PLATFORM rp2350-arm-s)
    
    set(PICO_NO_PICOTOOL 1)

    # If the user set these environment variables to influence the picotool
    # build, unset them here so that they do not influence the pico-sdk
    # build. This is especially required for flags that are not supported
    # by arm-none-eabi compilers.
    unset(ENV{CFLAGS})
    unset(ENV{CXXFLAGS})
    unset(ENV{LDFLAGS})

    # Pull in SDK (must be before project)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

    project(xip_ram_perms C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
        message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
    endif()

    # Initialize the SDK
    pico_sdk_init()

    # XIP Ram OTP Perm Setter
    add_executable(xip_ram_perms
        set_perms.c
    )

    target_link_libraries(xip_ram_perms
        pico_stdlib
    )

    pico_set_binary_type(xip_ram_perms no_flash)
    # create linker script to run from 0x20070000
    file(READ ${PICO_LINKER_SCRIPT_PATH}/memmap_no_flash.ld LINKER_SCRIPT)
    string(REPLACE "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k" "RAM(rwx) : ORIGIN =  0x13ffc000, LENGTH = 16k" LINKER_SCRIPT "${LINKER_SCRIPT}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/memmap_xip_ram.ld "${LINKER_SCRIPT}")
    pico_set_linker_script(xip_ram_perms ${CMAKE_CURRENT_BINARY_DIR}/memmap_xip_ram.ld)
else()
    project(xip_ram_perms C CXX ASM)
    message("Using precompiled xip_ram_perms.elf")
    configure_file(${CMAKE_CURRENT_LIST_DIR}/xip_ram_perms.elf ${CMAKE_CURRENT_BINARY_DIR}/xip_ram_perms.elf COPYONLY)
    # Use manually specified variables
    set(NULL ${CMAKE_MAKE_PROGRAM})
    set(NULL ${PICO_SDK_PATH})
    set(NULL ${PICO_DEBUG_INFO_IN_RELEASE})
endif()
