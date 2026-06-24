add_library(fatfs INTERFACE)

target_sources(fatfs INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/ff.c
        ${CMAKE_CURRENT_LIST_DIR}/src/ffunicode.c)

target_include_directories(fatfs INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src)

function(suppress_oofatfs_warnings)
        if (NOT MSVC)
                set_source_files_properties(
                        # To work around lack of support for ${CMAKE_CURRENT_FUNCTION_LIST_DIR} in CMake <3.17
                        ${CMAKE_SOURCE_DIR}/lib/oofatfs/src/ff.c
                        PROPERTIES
                        COMPILE_OPTIONS "-Wno-stringop-overflow"
                        )
        endif()
endfunction()
