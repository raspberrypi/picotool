if (NOT TARGET mbedtls)
    message("lib/mbedtls submodule needs to be initialized for bintool hashing/signing")
    add_library(bintool STATIC
            bintool.cpp)
    target_compile_definitions(bintool PRIVATE
            NO_PICO_PLATFORM=1
            HAS_MBEDTLS=0
            )

    target_include_directories(bintool PUBLIC ${CMAKE_CURRENT_LIST_DIR})

    target_link_libraries(bintool PUBLIC
            elf
            errors
            boot_picobin_headers)
else()
    add_library(bintool STATIC
            bintool.cpp
            mbedtls_wrapper.c)
    target_compile_definitions(bintool PRIVATE
            NO_PICO_PLATFORM=1
            HAS_MBEDTLS=1
            )

    target_include_directories(bintool PUBLIC ${CMAKE_CURRENT_LIST_DIR})

    target_link_libraries(bintool PUBLIC
            mbedtls
            elf
            errors
            boot_picobin_headers)
endif()
