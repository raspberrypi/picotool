
#include <algorithm>
#include <memory>
#include <iostream>
#include <sstream>
#include <fstream>

#include "get_rpi_connect_provision.h"
#include "rpi_connect_provision_elf.h"

#include "data_locs.h"

#include "whereami++.h"


std::shared_ptr<std::iostream> get_rpi_connect_provision() {
    // search same directory as executable
    whereami::whereami_path_t executablePath = whereami::getExecutablePath();
    std::string local_loc = executablePath.dirname() + "/";
    if (std::find(data_locs.begin(), data_locs.end(), local_loc) == data_locs.end()) {
        data_locs.insert(data_locs.begin(), local_loc);
    }

    for (auto loc : data_locs) {
        std::string filename = loc + "rpi_connect_provision.elf";
        std::ifstream i(filename);
        if (i.good()) {
            printf("Picking file %s\n", filename.c_str());
            auto file = std::make_shared<std::fstream>(filename, std::ios::in|std::ios::binary);
            return file;
        }
    }

    // fall back to embedded rpi_connect_provision.elf file
    printf("Could not find rpi_connect_provision.elf file - using embedded binary\n");
    auto tmp = std::make_shared<std::stringstream>();
    tmp->write(reinterpret_cast<const char*>(rpi_connect_provision_elf), rpi_connect_provision_elf_SIZE);
    return tmp;
}
