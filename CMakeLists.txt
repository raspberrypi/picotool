cmake_minimum_required(VERSION 3.12)

project(picotool)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    message("Using PICO_SDK_PATH from environment ('${PICO_SDK_PATH}')")
endif ()
if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is not defined")
endif()
get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_SDK_PATH})
    message(FATAL_ERROR "Directory '${PICO_SDK_PATH}' not found")
endif ()
include(${PICO_SDK_PATH}/pico_sdk_version.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Set PICOTOOL_CODE_OTP to compile OTP definitions in - otherwise, they are included from JSON
if (NOT PICOTOOL_CODE_OTP)
    set(PICOTOOL_CODE_OTP 0)
endif()

# allow installing to flat dir
include(GNUInstallDirs)
if (PICOTOOL_FLAT_INSTALL)
    set(INSTALL_CONFIGDIR picotool)
    set(INSTALL_DATADIR picotool)
    set(INSTALL_BINDIR picotool)
else()
    set(INSTALL_CONFIGDIR lib/cmake/picotool)
    set(INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR}/picotool)
    set(INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

# todo better install paths for this
set(DATA_LOCS "./" "${CMAKE_INSTALL_PREFIX}/${INSTALL_DATADIR}/")
message(${DATA_LOCS})
string(REGEX REPLACE ";" "\",\"" DATA_LOCS_VEC "${DATA_LOCS}")
configure_file(data_locs.template.cpp ${CMAKE_CURRENT_BINARY_DIR}/data_locs.cpp)


include(ExternalProject)

if (MSVC)
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 14)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

if (NOT PICOTOOL_NO_LIBUSB)
    # compile xip_ram_perms.elf
    if (NOT DEFINED USE_PRECOMPILED)
        set(USE_PRECOMPILED true)
    endif()
    ExternalProject_Add(xip_ram_perms
            PREFIX xip_ram_perms
            SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/xip_ram_perms
            BINARY_DIR ${CMAKE_BINARY_DIR}/xip_ram_perms
            CMAKE_ARGS 
                "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
                "-DPICO_SDK_PATH:FILEPATH=${PICO_SDK_PATH}"
                "-DUSE_PRECOMPILED:BOOL=${USE_PRECOMPILED}"
                "-DPICO_DEBUG_INFO_IN_RELEASE=OFF"
            BUILD_ALWAYS 1 # todo remove this
            INSTALL_COMMAND ""
            )

    set(XIP_RAM_PERMS_ELF ${CMAKE_BINARY_DIR}/xip_ram_perms/xip_ram_perms.elf)
    add_executable(xip_ram_perms_elf IMPORTED)
    add_dependencies(xip_ram_perms_elf xip_ram_perms)
    set_property(TARGET xip_ram_perms_elf PROPERTY IMPORTED_LOCATION ${XIP_RAM_PERMS_ELF})
    # copy xip_ram_perms.elf into build directory
    add_custom_command(TARGET xip_ram_perms
        COMMAND ${CMAKE_COMMAND} -E copy ${XIP_RAM_PERMS_ELF} ${CMAKE_BINARY_DIR}/xip_ram_perms.elf
        DEPENDS xip_ram_perms
    )

    # compile flash_id
    ExternalProject_Add(flash_id
            PREFIX picoboot_flash_id
            SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/picoboot_flash_id
            BINARY_DIR ${CMAKE_BINARY_DIR}/picoboot_flash_id
            CMAKE_ARGS 
                "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
                "-DPICO_SDK_PATH:FILEPATH=${PICO_SDK_PATH}"
                "-DUSE_PRECOMPILED:BOOL=${USE_PRECOMPILED}"
                "-DPICO_DEBUG_INFO_IN_RELEASE=OFF"
            BUILD_ALWAYS 1 # todo remove this
            INSTALL_COMMAND ""
            )

    set(FLASH_ID_BIN ${CMAKE_BINARY_DIR}/picoboot_flash_id/flash_id.bin)
    add_executable(flash_id_bin IMPORTED)
    add_dependencies(flash_id_bin flash_id)
    set_property(TARGET flash_id_bin PROPERTY IMPORTED_LOCATION ${FLASH_ID_BIN})
    # copy flash_id.bin into build directory
    add_custom_command(TARGET flash_id
        COMMAND ${CMAKE_COMMAND} -E copy ${FLASH_ID_BIN} ${CMAKE_BINARY_DIR}/flash_id.bin
        DEPENDS flash_id
    )

    # We want to generate headers from WELCOME.HTM etc.
    ExternalProject_Add(otp_header_parser
            PREFIX otp_header_parser
            SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/otp_header_parser
            BINARY_DIR ${CMAKE_BINARY_DIR}/otp_header_parser
            CMAKE_ARGS "-DCODE_OTP=${PICOTOOL_CODE_OTP}"
            BUILD_ALWAYS 1 # todo remove this
            DOWNLOAD_COMMAND ""
            INSTALL_COMMAND ""
            )

    add_executable(otp_header_parse IMPORTED)
    # think this is the best way to do this - this should work in MSVC now, and possibly Xcode but that's untested
    add_dependencies(otp_header_parse otp_header_parser)
    get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (is_multi_config)
        # use the first config
        list(GET CMAKE_CONFIGURATION_TYPES 0 tmp_config)
        set_property(TARGET otp_header_parse PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/otp_header_parser/${tmp_config}/otp_header_parse)
    else()
        set_property(TARGET otp_header_parse PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/otp_header_parser/otp_header_parse)
    endif()

    if (PICOTOOL_CODE_OTP)
        set(GENERATED_H ${CMAKE_CURRENT_BINARY_DIR}/otp_contents.h)
        add_custom_target(generate_otp_header DEPENDS ${GENERATED_H})
        add_custom_command(OUTPUT ${GENERATED_H}
                COMMENT "Generating ${GENERATED_H}"
                DEPENDS ${PICO_SDK_PATH}/src/rp2350/hardware_regs/include/hardware/regs/otp_data.h
                COMMAND otp_header_parse ${PICO_SDK_PATH}/src/rp2350/hardware_regs/include/hardware/regs/otp_data.h ${GENERATED_H}
                )
    elseif(MSVC)
        set(GENERATED_JSON ${CMAKE_CURRENT_BINARY_DIR}/rp2350_otp_contents.json)
        add_custom_target(generate_otp_header DEPENDS ${GENERATED_JSON})
        add_custom_command(OUTPUT ${GENERATED_JSON}
                COMMENT "Generating ${GENERATED_JSON}"
                DEPENDS ${PICO_SDK_PATH}/src/rp2350/hardware_regs/include/hardware/regs/otp_data.h
                COMMAND otp_header_parse ${PICO_SDK_PATH}/src/rp2350/hardware_regs/include/hardware/regs/otp_data.h ${GENERATED_JSON}
                )
        # Cannot include json in the binary, as the string is too long, so needs to use pre-generated xxd output
        configure_file(${CMAKE_CURRENT_LIST_DIR}/otp_header_parser/rp2350.json.h ${CMAKE_CURRENT_BINARY_DIR}/rp2350.json.h COPYONLY)
    else()
        set(GENERATED_JSON ${CMAKE_CURRENT_BINARY_DIR}/rp2350_otp_contents.json)
        add_custom_target(generate_otp_header DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/rp2350.json.h)
        add_custom_command(OUTPUT ${GENERATED_JSON}
                COMMENT "Generating ${GENERATED_JSON}"
                DEPENDS ${PICO_SDK_PATH}/src/rp2350/hardware_regs/include/hardware/regs/otp_data.h
                COMMAND otp_header_parse ${PICO_SDK_PATH}/src/rp2350/hardware_regs/include/hardware/regs/otp_data.h ${GENERATED_JSON}
                )
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rp2350.json.h
                COMMAND ${CMAKE_COMMAND}
                    -D GENERATED_JSON=${GENERATED_JSON}
                    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/jsonh.cmake
                DEPENDS ${GENERATED_JSON}
                COMMENT "Configuring rp2350.json.h"
                VERBATIM)
              
    endif()
