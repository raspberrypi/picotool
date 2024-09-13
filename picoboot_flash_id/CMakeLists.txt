cmake_minimum_required(VERSION 3.12)

if (NOT USE_PRECOMPILED)
    set(PICO_NO_PICOTOOL 1)

    # default build type
    set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING "build type")

    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    project(flash_id C CXX ASM)
    pico_sdk_init()

    add_executable(flash_id flash_id.c)
    target_link_libraries(flash_id PRIVATE
        hardware_regs hardware_structs hardware_flash_headers
    )
    target_link_options(flash_id PRIVATE -nostartfiles -nodefaultlibs -Ttext=0)
    pico_add_bin_output(flash_id)
    pico_add_dis_output(flash_id)
else()
    project(flash_id C CXX ASM)
    message("Using precompiled flash_id.bin")
    configure_file(${CMAKE_CURRENT_LIST_DIR}/flash_id.bin ${CMAKE_CURRENT_BINARY_DIR}/flash_id.bin COPYONLY)
    # Use manually specified variables
    set(NULL ${CMAKE_MAKE_PROGRAM})
    set(NULL ${PICO_SDK_PATH})
    set(NULL ${PICO_DEBUG_INFO_IN_RELEASE})
endif()
