#ifndef _ERRORS_H
#define _ERRORS_H

#ifdef _WIN32
#undef ERROR_CANCELLED
#endif

#include <string>

#define ERROR_ARGS (-1)
#define ERROR_FORMAT (-2)
#define ERROR_INCOMPATIBLE (-3)
#define ERROR_READ_FAILED (-4)
#define ERROR_WRITE_FAILED (-5)
#define ERROR_USB (-6)
#define ERROR_NO_DEVICE (-7)
#define ERROR_NOT_POSSIBLE (-8)
#define ERROR_CONNECTION (-9)
#define ERROR_CANCELLED (-10)
#define ERROR_VERIFICATION_FAILED (-11)
#define ERROR_UNKNOWN (-99)


struct command_failure : std::exception {
    command_failure(int code, std::string s) : c(code), s(std::move(s)) {}

    const char *what() const noexcept override {
        return s.c_str();
    }

    int code() const { return c; }
private:
    int c;
    std::string s;
};


void fail(int code, std::string msg);
void fail(int code, const char *format, ...);

#endif