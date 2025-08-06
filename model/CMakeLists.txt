add_library(model STATIC
        model.cpp)

target_include_directories(model PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(model PUBLIC boot_uf2_headers boot_picoboot_headers pico_platform_headers errors)

add_custom_target(unreadable_rom_data DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/rp2350_a2_rom_end.h
        ${CMAKE_CURRENT_BINARY_DIR}/rp2350_a3_rom_end.h
        ${CMAKE_CURRENT_BINARY_DIR}/rp2350_a4_rom_end.h)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rp2350_a2_rom_end.h
        COMMAND ${CMAKE_COMMAND}
            -D BINARY_FILE=${CMAKE_CURRENT_LIST_DIR}/rp2350_a2_rom_end.bin
            -D OUTPUT_NAME=rp2350_a2_rom_end
            -P ${CMAKE_CURRENT_LIST_DIR}/../cmake/binh.cmake
        COMMENT "Configuring rp2350_a2_rom_end.h"
        VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rp2350_a3_rom_end.h
        COMMAND ${CMAKE_COMMAND}
            -D BINARY_FILE=${CMAKE_CURRENT_LIST_DIR}/rp2350_a3_rom_end.bin
            -D OUTPUT_NAME=rp2350_a3_rom_end
            -P ${CMAKE_CURRENT_LIST_DIR}/../cmake/binh.cmake
        COMMENT "Configuring rp2350_a3_rom_end.h"
        VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rp2350_a4_rom_end.h
        COMMAND ${CMAKE_COMMAND}
            -D BINARY_FILE=${CMAKE_CURRENT_LIST_DIR}/rp2350_a4_rom_end.bin
            -D OUTPUT_NAME=rp2350_a4_rom_end
            -P ${CMAKE_CURRENT_LIST_DIR}/../cmake/binh.cmake
        COMMENT "Configuring rp2350_a4_rom_end.h"
        VERBATIM)

add_dependencies(model unreadable_rom_data)
