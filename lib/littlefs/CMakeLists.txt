add_library(littlefs INTERFACE)

target_sources(littlefs INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/lfs.c
        ${CMAKE_CURRENT_LIST_DIR}/lfs_util.c)

target_include_directories(littlefs INTERFACE ${CMAKE_CURRENT_LIST_DIR})
