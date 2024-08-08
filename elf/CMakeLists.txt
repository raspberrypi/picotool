add_library(elf STATIC
        elf_file.cpp)

target_include_directories(elf PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(elf PRIVATE errors)
