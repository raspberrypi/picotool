/**
 * Copyright (c) 2025 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Generates the table of board pin defaults used by `picotool provision connect --board`,
// from the Pico SDK board headers.
//
// Usage: board_header_parse <output.h> <board header>...
//
// Only RP2350 boards with a CYW43 wireless chip are included, as the provisioning binary
// needs WiFi. The headers are read as text, following #include "boards/..." like the SDK's
// generic_board.cmake does: the first #define of a macro wins (as the #ifndef guards make
// it), unless it is #undef'd first.

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using std::string;

struct board_state {
    std::map<string, string> defines;
    std::map<string, string> cmake_sets;
    std::set<string> included;
};

static string basename_of(const string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == string::npos ? path : path.substr(slash + 1);
}

static bool parse_file(const string &path, const std::map<string, string> &headers, board_state &state) {
    if (!state.included.insert(path).second) return true;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Cannot open " << path << "\n";
        return false;
    }
    static const std::regex define_re(R"re(^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*?)\s*(//.*)?$)re");
    static const std::regex undef_re(R"re(^\s*#\s*undef\s+([A-Za-z_][A-Za-z0-9_]*))re");
    static const std::regex include_re(R"re(^\s*#\s*include\s*"boards/([^"]+)")re");
    static const std::regex cmake_set_re(R"re(^\s*pico_board_cmake_set(_default)?\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*(.*?)\s*\))re");
    string line;
    std::smatch m;
    while (std::getline(in, line)) {
        if (std::regex_search(line, m, define_re)) {
            // first definition wins, as with the #ifndef guards
            state.defines.emplace(m[1].str(), m[2].str());
        } else if (std::regex_search(line, m, undef_re)) {
            state.defines.erase(m[1].str());
        } else if (std::regex_search(line, m, include_re)) {
            auto it = headers.find(m[1].str());
            if (it != headers.end() && !parse_file(it->second, headers, state)) return false;
        } else if (std::regex_search(line, m, cmake_set_re)) {
            // pico_board_cmake_set always sets, pico_board_cmake_set_default only if unset
            if (m[1].matched) {
                state.cmake_sets.emplace(m[2].str(), m[3].str());
            } else {
                state.cmake_sets[m[2].str()] = m[3].str();
            }
        }
    }
    return true;
}

// Returns the integer value of a macro, following macros defined as other macros
static bool get_int(const board_state &state, const string &name, long &value, int depth = 0) {
    auto it = state.defines.find(name);
    if (it == state.defines.end() || depth > 8) return false;
    string v = it->second;
    // strip any surrounding brackets and unsigned/long suffixes
    while (v.size() >= 2 && v.front() == '(' && v.back() == ')') v = v.substr(1, v.size() - 2);
    while (!v.empty() && strchr("uUlL", v.back())) v.pop_back();
    try {
        size_t pos;
        value = std::stol(v, &pos, 0);
        if (pos == v.size()) return true;
    } catch (std::exception &) {
    }
    return get_int(state, v, value, depth + 1);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output.h> <board header>...\n";
        return 1;
    }
    // boards/<name>.h -> path, for resolving includes
    std::map<string, string> headers;
    for (int i = 2; i < argc; i++) {
        headers[basename_of(argv[i])] = argv[i];
    }

    std::vector<string> entries;
    for (auto &h : headers) {
        board_state state;
        if (!parse_file(h.second, headers, state)) return 1;
        auto platform = state.cmake_sets.find("PICO_PLATFORM");
        auto cyw43 = state.cmake_sets.find("PICO_CYW43_SUPPORTED");
        if (platform == state.cmake_sets.end() || platform->second.rfind("rp2350", 0) != 0) continue;
        if (cyw43 == state.cmake_sets.end() || cyw43->second != "1") continue;

        auto field = [&](const char *macro) {
            long v;
            return get_int(state, macro, v) ? std::to_string(v) : string("provision_unset");
        };
        string uart, uart_tx, uart_rx;
        long v;
        if (get_int(state, "PICO_DEFAULT_UART", v)) {
            uart = std::to_string(v);
            uart_tx = get_int(state, "PICO_DEFAULT_UART_TX_PIN", v) ? std::to_string(v) : "-1";
            uart_rx = get_int(state, "PICO_DEFAULT_UART_RX_PIN", v) ? std::to_string(v) : "-1";
        } else {
            // no UART on this board
            uart = "-1";
            uart_tx = uart_rx = "provision_unset";
        }
        // a GPIO LED if there is one, otherwise the wireless chip's
        string led = get_int(state, "PICO_DEFAULT_LED_PIN", v) ? std::to_string(v) : "-1";

        string name = h.first.substr(0, h.first.size() - 2);
        entries.push_back("    {\"" + name + "\", " + uart + ", " + uart_tx + ", " + uart_rx + ", " +
                          field("PICO_DEFAULT_UART_BAUD_RATE") + ", " + led + ", " +
                          field("CYW43_DEFAULT_PIN_WL_REG_ON") + ", " +
                          field("CYW43_DEFAULT_PIN_WL_DATA_OUT") + ", " +
                          field("CYW43_DEFAULT_PIN_WL_DATA_IN") + ", " +
                          field("CYW43_DEFAULT_PIN_WL_HOST_WAKE") + ", " +
                          field("CYW43_DEFAULT_PIN_WL_CLOCK") + ", " +
                          field("CYW43_DEFAULT_PIN_WL_CS") + "},");
    }

    std::ofstream out(argv[1]);
    if (!out) {
        std::cerr << "Cannot write " << argv[1] << "\n";
        return 1;
    }
    out << "// Generated by board_header_parse from the Pico SDK board headers - do not edit\n";
    out << "// {name, uart, uart_tx, uart_rx, uart_baud, led, wl_reg_on, wl_data_out, wl_data_in, wl_host_wake, wl_clock, wl_cs}\n";
    for (auto &e : entries) out << e << "\n";
    return 0;
}
