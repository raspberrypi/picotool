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

# Taken from pico-sdk/src/rp2_common/pico_mbedtls/CMakeLists.txt
if (DEFINED ENV{PICO_MBEDTLS_PATH} AND (NOT PICO_MBEDTLS_PATH))
    set(PICO_MBEDTLS_PATH $ENV{PICO_MBEDTLS_PATH})
    message("Using PICO_MBEDTLS_PATH from environment ('${PICO_MBEDTLS_PATH}')")
endif()

set(MBEDTLS_TEST_PATH "library/aes.c")
if (NOT PICO_MBEDTLS_PATH)
    set(PICO_MBEDTLS_PATH ${PICO_SDK_PATH}/lib/mbedtls)
elseif (NOT EXISTS "${PICO_MBEDTLS_PATH}/${MBEDTLS_TEST_PATH}")
    message(WARNING "PICO_MBEDTLS_PATH specified but content not present.")
endif()

if(EXISTS "${PICO_MBEDTLS_PATH}/${MBEDTLS_TEST_PATH}")
    # Support version 2.28.8 or 3.6.2
    if (EXISTS ${PICO_MBEDTLS_PATH}/library/ssl_cli.c)
        set(MBEDTLS_VERSION_MAJOR 2)
    elseif (EXISTS ${PICO_MBEDTLS_PATH}/library/ssl_client.c)
        set(MBEDTLS_VERSION_MAJOR 3)
    else()
        message(WARNING "Cannot determine the version of mbedtls")
    endif()

    function(src_crypto_list)
        set(src_crypto
            aes.c
            aesni.c
            aria.c
            asn1parse.c
            asn1write.c
            base64.c
            bignum.c
            camellia.c
            ccm.c
            chacha20.c
            chachapoly.c
            cipher.c
            cipher_wrap.c
            constant_time.c
            cmac.c
            ctr_drbg.c
            des.c
            dhm.c
            ecdh.c
            ecdsa.c
            ecjpake.c
            ecp.c
            ecp_curves.c
            entropy.c
            entropy_poll.c
            error.c
            gcm.c
            hkdf.c
            hmac_drbg.c
            md.c
            md5.c
            memory_buffer_alloc.c
            mps_reader.c
            mps_trace.c
            nist_kw.c
            oid.c
            padlock.c
            pem.c
            pk.c
            pk_wrap.c
            pkcs12.c
            pkcs5.c
            pkparse.c
            pkwrite.c
            platform.c
            platform_util.c
            poly1305.c
            psa_crypto.c
            psa_crypto_aead.c
            psa_crypto_cipher.c
            psa_crypto_client.c
            psa_crypto_ecp.c
            psa_crypto_hash.c
            psa_crypto_mac.c
            psa_crypto_rsa.c
            psa_crypto_se.c
            psa_crypto_slot_management.c
            psa_crypto_storage.c
            psa_its_file.c
            ripemd160.c
            rsa.c
            sha1.c
            sha256.c
            sha512.c
            threading.c
            timing.c
            version.c
            version_features.c
        )
        if (MBEDTLS_VERSION_MAJOR EQUAL 2)
            list(APPEND src_crypto
                arc4.c
                blowfish.c
                havege.c
                md2.c
                md4.c
                psa_crypto_driver_wrappers.c
                rsa_internal.c xtea.c
            )
        elseif (MBEDTLS_VERSION_MAJOR EQUAL 3)
            list(APPEND src_crypto
                bignum_core.c
                rsa_alt_helpers.c
                pk_ecc.c
            )
        endif()
        list(TRANSFORM src_crypto PREPEND ${PICO_MBEDTLS_PATH}/library/)
        set(src_crypto ${src_crypto} PARENT_SCOPE)
    endfunction()

    src_crypto_list()


    # Create library
    add_library(mbedtls STATIC ${src_crypto})

    if(WIN32)
        target_link_libraries(mbedtls ws2_32 bcrypt)
    endif(WIN32)

    target_compile_definitions(mbedtls PUBLIC MBEDTLS_CONFIG_FILE="picotool_mbedtls_config.h")
    target_include_directories(mbedtls SYSTEM PUBLIC ${PICO_MBEDTLS_PATH}/include)
    target_include_directories(mbedtls PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
endif()
