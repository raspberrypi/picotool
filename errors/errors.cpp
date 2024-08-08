#include <cstdarg>

#include "errors.h"

void fail(int code, std::string msg) {
    throw command_failure(code, std::move(msg));
}

void fail(int code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    static char error_msg[512];
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);
    fail(code, std::string(error_msg));
}
