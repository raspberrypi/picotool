
#include <algorithm>
#include <map>
#include <fstream>

#include "otp.h"

#include "whereami++.h"

#if CODE_OTP
#include "otp_contents.h"
#else
#include "data_locs.h"
#endif

#ifndef NDEBUG
#define DEBUG_LOG(...) printf(__VA_ARGS__)
#else
#define DEBUG_LOG(...) ((void)0)
#endif

#include "rp2350.json.h"

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

void init_otp(std::map<uint32_t, otp_reg> &otp_regs, std::vector<std::string> extra_otp_files) {
#if CODE_OTP
    std::transform(otp_reg_list.begin(), otp_reg_list.end(), std::inserter(otp_regs, otp_regs.end()), [](const otp_reg& r) { return std::make_pair( r.row, r); });
#elif 0
    // search same directory as executable
    whereami::whereami_path_t executablePath = whereami::getExecutablePath();
    std::string local_loc = executablePath.dirname() + "/";
    if (std::find(data_locs.begin(), data_locs.end(), local_loc) == data_locs.end()) {
        data_locs.insert(data_locs.begin(), local_loc);
    }
    std::string local_loc_debug = executablePath.dirname() + "/../";
    if (std::find(data_locs.begin(), data_locs.end(), local_loc_debug) == data_locs.end()) {
        data_locs.insert(data_locs.begin(), local_loc_debug);
    }
    // and search ../ in case exe in subdirectory Debug
    for (auto loc : data_locs) {
        std::string filename = loc + "rp2350_otp_contents.json";
        std::ifstream i(filename);
        if (i.good()) {
            printf("Picking OTP JSON file %s\n", filename.c_str());
            json j;
            i >> j;
            std::transform(j.begin(), j.end(), std::inserter(otp_regs, otp_regs.end()), [](const otp_reg& r) { return std::make_pair( r.row, r); });
            break;
        } else {
            DEBUG_LOG("Can't find JSON file %s\n", filename.c_str());
        }
    }
#else
    json j = json::parse(rp2350_json);
    std::transform(j.begin(), j.end(), std::inserter(otp_regs, otp_regs.end()), [](const otp_reg& r) { return std::make_pair( r.row, r); });
#endif

    for (auto filename : extra_otp_files) {
        std::ifstream i(filename);
        if (i.good()) {
            printf("Adding OTP JSON file %s\n", filename.c_str());
            json j;
            i >> j;
            std::transform(j.begin(), j.end(), std::inserter(otp_regs, otp_regs.end()), [](const otp_reg& r) { return std::make_pair( r.row, r); });
        } else {
            printf("Can't find JSON file %s\n", filename.c_str());
        }
    }
}
