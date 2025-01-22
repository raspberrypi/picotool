# This is a modified copy of <PICO_SDK_PATH>/external/pico_sdk_import.cmake to use a specific git repo and branch

set(PICO_SDK_FETCH_FROM_GIT_URL "https://github.com/will-v-pi/pico-sdk")
set(PICO_SDK_FETCH_FROM_GIT_TAG "min-vtor")

include(FetchContent)
FetchContent_Declare(
        pico_sdk
        GIT_REPOSITORY ${PICO_SDK_FETCH_FROM_GIT_URL}
        GIT_TAG ${PICO_SDK_FETCH_FROM_GIT_TAG}
)

if (NOT pico_sdk)
    message("Downloading Raspberry Pi Pico SDK")
    # GIT_SUBMODULES_RECURSE was added in 3.17
    if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.17.0")
        FetchContent_Populate(
                pico_sdk
                QUIET
                GIT_REPOSITORY ${PICO_SDK_FETCH_FROM_GIT_URL}
                GIT_TAG ${PICO_SDK_FETCH_FROM_GIT_TAG}
                GIT_SUBMODULES_RECURSE FALSE

                SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/pico_sdk-src
                BINARY_DIR ${FETCHCONTENT_BASE_DIR}/pico_sdk-build
                SUBBUILD_DIR ${FETCHCONTENT_BASE_DIR}/pico_sdk-subbuild
        )
    else ()
        FetchContent_Populate(
                pico_sdk
                QUIET
                GIT_REPOSITORY ${PICO_SDK_FETCH_FROM_GIT_URL}
                GIT_TAG ${PICO_SDK_FETCH_FROM_GIT_TAG}

                SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/pico_sdk-src
                BINARY_DIR ${FETCHCONTENT_BASE_DIR}/pico_sdk-build
                SUBBUILD_DIR ${FETCHCONTENT_BASE_DIR}/pico_sdk-subbuild
        )
    endif ()

    set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
endif ()


include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
