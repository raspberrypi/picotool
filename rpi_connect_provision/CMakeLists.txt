cmake_minimum_required(VERSION 3.12)

if (NOT USE_PRECOMPILED)
    set(PICO_PLATFORM rp2350-arm-s)
    # Pico 2 W, but RP2350B with maximum flash size - see the header
    set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
    set(PICO_BOARD rpi_connect_provision_board)

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

    project(rpi_connect_provision C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    if (PICO_SDK_VERSION_STRING VERSION_LESS "2.3.2")
        message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.3.2 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
    endif()

    # Initialize the SDK
    pico_sdk_init()

    # One-shot provisioning image: the on-device equivalent of the host tool's
    # provisioning operations (e.g. --create-device-identity). The operation and
    # its inputs are set via binary info with picotool config.
    add_executable(rpi_connect_provision
        rpi_connect_provision.c
        )
    target_link_libraries(rpi_connect_provision
        pico_rpi_connect
        pico_rpi_connect_ota
        pico_stdlib
        pico_cyw43_arch_lwip_poll
        pico_cjson
        pico_ffs
        pico_flash_image
        pico_lwip_mbedtls
        pico_mbedtls
        pico_rpi_connect_ota_default_config_headers
        )
    # Reboots to bootsel after exiting, so you can flash the demo
    target_compile_definitions(rpi_connect_provision PRIVATE
        PICO_ENTER_USB_BOOT_ON_EXIT=1
        )
    # Both UART and USB, so progress can be followed over either
    pico_enable_stdio_usb(rpi_connect_provision 1)
    pico_enable_stdio_uart(rpi_connect_provision 1)

    # Use firmware partitions - but assume wifi firmware is already provisioned
    target_compile_definitions(rpi_connect_provision PRIVATE CYW43_USE_FIRMWARE_PARTITION=1)

    target_compile_definitions(rpi_connect_provision PRIVATE
        # --- Logging ----------------------------------------------------------
        # Default: show the flow of control (demo-app + library INFO and ERROR)
        # without low-level debug spam. The libraries log nothing by default.
        RPI_CONNECT_ERROR_ENABLE=1
        RPI_CONNECT_OTA_ERROR_ENABLE=1
        RPI_CONNECT_INFO_ENABLE=1
        RPI_CONNECT_OTA_INFO_ENABLE=1

        # For data-limited or metered connections: check once for a pending deployment
        # after boot sync instead of holding the SSE event stream open.
        # RPI_CONNECT_OTA_DEMO_POLL_ONLY=1
        # If artefacts are hosted on a server that does not chain to the Pi
        # Connect root CA, disable TLS verification for the download only;
        # integrity then rests on the deployment's SHA-256 checksum.
        # RPI_CONNECT_OTA_DEMO_UNVERIFIED_DOWNLOAD=1
        # For debugging, uncomment any of these to enable verbose logging in the
        # libraries (or per-chunk progress etc. in the demo app):
        #   RPI_CONNECT_DEBUG_ENABLE=1              # core rpi_connect debug (incl. HTTP request layer)
        #   RPI_CONNECT_VERBOSE_DEBUG_ENABLE=1      # ...plus header/body dumps
        #   RPI_CONNECT_OTA_DEBUG_ENABLE=1          # OTA library debug
        #   HTTPC_DEBUG=LWIP_DBG_ON                 # lwIP HTTP client debug
        #   RPI_CONNECT_OTA_DEMO_DEBUG_ENABLE=1     # demo-app DEBUG (download progress)

        # User-Agent for all requests from the lwIP HTTP client
        HTTPC_CLIENT_AGENT=\"rpi-connect-pico/${PICO_SDK_VERSION_MAJOR}.${PICO_SDK_VERSION_MINOR}\"
        ALTCP_MBEDTLS_AUTHMODE=MBEDTLS_SSL_VERIFY_OPTIONAL
        RPI_CONNECT_MBEDTLS_DEBUG_LEVEL=0      # mbedTLS debug verbosity (0=off, up to 4)

        # The TLS handshake nests under the synchronous request path; the 2K
        # default overflows.
        PICO_STACK_SIZE=0x1000
        )

    pico_set_binary_type(rpi_connect_provision no_flash)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rpi_connect_provision.elf DESTINATION ${CMAKE_CURRENT_LIST_DIR})
else()
    project(rpi_connect_provision C CXX ASM)
    message("Using precompiled rpi_connect_provision.elf")
    configure_file(${CMAKE_CURRENT_LIST_DIR}/rpi_connect_provision.elf ${CMAKE_CURRENT_BINARY_DIR}/rpi_connect_provision.elf COPYONLY)
    # Use manually specified variables
    set(NULL ${CMAKE_MAKE_PROGRAM})
    set(NULL ${PICO_SDK_PATH})
    set(NULL ${PICO_DEBUG_INFO_IN_RELEASE})
endif()
