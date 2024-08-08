# Ensure submodules are initialised - no longer needed as there are no submodules
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" OFF)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init lib/mbedtls
                        WORKING_DIRECTORY ${PICO_SDK_PATH}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message("git submodule update --init lib/mbedtls failed with ${GIT_SUBMOD_RESULT}")
        endif()
    endif()
endif()


set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(nlohmann_json EXCLUDE_FROM_ALL)

add_subdirectory(whereami EXCLUDE_FROM_ALL)

if(EXISTS "${PICO_SDK_PATH}/lib/mbedtls/CMakeLists.txt")
    option(ENABLE_PROGRAMS "Build Mbed TLS programs." OFF)
    option(ENABLE_TESTING "Build Mbed TLS tests." OFF)
    add_subdirectory(${PICO_SDK_PATH}/lib/mbedtls mbedtls EXCLUDE_FROM_ALL)
endif()
