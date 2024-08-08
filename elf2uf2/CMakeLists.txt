add_library(elf2uf2 STATIC
        elf2uf2.cpp)

target_include_directories(elf2uf2 PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(elf2uf2 PRIVATE boot_uf2_headers elf errors)
