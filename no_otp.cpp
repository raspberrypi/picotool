
#include <algorithm>
#include <map>

#include "otp.h"

template <typename T>
std::basic_string<T> lowercase(const std::basic_string<T>& s)
{
    std::basic_string<T> s2 = s;
    std::transform(s2.begin(), s2.end(), s2.begin(), tolower);
    return s2;
}

template <typename T>
std::basic_string<T> uppercase(const std::basic_string<T>& s)
{
    std::basic_string<T> s2 = s;
    std::transform(s2.begin(), s2.end(), s2.begin(), toupper);
    return s2;
}

void init_otp(std::map<uint32_t, otp_reg> &otp_regs, std::vector<std::string> extra_otp_files) {}