endif()

add_custom_target(binary_data DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/rp2350.rom.h
        ${CMAKE_CURRENT_BINARY_DIR}/xip_ram_perms_elf.h
        ${CMAKE_CURRENT_BINARY_DIR}/flash_id_bin.h)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rp2350.rom.h
        COMMAND ${CMAKE_COMMAND}
            -D BINARY_FILE=${CMAKE_CURRENT_LIST_DIR}/bootrom.end.bin
            -D OUTPUT_NAME=rp2350.rom
            -P ${CMAKE_CURRENT_LIST_DIR}/cmake/binh.cmake
        COMMENT "Configuring rp2350.rom.h"
        VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xip_ram_perms_elf.h
        COMMAND ${CMAKE_COMMAND}
            -D BINARY_FILE=${XIP_RAM_PERMS_ELF}
            -D OUTPUT_NAME=xip_ram_perms_elf
            -P ${CMAKE_CURRENT_LIST_DIR}/cmake/binh.cmake
        DEPENDS xip_ram_perms
        COMMENT "Configuring xip_ram_perms_elf.h"
        VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/flash_id_bin.h
        COMMAND ${CMAKE_COMMAND}
            -D BINARY_FILE=${FLASH_ID_BIN}
            -D OUTPUT_NAME=flash_id_bin
            -P ${CMAKE_CURRENT_LIST_DIR}/cmake/binh.cmake
        DEPENDS flash_id
        COMMENT "Configuring flash_id_bin.h"
        VERBATIM)

add_subdirectory(errors)

add_subdirectory(picoboot_connection)
add_subdirectory(elf)
add_subdirectory(elf2uf2)

# To configure mbedtls
# todo make the configuration better
set(MBEDTLS_CONFIG_FILE "mbedtls_config.h")
add_compile_options(-I${CMAKE_SOURCE_DIR}/lib/include)

