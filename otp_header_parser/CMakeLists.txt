cmake_minimum_required(VERSION 3.12)
PROJECT(otp_header_parser CXX)
set(CMAKE_CXX_STANDARD 14)

add_executable(otp_header_parse otp_header_parse.cpp)

if (CODE_OTP)
    target_compile_definitions(otp_header_parse PRIVATE CODE_OTP=${CODE_OTP})
endif()

set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(../lib/nlohmann_json ${CMAKE_CURRENT_BINARY_DIR}/../lib/nlohmann_json EXCLUDE_FROM_ALL)

if (NOT TARGET nlohmann_json)
    message(FATAL_ERROR "JSON library not found - check you have all the submodules")
else()
    target_link_libraries(otp_header_parse nlohmann_json)
endif()
