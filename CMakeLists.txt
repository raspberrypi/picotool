cmake_minimum_required(VERSION 3.12)

project(picotool)

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
    add_subdirectory(${PICO_SDK_PATH}/src/host/pico_platform pico_platform)

    add_executable(picotool main.cpp)
    target_include_directories(picotool PRIVATE ${LIBUSB_INCLUDE_DIR})
    target_link_libraries(picotool pico_binary_info boot_uf2_headers boot_picoboot_headers pico_platform_headers picoboot_connection_cxx ${LIBUSB_LIBRARIES})
endif()
