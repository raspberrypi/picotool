add_library(fatfs INTERFACE)

target_sources(fatfs INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/ff.c)

target_include_directories(fatfs INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src)
