add_library(picoboot_connection INTERFACE)

target_sources(picoboot_connection INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/picoboot_connection.c)

target_include_directories(picoboot_connection INTERFACE ${CMAKE_CURRENT_LIST_DIR})

add_library(picoboot_connection_cxx INTERFACE)
target_sources(picoboot_connection_cxx INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/picoboot_connection_cxx.cpp)

target_link_libraries(picoboot_connection_cxx INTERFACE picoboot_connection)