
#include <algorithm>
#include <memory>
#include <iostream>
#include <sstream>
#include <fstream>

#include "get_enc_bootloader.h"
#include "enc_bootloader_elf.h"
#if HAS_MBEDTLS
#include "enc_bootloader_mbedtls_elf.h"
#endif

#include "data_locs.h"

#include "whereami++.h"


std::shared_ptr<std::iostream> get_enc_bootloader(bool use_mbedtls) {
    // search same directory as executable
    whereami::whereami_path_t executablePath = whereami::getExecutablePath();
    std::string local_loc = executablePath.dirname() + "/";
    if (std::find(data_locs.begin(), data_locs.end(), local_loc) == data_locs.end()) {
        data_locs.insert(data_locs.begin(), local_loc);
    }

    for (auto loc : data_locs) {
        std::string filename = loc
                                + "enc_bootloader"
                                + (use_mbedtls ? "_mbedtls" : "")
                                + ".elf";
        std::ifstream i(filename);
        if (i.good()) {
            printf("Picking file %s\n", filename.c_str());
            auto file = std::make_shared<std::fstream>(filename, std::ios::in|std::ios::binary);
            return file;
        }
    }

    // fall back to embedded enc_bootloader.elf file
    printf("Could not find enc_bootloader%s.elf file - using embedded binary\n", use_mbedtls ? "_mbedtls" : "");
    auto tmp = std::make_shared<std::stringstream>();
#if HAS_MBEDTLS
    if (use_mbedtls) {
        tmp->write(reinterpret_cast<const char*>(enc_bootloader_mbedtls_elf), enc_bootloader_mbedtls_elf_SIZE);
    } else
#endif
    {
        tmp->write(reinterpret_cast<const char*>(enc_bootloader_elf), enc_bootloader_elf_SIZE);
    }
    return tmp;
}
