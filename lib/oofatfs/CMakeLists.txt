add_library(fatfs INTERFACE)

target_sources(fatfs INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/ff.c
        ${CMAKE_CURRENT_LIST_DIR}/src/ffunicode.c)

target_include_directories(fatfs INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src)
