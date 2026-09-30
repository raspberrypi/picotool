file(READ ${BINARY_FILE} FILE_CONTENT HEX)
string(LENGTH ${FILE_CONTENT} FILE_CONTENT_LENGTH)
math(EXPR BIN_LENGTH "${FILE_CONTENT_LENGTH} / 2")

# split into lines of 16 bytes (32 hex characters), each preceded by a newline
math(EXPR full_length "${FILE_CONTENT_LENGTH} - ${FILE_CONTENT_LENGTH} % 32")
string(SUBSTRING "${FILE_CONTENT}" 0 ${full_length} full_lines)
string(SUBSTRING "${FILE_CONTENT}" ${full_length} -1 last_line)
string(REGEX REPLACE "(................................)" "\n\\1" FILE_CONTENT "${full_lines}")
if (NOT last_line STREQUAL "")
    set(FILE_CONTENT "${FILE_CONTENT}\n${last_line}")
endif()

# adds '0x' prefix and comma suffix before and after every byte respectively
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " FILE_CONTENT ${FILE_CONTENT})
string(MAKE_C_IDENTIFIER "${OUTPUT_NAME}" C_NAME)

configure_file(${CMAKE_CURRENT_LIST_DIR}/bin.template.h ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.h @ONLY)