add_subdirectory(lib)

add_subdirectory(bintool)

if (NOT PICOTOOL_NO_LIBUSB)
    find_package(LIBUSB)
    set(OTP_EXE otp.cpp)
else()
    set(OTP_EXE no_otp.cpp)
endif()

add_subdirectory(${PICO_SDK_PATH}/src/common/pico_binary_info pico_binary_info)
add_subdirectory(${PICO_SDK_PATH}/src/common/boot_uf2_headers boot_uf2_headers)
add_subdirectory(${PICO_SDK_PATH}/src/common/boot_picoboot_headers boot_picoboot_headers)
add_subdirectory(${PICO_SDK_PATH}/src/common/boot_picobin_headers boot_picobin_headers)
add_subdirectory(${PICO_SDK_PATH}/src/common/pico_usb_reset_interface_headers pico_usb_reset_interface_headers)
add_subdirectory(${PICO_SDK_PATH}/src/rp2_common/boot_bootrom_headers boot_bootrom_headers)
add_subdirectory(${PICO_SDK_PATH}/src/host/pico_platform pico_platform)

add_library(regs_headers INTERFACE)
target_include_directories(regs_headers INTERFACE ${PICO_SDK_PATH}/src/rp2350/hardware_regs/include)

# Main picotool executable
add_executable(picotool
    data_locs.cpp
    ${OTP_EXE}
    main.cpp)
if (NOT PICOTOOL_NO_LIBUSB)
    target_sources(picotool PRIVATE xip_ram_perms.cpp)
    add_dependencies(picotool generate_otp_header xip_ram_perms_elf binary_data)
endif()
set(PROJECT_VERSION 2.1.0)
set(PICOTOOL_VERSION 2.1.0)
set(SYSTEM_VERSION "${CMAKE_SYSTEM_NAME}")
set(COMPILER_INFO "${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}, ${CMAKE_BUILD_TYPE}")
target_compile_definitions(picotool PRIVATE
        PICOTOOL_VERSION="${PICOTOOL_VERSION}"
        SYSTEM_VERSION="${SYSTEM_VERSION}"
        COMPILER_INFO="${COMPILER_INFO}"
        SUPPORT_A2=1
        CODE_OTP=${PICOTOOL_CODE_OTP}
        )
# for OTP info
target_include_directories(picotool PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# todo, this is a bit of an abstraction failure; but don't want to rev the SDK just for this right now
target_include_directories(picotool PRIVATE ${PICO_SDK_PATH}/src/rp2_common/pico_stdio_usb/include)
target_link_libraries(picotool
        pico_binary_info
        boot_uf2_headers
        boot_picoboot_headers
        boot_picobin_headers
        boot_bootrom_headers
        pico_platform_headers
        pico_usb_reset_interface_headers
        regs_headers
        bintool
        elf2uf2
        errors
        nlohmann_json
        whereami)

if (NOT TARGET mbedtls)
    message("mbedtls not found - no signing/hashing support will be built")
    target_compile_definitions(picotool PRIVATE HAS_MBEDTLS=0)
else()
    target_compile_definitions(picotool PRIVATE HAS_MBEDTLS=1)
endif()

if (NOT LIBUSB_FOUND)
    if (PICOTOOL_NO_LIBUSB)
        message("PICOTOOL_NO_LIBUSB is set - no USB support will be built")
    else()
        message("libUSB is not found - no USB support will be built")
    endif()
    target_compile_definitions(picotool PRIVATE HAS_LIBUSB=0)
    target_link_libraries(picotool 
        picoboot_connection_header)
else()
    target_include_directories(picotool PRIVATE ${LIBUSB_INCLUDE_DIR})
    target_compile_definitions(picotool PRIVATE HAS_LIBUSB=1)
    target_link_libraries(picotool 
        picoboot_connection_cxx
        ${LIBUSB_LIBRARIES})
endif()

# allow `make install`
install(TARGETS picotool
    EXPORT picotool-targets
    RUNTIME DESTINATION ${INSTALL_BINDIR}
)

#Export the targets to a script
install(EXPORT picotool-targets
    FILE
        picotoolTargets.cmake
    DESTINATION
        ${INSTALL_CONFIGDIR}
)

#Create a ConfigVersion.cmake file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/picotoolConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)

configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/cmake/picotoolConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/picotoolConfig.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

#Install the config and configversion
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/picotoolConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/picotoolConfigVersion.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)

if (NOT PICOTOOL_NO_LIBUSB)
    if (NOT PICOTOOL_CODE_OTP)
        #Install the otp json
        install(FILES
            ${GENERATED_JSON}
            DESTINATION ${INSTALL_DATADIR}
        )
    endif()

    #Install xip_ram_perms.elf
    install(FILES
        ${XIP_RAM_PERMS_ELF}
        DESTINATION ${INSTALL_DATADIR}
    )
endif()
