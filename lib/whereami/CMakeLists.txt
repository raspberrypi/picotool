add_library(whereami INTERFACE)

target_sources(whereami INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/whereami.c
        ${CMAKE_CURRENT_LIST_DIR}/whereami++.cpp)

target_include_directories(whereami INTERFACE ${CMAKE_CURRENT_LIST_DIR})
