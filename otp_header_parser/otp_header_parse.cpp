/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <exception>
#include <iostream>
#include <fstream>
#include <regex>
#include <map>
#include <cassert>
#include <cstdint>

#include "nlohmann/json.hpp"

// missing __builtins on windows
#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
#  define __builtin_popcount __popcnt
static __forceinline int __builtin_ctz(unsigned x) {
    unsigned long r;
    _BitScanForward(&r, x);
    return (int)r;
}
#endif

using std::cout;
using std::cerr;

using json = nlohmann::json;

// todo values?

static void usage() {
    std::cerr << "usage: otp_header_parser <otp_data.h filename> <output header filename>" << std::endl;
}

enum {
    ERROR_ARGS = 1,
    ERROR_INPUT = 2,
    ERROR_UNKNOWN = 3,
};

struct otp_field {
    otp_field() = default;
    std::string name;
    uint32_t mask = 0;
    std::string description;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(otp_field, name, mask, description)
};

struct otp_reg {
    otp_reg() = default;
    uint32_t row = 0xffffffff;
    uint32_t mask = 0;
    int redundancy = 1;
    bool ecc = false;
    bool crit = false;
    std::string seq_prefix;
    int seq_length = 0;
    int seq_index = 0;
    std::string description;
    std::string name;
    std::vector<otp_field> fields;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(otp_reg, row, mask, redundancy, ecc, crit, seq_prefix, seq_length, seq_index, description, name, fields)
};

std::ostream &operator<<(std::ostream &outs, const otp_reg &e) {
    auto &x = outs << "row=" << std::hex << e.row << ", mask=" << e.mask << ", ecc=" << e.ecc << ", rdncy=" << std::dec << e.redundancy;
    if (e.description.empty()) {
        return x;
    } else {
        return x << ", '" << e.description << "'";
    }
}

std::ostream &operator<<(std::ostream &outs, const std::pair<std::string, otp_reg> &p) {
    const auto&e = p.second;
    return outs << p.first << "(" << e << ")";
}

std::ostream &operator<<(std::ostream &outs, const otp_field &f) {
    auto &x = outs << f.name << " mask=" << std::hex << f.mask;
    if (f.description.empty()) {
        return x;
    } else {
        return x << ", '" << f.description << "'";
    }
}

std::map<std::string, otp_reg> otp_regs;

bool starts_with(std::string const &fullString, std::string const &prefix) {
    if (fullString.length() >= prefix.length()) {
        return (0 == fullString.compare(0, prefix.length(), prefix));
    } else {
        return false;
    }
}

