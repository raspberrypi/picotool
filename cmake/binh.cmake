file(READ ${BINARY_FILE} FILE_CONTENT HEX)
string(LENGTH ${FILE_CONTENT} FILE_CONTENT_LENGTH)
math(EXPR BIN_LENGTH "${FILE_CONTENT_LENGTH} / 2")

math(EXPR offset "0")

while(FILE_CONTENT_LENGTH GREATER 0)

    if(FILE_CONTENT_LENGTH GREATER 32)
        math(EXPR length "32")
    else()
        math(EXPR length "${FILE_CONTENT_LENGTH}")
    endif()

    string(SUBSTRING ${FILE_CONTENT} ${offset} ${length} line)
    set(lines "${lines}\n${line}")

    math(EXPR FILE_CONTENT_LENGTH "${FILE_CONTENT_LENGTH} - ${length}")
    math(EXPR offset "${offset} + ${length}")
endwhile()

set(FILE_CONTENT "${lines}")

# adds '0x' prefix and comma suffix before and after every byte respectively
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " FILE_CONTENT ${FILE_CONTENT})
string(MAKE_C_IDENTIFIER "${OUTPUT_NAME}" C_NAME)

configure_file(${CMAKE_CURRENT_LIST_DIR}/bin.template.h ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.h @ONLY)
