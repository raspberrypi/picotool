cmake_minimum_required(VERSION 3.12)

if (NOT USE_PRECOMPILED)
    set(PICO_PLATFORM rp2350-arm-s)
    
    set(PICO_NO_PICOTOOL 1)

    # Ensure we're using a MinSizeRel build
    set(CMAKE_BUILD_TYPE MinSizeRel)

    # If the user set these environment variables to influence the picotool
    # build, unset them here so that they do not influence the pico-sdk
    # build. This is especially required for flags that are not supported
    # by arm-none-eabi compilers.
    unset(ENV{CFLAGS})
    unset(ENV{CXXFLAGS})
    unset(ENV{LDFLAGS})

    # Pull in SDK (must be before project)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

    project(enc_bootloader C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    if (PICO_SDK_VERSION_STRING VERSION_LESS "2.1.2")
        message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.1.2 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
    endif()

    # Initialize the SDK
    pico_sdk_init()

    # Encrypted Bootloader
    add_executable(enc_bootloader
        enc_bootloader.c
    )

    target_link_libraries(enc_bootloader
        pico_stdlib
    )

    if (USE_MBEDTLS)
        target_sources(enc_bootloader PRIVATE mbedtls_aes.c)

        target_link_libraries(enc_bootloader pico_mbedtls)

        target_compile_definitions(enc_bootloader PRIVATE
            PICO_STACK_SIZE=0x800
            # 0x20080000 -> 0x20081000 doesn't overlap the stack
            ROM_CHAIN_WORKSPACE=0x20080000)

        target_include_directories(enc_bootloader PRIVATE ${CMAKE_CURRENT_LIST_DIR})

        pico_set_linker_script(enc_bootloader ${CMAKE_CURRENT_LIST_DIR}/memmap_mbedtls.ld)
    else()
        target_sources(enc_bootloader PRIVATE aes.S)

        target_compile_definitions(enc_bootloader PRIVATE
            PICO_STACK_SIZE=0x180
            # AES Code & workspace from 0x20080044 -> 0x20081604, so 0x20080200 -> 0x20081200 is inside that
            ROM_CHAIN_WORKSPACE=0x20080200)

        pico_set_linker_script(enc_bootloader ${CMAKE_CURRENT_LIST_DIR}/memmap_enc_bootloader.ld)
    endif()

    target_compile_definitions(enc_bootloader PRIVATE
        # use stack guards, as AES variables are written near the stack
        PICO_USE_STACK_GUARDS=1
        # The following are to reduce the size of the binary
        PICO_NO_PROGRAM_INFO=1
        # No spinlocks used
        PICO_USE_SW_SPIN_LOCKS=0
        # No heap is used
        PICO_HEAP_SIZE=0
        # These inits are not required
        PICO_RUNTIME_SKIP_INIT_SPIN_LOCKS_RESET=1
        PICO_RUNTIME_SKIP_INIT_PER_CORE_IRQ_PRIORITIES=1
        PICO_BOOTROM_LOCKING_ENABLED=0
        # Don't need any vtor irqs
        PICO_MINIMAL_STORED_VECTOR_TABLE=1
        PICO_NO_RAM_VECTOR_TABLE=1
        )

    # print memory usage
    target_link_options(enc_bootloader PUBLIC -Wl,--print-memory-usage)

    pico_minimize_runtime(enc_bootloader)

    pico_set_binary_type(enc_bootloader no_flash)
    pico_add_dis_output(enc_bootloader)
else()
    project(enc_bootloader C CXX ASM)
    message("Using precompiled enc_bootloader.elf")
    if (USE_MBEDTLS)
        configure_file(${CMAKE_CURRENT_LIST_DIR}/enc_bootloader_mbedtls.elf ${CMAKE_CURRENT_BINARY_DIR}/enc_bootloader.elf COPYONLY)
    else()
        configure_file(${CMAKE_CURRENT_LIST_DIR}/enc_bootloader.elf ${CMAKE_CURRENT_BINARY_DIR}/enc_bootloader.elf COPYONLY)
    endif()
    # Use manually specified variables
    set(NULL ${CMAKE_MAKE_PROGRAM})
    set(NULL ${PICO_SDK_PATH})
    set(NULL ${PICO_DEBUG_INFO_IN_RELEASE})
endif()