bool ends_with(std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

std::string trim(std::string str)
{
    str.erase(str.find_last_not_of(' ')+1);
    str.erase(0, str.find_first_not_of(' '));
    return str;
}

bool valid_description(std::string description) {
    if (description.empty()) return false;
    if (description == "None") return false;
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return ERROR_ARGS;
    }
    try {
        std::ifstream infile(argv[1]);
        std::string line;
        std::regex define_u_regex(R"(#define[\s]+([^\s]*)[\s]+_u\(0x(.*)\))");
        std::regex reg_regex(R"(// Register[\s]+:[\s]+(.*)[\s]*)");
        std::regex field_regex(R"(// Field[\s]+:[\s]+(.*)[\s]*)");
        std::regex desc_regex("// Description[\\s]+:[\\s]+(.*)");
        std::regex ecc_regex("//.*\\(ECC\\).*");
        std::regex rbit_regex(R"(//.*\(RBIT-\([0-9]\)\).*)");
        std::regex rn_regex(".*_R[0-9]$");
        std::regex zeroth_regex("([a-zA-Z_0-9]*[a-zA-Z_])0$");
        enum {
            REGISTER,
            FIELD,
            NONE
        } type = NONE;
        bool ecc;
        std::string reg_name;
        std::string field_name;
        std::string comment;
        int expected_redundancy;
        otp_reg reg;
        otp_field field;
        while (std::getline(infile, line)) {
//            std::cout << "LINE " << line << std::endl;
            std::smatch smatch;
            if (starts_with(line, "// ======")) {
                // start of a new register (or end of file)
                if (reg.mask) {
                    if (reg.redundancy != expected_redundancy) {
                        cerr << reg_name << " redundancy count mismatch " << reg.redundancy << " != " << expected_redundancy << std::endl;
                        return ERROR_INPUT;
                    }
                    if (!field.name.empty()) {
                        reg.fields.push_back(field);
                    }
                    otp_regs.emplace(reg_name, reg);
                    reg = otp_reg();
                }
                expected_redundancy = 1;
                reg_name = "";
                field.name = "";
                type = NONE;
            } else if (starts_with(line, "// ------")) {
                if (type == FIELD && !field.name.empty()) {
                    reg.fields.push_back(field);
                }
                // we are between regs/fields
                type = NONE;
            } else if (std::regex_match(line, smatch, reg_regex)) {
                type = REGISTER;
                field_name = "";
                reg_name = smatch[1].str();
                reg.name = reg_name;
            } else if (std::regex_match(line, smatch, field_regex)) {
                field_name = smatch[1];
                if (!starts_with(field_name, reg_name+"_")) {
                    cerr << "ERROR: field name " << field_name << " is not prefixed with expected " << reg_name << "_" << std::endl;
                    return ERROR_INPUT;
                }
                type = FIELD;
                field.name = field_name.substr(reg_name.length() + 1);
                field.mask = 0;
                field.description = "";
            } else if (std::regex_match( line, smatch, define_u_regex)) {
                const auto& define_name = smatch[1].str();
                const auto& define_hex = smatch[2].str();
                if (reg_name.empty()) {
                    std::cerr << "Got define '" << define_name << "' outside of register" << std::endl;
                    return ERROR_INPUT;
                }
                if (!starts_with(define_name, reg_name + "_")) {
                    std::cerr << "Got define '" << define_name << "' which doesn't start with " << reg_name << std::endl;
                    return ERROR_INPUT;
                }
                uint32_t define_value = std::stoul(define_hex, nullptr, 16);
                if (define_name == reg_name + "_ROW") {
                    reg.row = define_value;
                } else if (type == REGISTER && define_name == reg_name + "_BITS") {
                    reg.mask = define_value;
                } else if (type == FIELD && define_name == field_name + "_BITS") {
                    field.mask = define_value;
                } else if (ends_with(define_name, "_BITS")) {
                    cout << "   " << define_name << " : " << define_hex << std::endl;
                }
            } else if (std::regex_match( line, smatch, desc_regex)) {
                if (type == REGISTER) {
                    reg.description = smatch[1];
                } else if (type == FIELD) {
                    field.description = smatch[1];
                }
            } else if (starts_with(line, "// ")) {
                if (type == REGISTER) {
                    reg.description += " " + trim(line.substr(2));
                } else if (type == FIELD) {
                    field.description += " " + trim(line.substr(2));
                }
            }
            if (std::regex_match(line, smatch, ecc_regex)) {
                if (type == REGISTER) {
                    reg.ecc = true;
                } else {
                    cerr << "ERROR: found (ECC) directive outside of register description" << std::endl;
                    return ERROR_INPUT;
                }
            }
            if (std::regex_match(line, smatch, rbit_regex)) {
                if (type == REGISTER) {
                    expected_redundancy = smatch[1].str()[0] - '0';
                } else {
                    cerr << "ERROR: found (RBIT) directive outside of register description" << std::endl;
                    return ERROR_INPUT;
                }
            }
        }
        for (auto it = otp_regs.cbegin(); it != otp_regs.cend();) {
            const auto &name = it->first;
            std::smatch smatch;
            if (std::regex_match(name, smatch, rn_regex)) {
                int n = name[name.length() - 1] - '0';
                auto it2 = otp_regs.find(name.substr(0, name.length() - 3));
                if (it2 != otp_regs.end()) {
                    if (it->second.row != it2->second.row + n) {
                        cerr << "ERROR " << *it << " has redundancy relationship to " << *it2
                             << " but offsets are wrong" << std::endl;
//                        return ERROR_INPUT;
                    }
                    if (it2->second.redundancy != n) {
                        // should appear in order
                        cerr << "ERROR out of order redundant field " << name << std::endl;
                        return ERROR_INPUT;
                    }
                    it2->second.redundancy++;
                    otp_regs.erase(it++);
                    if (it2->second.redundancy == 8) {
                        // for the crit rows
                        it2->second.crit = true;
                    }
                    continue;
                }
            }
            it++;
        }
        for (auto it = otp_regs.begin(); it != otp_regs.end(); it++) {
            const auto &name = it->first;
            std::smatch smatch;
            if (std::regex_match(name, smatch, zeroth_regex)) {
                auto prefix = smatch[1].str();
                uint32_t relmask = 0;
                uint32_t relmask_nofields = 0;
//                cout << "HAHAH " << prefix << " from " << name << std::endl;
                std::vector<std::string> names;
                for (auto &e: otp_regs) {
                    if (starts_with(e.first, prefix)) {
//                        cout << e.first << std::endl;
//                        cout << "  '" << e.first.substr(prefix.length()) << "'" << std::endl;
                        uint32_t index;
                        try {
                            index = std::stoul(e.first.substr(prefix.length()));
                        } catch (std::invalid_argument &ex) {
                            // not a numeric suffix
                            continue;
                        }
                        if (e.second.row != it->second.row +index * it->second.redundancy) {
                            cerr << "ERROR " << e << " has sequential relationship to " << *it
                                 << " but offsets are wrong" << std::endl;
                            return ERROR_INPUT;
                        }
                        e.second.seq_index = (int)index;
                        relmask |= 1u << index;
                        if (e.second.fields.empty()) relmask_nofields |= 1u << index;
                        names.push_back(e.first);
                    }
                }
                assert(relmask);
                if (relmask > 1) {
                    if (relmask_nofields & 1) {
                        if (ends_with(prefix, "_")) prefix = prefix.substr(0, prefix.length()-1);
                        if (relmask != relmask_nofields) {
                            cerr << "ERROR " << prefix << " sequence a subset of members register which have fields" << std::endl;
                            return ERROR_INPUT;
                        }
                        int len = __builtin_ctz(~relmask);
                        if (relmask != (1u << len) - 1) {
                            cerr << "ERROR " << prefix << " sequence is missing members" << std::endl;
                            return ERROR_INPUT;
                        }
                        for (const auto &n: names) {
                            otp_regs[n].seq_prefix = prefix;
                            otp_regs[n].seq_length = len;
                        }
                        assert(len);
                    } else {
                        for (const auto &n: names) {
                            otp_regs[n].seq_index = 0; // unset index
                        }
//                        cerr << "WARNING " << prefix << " is ignored as a sequential register as it has fields" << std::endl;
                    }
                } else {
                    cerr << "WARNING " << it->first << " ends in 0 but is not part of a sequnce" << std::endl;
                }
            }
        }

        assert(!reg.mask); // we should have seen a // ==== at the end
        for (const auto &e : otp_regs) {
            if (e.second.redundancy > 1 && e.second.ecc) {
                cerr << e.first << " has both redundancy and ECC" << std::endl;
                return ERROR_INPUT;
            }
        }
        std::ofstream out_file(argv[2]);
    #if CODE_OTP
        out_file << "// GENERATE FILE; DO NOT EDIT // " << std::endl << std::endl;
        out_file << "#pragma once" << std::endl;
        out_file << "std::vector<otp_reg> otp_reg_list = {" << std::endl;
        for (const auto &e : otp_regs) {
            const auto &r = e.second;
            out_file << "    otp_reg(\"" << e.first << "\", 0x" << std::hex << r.row << ", 0x" << r.mask << ")";
            if (r.ecc) {
                out_file << std::endl << "        .with_ecc()";
            }
            if (r.redundancy > 1) {
                out_file << std::endl << "        .with_redundancy(" << std::dec << r.redundancy << ")";
            }
            if (r.crit) {
                out_file << std::endl << "        .with_crit()";
            }
            if (valid_description(r.description)) {
                out_file << std::endl << "        .with_description(\"" << r.description << "\")";
            }
            if (!r.seq_prefix.empty()) {
                out_file << std::endl << "        .with_sequence(\"" << std::dec << r.seq_prefix << "\", " << r.seq_index << ", " << r.seq_length << ")";
            }
            for(const auto &f : r.fields) {
                out_file << std::endl << "        .with_field(otp_field(\"" << f.name << "\", 0x" << std:: hex << f.mask;
                if (valid_description(f.description)) {
                    out_file << ", \"" << f.description << "\"";
                }
                out_file << "))";
            }
            out_file << ",";
            out_file << std::endl;
        }
        out_file << "};" << std::endl;
    #else
        json j;

        std::vector<otp_reg> otp_regs_vec;
        for(auto const& e: otp_regs)
            otp_regs_vec.push_back(e.second);
        j = otp_regs_vec;
        out_file << std::setw(4) << j << std::endl;
    #endif
    } catch (std::exception &e) {
        cerr << "ERROR: " << e.what() << "\n\n";
        return ERROR_UNKNOWN;
    }
    return 0;
}