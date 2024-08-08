add_library(errors STATIC errors.cpp)

target_include_directories(errors PUBLIC ${CMAKE_CURRENT_LIST_DIR})
