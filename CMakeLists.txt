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

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(CMAKE_CXX_STANDARD 14)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

add_subdirectory(picoboot_connection)

find_package(LIBUSB)
if (NOT LIBUSB_FOUND)
    message(FATAL_ERROR "picotool cannot be built because libUSB is not found")
else()
    add_subdirectory(${PICO_SDK_PATH}/src/common/pico_binary_info pico_binary_info)
    add_subdirectory(${PICO_SDK_PATH}/src/common/boot_uf2 boot_uf2_headers)
    add_subdirectory(${PICO_SDK_PATH}/src/common/boot_picoboot boot_picoboot_headers)
    add_subdirectory(${PICO_SDK_PATH}/src/common/pico_usb_reset_interface pico_usb_reset_interface)
    add_subdirectory(${PICO_SDK_PATH}/src/host/pico_platform pico_platform)

    add_executable(picotool main.cpp)
    set(PICOTOOL_VERSION 1.1.2)
    set(SYSTEM_VERSION "${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")
    set(COMPILER_INFO "${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}, ${CMAKE_BUILD_TYPE}")
    target_compile_definitions(picotool PRIVATE
            PICOTOOL_VERSION="${PICOTOOL_VERSION}"
            SYSTEM_VERSION="${SYSTEM_VERSION}"
            COMPILER_INFO="${COMPILER_INFO}"
            )
    target_include_directories(picotool PRIVATE ${LIBUSB_INCLUDE_DIR})
    # todo, this is a bit of an abstraction failure; but don't want to rev the SDK just for this right now
    target_include_directories(picotool PRIVATE ${PICO_SDK_PATH}/src/rp2_common/pico_stdio_usb/include)
    target_link_libraries(picotool
            pico_binary_info
            boot_uf2_headers
            boot_picoboot_headers
            pico_platform_headers
            pico_usb_reset_interface_headers
            picoboot_connection_cxx
            ${LIBUSB_LIBRARIES})
    # allow `make install`
    install(TARGETS picotool RUNTIME DESTINATION bin)
endif()
