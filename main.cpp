/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "cli.h"
#include "clipp/clipp.h"
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <regex>
#if !defined(__APPLE__) && !defined(__FreeBSD__)
#include <cuchar>
#endif
#include <cwchar>
#include <map>
#include <iostream>
#include <vector>
#include <set>
#include <array>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <memory>
#include <functional>

#include "boot/uf2.h"
#include "boot/picobin.h"
#if HAS_LIBUSB
    #include "picoboot_connection_cxx.h"
    #include "rp2350.rom.h"
    #include "xip_ram_perms.h"
#else
    #include "picoboot_connection.h"
#endif
#include "bintool.h"
#include "elf2uf2.h"
#include "boot/bootrom_constants.h"
#include "pico/binary_info.h"
#include "pico/stdio_usb/reset_interface.h"
#include "elf.h"
#include "otp.h"
#include "errors.h"
#include "hardware/regs/otp_data.h"

#include "nlohmann/json.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

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

// tsk namespace is polluted on windows
#ifdef _WIN32
#undef min
#undef max

#define _CRT_SECURE_NO_WARNINGS
#endif

#define MAX_REBOOT_TRIES 5

#define OTP_PAGE_COUNT 64
#define OTP_PAGE_ROWS  64
#define OTP_ROW_COUNT (OTP_PAGE_COUNT * OTP_PAGE_ROWS)

using std::string;
using std::vector;
using std::pair;
using std::map;
using std::tuple;
using std::ios;
using json = nlohmann::json;

#if HAS_LIBUSB
typedef map<enum picoboot_device_result,vector<tuple<model_t, libusb_device *, libusb_device_handle *>>> device_map;
#else
typedef map<enum picoboot_device_result,vector<tuple<model_t, void *, void *>>> device_map;
#endif

auto memory_names = map<enum memory_type, string>{
        {memory_type::sram, "RAM"},
        {memory_type::flash, "Flash"},
        {memory_type::xip_sram, "XIP RAM"},
        {memory_type::rom, "ROM"}
};

static const string tool_name = "picotool";

static const string data_family_name = "data";
static const string absolute_family_name = "absolute";
static const string rp2040_family_name = "rp2040";
static const string rp2350_arm_s_family_name = "rp2350-arm-s";
static const string rp2350_arm_ns_family_name = "rp2350-arm-ns";
static const string rp2350_riscv_family_name = "rp2350-riscv";

static string hex_string(int64_t value, int width=8, bool prefix=true, bool uppercase=false) {
    std::stringstream ss;
    if (prefix) ss << "0x";
    ss << std::setfill('0') << std::setw(width);
    if (uppercase) ss << std::uppercase;
    ss << std::hex << value;
    return ss.str();
}

std::array<std::array<string, 30>, 10> pin_functions_rp2040{{
    {"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""},
    {"SPI0 RX", "SPI0 CSn", "SPI0 SCK", "SPI0 TX", "SPI0 RX", "SPI0 CSn", "SPI0 SCK", "SPI0 TX", "SPI1 RX", "SPI1 CSn", "SPI1 SCK", "SPI1 TX", "SPI1 RX", "SPI1 CSn", "SPI1 SCK", "SPI1 TX", "SPI0 RX", "SPI0 CSn", "SPI0 SCK", "SPI0 TX", "SPI0 RX", "SPI0 CSn", "SPI0 SCK", "SPI0 TX", "SPI1 RX", "SPI1 CSn", "SPI1 SCK", "SPI1 TX", "SPI1 RX", "SPI1 CSn"},
    {"UART0 TX", "UART0 RX", "UART0 CTS", "UART0 RTS", "UART1 TX", "UART1 RX", "UART1 CTS", "UART1 RTS", "UART1 TX", "UART1 RX", "UART1 CTS", "UART1 RTS", "UART0 TX", "UART0 RX", "UART0 CTS", "UART0 RTS", "UART0 TX", "UART0 RX", "UART0 CTS", "UART0 RTS", "UART1 TX", "UART1 RX", "UART1 CTS", "UART1 RTS", "UART1 TX", "UART1 RX", "UART1 CTS", "UART1 RTS", "UART0 TX", "UART0 RX"},
    {"I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL"},
    {"PWM0 A", "PWM0 B", "PWM1 A", "PWM1 B", "PWM2 A", "PWM2 B", "PWM3 A", "PWM3 B", "PWM4 A", "PWM4 B", "PWM5 A", "PWM5 B", "PWM6 A", "PWM6 B", "PWM7 A", "PWM7 B", "PWM0 A", "PWM0 B", "PWM1 A", "PWM1 B", "PWM2 A", "PWM2 B", "PWM3 A", "PWM3 B", "PWM4 A", "PWM4 B", "PWM5 A", "PWM5 B", "PWM6 A", "PWM6 B"},
    {"SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO", "SIO"},
    {"PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0", "PIO0"},
    {"PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1", "PIO1"},
    {"","","","","","","","","","","","","","","","","","","","","CLOCK GPIN0","CLOCK GPOUT0","CLOCK GPIN1","CLOCK GPOUT1","CLOCK GPOUT2","CLOCK GPOUT3","","","",""},
    {"USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN"},
}};

std::array<std::array<string, 48>, 12> pin_functions_rp2350{{
    {"JTAG TCK","JTAG TMS", "JTAG TDI", "JTAG TDO", "",         "",         "",         "",         "",         "",         "",         "",         "HSTX0",    "HSTX1",    "HSTX2",    "HSTX3",    "HSTX4",    "HSTX5",    "HSTX6",    "HSTX7",},
    {"SPI0 RX", "SPI0 CSn", "SPI0 SCK", "SPI0 TX",  "SPI0 RX",  "SPI0 CSn", "SPI0 SCK", "SPI0 TX",  "SPI1 RX",  "SPI1 CSn", "SPI1 SCK", "SPI1 TX",  "SPI1 RX",  "SPI1 CSn", "SPI1 SCK", "SPI1 TX",  "SPI0 RX",  "SPI0 CSn", "SPI0 SCK", "SPI0 TX",  "SPI0 RX",  "SPI0 CSn", "SPI0 SCK", "SPI0 TX",  "SPI1 RX",  "SPI1 CSn", "SPI1 SCK", "SPI1 TX",  "SPI1 RX",  "SPI1 CSn", "SPI1 SCK", "SPI1 TX",  "SPI0 RX",  "SPI0 CSn", "SPI0 SCK", "SPI0 TX",  "SPI0 RX",  "SPI0 CSn", "SPI0 SCK", "SPI0 TX",  "SPI1 RX",  "SPI1 CSn", "SPI1 SCK", "SPI1 TX",  "SPI1 RX",  "SPI1 CSn", "SPI1 SCK", "SPI1 TX"},
    {"UART0 TX","UART0 RX", "UART0 CTS","UART0 RTS","UART1 TX", "UART1 RX", "UART1 CTS","UART1 RTS","UART1 TX", "UART1 RX", "UART1 CTS","UART1 RTS","UART0 TX", "UART0 RX", "UART0 CTS","UART0 RTS","UART0 TX", "UART0 RX", "UART0 CTS","UART0 RTS","UART1 TX", "UART1 RX", "UART1 CTS","UART1 RTS","UART1 TX", "UART1 RX", "UART1 CTS","UART1 RTS","UART0 TX", "UART0 RX", "UART0 CTS","UART0 RTS","UART0 TX", "UART0 RX", "UART0 CTS","UART0 RTS","UART1 TX", "UART1 RX", "UART1 CTS","UART1 RTS","UART1 TX", "UART1 RX", "UART1 CTS","UART1 RTS","UART0 TX", "UART0 RX", "UART0 CTS","UART0 RTS"},
    {"I2C0 SDA","I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL", "I2C0 SDA", "I2C0 SCL", "I2C1 SDA", "I2C1 SCL"},
    {"PWM0 A",  "PWM0 B",   "PWM1 A",   "PWM1 B",   "PWM2 A",   "PWM2 B",   "PWM3 A",   "PWM3 B",   "PWM4 A",   "PWM4 B",   "PWM5 A",   "PWM5 B",   "PWM6 A",   "PWM6 B",   "PWM7 A",   "PWM7 B",   "PWM0 A",   "PWM0 B",   "PWM1 A",   "PWM1 B",   "PWM2 A",   "PWM2 B",   "PWM3 A",   "PWM3 B",   "PWM4 A",   "PWM4 B",   "PWM5 A",   "PWM5 B",   "PWM6 A",   "PWM6 B",   "PWM7 A",   "PWM7 B",   "PWM8 A",   "PWM8 B",   "PWM9 A",   "PWM9 B",   "PWM10 A",  "PWM10 B",  "PWM11 A",  "PWM11 B",  "PWM8 A",   "PWM8 B",   "PWM9 A",   "PWM9 B",   "PWM10 A",  "PWM10 B",  "PWM11 A",  "PWM11 B"},
    {"SIO",     "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO",      "SIO"},
    {"PIO0",    "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0",     "PIO0"},
    {"PIO1",    "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1",     "PIO1"},
    {"PIO2",    "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2",     "PIO2"},
    {"XIP CS1", "CORESIGHT TRACECLK","CORESIGHT TRACEDATA0","CORESIGHT TDATA1","CORESIGHT TDATA2","CORESIGHT TDATA3","","","XIP CS1", "", "","",    "CLK GPIN", "CLK GPOUT","CLK GPIN", "CLK GPOUT","",         "",         "",         "XIP CS1",  "CLK GPIN", "CLK GPOUT","CLK GPIN", "CLK GPOUT","CLK GPOUT","CLK GPOUT","",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "",         "XIP CS1"},
    {"USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN", "USB OVCUR DET", "USB VBUS DET", "USB VBUS EN"},
    {"",        "",         "UART0 TX", "UART0 RX", "",         "",         "UART1 TX", "UART1 RX", "",         "",         "UART1 TX", "UART1 RX", "",         "",         "UART0 TX", "UART0 RX", "",         "",         "UART0 TX", "UART0 RX", "",         "",         "UART1 TX", "UART1 RX", "",         "",         "UART1 TX", "UART1 RX", "",         "",         "UART0 TX", "UART0 RX", "",         "",         "UART0 TX", "UART0 RX", "",         "",         "UART1 TX", "UART1 RX", "",         "",         "UART1 TX", "UART1 RX", "",         "",         "UART0 TX", "UART0 RX"}
}};

std::map<uint32_t, otp_reg> otp_regs;

#if HAS_LIBUSB
auto bus_device_string = [](struct libusb_device *device, model_t model) {
    string bus_device;
    if (model == rp2040) {
        bus_device = string("RP2040 device at bus ");
    } else if (model == rp2350) {
        bus_device = string("RP2350 device at bus ");
    } else {
        bus_device = string("Device at bus ");
    }
    return bus_device + std::to_string(libusb_get_bus_number(device)) + ", address " + std::to_string(libusb_get_device_address(device));
};
#endif

enum class filetype {bin, elf, uf2, pem, json};
const string getFiletypeName(enum filetype type) 
{
   switch (type) 
   {
      case filetype::elf: return "ELF";
      case filetype::bin: return "BIN";
      case filetype::uf2: return "UF2";
      case filetype::pem: return "PEM";
      case filetype::json: return "JSON";
      default: assert(false); return "ERROR_TYPE";
   }
}

struct cancelled_exception : std::exception { };

struct not_mapped_exception : std::exception {
    const char *what() const noexcept override {
        return "Hmm uncaught not mapped";
    }
};

// from -> to
struct range {
    range() : from(0), to(0) {}
    range(uint32_t from, uint32_t to) : from(from), to(to) {}
    uint32_t from;
    uint32_t to;

    uint32_t len() const {
        return to - from;
    }

    bool empty() const {
        return from >= to;
    }
    bool contains(uint32_t addr) const { return addr>=from && addr<to; }
    uint32_t clamp(uint32_t addr) const {
        if (addr < from) addr = from;
        if (addr > to) addr = to;
        return addr;
    }

    void intersect(const range& other) {
        from = other.clamp(from);
        to = other.clamp(to);
    }

    bool intersects(const range& other) const {
        return !(other.from >= to || other.to < from);
    }

};

// ranges should not overlap
template <typename T> struct range_map {
    range_map() = default;
    struct mapping {
        mapping(uint32_t offset, uint32_t max_offset) : offset(offset), max_offset(max_offset) {}
        const uint32_t offset;
        const uint32_t max_offset;
    };

    void insert(const range& r, T t) {
        if (r.to != r.from) {
            assert(r.to > r.from);
            // check we don't overlap any existing map entries

            auto f = m.upper_bound(r.from); // first element that starts after r.from
            if (f != m.begin()) f--; // back up, to catch element that starts on or before r.from
            for(; f != m.end() && f->first < r.to; f++) { // loop till we can't possibly overlap
                range r2(f->first, f->second.first);
                if (r2.intersects(r)) {
                    fail(ERROR_FORMAT, "Found overlapping memory ranges 0x%08x->0x%08x and 0x%08x->%08x\n",
                         r.from, r.to, r2.from, r2.to);
                }
            }
            m.insert(std::make_pair(r.from, std::make_pair(r.to, t)));
        }
    }

    pair<mapping, T> get(uint32_t p) {
        auto f = m.upper_bound(p);
        if (f == m.end()) {
            if (m.empty())
                throw not_mapped_exception();
        } else if (f == m.begin()) {
            throw not_mapped_exception();
        }
        f--;
        assert(p >= f->first);
        if (p >= f->second.first) {
            throw not_mapped_exception();
        }
        return std::make_pair(mapping(p - f->first, f->second.first - f->first), f->second.second);
    }

    uint32_t next(uint32_t p) {
        auto f = m.upper_bound(p);
        if (f == m.end()) {
            return std::numeric_limits<uint32_t>::max();
        }
        return f->first;
    }

    vector<range> ranges() {
        vector<range> r;
        r.reserve(m.size());
        for(const auto &e : m) {
            r.emplace_back(range(e.first, e.second.first));
        }
        return r;
    }

    size_t size() const { return m.size(); }

    range_map<T> offset_by(uint32_t offset) {
        range_map<T> rmap_offset;
        for(const auto &e : m) {
            rmap_offset.insert(range(e.first + offset, e.second.first + offset), e.second.second);
        }
        return rmap_offset;
    }
private:
    map<uint32_t, pair<uint32_t, T>> m;
};

using cli::group;
using cli::option;
using cli::integer;
using cli::hex;
using cli::value;

// todo can we derive from hex?
struct family_id : public cli::value_base<family_id> {
    explicit family_id(string name) : value_base(std::move(name)) {}

    template<typename T>
    family_id &set(T &t) {
        string nm = "<" + name() + ">";
        // note we cannot capture "this"
        on_action([&t, nm](string value) {
            auto ovalue = value;
            if (value == data_family_name) {
                t = DATA_FAMILY_ID;
            } else if (value == absolute_family_name) {
                t = ABSOLUTE_FAMILY_ID;
            } else if (value == rp2040_family_name) {
                t = RP2040_FAMILY_ID;
            } else if (value == rp2350_arm_s_family_name) {
                t = RP2350_ARM_S_FAMILY_ID;
            } else if (value == rp2350_arm_s_family_name) {
                t = RP2350_ARM_NS_FAMILY_ID;
            } else if (value == rp2350_riscv_family_name) {
                t = RP2350_RISCV_FAMILY_ID;
            } else {
                if (value.find("0x") == 0) {
                    value = value.substr(2);
                    size_t pos = 0;
                    long lvalue = std::numeric_limits<long>::max();
                    try {
                        lvalue = std::stoul(value, &pos, 16);
                        if (pos != value.length()) {
                            return "Garbage after hex value: " + value.substr(pos);
                        }
                    } catch (std::invalid_argument &) {
                        return ovalue + " is not a valid hex value";
                    } catch (std::out_of_range &) {
                    }
                    if (lvalue != (unsigned int) lvalue) {
                        return value + " is not a valid 32 bit value";
                    }
                    t = (unsigned int) lvalue;
                } else {
                    return value + " is not a valid family ID";
                }
            }
            return string("");
        });
        return *this;
    }
};

string family_name(unsigned int family_id) {
    if (family_id == DATA_FAMILY_ID) return "'" + data_family_name + "'";
    if (family_id == ABSOLUTE_FAMILY_ID) return "'" + absolute_family_name + "'";
    if (family_id == RP2040_FAMILY_ID) return "'" + rp2040_family_name + "'";
    if (family_id == RP2350_ARM_S_FAMILY_ID) return "'" + rp2350_arm_s_family_name + "'";
    if (family_id == RP2350_ARM_NS_FAMILY_ID) return "'" + rp2350_arm_ns_family_name + "'";
    if (family_id == RP2350_RISCV_FAMILY_ID) return "'" + rp2350_riscv_family_name + "'";
    if (!family_id) return "none";
    return hex_string(family_id);
}

struct cmd {
    explicit cmd(string name) : _name(std::move(name)) {}
    virtual ~cmd() = default;
    enum device_support { none, one, zero_or_more };
    virtual group get_cli() = 0;
    virtual string get_doc() const = 0;
    virtual device_support get_device_support() { return one; }
    virtual bool force_requires_pre_reboot() { return true; }
    // return true if the command caused a reboot
    virtual bool execute(device_map& devices) = 0;
    virtual bool is_multi() const { return false; }
    virtual bool requires_rp2350() const { return false; }
    virtual std::vector<std::shared_ptr<cmd>> sub_commands() const { return std::vector<std::shared_ptr<cmd>>(); }
    const string& name() { return _name; }
private:
    string _name;
};

struct multi_cmd : public cmd {
    explicit multi_cmd(std::string name, std::vector<std::shared_ptr<cmd>> sub_commands) : cmd(name), _sub_commands(sub_commands) {}
    virtual group get_cli() override { assert(false); return group(); }
    virtual bool execute(device_map& devices) override { assert(false); return false; }
    virtual bool is_multi() const override { return true; }
    virtual std::vector<std::shared_ptr<cmd>> sub_commands() const override {
        return _sub_commands;
    }
private:
     std::vector<std::shared_ptr<cmd>> _sub_commands;
};

struct _settings {
    std::array<std::string, 4> filenames;
    std::array<std::string, 4> file_types;
    uint32_t binary_start = FLASH_START;
    int bus=-1;
    int address=-1;
    int vid=-1;
    int pid=-1;
    string ser;
    uint32_t offset = 0;
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t partition_size = 0;
    bool offset_set = false;
    bool range_set = false;
    bool reboot_usb = false;
    bool reboot_app_specified = false;
    int reboot_diagnostic_partition = BOOT_PARTITION_NONE;
    bool force = false;
    bool force_no_reboot = false;
    string switch_cpu;
    uint32_t family_id = 0;
    bool quiet = false;
    bool verbose = false;

    struct {
        int redundancy = -1;
        bool raw = false;
        bool ecc = false;
        bool ignore_set = false;
        bool fuzzy = false;
        uint32_t value = 0;
        uint8_t lock0 = 0;
        uint8_t lock1 = 0;
        int8_t led_pin = -1;
        std::vector<uint32_t> pages;
        bool list_pages = false;
        bool list_no_descriptions = false;
        bool list_field_descriptions = false;
        std::vector<std::string> selectors;
        uint32_t row = 0;
        std::vector<std::string> extra_files;
    } otp;

    struct {
        bool show_basic = false;
        bool all = false;
        bool show_metadata = false;
        bool show_pins = false;
        bool show_device = false;
        bool show_debug = false;
        bool show_build = false;
    } info;

    struct {
        string group;
        string key;
        string value;
    } config;

    struct {
        bool verify = false;
        bool execute = false;
        bool no_overwrite = false;
        bool no_overwrite_force = false;
        bool update = false;
        bool ignore_pt = false;
        int partition = -1;
    } load;

    struct {
        bool hash = false;
        bool sign = false;
        bool clear_sram = false;
        uint16_t major_version = 0;
        uint16_t minor_version = 0;
        uint16_t rollback_version = 0;
        std::vector<uint16_t> rollback_rows;
    } seal;

    struct {
        uint32_t align = 0x1000;
    } link;

    struct {
        bool all = false;
        bool verify = false;
    } save;

    struct {
        bool semantic = false;
        string version;
    } version;

    struct {
        #if HAS_MBEDTLS
        bool hash = true;
        #else
        bool hash = false;
        #endif
        bool sign = false;
        bool singleton = false;
    } partition;

    struct {
        bool abs_block = false;
        #if SUPPORT_A2
        uint32_t abs_block_loc = 0x11000000 - UF2_PAGE_SIZE;
        #else
        uint32_t abs_block_loc = 0;
        #endif
    } uf2;
};
_settings settings;
std::shared_ptr<cmd> selected_cmd;
model_t selected_model = unknown;

auto device_selection =
    (
        (option("--bus") & integer("bus").min_value(0).max_value(255).set(settings.bus)
            .if_missing([] { return "missing bus number"; })) % "Filter devices by USB bus number" +
        (option("--address") & integer("addr").min_value(1).max_value(127).set(settings.address)
            .if_missing([] { return "missing address"; })) % "Filter devices by USB device address" +
        (option("--vid") & integer("vid").set(settings.vid).if_missing([] { return "missing vid"; })) % "Filter by vendor id" +
        (option("--pid") & integer("pid").set(settings.pid)) % "Filter by product id" +
        (option("--ser") & value("ser").set(settings.ser)) % "Filter by serial number"
        + option('f', "--force").set(settings.force) % "Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the command (unless the command itself is a 'reboot') the device will be rebooted back to application mode" +
                option('F', "--force-no-reboot").set(settings.force_no_reboot) % "Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the RPI-RP2 drive mounted"
    ).min(0).doc_non_optional(true).collapse_synopsys("device-selection");

#define file_types_x(i)\
(option ('t', "--type") & value("type").set(settings.file_types[i]))\
    % "Specify file type (uf2 | elf | bin) explicitly, ignoring file extension"

#define named_file_types_x(types, i)\
(option ('t', "--type") & value("type").set(settings.file_types[i]))\
    % "Specify file type (" types ") explicitly, ignoring file extension"


#define file_selection_x(i)\
(\
    value("filename").with_exclusion_filter([](const string &value) {\
            return value.find_first_of('-') == 0;\
        }).set(settings.filenames[i]) % "The file name" +\
    file_types_x(i)\
)

#define named_file_selection_x(name, i)\
(\
    value(name).with_exclusion_filter([](const string &value) {\
            return value.find_first_of('-') == 0;\
        }).set(settings.filenames[i]) % "The file name" +\
    file_types_x(i)\
)

#define named_typed_file_selection_x(name, i, types)\
(\
    value(name).with_exclusion_filter([](const string &value) {\
            return value.find_first_of('-') == 0;\
        }).set(settings.filenames[i]) % "The file name" +\
    named_file_types_x(types, i)\
)

#define optional_file_selection_x(name, i)\
(\
    value(name).with_exclusion_filter([](const string &value) {\
            return value.find_first_of('-') == 0;\
        }).set(settings.filenames[i]).min(0) % "The file name" +\
    file_types_x(i)\
).min(0).doc_non_optional(true)

#define optional_typed_file_selection_x(name, i, types)\
(\
    value(name).with_exclusion_filter([](const string &value) {\
            return value.find_first_of('-') == 0;\
        }).set(settings.filenames[i]).min(0) % "The file name" +\
    named_file_types_x(types, i)\
).min(0).doc_non_optional(true)

#define option_file_selection_x(option, i)\
(\
    option & value("filename").with_exclusion_filter([](const string &value) {\
            return value.find_first_of('-') == 0;\
        }).set(settings.filenames[i]) % "The file name" +\
    file_types_x(i)\
)

auto file_types = (option ('t', "--type") & value("type").set(settings.file_types[0]))
            % "Specify file type (uf2 | elf | bin) explicitly, ignoring file extension";

auto file_selection =
        (
            value("filename").with_exclusion_filter([](const string &value) {
                    return value.find_first_of('-') == 0;
                }).set(settings.filenames[0]) % "The file name" +
            file_types
        );

struct info_command : public cmd {
    info_command() : cmd("info") {}
    bool execute(device_map& devices) override;
    device_support get_device_support() override {
        if (settings.filenames[0].empty())
            return zero_or_more;
        else
            return none;
    }

    group get_cli() override {
        return (
            (
                option('b', "--basic").set(settings.info.show_basic) % "Include basic information. This is the default" +
                option('m', "--metadata").set(settings.info.show_metadata) % "Include all metadata blocks" +
                option('p', "--pins").set(settings.info.show_pins) % "Include pin information" +
                option('d', "--device").set(settings.info.show_device) % "Include device information" +
                option("--debug").set(settings.info.show_debug) % "Include device debug information" +
                option('l', "--build").set(settings.info.show_build) % "Include build attributes" +
                option('a', "--all").set(settings.info.all) % "Include all information"
            ).min(0).doc_non_optional(true) % "Information to display" +
            (
            #if HAS_LIBUSB
                device_selection % "To target one or more connected RP-series device(s) in BOOTSEL mode (the default)" |
            #endif
                file_selection % "To target a file"
            ).major_group("TARGET SELECTION").min(0).doc_non_optional(true)
         );
    }

    string get_doc() const override {
        return "Display information from the target device(s) or file.\nWithout any arguments, this will display basic information for all connected RP-series devices in BOOTSEL mode";
    }
};

struct config_command : public cmd {
    config_command() : cmd("config") {}
    bool execute(device_map& devices) override;
    device_support get_device_support() override {
        if (settings.filenames[0].empty())
            return zero_or_more;
        else
            return none;
    }

    group get_cli() override {
        return (
            (option('s', "--set") & (
                value("key").set(settings.config.key) % "Variable name" +
                value("value").set(settings.config.value) % "New value")
            ).force_expand_help(true) +
            (option('g', "--group") & value("group").set(settings.config.group)) % "Filter by feature group" + 
            (
            #if HAS_LIBUSB
                device_selection % "To target one or more connected RP-series device(s) in BOOTSEL mode (the default)" |
            #endif
                file_selection % "To target a file"
            ).major_group("TARGET SELECTION").min(0).doc_non_optional(true)
         );
    }

    string get_doc() const override {
        return "Display or change program configuration settings from the target device(s) or file.";
    }
};

#if HAS_LIBUSB
struct verify_command : public cmd {
    verify_command() : cmd("verify") {}
    bool execute(device_map &devices) override;

    group get_cli() override {
        return (
            device_selection % "Target device selection" +
            file_selection % "The file to compare against" +
            (
                (option('r', "--range").set(settings.range_set) % "Compare a sub range of memory only" &
                    hex("from").set(settings.from) % "The lower address bound in hex" &
                    hex("to").set(settings.to) % "The upper address bound in hex").force_expand_help(true) +
                (option('o', "--offset").set(settings.offset_set) % "Specify the load address when comparing with a BIN file" &
                    hex("offset").set(settings.offset) % "Load offset (memory address; default 0x10000000)").force_expand_help(true)
           ).min(0).doc_non_optional(true) % "Address options"
        );
    }

    string get_doc() const override {
        return "Check that the device contents match those in the file.";
    }
};

struct save_command : public cmd {
    save_command() : cmd("save") {}
    bool execute(device_map &devices) override;

    group get_cli() override {
        return (
            (
                option('p', "--program") % "Save the installed program only. This is the default" |
                option('a', "--all").doc_non_optional(true).set(settings.save.all) % "Save all of flash memory" |
                (
                    option('r', "--range").set(settings.range_set) % "Save a range of memory. Note that UF2s always store complete 256 byte-aligned blocks of 256 bytes, and the range is expanded accordingly" &
                        hex("from").set(settings.from) % "The lower address bound in hex" &
                        hex("to").set(settings.to) % "The upper address bound in hex"
                ).min(0).doc_non_optional(true)
            ).min(0).doc_non_optional(true).no_match_beats_error(false) % "Selection of data to save" +
            option('v', "--verify").set(settings.save.verify) % "Verify the data was saved correctly" +
            (option("--family") % "Specify the family ID to save the file as" &
                family_id("family_id").set(settings.family_id) % "family ID to save file as").force_expand_help(true) +
            ( // note this parenthesis seems to help with error messages for say save --foo
                device_selection % "Source device selection" +
                file_selection % "File to save to"
            )
        );
    }
    string get_doc() const override {
        return "Save the program / memory stored in flash on the device to a file.";
    }
};

struct load_command : public cmd {
    load_command() : cmd("load") {}
    bool execute(device_map &devices) override;

    group get_cli() override {
        return (
            (
                option("--ignore-partitions").set(settings.load.ignore_pt) % "When writing flash data, ignore the partition table and write to absolute space" +
                (option("--family") % "Specify the family ID of the file to load" &
                        family_id("family_id").set(settings.family_id) % "family ID to use for load").force_expand_help(true) +
                (option('p', "--partition") % "Specify the partition to load into" &
                        integer("partition").set(settings.load.partition) % "partition to load into").force_expand_help(true) +
                option('n', "--no-overwrite").set(settings.load.no_overwrite) % "When writing flash data, do not overwrite an existing program in flash. If picotool cannot determine the size/presence of the program in flash, the command fails" +
                option('N', "--no-overwrite-unsafe").set(settings.load.no_overwrite_force) % "When writing flash data, do not overwrite an existing program in flash. If picotool cannot determine the size/presence of the program in flash, the load continues anyway" +
                option('u', "--update").set(settings.load.update) % "Skip writing flash sectors that already contain identical data" +
                option('v', "--verify").set(settings.load.verify) % "Verify the data was written correctly" +
                option('x', "--execute").set(settings.load.execute) % "Attempt to execute the downloaded file as a program after the load"
            ).min(0).doc_non_optional(true) % "Post load actions" +
            file_selection % "File to load from" +
            (
                option('o', "--offset").set(settings.offset_set) % "Specify the load address for a BIN file" &
                     hex("offset").set(settings.offset) % "Load offset (memory address; default 0x10000000)"
            ).force_expand_help(true) % "BIN file options" +
            device_selection % "Target device selection"
        );
    }

    string get_doc() const override {
        return "Load the program / memory range stored in a file onto the device.";
    }
};

struct erase_command : public cmd {
    erase_command() : cmd("erase") {}
    bool execute(device_map &devices) override;

    group get_cli() override {
        return (
            (
                option('a', "--all") % "Erase all of flash memory. This is the default" |
                (
                    option('p', "--partition") % "Erase a partition" &
                        integer("partition").set(settings.load.partition) % "Partition number to erase"
                ).min(0).doc_non_optional(true) |
                (
                    option('r', "--range").set(settings.range_set) % "Erase a range of memory. Note that erases must be 4096 byte-aligned, so the range is expanded accordingly" &
                        hex("from").set(settings.from) % "The lower address bound in hex" &
                        hex("to").set(settings.to) % "The upper address bound in hex"
                ).min(0).doc_non_optional(true)
            ).min(0).doc_non_optional(true).no_match_beats_error(false) % "Selection of data to erase" +
            ( // note this parenthesis seems to help with error messages for say erase --foo
                device_selection % "Source device selection"
            )
        );
    }
    string get_doc() const override {
        return "Erase the program / memory stored in flash on the device.";
    }
};
#endif

#if HAS_MBEDTLS
struct encrypt_command : public cmd {
    encrypt_command() : cmd("encrypt") {}
    bool execute(device_map &devices) override;
    virtual device_support get_device_support() override { return none; }

    group get_cli() override {
        return (
            option("--quiet").set(settings.quiet) % "Don't print any output" +
            option("--verbose").set(settings.verbose) % "Print verbose output" +
            (
                option("--hash").set(settings.seal.hash) % "Hash the encrypted file" +
                option("--sign").set(settings.seal.sign) % "Sign the encrypted file"
            ).min(0).doc_non_optional(true) % "Signing Configuration" +
            named_file_selection_x("infile", 0) % "File to load from" +
            (
                option('o', "--offset").set(settings.offset_set) % "Specify the load address for a BIN file" &
                     hex("offset").set(settings.offset) % "Load offset (memory address; default 0x10000000)"
            ).force_expand_help(true) % "BIN file options" +
            named_file_selection_x("outfile", 1) % "File to save to" +
            named_typed_file_selection_x("aes_key", 2, "bin") % "AES Key" +
            optional_typed_file_selection_x("signing_key", 3, "pem") % "Signing Key file"
        );
    }

    string get_doc() const override {
        return "Encrypt the program.";
    }
};

struct seal_command : public cmd {
    seal_command() : cmd("seal") {}
    bool execute(device_map &devices) override;
    virtual device_support get_device_support() override { return none; }

    group get_cli() override {
        return (
            option("--quiet").set(settings.quiet) % "Don't print any output" +
            option("--verbose").set(settings.verbose) % "Print verbose output" +
            (
                option("--hash").set(settings.seal.hash) % "Hash the file" +
                option("--sign").set(settings.seal.sign) % "Sign the file" +
                option("--clear").set(settings.seal.clear_sram) % "Clear all of SRAM on load"
            ).min(0).doc_non_optional(true) % "Configuration" +
            named_file_selection_x("infile", 0) % "File to load from" +
            (
                option('o', "--offset").set(settings.offset_set) % "Specify the load address for a BIN file" &
                     hex("offset").set(settings.offset) % "Load offset (memory address; default 0x10000000)"
            ).force_expand_help(true) % "BIN file options" +
            named_file_selection_x("outfile", 1) % "File to save to" +
            optional_typed_file_selection_x("key", 2, "pem") % "Key file" +
            optional_typed_file_selection_x("otp", 3, "json") % "File to save OTP to (will edit existing file if it exists)" + 
            (
                option("--major") &
                    integer("major").set(settings.seal.major_version)
            ).min(0) % "Add Major Version" +
            (
                option("--minor") &
                    integer("minor").set(settings.seal.minor_version)
            ).min(0) % "Add Minor Version" +
            (
                option("--rollback") &
                    integer("rollback").set(settings.seal.rollback_version) +
                    hex("rows").add_to(settings.seal.rollback_rows).min(0).repeatable()
            ).min(0) % "Add Rollback Version"
        );
    }

    string get_doc() const override {
        return "Add final metadata to a binary, optionally including a hash and/or signature.";
    }
};
#endif

struct link_command : public cmd {
    link_command() : cmd("link") {}
    bool execute(device_map &devices) override;
    virtual device_support get_device_support() override { return none; }

    group get_cli() override {
        return (
            option("--quiet").set(settings.quiet) % "Don't print any output" +
            option("--verbose").set(settings.verbose) % "Print verbose output" +
            named_typed_file_selection_x("outfile", 0, "uf2 | bin") % "File to write to" +
            named_file_selection_x("infile1", 1) % "Files to link" +
            named_file_selection_x("infile2", 2) % "Files to link" +
            optional_file_selection_x("infile3", 3) % "Files to link" +
            option('p', "--pad") & hex("pad").set(settings.link.align) % "Specify alignment to pad to, defaults to 0x1000"
        );
    }

    string get_doc() const override {
        return "Link multiple binaries into one block loop.";
    }
};

#if HAS_LIBUSB
struct partition_info_command : public cmd {
    partition_info_command() : cmd("info") {}
    bool execute(device_map &devices) override;

    group get_cli() override {
        return (
                (option('m', "--family") & family_id("family_id").set(settings.family_id)) % "family ID (will show target partition for said family)" +
                device_selection % "Target device selection"
        );
    }

    string get_doc() const override {
        return "Print the device's partition table.";
    }
};
#endif

struct partition_create_command : public cmd {
    partition_create_command() : cmd("create") {}
    bool execute(device_map &devices) override;
    virtual device_support get_device_support() override { return none; }

    group get_cli() override {
        return (
                option("--quiet").set(settings.quiet) % "Don't print any output" +
                option("--verbose").set(settings.verbose) % "Print verbose output" +
                named_typed_file_selection_x("infile", 0, "json") % "partition table JSON" +
                (named_file_selection_x("outfile", 1) % "output file" +
                (
                    (option('o', "--offset").set(settings.offset_set) % "Specify the load address for UF2 file output" &
                        hex("offset").set(settings.offset) % "Load offset (memory address; default 0x10000000)").force_expand_help(true) +
                    (option("--family") % "Specify the family if for UF2 file output" &
                        family_id("family_id").set(settings.family_id) % "family ID for UF2 (default absolute)").force_expand_help(true)
                ).min(0).force_expand_help(true) % "UF2 output options") +
                optional_typed_file_selection_x("bootloader", 2, "elf") % "embed partition table into bootloader ELF" + 
                (
                #if HAS_MBEDTLS
                    // todo why doesn't this set settings.partition.sign?
                    ((option("--sign").set(settings.partition.sign) & value("keyfile").with_exclusion_filter([](const string &value) {
                            return value.find_first_of('-') == 0;
                        }).set(settings.filenames[3])) % "The file name" +
                    named_file_types_x("pem", 3)) % "Sign the partition table" + 
                    (option("--no-hash").clear(settings.partition.hash) % "Don't hash the partition table") + 
                #endif
                    (option("--singleton").set(settings.partition.singleton) % "Singleton partition table")
                ).min(0).force_expand_help(true) % "Partition Table Options"
            #if SUPPORT_A2
                + (
                    option("--abs-block").set(settings.uf2.abs_block) % "Enforce support for an absolute block" +
                        hex("abs_block_loc").set(settings.uf2.abs_block_loc).min(0) % "absolute block location (default to 0x10ffff00)"
                ).force_expand_help(true).min(0) % "Errata RP2350-E10 Fix"
            #endif
        );
    }

    string get_doc() const override {
        return "Create a partition table from json";
    }
};


vector<std::shared_ptr<cmd>> partition_sub_commands {
    #if HAS_LIBUSB
        std::shared_ptr<cmd>(new partition_info_command()),
    #endif
        std::shared_ptr<cmd>(new partition_create_command()),
};

struct partition_command : public multi_cmd {
    partition_command() : multi_cmd("partition", partition_sub_commands) {}
    string get_doc() const override {
        return "Commands related to RP2350 Partition Tables";
    }
};


struct otp_list_command : public cmd {
    otp_list_command() : cmd("list") {}
    bool execute(device_map& devices) override;
    virtual device_support get_device_support() override { return none; }

    group get_cli() override {
        return (
                (
                        option('p', "--pages").set(settings.otp.list_pages) % "Show page number/page row number" +
                        option('n', "--no-descriptions").set(settings.otp.list_no_descriptions) % "Don't show descriptions" +
                        option('f', "--field-descriptions").set(settings.otp.list_field_descriptions) % "Show all field descriptions" +
                        (option('i', "--include") & value("filename").add_to(settings.otp.extra_files)).min(0).max(1) % "Include extra otp definition" + // todo more than 1
                        (value("selector").add_to(settings.otp.selectors) %
                         "The row/field selector, each of which can select a whole row:\n\n" \
                        "ROW_NAME to select a whole row by name.\n" \
                        "ROW_NUMBER to select a whole row by number.\n" \
                        "PAGE:PAGE_ROW_NUMBER to select a whole row by page and number within page.\n\n" \
                        "... or can select a single field/subset of a row (where REG_SEL is one of the above row selectors):\n\n"
                         "REG_SEL.FIELD_NAME to select a field within a row by name.\n" \
                        "REG_SEL.n-m to select a range of bits within a row.\n" \
                        "REG_SEL.n to select a single bit within a row.\n" \
                        ".FIELD_NAME to select any row's field by name.\n\n" \
                        ".. or can selected multiple rows by using blank or '*' for PAGE or PAGE_ROW_NUMBER").repeatable().min(0)
                ) % "Row/Field Selection"
        );
    }

    string get_doc() const override {
        return "List matching known registers/fields";
    }
};
#if HAS_LIBUSB
struct otp_get_command : public cmd {
    otp_get_command() : cmd("get") {}
    bool execute(device_map& devices) override;
    virtual bool requires_rp2350() const override { return true; }

    group get_cli() override {
        return (
                (
                        (option('c', "--copies") & integer("copies").min(1).set(settings.otp.redundancy)) % "Read multiple redundant values" +
                        option('r', "--raw").set(settings.otp.raw) % "Get raw 24 bit values" +
                        option('e', "--ecc").set(settings.otp.ecc) % "Use error correction" +
                        option('n', "--no-descriptions").set(settings.otp.list_no_descriptions) % "Don't show descriptions" +
                        (option('i', "--include") & value("filename").add_to(settings.otp.extra_files)).min(0).max(1) % "Include extra otp definition" // todo more than 1
                ).min(0).doc_non_optional(true) % "Row/field options" +
                (
                        device_selection % "Target device selection"
                ).major_group("TARGET SELECTION").min(0).doc_non_optional(true) +
                (
                        option('z', "--fuzzy").set(settings.otp.fuzzy) % "Allow fuzzy name searches in selector vs exact match" +
                        (value("selector").add_to(settings.otp.selectors) %
                         "The row/field selector, each of which can select a whole row:\n\n" \
                        "ROW_NAME to select a whole row by name.\n" \
                        "ROW_NUMBER to select a whole row by number.\n" \
                        "PAGE:PAGE_ROW_NUMBER to select a whole row by page and number within page.\n\n" \
                        "... or can select a single field/subset of a row (where REG_SEL is one of the above row selectors):\n\n"
                         "REG_SEL.FIELD_NAME to select a field within a row by name.\n" \
                        "REG_SEL.n-m to select a range of bits within a row.\n" \
                        "REG_SEL.n to select a single bit within a row.\n" \
                        ".FIELD_NAME to select any row's field by name.\n\n" \
                        ".. or can selected multiple rows by using blank or '*' for PAGE or PAGE_ROW_NUMBER").repeatable().min(0)
                ) % "Row/Field Selection"
        );
    }

    string get_doc() const override {
        return "Get the value of one or more OTP registers/fields";
    }
};

// possible temporary
struct otp_dump_command : public cmd {
    otp_dump_command() : cmd("dump") {}
    bool execute(device_map& devices) override;
    virtual bool requires_rp2350() const override { return true; }

    group get_cli() override {
        return (
                (
                        option('r', "--raw").set(settings.otp.raw) % "Get raw 24 bit values" +
                        option('e', "--ecc").set(settings.otp.ecc) % "Use error correction"
                ).min(0).doc_non_optional(true) % "Row/field options" +
                (
                        device_selection % "Target device selection"
                ).major_group("TARGET SELECTION").min(0).doc_non_optional(true)
        );
    }

    string get_doc() const override {
        return "Dump entire OTP";
    }
};

struct otp_load_command : public cmd {
    otp_load_command() : cmd("load") {}
    bool execute(device_map &devices) override;
    virtual bool requires_rp2350() const override { return true; }

    group get_cli() override {
        return (
                (
                        option('r', "--raw").set(settings.otp.raw) % "Get raw 24 bit values" +
                        option('e', "--ecc").set(settings.otp.ecc) % "Use error correction" +
                        (option('s', "--start_row") & integer("row").set(settings.otp.row)) % "Start row to load at (note use 0x for hex)" +
                        (option('i', "--include") & value("filename").add_to(settings.otp.extra_files)).min(0).max(1) % "Include extra otp definition" // todo more than 1
                ).min(0).doc_non_optional(true) % "Row options" +
                named_typed_file_selection_x("filename", 0, "json | bin") % "File to load row(s) from" +
                device_selection % "Target device selection"
        );
    }

    string get_doc() const override {
        return "Load the row range stored in a file into OTP and verify. Data is 2 bytes/row for ECC, 4 bytes/row for raw.";
    }
};

struct otp_set_command : public cmd {
    otp_set_command() : cmd("set") {}
    virtual bool requires_rp2350() const override { return true; }

    bool execute(device_map& devices) override;

    group get_cli() override {
        return (
                (
                        (option('c', "--copies") & integer("copies").min(1).set(settings.otp.redundancy)) % "Read multiple redundant values" +
                        option('r', "--raw").set(settings.otp.raw) % "Set raw 24 bit values" +
                        option('e', "--ecc").set(settings.otp.ecc) % "Use error correction" +
                        option('s', "--set-bits").set(settings.otp.ignore_set) % "Set bits only" +
                        (option('i', "--include") & value("filename").add_to(settings.otp.extra_files)).min(0).max(1) % "Include extra otp definition" // todo more than 1
                ).min(0).doc_non_optional(true) % "Redundancy/Error Correction Overrides" +
                (
                        option('z', "--fuzzy").set(settings.otp.fuzzy) % "Allow fuzzy name searches in selector vs exact match" +
                        (value("selector").add_to(settings.otp.selectors) %
                        "The row/field selector, which can be:\nROW_NAME or ROW_NUMBER or PAGE:PAGE_ROW_NUMBER to select a whole row.\n"
                        "FIELD, REG.FIELD, REG.n-m, PAGE:PAGE_ROW_NUMBER.FIELD or PAGE:PAGE_ROW_NUMBER.n-m to select a row field.\n\n"
                        "where:\n\nREG and FIELD are names (or parts of names with fuzzy searches).\nPAGE and PAGE_ROW_NUMBER are page numbers and row within a page, "
                        "ROW_NUMBER is an absolute row number offset, and n-m are the inclusive bit ranges of a field.")
                ) % "Row/Field Selection" +
                integer("value").set(settings.otp.value) % "The value to set" +
                (
                        device_selection % "Target device selection"
                ).major_group("TARGET SELECTION").min(0).doc_non_optional(true)
        );
    }

    string get_doc() const override {
        return "Set the value of an OTP row/field";
    }
};

struct otp_permissions_command : public cmd {
    otp_permissions_command() : cmd("permissions") {}
    virtual bool requires_rp2350() const override { return true; }

    bool execute(device_map& devices) override;

    group get_cli() override {
        return (
                named_typed_file_selection_x("filename", 0, "json") % "File to load permissions from" +
                (option("--led") & integer("pin").set(settings.otp.led_pin)) % "LED Pin to flash; default 25" +
                (
                    option("--hash").set(settings.seal.hash) % "Hash the executable" +
                    option("--sign").set(settings.seal.sign) % "Sign the executable" +
                    optional_typed_file_selection_x("key", 2, "pem") % "Key file"
                ).min(0).doc_non_optional(true) % "Signing Configuration" +
                device_selection % "Target device selection"
        );
    }

    string get_doc() const override {
        return "Set the OTP access permissions";
    }
};

struct otp_white_label_command : public cmd {
    otp_white_label_command() : cmd("white-label") {}
    virtual bool requires_rp2350() const override { return true; }

    bool execute(device_map& devices) override;

    group get_cli() override {
        return (
                (
                        (option('s', "--start_row") & integer("row").set(settings.otp.row)) % "Start row for white label struct (default 0x100) (note use 0x for hex)"
                ).min(0).doc_non_optional(true) % "Row options" +
                named_typed_file_selection_x("filename", 0, "json") % "File with white labelling values" +
                device_selection % "Target device selection"
        );
    }

    string get_doc() const override {
        return "Set the white labelling values in OTP";
    }
};
#endif


vector<std::shared_ptr<cmd>> otp_sub_commands {
        std::shared_ptr<cmd>( new otp_list_command()),
    #if HAS_LIBUSB
        std::shared_ptr<cmd>(new otp_get_command()),
        std::shared_ptr<cmd>(new otp_set_command()),
        std::shared_ptr<cmd>(new otp_load_command()),
        std::shared_ptr<cmd>(new otp_dump_command()),
        std::shared_ptr<cmd>(new otp_permissions_command()),
        std::shared_ptr<cmd>(new otp_white_label_command()),
    #endif
};

struct otp_command : public multi_cmd {
    otp_command() : multi_cmd("otp", otp_sub_commands) {}
    string get_doc() const override {
        return "Commands related to the RP2350 OTP (One-Time-Programmable) Memory";
    }
};

#if HAS_LIBUSB
struct uf2_info_command : public cmd {
    uf2_info_command() : cmd("info") {}
    bool execute(device_map &devices) override;

    group get_cli() override {
        return (
              device_selection % "Target device selection"
        );
    }

    string get_doc() const override {
        return "Print info about UF2 download.";
    }
};
#endif

struct uf2_convert_command : public cmd {
    uf2_convert_command() : cmd("convert") {}
    bool execute(device_map &devices) override;
    virtual device_support get_device_support() override { return none; }

    group get_cli() override {
        return (
                option("--quiet").set(settings.quiet) % "Don't print any output" +
                option("--verbose").set(settings.verbose) % "Print verbose output" +
                named_file_selection_x("infile", 0) % "File to load from" +
                named_typed_file_selection_x("outfile", 1, "uf2") % "File to save UF2 to" +
                (
                    option('o', "--offset").set(settings.offset_set) % "Specify the load address" &
                        hex("offset").set(settings.offset) % "Load offset (memory address; default 0x10000000 for BIN file)"
                ).force_expand_help(true) % "Packaging Options" + 
                (
                    option("--family") & family_id("family_id").set(settings.family_id) % "family ID for UF2"
                ).force_expand_help(true) % "UF2 Family options"
            #if SUPPORT_A2
                + (
                    option("--abs-block").set(settings.uf2.abs_block) % "Add an absolute block" +
                        hex("abs_block_loc").set(settings.uf2.abs_block_loc).min(0) % "absolute block location (default to 0x10ffff00)"
                ).force_expand_help(true).min(0) % "Errata RP2350-E10 Fix"
            #endif
        );
    }

    string get_doc() const override {
        return "Convert ELF/BIN to UF2.";
    }
};

vector<std::shared_ptr<cmd>> uf2_sub_commands {
    #if HAS_LIBUSB
        std::shared_ptr<cmd>(new uf2_info_command()),
    #endif
        std::shared_ptr<cmd>(new uf2_convert_command()),
};

struct uf2_command : public multi_cmd {
    uf2_command() : multi_cmd("uf2", uf2_sub_commands) {}
    string get_doc() const override {
        return "Commands related to UF2 creation and status";
    }
};

struct coprodis_command : public cmd {
    coprodis_command() : cmd("coprodis") {}
    bool execute(device_map &devices) override;
    virtual device_support get_device_support() override { return none; }

    group get_cli() override {
        return (
                option("--quiet").set(settings.quiet) % "Don't print any output" +
                option("--verbose").set(settings.verbose) % "Print verbose output" +
                named_file_selection_x("infile", 0) % "Input DIS" +
                named_file_selection_x("outfile", 1) % "Output DIS"
        );
    }

    string get_doc() const override {
        return "Post-process coprocessor instructions in disassembly files.";
    }
};

struct help_command : public cmd {
    help_command() : cmd("help") {}
    bool execute(device_map &devices) override;

    device_support get_device_support() override {
        return device_support::none;
    }

    group get_cli() override {
        return group(
            value("cmd").min(0) % "The command to get help for"
        );
    }

    string get_doc() const override {
        return "Show general help or help for a specific command";
    }
};

struct version_command : public cmd {
    version_command() : cmd("version") {}
    bool execute(device_map &devices) override {
        if (settings.version.semantic)
            std::cout << PICOTOOL_VERSION << "\n";
        else
            std::cout << "picotool v" << PICOTOOL_VERSION << " (" << SYSTEM_VERSION << ", " << COMPILER_INFO << ")\n";
        if (!settings.version.version.empty()) {
            string picotool_v = string(PICOTOOL_VERSION);
            picotool_v = picotool_v.substr(0, picotool_v.find("-"));

            int check_v[3], cur_v[3];
            sscanf(settings.version.version.c_str(), "%d.%d.%d", &check_v[0], &check_v[1], &check_v[2]);
            sscanf(picotool_v.c_str(), "%d.%d.%d", &cur_v[0], &cur_v[1], &cur_v[2]);

            if (check_v[0] != cur_v[0])
                fail(ERROR_INCOMPATIBLE, "Version %s not compatible with this software\n", settings.version.version.c_str());
            for (int i = 1; i < 3; i++) {
                if (check_v[i] > cur_v[i])
                    fail(ERROR_INCOMPATIBLE, "Version %s not compatible with this software\n", settings.version.version.c_str());
                else if (check_v[i] < cur_v[i])
                    break;
            }
        }
        return false;
    }

    device_support get_device_support() override {
        return device_support::none;
    }

    group get_cli() override {
        return group(
                option('s', "--semantic").set(settings.version.semantic) % "Output semantic version number only" +
                value("version").set(settings.version.version).min(0) % "Check compatibility with version"
        );
    }

    string get_doc() const override {
        return "Display picotool version";
    }
};

#if HAS_LIBUSB
struct reboot_command : public cmd {
    bool quiet;
    reboot_command() : cmd("reboot") {}
    bool execute(device_map &devices) override;

    group get_cli() override {
        return
        (
            option('a', "--application").set(settings.reboot_app_specified) % "Reboot back into the application (this is the default)" +
            option('u', "--usb").set(settings.reboot_usb) % "Reboot back into BOOTSEL mode" +
            (option('g', "--diagnostic") & integer("partition").min_value(-3).max_value(15).set(settings.reboot_diagnostic_partition)).min(0) +
            (option('c', "--cpu") & value("cpu").set(settings.switch_cpu)) % "Select arm | riscv CPU (if possible)"
        ).min(0).doc_non_optional(true) % "Reboot type" +
        device_selection % "Selecting the device to reboot";
    }

    bool force_requires_pre_reboot() override {
        // no point in rebooting twice
        return false;
    }

    string get_doc() const override {
        return "Reboot the device";
    }
};
auto reboot_cmd = std::shared_ptr<reboot_command>(new reboot_command());
#endif
auto help_cmd = std::shared_ptr<help_command>(new help_command());

vector<std::shared_ptr<cmd>> commands {
        std::shared_ptr<cmd>(new info_command()),
        std::shared_ptr<cmd>(new config_command()),
    #if HAS_LIBUSB
        std::shared_ptr<cmd>(new load_command()),
    #endif
    #if HAS_MBEDTLS
        std::shared_ptr<cmd>(new encrypt_command()),
        std::shared_ptr<cmd>(new seal_command()),
    #endif
        std::shared_ptr<cmd>(new link_command()),
    #if HAS_LIBUSB
        std::shared_ptr<cmd>(new save_command()),
        std::shared_ptr<cmd>(new erase_command()),
        std::shared_ptr<cmd>(new verify_command()),
        reboot_cmd,
    #endif
        std::shared_ptr<cmd>(new otp_command()),
        std::shared_ptr<cmd>(new partition_command()),
        std::shared_ptr<cmd>(new uf2_command()),
        std::shared_ptr<cmd>(new version_command()),
        std::shared_ptr<cmd>(new coprodis_command()),
        help_cmd
};

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


std::ostream null_buffer(nullptr);
clipp::formatting_ostream<std::ostream> fos_base(std::cout);
clipp::formatting_ostream<std::ostream> fos_null(null_buffer);

auto fos_base_ptr = std::make_shared<clipp::formatting_ostream<std::ostream>>(fos_base);
auto fos_null_ptr = std::make_shared<clipp::formatting_ostream<std::ostream>>(fos_null);
auto fos_ptr = fos_base_ptr;
#define fos (*fos_ptr)
#define fos_verbose if(settings.verbose) fos

using cli::option;
using cli::integer;
int parse(const int argc, char **argv) {
    bool help_mode = false;
    bool no_global_header = false;
    bool no_synopsis = false;
    std::string help_mode_prefix;

    int tab = 4;
    bool first = true;
    auto section_header=[&](const string &name) {
        fos.first_column(0);
        fos.hanging_indent(0);
        if (!first) fos.wrap_hard();
        first = false;
        fos << (uppercase(name) + ":\n");
    };
    auto usage=[&]() {
        if (help_mode && selected_cmd) {
            section_header(help_mode_prefix + selected_cmd->name());
            fos.first_column(tab);
            fos << selected_cmd->get_doc() << "\n";
        } else if (!selected_cmd && !no_global_header) {
            section_header(tool_name);
            fos.first_column(tab);
        #if HAS_LIBUSB
            fos << "Tool for interacting with RP-series device(s) in BOOTSEL mode, or with an RP-series binary" << "\n";
        #else
            fos << "Tool for interacting with an RP-series binary" << "\n";
        #endif
        }
        vector<string> synopsis;
        auto maybe_add_synopsys = [&](const std::string& name, std::shared_ptr<cmd>& c, bool force = false) {
            if (!force && selected_cmd && c != selected_cmd) return;
            vector<string> cmd_synopsis;
            if (c->is_multi()) {
                string s;
                for(auto& subc : c->sub_commands()) { s += subc->name() + "|"; }
                if (s.size() > 0) s.pop_back();
                cmd_synopsis.push_back(s);
            } else {
                cmd_synopsis = c->get_cli().synopsys();
            }
            for(auto &s : cmd_synopsis) {
                synopsis.emplace_back(name + " " + s);
            }
        };
        for(auto &c : commands) {
            if (c->is_multi()) {
                if (c == selected_cmd) {
                    // Selected multi-command, so print sub commands
                    for(auto& subc : c->sub_commands()) {
                        maybe_add_synopsys( c->name() + " " + subc->name(), subc, true);
                    }
                } else if (selected_cmd) {
                    for(auto& subc : c->sub_commands()) {
                        // Selected sub-command, so print that
                        if (selected_cmd && subc == selected_cmd) maybe_add_synopsys( c->name() + " " + subc->name(), subc, true);
                    }
                } else {
                    // No command selected, so print multi-command
                    maybe_add_synopsys(c->name(), c);
                }
            } else {
                maybe_add_synopsys(c->name(), c);
            }
        }
        if (!no_synopsis) {
            section_header("SYNOPSIS");
            for (auto &s : synopsis) {
                fos.first_column(tab);
                fos.hanging_indent((int)tool_name.length() + tab);
                fos << (tool_name + " ").append(s).append("\n");
            }
        }
        auto write_command = [&](size_t max, const std::string& name, std::shared_ptr<cmd> c) {
            fos.first_column(tab);
            fos << name;
            fos.first_column((int) (max + tab + 3));
            std::stringstream s;
            s << c->get_doc();
            if (c->requires_rp2350()) {
                s << " (RP2350 only)";
            }
            s << "\n";
            fos << s.str();
        };
        if (!selected_cmd) {
            size_t max = 0;
            section_header("COMMANDS");
            for (auto &cmd: commands) {
                max = std::max(cmd->name().size(), max);
            }
            for (auto &cmd: commands) {
                write_command(max, cmd->name(), cmd);
            }
        } else if (selected_cmd->is_multi()) {
            section_header("SUB COMMANDS");
            size_t max = 0;
            auto sub_commands = selected_cmd->sub_commands();
            for (auto &cmd: sub_commands) {
                max = std::max(cmd->name().size(), max);
            }
            for (auto &cmd: sub_commands) {
                write_command(max, cmd->name(), cmd);
            }
        } else if (!help_mode) {
            fos.first_column(0);
            fos.hanging_indent(0);
            fos.wrap_hard();
            // Check if sub command
            std::shared_ptr<cmd> super_command = nullptr;
            for(auto &c : commands) {
                if (c->is_multi()) {
                    if (selected_cmd) {
                        for(auto& subc : c->sub_commands()) {
                            // Selected sub-command, so print that
                            if (selected_cmd && subc == selected_cmd) super_command = c;
                        }
                    }
                }
            }
            if (super_command != nullptr) {
                fos << string("Use \"picotool help ").append(super_command->name() +" "+ selected_cmd->name()).append("\" for more info\n");
            } else {
                fos << string("Use \"picotool help ").append(selected_cmd->name()).append("\" for more info\n");
            }
        } else {
            cli::option_map options;
            selected_cmd->get_cli().get_option_help("", "", options);
            for (const auto &major : options.contents.ordered_keys()) {
                section_header(major.empty() ? "OPTIONS" : major);
                bool first = true;
                for (const auto &minor : options.contents[major].ordered_keys()) {
                    fos.first_column(tab);
                    fos.hanging_indent(tab*2);
                    if (!minor.empty()) {
                        fos << minor << "\n";
                    } else if (!first) {
                        fos << "Other\n";
                    }
                    first = false;
                    for (const auto &opts : options.contents[major][minor]) {
                        fos.first_column(tab*2);
                        fos.hanging_indent(0);
                        fos << opts.first << "\n";
                        fos.first_column(tab*3);
                        fos.hanging_indent(0);
                        fos << opts.second << "\n";
                    }
                }
            }
        }
        if (!selected_cmd) {
            fos.first_column(0);
            fos.hanging_indent(0);
            fos.wrap_hard();
            fos << "Use \"picotool help <cmd>\" for more info\n";
        }
        fos.flush();
    };

    auto args = cli::make_args(argc, argv);
    if (args.empty()) {
        usage();
        return 0;
    }

    auto find_command = [&](const string& name) {
        if (name[0]=='-') {
            no_global_header = true;
            throw cli::parse_error("Expected command name before any options");
        }
        auto cmd = std::find_if(commands.begin(), commands.end(), [&](auto &x) { return x->name() == name; });
        if (cmd == commands.end()) {
            selected_cmd = nullptr; // we want to list all commands
            no_synopsis = true;
            no_global_header = true;
            throw cli::parse_error("Unknown command: " + name);
        }
        return *cmd;
    };

    auto find_sub_command = [&](std::shared_ptr<cmd>& parent_cmd, const string& name) {
        if (name[0]=='-') {
            no_global_header = true;
            throw cli::parse_error("Expected "+parent_cmd->name()+" sub command name before any options");
        }
        auto commands = parent_cmd->sub_commands();
        auto cmd = std::find_if(commands.begin(), commands.end(), [&](auto &x) { return x->name() == name; });
        if (cmd == commands.end()) {
            selected_cmd = parent_cmd; // we want to list all commands
            no_synopsis = true;
            no_global_header = true;
            throw cli::parse_error("Unknown "+parent_cmd->name()+" sub command: " + name);
        }
        return *cmd;
    };

    try {
        selected_cmd = find_command(args[0]);
        args.erase(args.begin()); // remove the cmd itself
        if (selected_cmd->is_multi()) {
            if (args.empty()) {
                no_synopsis = true;
                no_global_header = true;
                throw cli::parse_error("Expected "+selected_cmd->name()+" sub-command");
            } else {
                selected_cmd = find_sub_command(selected_cmd, args[0]);
                args.erase(args.begin());
            }
        }
        if (selected_cmd->name() == "help") {
            help_mode = true;
            if (args.empty()) {
                selected_cmd = nullptr;
                usage();
                return 0;
            } else {
                selected_cmd = find_command(args[0]);
                if (selected_cmd->is_multi() && args.size() > 1) {
                    help_mode_prefix = selected_cmd->name() + " ";
                    selected_cmd = find_sub_command(selected_cmd, args[1]);
                }
                usage();
                selected_cmd = nullptr;
                return 0;
            }
        } else if (!selected_cmd) {
            no_synopsis = true;
            no_global_header = true;
            throw cli::parse_error("unknown command '" + args[0] + "'");
        }
        cli::match(settings, selected_cmd->get_cli(), args);
    } catch (std::exception &e) {
        fos.wrap_hard();
        fos << "ERROR: " << e.what() << "\n\n";
        usage();
        return ERROR_ARGS;
    }
    return 0;
}

template <typename T> struct raw_type_mapping {
};

#define SAFE_MAPPING(type) template<> struct raw_type_mapping<type> { typedef type access_type; }

//template<> struct raw_type_mapping<uint32_t> {
//    typedef uint32_t access_type;
//};

// these types may be filled directly from byte representation
SAFE_MAPPING(uint8_t);
SAFE_MAPPING(char);
SAFE_MAPPING(uint16_t);
SAFE_MAPPING(uint32_t);
SAFE_MAPPING(binary_info_core_t);
SAFE_MAPPING(binary_info_id_and_int_t);
SAFE_MAPPING(binary_info_id_and_string_t);
SAFE_MAPPING(binary_info_ptr_int32_with_name_t);
SAFE_MAPPING(binary_info_ptr_string_with_name_t);
SAFE_MAPPING(binary_info_block_device_t);
SAFE_MAPPING(binary_info_pins_with_func_t);
SAFE_MAPPING(binary_info_pins_with_name_t);
SAFE_MAPPING(binary_info_pins64_with_func_t);
SAFE_MAPPING(binary_info_pins64_with_name_t);
SAFE_MAPPING(binary_info_named_group_t);

#define BOOTROM_MAGIC_RP2040 0x01754d
#define BOOTROM_MAGIC_RP2350 0x02754d
#define BOOTROM_MAGIC_UNKNOWN 0x000000
#define BOOTROM_MAGIC_ADDR 0x00000010

static inline uint32_t rom_table_code(char c1, char c2) {
    return (c2 << 8u) | c1;
}

struct memory_access {
    virtual void read(uint32_t p, uint8_t *buffer, unsigned int size) {
        read(p, buffer, size, false);
    }

    virtual void read(uint32_t, uint8_t *buffer, unsigned int size, bool zero_fill) = 0;

    virtual void write(uint32_t, uint8_t *buffer, unsigned int size) = 0;

    virtual bool is_device() { return false; }

    virtual uint32_t get_binary_start() = 0;

    uint32_t read_int(uint32_t addr) {
        assert(!(addr & 3u));
        uint32_t rc;
        read(addr, (uint8_t *)&rc, 4);
        return rc;
    }

    uint32_t read_short(uint32_t addr) {
        assert(!(addr & 1u));
        uint16_t rc;
        read(addr, (uint8_t *)&rc, 2);
        return rc;
    }

    // read a vector of types that have a raw_type_mapping
    template <typename T> void read_raw(uint32_t addr, T &v) {
        typename raw_type_mapping<T>::access_type& check = v; // ugly check that we aren't trying to read into something we shouldn't
        read(addr, (uint8_t *)&v, sizeof(typename raw_type_mapping<T>::access_type));
    }

    // read a vector of types that have a raw_type_mapping
    template <typename T> vector<T> read_vector(uint32_t addr, unsigned int count, bool zero_fill = false) {
        assert(count);
        vector<typename raw_type_mapping<T>::access_type> buffer(count);
        read(addr, (uint8_t *)buffer.data(), count * sizeof(typename raw_type_mapping<T>::access_type), zero_fill);
        vector<T> v;
        v.reserve(count);
        for(const auto &e : buffer) {
            v.push_back(e);
        }
        return v;
    }

    // write a vector of types that have a raw_type_mapping
    template <typename T> void write_vector(uint32_t addr, vector<T> &v) {
        assert(v.size());
        vector<typename raw_type_mapping<T>::access_type> buffer(v.size());
        for(const auto &e : v) {
            buffer.push_back(e);
        }
        write(addr, (uint8_t *)buffer.data(), v.size() * sizeof(typename raw_type_mapping<T>::access_type));
    }

    template <typename T> void read_into_vector(uint32_t addr, unsigned int count, vector<T> &v, bool zero_fill = false) {
        vector<typename raw_type_mapping<T>::access_type> buffer(count);
        if (count) read(addr, (uint8_t *)buffer.data(), count * sizeof(typename raw_type_mapping<T>::access_type), zero_fill);
        v.clear();
        v.reserve(count);
        for(const auto &e : buffer) {
            v.push_back(e);
        }
    }
};

static model_t get_model(memory_access &raw_access) {
    auto magic = raw_access.read_int(BOOTROM_MAGIC_ADDR);
    magic &= 0xffffff; // ignore bootrom version
    if (magic == BOOTROM_MAGIC_RP2040) {
        return rp2040;
    } else if (magic == BOOTROM_MAGIC_RP2350) {
        return rp2350;
    } else {
        return unknown;
    }
}

template<typename T>
bool get_int(const std::string& s, T& out) {
    return integer::parse_string(s, out).empty();
}

template<typename T>
bool get_json_int(json value, T& out) {
    if (value.is_string()) {
        string str = value;
        if (str.back() == 'k' || str.back() == 'K') {
            str.pop_back();
            int tmp;
            if (get_int(str, tmp)) {
                out = tmp * 1024;
                return true;
            } else {
                return false;
            }
        }
        return get_int(str, out);
    } else if (value.is_number_integer()) {
        out = value;
        return true;
    } else {
        return false;
    }
}

uint32_t bootrom_func_lookup(memory_access& access, uint16_t tag) {
    model_t model = get_model(access);
    // we are only used on RP2040
    if (model != rp2040) {
        fail(ERROR_INCOMPATIBLE, "RP2040 BOOT ROM not found");
    }

    // dereference the table pointer
    uint32_t table_entry = access.read_short(BOOTROM_MAGIC_ADDR + 4);
    uint16_t entry_tag;
    do {
        entry_tag = access.read_short(table_entry);
        if (entry_tag == tag) {
            // 16 bit symbol is next
            return access.read_short(table_entry+2);
        }
        table_entry += 4;
    } while (entry_tag);
    fail(ERROR_INCOMPATIBLE, "Function not found in BOOT ROM");
    return 0;
}

uint32_t bootrom_table_lookup_rp2350(memory_access& access, uint16_t tag, uint16_t flags) {
    model_t model = get_model(access);
    // we are only used on RP2350
    if (model != rp2350) {
        fail(ERROR_INCOMPATIBLE, "RP2350 BOOT ROM not found");
    }

    // dereference the table pointer
    uint32_t table_entry = access.read_short(BOOTROM_MAGIC_ADDR + 4);
    uint16_t entry_tag, entry_flags;
    do {
        entry_tag = access.read_short(table_entry);
        entry_flags = access.read_short(table_entry + 2);
        uint16_t matching_flags = flags & entry_flags;
        table_entry += 4;
        if (tag == entry_tag && matching_flags != 0) {
            /* This is our entry, seek to the correct data item and return it. */
            bool is_riscv_func = matching_flags & RT_FLAG_FUNC_RISCV;
            while (!(matching_flags & 1)) {
                if (entry_flags & 1) {
                    table_entry += 2;
                }
                matching_flags >>= 1;
                entry_flags >>= 1;
            }
            if (is_riscv_func) {
                /* For RISC-V, the table entry itself is the entry point -- trick
                    to make shared function implementations smaller */
                return table_entry;
            } else {
                return access.read_short(table_entry);
            }
        } else {
            /* Skip past this entry */
            while (entry_flags) {
                if (entry_flags & 1) {
                    table_entry += 2;
                }
                entry_flags >>= 1;
            }
        }
    } while (entry_tag);
    fail(ERROR_INCOMPATIBLE, "Entry not found in BOOT ROM");
    return 0;
}

static uint32_t get_rom_git_revision(memory_access &raw_access) {
    unsigned int addr = bootrom_table_lookup_rp2350(raw_access, rom_table_code('G','R'), RT_FLAG_DATA);
    return raw_access.read_int(addr);
}

static rp2350_version_t get_rp2350_version(memory_access &raw_access) {
    switch (get_rom_git_revision(raw_access)) {
        case 0x312e22fa:
            return rp2350_a2;
        default:
            return rp2350_unknown;
    }
}
#if HAS_LIBUSB
struct picoboot_memory_access : public memory_access {
    model_t model = unknown; // must be initialized to something up front as it is referenced before it is set to its final value
    explicit picoboot_memory_access(picoboot::connection &connection) : connection(connection) {
        model = get_model(*this);
    }

    bool is_device() override {
        return true;
    }

    uint32_t get_binary_start() override {
        return FLASH_START;
    }

    void read(uint32_t address, uint8_t *buffer, unsigned int size, __unused bool zero_fill) override {
        if (flash == get_memory_type(address, model)) {
            connection.exit_xip();
        }
        if (model == rp2040 && rom == get_memory_type(address, model) && (address+size) >= 0x2000) {
            // read by memcpy instead
            unsigned int program_base = SRAM_START + 0x4000;
            // program is "return memcpy(SRAM_BASE, 0, 0x4000);"
            std::vector<uint32_t> program = {
                    0x07482101, // movs r1, #1;       lsls r0, r1, #29
                    0x2100038a, // lsls r2, r1, #14;  movs r1, #0
                    0x47184b00, // ldr  r3, [pc, #0]; bx r3
                    bootrom_func_lookup(*this, rom_table_code('M','C'))
            };
            write_vector(program_base, program);
            connection.exec(program_base);
            // 4k is copied into the start of RAM
            connection.read(SRAM_START + address, (uint8_t *) buffer, size);
        } else if (model == rp2350 && rom == get_memory_type(address, model) && (address+size) > 0x7e00) {
            // Cannot read end section of rom from device
            uint16_t unreadable_start = MAX(address, 0x7e00);
            uint16_t unreadable_end = MIN(address+size, 0x8000);
            uint16_t idx = 0;
            if (address < unreadable_start) {
                connection.read(address, (uint8_t *) buffer, unreadable_start - address);
                idx += unreadable_start - address;
            }
            memcpy(buffer+idx, rp2350_rom+(unreadable_start-0x7e00), unreadable_end - unreadable_start);
            idx += unreadable_end - unreadable_start;
            if (address + size > unreadable_end) {
                connection.read(unreadable_end, (uint8_t *) buffer+idx, size - idx);
            }
        } else if (is_transfer_aligned(address, model) && is_transfer_aligned(address + size, model)) {
            connection.read(address, (uint8_t *) buffer, size);
        } else {
            if (flash == get_memory_type(address, model)) {
                uint32_t aligned_start = address & ~(PAGE_SIZE - 1);
                uint32_t aligned_end = (address + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                vector<uint8_t> tmp_buffer(aligned_end - aligned_start);
                connection.read(aligned_start, tmp_buffer.data(), aligned_end - aligned_start);
                    std::copy(tmp_buffer.cbegin() + (address - aligned_start), tmp_buffer.cbegin() + (address + size - aligned_start), buffer);
            } else {
                std::stringstream sstream;
                sstream << "Address range " << hex_string(address) << " + " << hex_string(size);
                throw std::invalid_argument(sstream.str());
            }
        }
    }

    // note this does not automatically erase flash unless erase is set
    void write(uint32_t address, uint8_t *buffer, unsigned int size) override {
        vector<uint8_t> write_data; // used when erasing flash
        if (flash == get_memory_type(address, model)) {
            connection.exit_xip();
            if (erase) {
                // Do automatically erase flash, and make it aligned
                // we have to erase in whole pages
                range aligned_range(address & ~(FLASH_SECTOR_ERASE_SIZE - 1),
                                    ((address + size) & ~(FLASH_SECTOR_ERASE_SIZE - 1)) + FLASH_SECTOR_ERASE_SIZE);
                assert(aligned_range.contains(address));
                assert(aligned_range.contains(address + size));

                uint32_t pre_len = address - aligned_range.from;
                uint32_t post_len = aligned_range.to - (address + size);
                assert(pre_len + size + post_len == aligned_range.len());

                // save data before the changing data
                write_data.resize(pre_len);
                read(aligned_range.from, write_data.data(), write_data.size(), false);
                // now add the data that is changing
                write_data.insert(write_data.end(), buffer, buffer + size);
                // save data after the changing data
                write_data.resize(aligned_range.len());
                read(address + size, write_data.data() + pre_len + size, post_len, false);

                // Do the erase
                connection.flash_erase(aligned_range.from, aligned_range.len());

                // Update what will now be written
                address = aligned_range.from;
                buffer = write_data.data();
                size = aligned_range.len();
            }
        }
        if (is_transfer_aligned(address, model) && is_transfer_aligned(address + size, model)) {
            connection.write(address, (uint8_t *) buffer, size);
        } else {
            // for write, we must be correctly sized/aligned in 256 byte chunks
            std::stringstream sstream;
            sstream << "Address range " << hex_string(address) << " + " << hex_string(size);
            throw std::invalid_argument(sstream.str());
        }
    }

    template <typename T> void write_vector(uint32_t addr, vector<T> v) {
        assert(!v.empty());
        write(addr, (uint8_t *)v.data(), v.size() * sizeof(typename raw_type_mapping<T>::access_type));
    }

    bool erase = false;
private:
    picoboot::connection& connection;
};
#endif


struct iostream_memory_access : public memory_access {
    iostream_memory_access(std::shared_ptr<std::iostream> file, range_map<size_t>& rmap, uint32_t binary_start) : file(file), rmap(rmap), binary_start(binary_start) {

    }

    uint32_t get_binary_start() override {
        return binary_start;
    }

    void set_model(model_t m) {
        model = m;
    }

    void read(uint32_t address, uint8_t *buffer, uint32_t size, bool zero_fill) override {
        if (address == BOOTROM_MAGIC_ADDR && size == 4) {
            // return the memory model
            if (model == rp2040) {
                *(uint32_t*)buffer = BOOTROM_MAGIC_RP2040;
                return;
            } else if (model == rp2350) {
                *(uint32_t*)buffer = BOOTROM_MAGIC_RP2350;
                return;
            } else {
                *(uint32_t*)buffer = BOOTROM_MAGIC_UNKNOWN;
                return;
            }
        }
        while (size) {
            unsigned int this_size;
            try {
                auto result = rmap.get(address);
                this_size = std::min(size, result.first.max_offset - result.first.offset);
                assert(this_size);
                file->seekg(result.second + result.first.offset, ios::beg);
                file->read((char*)buffer, this_size);
            } catch (not_mapped_exception &e) {
                if (zero_fill) {
                    // address is not in a range, so fill up to next range with zeros
                    this_size = rmap.next(address) - address;
                    this_size = std::min(this_size, size);
                    memset(buffer, 0, this_size);
                } else {
                    throw e;
                }
            }
            buffer += this_size;
            address += this_size;
            size -= this_size;
        }
    }

    void write(uint32_t address, uint8_t *buffer, uint32_t size) override {
        while (size) {
            unsigned int this_size;
            auto result = rmap.get(address);
            this_size = std::min(size, result.first.max_offset - result.first.offset);
            assert(this_size);
            file->seekp(result.second + result.first.offset, ios::beg);
            file->write((char*)buffer, this_size);
            if (file->fail()) {
                fail(ERROR_WRITE_FAILED, "Write to file failed");
            }
            buffer += this_size;
            address += this_size;
            size -= this_size;
        }
    }

    const range_map<size_t> &get_rmap() {
        return rmap;
    }
private:
    std::shared_ptr<std::iostream>file;
    range_map<size_t> rmap;
    uint32_t binary_start;
    model_t model = unknown;
};


struct file_memory_access : public iostream_memory_access {
    file_memory_access(std::shared_ptr<std::fstream> file, range_map<size_t>& rmap, uint32_t binary_start) : iostream_memory_access(file, rmap, binary_start), file(file) {
        
    }

    ~file_memory_access() {
        file->close();
    }
private:
    std::shared_ptr<std::fstream>file;
};

struct remapped_memory_access : public memory_access {
    remapped_memory_access(memory_access &wrap, range_map<uint32_t> rmap) : wrap(wrap), rmap(rmap) {}

    void read(uint32_t address, uint8_t *buffer, unsigned int size, bool zero_fill) override {
        while (size) {
            auto result = get_remapped(address);
            unsigned int this_size = std::min(size, result.first.max_offset - result.first.offset);
            assert( this_size);
            wrap.read(result.second + result.first.offset, buffer, this_size, zero_fill);
            buffer += this_size;
            address += this_size;
            size -= this_size;
        }
    }

    void write(uint32_t address, uint8_t *buffer, unsigned int size) override {
        while (size) {
            auto result = get_remapped(address);
            unsigned int this_size = std::min(size, result.first.max_offset - result.first.offset);
            assert( this_size);
            wrap.write(result.second + result.first.offset, buffer, this_size);
            buffer += this_size;
            address += this_size;
            size -= this_size;
        }
    }

    bool is_device() override {
        return wrap.is_device();
    }

    uint32_t get_binary_start() override {
        return wrap.get_binary_start(); // this is an absolute address
    }

    pair<range_map<uint32_t>::mapping, uint32_t> get_remapped(uint32_t address) {
        try {
            return rmap.get(address);
        } catch (not_mapped_exception&) {
            return std::make_pair(range_map<uint32_t>::mapping(0, rmap.next(address) - address), address);
        }
    }

private:
    memory_access& wrap;
    range_map<uint32_t> rmap;
};

struct partition_memory_access : public memory_access {
    partition_memory_access(memory_access &wrap, uint32_t partition_start) : wrap(wrap), partition_start(partition_start) {
        model = get_model(wrap);
    }

    void read(uint32_t address, uint8_t *buffer, unsigned int size, bool zero_fill) override {
        if (get_memory_type(address, model) == flash) {
            // Only change address when reading from flash
            wrap.read(address + partition_start, buffer, size, zero_fill);
        } else {
            wrap.read(address, buffer, size, zero_fill);
        }
    }

    void write(uint32_t address, uint8_t *buffer, unsigned int size) override {
        wrap.write(address + partition_start, buffer, size);
    }

    bool is_device() override {
        return wrap.is_device();
    }

    uint32_t get_binary_start() override {
        return wrap.get_binary_start(); // this is an absolute address
    }

private:
    memory_access& wrap;
    uint32_t partition_start;
    model_t model;
};

static void read_and_check_elf32_header(std::shared_ptr<std::iostream>in, elf32_header& eh_out) {
    in->read((char*)&eh_out, sizeof(eh_out));
    if (in->fail()) {
        fail(ERROR_FORMAT, "'" + settings.filenames[0] +"' is not an ELF file");
    }
    try {
        rp_check_elf_header(eh_out);
    } catch (command_failure &e) {
        fail(e.code(), "'" + settings.filenames[0] +"' failed validation - " + e.what());
    }
}

static void fail_read_error() {
    fail(ERROR_READ_FAILED, "Failed to read input file");
}

static void fail_write_error() {
    fail(ERROR_WRITE_FAILED, "Failed to write output file");
}

struct binary_info_header {
    vector<uint32_t> bi_addr;
    range_map<uint32_t> reverse_copy_mapping;
};

bool find_binary_info(memory_access& access, binary_info_header &hdr) {
    uint32_t base = access.get_binary_start();
    model_t model = get_model(access);
    if (!base) {
        fail(ERROR_FORMAT, "UF2 file does not contain a valid RP2 executable image");
    }
    uint32_t max_dist = 256;
    if (model == rp2040) {
        max_dist = 64;
        if (base == FLASH_START) base += 0x100; // skip the boot2
    }
    vector<uint32_t> buffer = access.read_vector<uint32_t>(base, max_dist, true);
    for(unsigned int i=0;i<buffer.size();i++) {
        if (buffer[i] == BINARY_INFO_MARKER_START) {
            if (i + 4 < max_dist && buffer[i+4] == BINARY_INFO_MARKER_END) {
                uint32_t from = buffer[i+1];
                uint32_t to = buffer[i+2];
                enum memory_type from_type = get_memory_type(from, model);
                enum memory_type to_type = get_memory_type(to, model);
                if (to > from &&
                        from_type == to_type &&
                        is_size_aligned(from, 4) &&
                        is_size_aligned(to, 4)) {
                    access.read_into_vector(from, (to - from) / 4, hdr.bi_addr);
                    uint32_t cpy_table = buffer[i+3];
                    vector<uint32_t> mapping;
                    do {
                        mapping = access.read_vector<uint32_t>(cpy_table, 3);
                        if (!mapping[0]) break;
                        // from, to_start, to_end
                        hdr.reverse_copy_mapping.insert(range(mapping[1], mapping[2]), mapping[0]);
                        cpy_table += 12;
                    } while (hdr.reverse_copy_mapping.size() < 10); // arbitrary max
                    return true;
                }
            }
        }
    }
    return false;
}

string read_string(memory_access &access, uint32_t addr) {
    const unsigned int max_length = 512;
    auto v = access.read_vector<char>(addr, max_length, true); // zero fill
    unsigned int length;
    for (length = 0; length < max_length; length++) {
        if (!v[length]) {
            break;
        }
    }
    return string(v.data(), length);
}

struct bi_visitor_base {
    template <typename T> void do_pins_func(T value) {
        uint8_t bpp = 5u; // bits per pin
        uint8_t pm = 0x1fu; // pin mask
        uint8_t fp = 7u; // first pin
        uint8_t max_pins = 5;
        if (std::is_same<T, binary_info_pins64_with_func_t>::value) {
            bpp = 8u;
            pm = 0xffu;
            fp = 8u;
            max_pins = 7;
        }
        unsigned int type = value.pin_encoding & 7u;
        unsigned int func = (value.pin_encoding >> 3u) & 0xfu;
        if (type == BI_PINS_ENCODING_RANGE) {
            uint64_t mask = 0;
            uint8_t plo = (value.pin_encoding >> fp) & pm;
            uint8_t phi = (value.pin_encoding >> (fp + bpp)) & pm;
            for(int i=plo;i<=phi;i++) { // range includes end
                mask |= 1ull << i;
            }
            pins(mask, func, "");
        } else if (type == BI_PINS_ENCODING_MULTI) {
            uint64_t mask = 0;
            int last = -1;
            uint64_t work = value.pin_encoding >> fp;
            for(int i=0;i<max_pins;i++) {
                int cur = (int) (work & pm);
                mask |= 1ull << cur;
                if (cur == last) break;
                last = cur;
                work >>= bpp;
            }
            pins(mask, func, "");
        }
    }

    void visit(memory_access& access, const binary_info_header& hdr) {
        try {
            model = get_model(access);
        } catch (not_mapped_exception&) {
            model = rp2040;
        }
        for (const auto &a : hdr.bi_addr) {
            visit(access, a);
        }
    }

    void visit(memory_access& access, uint32_t addr) {
        binary_info_core_t bi;
        access.read_raw(addr, bi);
        switch (bi.type) {
            case BINARY_INFO_TYPE_RAW_DATA:
                break;
            case BINARY_INFO_TYPE_SIZED_DATA:
                break;
            case BINARY_INFO_TYPE_BINARY_INFO_LIST_ZERO_TERMINATED:
                zero_terminated_bi_list(access, bi, addr);
                break;
            case BINARY_INFO_TYPE_BSON:
                break;
            case BINARY_INFO_TYPE_ID_AND_INT: {
                binary_info_id_and_int_t value;
                access.read_raw(addr, value);
                id_and_value(bi.tag, value.id, value.value);
                break;
            }
            case BINARY_INFO_TYPE_ID_AND_STRING: {
                binary_info_id_and_string_t value;
                access.read_raw(addr, value);
                string s = read_string(access, value.value);
                id_and_string(bi.tag, value.id, s);
                break;
            }
            case BINARY_INFO_TYPE_PTR_INT32_WITH_NAME: {
                binary_info_ptr_int32_with_name_t value;
                access.read_raw(addr, value);
                string s = read_string(access, value.label);
                int i;
                access.read(value.value, (uint8_t*)&i, sizeof(i));
                ptr_int32_t_with_name(access, bi.tag, value.id, s, i, value.value);
                break;
            }
            case BINARY_INFO_TYPE_PTR_STRING_WITH_NAME: {
                binary_info_ptr_string_with_name_t value;
                access.read_raw(addr, value);
                string s = read_string(access, value.label);
                string v = read_string(access, value.value);
                ptr_string_t_with_name(access, bi.tag, value.id, s, v, value.value, value.len);
                break;
            }
            case BINARY_INFO_TYPE_BLOCK_DEVICE: {
                binary_info_block_device_t value;
                access.read_raw(addr, value);
                block_device(access, value);
                break;
            }
            case BINARY_INFO_TYPE_PINS_WITH_FUNC: {
                binary_info_pins_with_func_t value;
                access.read_raw(addr, value);
                do_pins_func(value);
                break;
            }
            case BINARY_INFO_TYPE_PINS64_WITH_FUNC: {
                binary_info_pins64_with_func_t value;
                access.read_raw(addr, value);
                do_pins_func(value);
                break;
            }
            case BINARY_INFO_TYPE_PINS_WITH_NAME: {
                binary_info_pins_with_name_t value;
                access.read_raw(addr, value);
                pins(value.pin_mask, -1, read_string(access, value.label));
                break;
            }
            case BINARY_INFO_TYPE_PINS64_WITH_NAME: {
                binary_info_pins64_with_name_t value;
                access.read_raw(addr, value);
                pins(value.pin_mask, -1, read_string(access, value.label));
                break;
            }
            case BINARY_INFO_TYPE_NAMED_GROUP: {
                binary_info_named_group_t value;
                access.read_raw(addr, value);
                named_group(value.core.tag, value.parent_id, value.group_tag, value.group_id, read_string(access, value.label), value.flags);
                break;
            }
            default:
                unknown(access, bi, addr);
        }
    }

    virtual void unknown(memory_access& access, const binary_info_core_t &bi_core, uint32_t addr) {}
    virtual void id_and_value(int tag, uint32_t id, uint32_t value) {

    }
    virtual void id_and_string(int tag, uint32_t id, const string& value) {

    }
    virtual void ptr_int32_t_with_name(memory_access& access, int tag, uint32_t id, const string& label, int32_t value, uint32_t addr) {

    }
    virtual void ptr_string_t_with_name(memory_access& access, int tag, uint32_t id, const string& label, const string& value, uint32_t addr, uint32_t max_len) {

    }
    virtual void block_device(memory_access& access, binary_info_block_device_t &bi_bdev) {
    }

    virtual void pins(uint64_t pin_mask, int func, string name) {
        if (model == rp2350) {
            pins(pin_mask, func, name, pin_functions_rp2350);
        } else {
            pins(pin_mask, func, name, pin_functions_rp2040);
        }
    }

    template <class pin_functions_t> void pins(uint64_t pin_mask, int func, string name, pin_functions_t pin_functions) {
        if (func != -1) {
            if (func >= (int)pin_functions.size())
                return;
        }
        for(unsigned int i=0; i<64; i++) {
            if (pin_mask & (1ull << i)) {
                if (func != -1) {
                    if (pin_functions[func][i].empty()) {
                        std::stringstream sstream;
                        sstream << "Unknown pin function " << func;
                        pin(i, sstream.str());
                    } else {
                        pin(i, pin_functions[func][i]);
                    }
                } else {
                    auto sep = name.find_first_of('|');
                    auto cur = name.substr(0, sep);
                    if (cur.empty()) continue;
                    pin(i, cur.c_str());
                    if (sep != string::npos) {
                        name = name.substr(sep + 1);
                    }
                }
            }
        }
    }

    virtual void pin(unsigned int i, const string& name) {

    }

    virtual void zero_terminated_bi_list(memory_access& access, const binary_info_core_t &bi_core, uint32_t addr) {
        uint32_t bi_addr;
        access.read_raw<uint32_t>(addr,bi_addr);
        while (bi_addr) {
            visit(access, addr);
            access.read_raw<uint32_t>(addr,bi_addr);
        }
    }

    virtual void named_group(int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id, const string& label, unsigned int flags) {

    }

    model_t model = model_t::unknown;
};

struct bi_visitor : public bi_visitor_base {
    typedef std::function<void(int tag, uint32_t id, uint32_t value)> id_and_int_fn;
    typedef std::function<void(int tag, uint32_t id, const string &value)> id_and_string_fn;
    typedef std::function<void(int tag, uint32_t id, const string &label, int32_t value)> ptr_int32_t_with_name_fn;
    typedef std::function<void(int tag, uint32_t id, const string &label, const string &value)> ptr_string_t_with_name_fn;
    typedef std::function<void(unsigned int num, const string &label)> pin_fn;
    typedef std::function<void(int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id,
                               const string &label, unsigned int flags)> named_group_fn;
    typedef std::function<void(memory_access &access, binary_info_block_device_t &bi_bdev)> block_device_fn;

    id_and_int_fn _id_and_int;
    id_and_string_fn _id_and_string;
    ptr_int32_t_with_name_fn _ptr_int32_t_with_name;
    ptr_string_t_with_name_fn _ptr_string_t_with_name;
    pin_fn _pin;
    named_group_fn _named_group;
    block_device_fn _block_device;

    bi_visitor &id_and_int(id_and_int_fn fn) {
        _id_and_int = std::move(fn);
        return *this;
    }

    bi_visitor &id_and_string(id_and_string_fn fn) {
        _id_and_string = std::move(fn);
        return *this;
    }

    bi_visitor &ptr_int32_t_with_name(ptr_int32_t_with_name_fn fn) {
        _ptr_int32_t_with_name = std::move(fn);
        return *this;
    }

    bi_visitor &ptr_string_t_with_name(ptr_string_t_with_name_fn fn) {
        _ptr_string_t_with_name = std::move(fn);
        return *this;
    }

    bi_visitor &pin(pin_fn fn) {
        _pin = std::move(fn);
        return *this;
    }

    bi_visitor &named_group(named_group_fn fn) {
        _named_group = std::move(fn);
        return *this;
    }

    bi_visitor &block_device(block_device_fn fn) {
        _block_device = std::move(fn);
        return *this;
    }

protected:
    void id_and_value(int tag, uint32_t id, uint32_t value) override {
        if (_id_and_int) _id_and_int(tag, id, value);
    }

    void id_and_string(int tag, uint32_t id, const string &value) override {
        if (_id_and_string) _id_and_string(tag, id, value);
    }

    void ptr_int32_t_with_name(memory_access& access, int tag, uint32_t id, const string &label, int32_t value, uint32_t addr) override {
        if (_ptr_int32_t_with_name) _ptr_int32_t_with_name(tag, id, label, value);
    }

    void ptr_string_t_with_name(memory_access& access, int tag, uint32_t id, const string &label, const string &value, uint32_t addr, uint32_t max_len) override {
        if (_ptr_string_t_with_name) _ptr_string_t_with_name(tag, id, label, value);
    }

    void pin(unsigned int i, const string &name) override {
        if (_pin) _pin(i, name);
    }

    void named_group(int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id, const string &label,
                     unsigned int flags) override {
        if (_named_group) _named_group(parent_tag, parent_id, group_tag, group_id, label, flags);
    }

    void block_device(memory_access &access, binary_info_block_device_t &bi_bdev) override {
        if (_block_device) _block_device(access, bi_bdev);
    }
};

struct bi_modifier : public bi_visitor_base {
    typedef std::function<bool(int tag, uint32_t id, const string &label, int32_t value, int32_t& new_value)> ptr_int32_t_with_name_fn;
    typedef std::function<bool(int tag, uint32_t id, const string &label, const string &value, string& new_value)> ptr_string_t_with_name_fn;

    ptr_int32_t_with_name_fn _ptr_int32_t_with_name;
    ptr_string_t_with_name_fn _ptr_string_t_with_name;

    bi_modifier &ptr_int32_t_with_name(ptr_int32_t_with_name_fn fn) {
        _ptr_int32_t_with_name = std::move(fn);
        return *this;
    }

    bi_modifier &ptr_string_t_with_name(ptr_string_t_with_name_fn fn) {
        _ptr_string_t_with_name = std::move(fn);
        return *this;
    }

protected:
    void ptr_int32_t_with_name(memory_access& access, int tag, uint32_t id, const string &label, int32_t value, uint32_t addr) override {
        if (_ptr_int32_t_with_name) {
            int32_t ret;
            if (_ptr_int32_t_with_name(tag, id, label, value, ret)) {
                DEBUG_LOG("Setting %x to %d\n", addr, ret);
                access.write(addr, (uint8_t*)&ret, sizeof(ret));
            }
        }
    }

    void ptr_string_t_with_name(memory_access& access, int tag, uint32_t id, const string &label, const string &value, uint32_t addr, uint32_t max_len) override {
        if (_ptr_string_t_with_name) {
            string ret;
            if (_ptr_string_t_with_name(tag, id, label, value, ret)) {
                if (ret.size() < max_len) {
                    DEBUG_LOG("Setting %x to %s\n", addr, ret.c_str());
                    access.write(addr, (unsigned char*)ret.c_str(), ret.size() + 1);
                } else {
                    fail(ERROR_INCOMPATIBLE, "String \"%s\" does not fit in %s - max length is %d (including null termination)", ret.c_str(), label.c_str(), max_len);
                }
            }
        }
    }
};

uint32_t guess_flash_size(memory_access &access) {
    assert(access.is_device());
    // Check that flash is not erased (TODO should check for second stage)
    auto first_two_pages = access.read_vector<uint8_t>(FLASH_START, 2 * PAGE_SIZE);
    bool all_match = std::equal(first_two_pages.begin(),
                                first_two_pages.begin() + PAGE_SIZE,
                                first_two_pages.begin() + PAGE_SIZE);
    if (all_match) {
        return 0;
    }

    // Read at decreasing power-of-two addresses until we don't see the boot pages again
    const int min_size = 16 * PAGE_SIZE;
    const int max_size = 8 * 1024 * 1024;
    int size;
    for (size = max_size; size >= min_size; size >>= 1) {
        auto new_pages = access.read_vector<uint8_t>(FLASH_START + size, 2 * PAGE_SIZE);
        if (!std::equal(first_two_pages.begin(), first_two_pages.end(), new_pages.begin())) break;
    }
    return size * 2;
}

std::shared_ptr<std::fstream> get_file_idx(ios::openmode mode, uint8_t idx) {
    auto filename = settings.filenames[idx];
    auto file = std::make_shared<std::fstream>(filename, mode);
    if (file->fail()) fail(ERROR_READ_FAILED, "Could not open '%s'", filename.c_str());
    return file;
}

std::shared_ptr<std::fstream> get_file(ios::openmode mode) {
    return get_file_idx(mode, 0);
}

enum filetype get_file_type_idx(uint8_t idx) {
    auto filename = settings.filenames[idx];
    auto file_type = settings.file_types[idx];
    auto low = lowercase(filename);
    if (file_type.empty() && low.size() >= 4) {
        if (low.rfind(".uf2") == low.size() - 4) {
            return filetype::uf2;
        } else if (low.rfind(".elf") == low.size() - 4) {
            return filetype::elf;
        } else if (low.rfind(".bin") == low.size() - 4) {
            return filetype::bin;
        } else if (low.rfind(".pem") == low.size() - 4) {
            return filetype::pem;
        } else if (low.rfind(".json") == low.size() - 5) {
            return filetype::json;
        }
    } else if (!file_type.empty()) {
        low = lowercase(file_type);
        if (low == "uf2") {
            return filetype::uf2;
        }
        if (low == "bin") {
            return filetype::bin;
        }
        if (low == "elf") {
            return filetype::elf;
        }
        if (low == "pem") {
            return filetype::pem;
        }
        if (low == "json") {
            return filetype::json;
        }
        throw cli::parse_error("unsupported file type '" + low + "'");
    }
    throw cli::parse_error("filename '" + filename+ "' does not have a recognized file type (extension)");
}

enum filetype get_file_type() {
    return get_file_type_idx(0);
}

void build_rmap_elf(std::shared_ptr<std::iostream>file, range_map<size_t>& rmap) {
    elf32_header eh;
    read_and_check_elf32_header(file, eh);
    if (eh.ph_entry_size != sizeof(elf32_ph_entry)) {
        fail(ERROR_FORMAT, "Invalid ELF32 program header");
    }
    if (eh.ph_num) {
        vector<elf32_ph_entry> entries(eh.ph_num);
        file->seekg(eh.ph_offset, ios::beg);
        if (file->fail()) {
            return fail_read_error();
        }
        file->read((char*)&entries[0], sizeof(struct elf32_ph_entry) * eh.ph_num);
        if (file->fail()) {
            fail_read_error();
        }
        for (unsigned int i = 0; i < eh.ph_num; i++) {
             elf32_ph_entry &entry = entries[i];
             if (entry.type == PT_LOAD && entry.memsz) {
                unsigned int mapped_size = std::min(entry.filez, entry.memsz);
                if (mapped_size) {
                    auto r = range(entry.paddr, entry.paddr + mapped_size);
                    rmap.insert(r, entry.offset);
                    if (entry.memsz > entry.filez) {
                        // ignore uninitialized for now
                    }
                }
            }
        }
    }
}

uint32_t build_rmap_uf2(std::shared_ptr<std::iostream>file, range_map<size_t>& rmap, uint32_t family_id=0) {
    file->seekg(0, ios::beg);
    uf2_block block;
    unsigned int pos = 0;
    uint32_t next_family_id = 0;
    do {
        file->read((char*)&block, sizeof(uf2_block));
        if (file->fail()) {
            if (file->eof()) { file->clear(); break; }
            fail(ERROR_READ_FAILED, "unexpected end of input file");
        }
        if (block.magic_start0 == UF2_MAGIC_START0 && block.magic_start1 == UF2_MAGIC_START1 &&
            block.magic_end == UF2_MAGIC_END) {
            if (block.flags & UF2_FLAG_FAMILY_ID_PRESENT &&
                !(block.flags & UF2_FLAG_NOT_MAIN_FLASH) && block.payload_size == PAGE_SIZE &&
                (!family_id || block.file_size == family_id)) {
                #if SUPPORT_A2
                // ignore the absolute block, but save the address
                if (check_abs_block(block)) {
                    DEBUG_LOG("Ignoring RP2350-E10 absolute block\n");
                    settings.uf2.abs_block_loc = block.target_addr;
                } else {
                    rmap.insert(range(block.target_addr, block.target_addr + PAGE_SIZE), pos + offsetof(uf2_block, data[0]));
                    family_id = block.file_size;
                    next_family_id = 0;
                }
                #else
                rmap.insert(range(block.target_addr, block.target_addr + PAGE_SIZE), pos + offsetof(uf2_block, data[0]));
                family_id = block.file_size;
                next_family_id = 0;
                #endif
            } else if (block.file_size != family_id && family_id && !next_family_id) {
                #if SUPPORT_A2
                if (!check_abs_block(block)) {
                #endif
                    next_family_id = block.file_size;
                #if SUPPORT_A2
                }
                #endif
            }
        }
        pos += sizeof(uf2_block);
    } while (true);

    return next_family_id;
}

void build_rmap_load_map(std::shared_ptr<load_map_item>load_map, range_map<uint32_t>& rmap) {
    for (unsigned int i=0; i < load_map->entries.size(); i++) {
        auto e = load_map->entries[i];
        if (e.storage_address != 0) {
            rmap.insert(range(e.runtime_address, e.runtime_address + e.size), e.storage_address);
        }
    }
}

uint32_t find_binary_start(range_map<size_t>& rmap) {
    range flash(FLASH_START, FLASH_END_RP2350); // pick biggest (rp2350) here for now
    range sram(SRAM_START, SRAM_END_RP2350); // pick biggest (rp2350) here for now
    range xip_sram(XIP_SRAM_START_RP2350, XIP_SRAM_END_RP2040); // pick biggest (rp2350-rp2040) here for now
    uint32_t binary_start = std::numeric_limits<uint32_t>::max();
    for (const auto &r : rmap.ranges()) {
        if (r.contains(FLASH_START)) {
            return FLASH_START;
        }
        if (sram.contains(r.from) || xip_sram.contains((r.from))) {
            if (r.from < binary_start || (xip_sram.contains(binary_start) && sram.contains(r.from))) {
                binary_start = r.from;
            }
        }
    }
    if (get_memory_type(binary_start, rp2350) == invalid) { // pick biggest (rp2350) here for now
        return 0;
    }
    return binary_start;
}

template <typename ACCESS, typename STREAM> ACCESS get_iostream_memory_access(std::shared_ptr<STREAM> file, filetype type, bool writeable = false, uint32_t *next_family_id=nullptr) {
    range_map<size_t> rmap;
    uint32_t binary_start = 0;
    uint32_t tmp = 0;
    if (next_family_id != nullptr) tmp = *next_family_id;
    switch (type) {
        case filetype::bin:
            file->seekg(0, std::ios::end);
            binary_start = settings.offset_set ? settings.offset : FLASH_START;
            rmap.insert(range(binary_start, binary_start + file->tellg()), 0);
            return ACCESS(file, rmap, binary_start);
        case filetype::elf:
            build_rmap_elf(file, rmap);
            binary_start = find_binary_start(rmap);
            break;
        case filetype::uf2:
            tmp = build_rmap_uf2(file, rmap, tmp);
            if (next_family_id != nullptr) {
                *next_family_id = tmp;
            } else if (tmp) {
                fos << "WARNING: Multiple family IDs in a single UF2 file - only using first one\n";
            }
            binary_start = find_binary_start(rmap);
            break;
        default:
            fail(ERROR_INCOMPATIBLE, "Cannot create memory access with filetype %s", getFiletypeName(type).c_str());
    }
    if (settings.offset_set) {
        unsigned int rel_offset = settings.offset - binary_start;
        rmap = rmap.offset_by(rel_offset);
        binary_start = settings.offset;
        DEBUG_LOG("BINARY START now %08x, rmaps offset by %08x\n", binary_start, rel_offset);
    }
    return ACCESS(file, rmap, binary_start);
}

file_memory_access get_file_memory_access(uint8_t idx, bool writeable = false, uint32_t *next_family_id=nullptr) {
    ios::openmode mode = (writeable ? ios::out|ios::in : ios::in)|ios::binary;
    auto file = get_file_idx(mode, idx);
    try {
        return get_iostream_memory_access<file_memory_access>(file, get_file_type_idx(idx), writeable, next_family_id);
    } catch (std::exception&) {
        file->close();
        throw;
    }
}

const char *cpu_name(unsigned int cpu) {
    if (cpu == PICOBIN_IMAGE_TYPE_EXE_CPU_ARM) return "ARM";
    if (cpu == PICOBIN_IMAGE_TYPE_EXE_CPU_RISCV) return "RISC-V";
    return "unknown";
}

std::string boot_type_string(uint8_t type) {
    std::string s;
    switch (type & 0x7f) {
        case BOOT_TYPE_BOOTSEL: s = "bootsel"; break;
        case 1: // old PC_SP; fall thru
        case BOOT_TYPE_PC_SP: s = "pc/sp"; break;
        case BOOT_TYPE_FLASH_UPDATE: s = "flash update"; break;
        case BOOT_TYPE_RAM_IMAGE: s = "ram image"; break;
        case BOOT_TYPE_NORMAL: s = "normal"; break;
        default: s = "<unknown>"; break;
    }
    if (type & BOOT_TYPE_CHAINED_FLAG) s += " into chained image";
    return s;
}

std::string boot_partition_string(int8_t type) {
    if (type == BOOT_PARTITION_NONE) return "none";
    if (type == BOOT_PARTITION_SLOT0) return "slot 0";
    if (type == BOOT_PARTITION_SLOT1) return "slot 1";
    if (type == BOOT_PARTITION_WINDOW) return "window";
    if (type >= 0 && type < PARTITION_TABLE_MAX_PARTITIONS) return "partition "+std::to_string(type);
    return "<invalid>";
}

#define OTP_CRITICAL_RISCV_DISABLE_BITS   _u(0x00020000)
#define OTP_CRITICAL_ARM_DISABLE_BITS   _u(0x00010000)
#define OTP_CRITICAL_GLITCH_DETECTOR_SENS_BITS   _u(0x00000060)
#define OTP_CRITICAL_GLITCH_DETECTOR_ENABLE_BITS   _u(0x00000010)
#define OTP_CRITICAL_DEFAULT_ARCHSEL_BITS   _u(0x00000008)
#define OTP_CRITICAL_DEBUG_DISABLE_BITS   _u(0x00000004)
#define OTP_CRITICAL_SECURE_DEBUG_DISABLE_BITS   _u(0x00000002)
#define OTP_CRITICAL_SECURE_BOOT_ENABLE_BITS   _u(0x00000001)

std::unique_ptr<block> find_best_block(memory_access &raw_access, vector<uint8_t> &bin, bool riscv = false) {
    // todo read the right amount
    uint32_t read_size = 0x1000;
    DEBUG_LOG("Reading from %x size %x\n", raw_access.get_binary_start(), read_size);
    bin = raw_access.read_vector<uint8_t>(raw_access.get_binary_start(), read_size, true);

    std::unique_ptr<block> best_block = find_first_block(bin, raw_access.get_binary_start());
    if (best_block) {
        // verify stuff
        get_more_bin_cb more_cb = [&raw_access](std::vector<uint8_t> &bin, uint32_t new_size) {
            DEBUG_LOG("Now reading from %x size %x\n", raw_access.get_binary_start(), new_size);
            bin = raw_access.read_vector<uint8_t>(raw_access.get_binary_start(), new_size, true);
        };
        auto all_blocks = get_all_blocks(bin, raw_access.get_binary_start(), best_block, more_cb);

        bool has_arch = false;
        // for (int i=0; i < all_blocks.size(); i++) {
        //     auto &block = all_blocks[i];
        for (auto &block : all_blocks) {
            DEBUG_LOG("Checking block at %x, num items %d\n", block->physical_addr, block->items.size());
            // Image Def
            auto image_def = block->get_item<image_type_item>();
            if (image_def != nullptr) {
                if (image_def->image_type() == type_exe) {
                    switch (image_def->chip()) {
                        case chip_rp2040:
                            break;
                        case chip_rp2350:
                            switch (image_def->cpu()) {
                                case cpu_riscv:
                                    if (riscv || !has_arch) {
                                        best_block.swap(block);
                                        has_arch = riscv;
                                    }
                                    break;
                                case cpu_varmulet:
                                    if (!has_arch) {
                                        best_block.swap(block);
                                    }
                                    break;
                                case cpu_arm:
                                    if (image_def->security() == sec_s) {
                                        if (!riscv || !has_arch) {
                                            best_block.swap(block);
                                            has_arch = !riscv;
                                        }
                                    }
                            }
                            break;
                        default:
                            break;
                    }
                }
            }

            // Image Def
            image_def = best_block->get_item<image_type_item>();
            DEBUG_LOG("Current best is at %x, num items %d\n", best_block->physical_addr, best_block->items.size());
        }
    }

    return best_block;
}

std::unique_ptr<block> find_last_block(memory_access &raw_access, vector<uint8_t> &bin) {
    // todo read the right amount
    uint32_t read_size = 0x1000;
    DEBUG_LOG("Reading from %x size %x\n", raw_access.get_binary_start(), read_size);
    bin = raw_access.read_vector<uint8_t>(raw_access.get_binary_start(), read_size, true);

    std::unique_ptr<block> first_block = find_first_block(bin, raw_access.get_binary_start());
    if (first_block) {
        // verify stuff
        get_more_bin_cb more_cb = [&raw_access](std::vector<uint8_t> &bin, uint32_t new_size) {
            DEBUG_LOG("Now reading from %x size %x\n", raw_access.get_binary_start(), new_size);
            bin = raw_access.read_vector<uint8_t>(raw_access.get_binary_start(), new_size, true);
        };
        auto last_block = get_last_block(bin, raw_access.get_binary_start(), first_block, more_cb);
        return last_block;
    }

    return nullptr;
}

std::shared_ptr<memory_access> get_bi_access(memory_access &raw_access) {
    vector<uint8_t> bin;
    std::unique_ptr<block> best_block = find_best_block(raw_access, bin);
    range_map<uint32_t> rmap;
    if (best_block) {
        auto load_map = best_block->get_item<load_map_item>();
        if (load_map != nullptr) {
            // Remap for find_binary_info
            build_rmap_load_map(load_map, rmap);
        }
    }

    return std::make_shared<remapped_memory_access>(raw_access, rmap);
}

string str_permissions(unsigned int p) {
    static_assert(PICOBIN_PARTITION_PERMISSION_S_R_BITS == (1u << 26), "");
    static_assert(PICOBIN_PARTITION_PERMISSION_S_W_BITS == (1u << 27), "");
    static_assert(PICOBIN_PARTITION_PERMISSION_NS_W_BITS == (1u << 29), "");
    static_assert(PICOBIN_PARTITION_PERMISSION_NSBOOT_W_BITS == (1u << 31), "");

    std::stringstream ss;
    ss << " S(";
    unsigned int r = (p >> 26) & 3;
    if (r & 1) ss << "r";
    if (r & 2) ss << "w"; else if (!r) ss << "-";
    ss << ") NSBOOT(";
    r = (p >> 30) & 3;
    if (r & 1) ss << "r";
    if (r & 2) ss << "w"; else if (!r) ss << "-";
    ss << ") NS(";
    r = (p >> 28) & 3;
    if (r & 1) ss << "r";
    if (r & 2) ss << "w"; else if (!r) ss << "-";
    ss << ")";

    return ss.str();
}

void insert_default_families(uint32_t flags_and_permissions, vector<std::string> &family_ids) {
    if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_ABSOLUTE_BITS) family_ids.emplace_back(absolute_family_name);
    if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_RP2040_BITS) family_ids.emplace_back(rp2040_family_name);
    if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_RP2350_ARM_S_BITS) family_ids.emplace_back(rp2350_arm_s_family_name);
    if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_RP2350_ARM_NS_BITS) family_ids.emplace_back(rp2350_arm_ns_family_name);
    if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_RP2350_RISCV_BITS) family_ids.emplace_back(rp2350_riscv_family_name);
    if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_DATA_BITS) family_ids.emplace_back(data_family_name);
}

#if HAS_LIBUSB
void info_guts(memory_access &raw_access, picoboot::connection *con) {
#else
void info_guts(memory_access &raw_access, void *con) {
#endif
    try {
        struct group {
            explicit group(string name, bool enabled = true, int min_tab = 0) : name(std::move(name)), enabled(enabled), min_tab(min_tab) {}
            string name;
            bool enabled;
            int min_tab;
        };
        vector<group> groups;
        string current_group;
        map<string, vector<pair<string,string>>> infos;
        // Set enable to true to enable the selected group
        auto select_group = [&](const group &g1, bool enable = false) {
            if (std::find_if(groups.begin(), groups.end(), [&](const group &g2) {
                return g1.name == g2.name;
            }) == groups.end()) {
                groups.push_back(g1);
            }
            auto enable_changed = std::find_if(groups.begin(), groups.end(), [&](const group &g2) {
                return g1.name == g2.name && (enable && !g2.enabled);
            });
            if (enable_changed != groups.end()) {
                enable_changed->enabled = true;
            }
            current_group = g1.name;
        };
        auto info_pair = [&](const string &name, const string &value) {
            if (!value.empty()) {
                assert(!current_group.empty());
                infos[current_group].emplace_back(std::make_pair(name, value));
            }
        };
        auto info_metadata = [&](std::vector<uint8_t> bin, block *current_block, bool verbose_metadata = false) {
            verified_t hash_verified = none;
            verified_t sig_verified = none;
        #if HAS_MBEDTLS
            verify_block(bin, raw_access.get_binary_start(), raw_access.get_binary_start(), current_block, hash_verified, sig_verified);
        #endif

            // Addresses
            if (verbose_metadata) {
                info_pair("address", hex_string(current_block->physical_addr));
                info_pair("next block address", hex_string(current_block->next_block_rel + current_block->physical_addr));
                if (current_block->get_item<ignored_item>() != nullptr) info_pair("block type", "ignored");
            }

            // Image Def
            auto image_def = current_block->get_item<image_type_item>();
            if (image_def != nullptr) {
                if (verbose_metadata) info_pair("block type", "image def");
                if (image_def->image_type() == type_exe) {
                    switch (image_def->chip()) {
                        case chip_rp2040:
                            info_pair("target chip", "RP2040");
                            break;
                        case chip_rp2350:
                            info_pair("target chip", "RP2350");
                            switch (image_def->cpu()) {
                                case cpu_riscv:
                                    info_pair("image type", "RISC-V");
                                    break;
                                case cpu_varmulet:
                                    info_pair("image type", "Varmulet");
                                    break;
                                case cpu_arm:
                                    if (image_def->security() == sec_s) {
                                        info_pair("image type", "ARM Secure");
                                    } else if (image_def->security() == sec_ns) {
                                        info_pair("image type", "ARM Non-Secure");
                                    } else if (image_def->security() == sec_unspecified) {
                                        info_pair("image type", "ARM");
                                    }
                            }
                            break;
                        default:
                            break;
                    }
                } else if (image_def->image_type() == type_data) {
                    info_pair("image type", "data");
                }
            }

            // Partition Table
            auto partition_table = current_block->get_item<partition_table_item>();
            if (partition_table != nullptr) {
                if (verbose_metadata) info_pair("block type", "partition table");
                info_pair("partition table", partition_table->singleton ? "singleton" : "non-singleton");
                std::stringstream unpartitioned;
                unpartitioned << str_permissions(partition_table->unpartitioned_flags);
                std::vector<std::string> family_ids;
                insert_default_families(partition_table->unpartitioned_flags, family_ids);
                unpartitioned << ", uf2 { " << cli::join(family_ids, ", ") << " }";
                info_pair("un-partitioned space", unpartitioned.str());

                for (int i=0; i < partition_table->partitions.size(); i++) {
                    std::stringstream pstring;
                    std::stringstream pname;
                    auto partition = partition_table->partitions[i];
                    uint32_t flags = partition.flags;
                    uint64_t id = partition.id;
                    pname << "partition " << i;
                    if ((flags & PICOBIN_PARTITION_FLAGS_LINK_TYPE_BITS) ==
                        PICOBIN_PARTITION_FLAGS_LINK_TYPE_AS_BITS(A_PARTITION)) {
                        pname << " (B w/ " << ((flags & PICOBIN_PARTITION_FLAGS_LINK_VALUE_BITS)
                                            >> PICOBIN_PARTITION_FLAGS_LINK_VALUE_LSB)
                           << ")";
                    } else if ((flags & PICOBIN_PARTITION_FLAGS_LINK_TYPE_BITS) ==
                            PICOBIN_PARTITION_FLAGS_LINK_TYPE_AS_BITS(OWNER_PARTITION)) {
                            pname << " (A ob/ " << ((flags & PICOBIN_PARTITION_FLAGS_LINK_VALUE_BITS)
                                                 >> PICOBIN_PARTITION_FLAGS_LINK_VALUE_LSB)
                               << ")";
                    } else {
                        pname << " (A)";
                    }
                    pstring << hex_string(partition.first_sector * 4096, 8, false) << "->" << hex_string((partition.last_sector + 1) * 4096, 8, false);
                    unsigned int p = partition.permissions;
                    pstring << str_permissions(p << PICOBIN_PARTITION_PERMISSIONS_LSB);
                    if (flags & PICOBIN_PARTITION_FLAGS_HAS_ID_BITS) {
                        pstring << ", id=" << hex_string(id, 16, false);
                    }
                    uint32_t num_extra_families = partition.extra_families.size();
                    family_ids.clear();
                    insert_default_families(flags, family_ids);
                    for (auto family : partition.extra_families) {
                        family_ids.emplace_back(hex_string(family));
                    }
                    if (flags & PICOBIN_PARTITION_FLAGS_HAS_NAME_BITS) {
                        pstring << ", \"";
                        pstring << partition.name;
                        pstring << '"';
                    }
                    pstring << ", uf2 { " << cli::join(family_ids, ", ") << " }";
                    pstring << ", arm_boot " << !(flags & PICOBIN_PARTITION_FLAGS_IGNORED_DURING_ARM_BOOT_BITS);
                    pstring << ", riscv_boot " << !(flags & PICOBIN_PARTITION_FLAGS_IGNORED_DURING_RISCV_BOOT_BITS);
                    info_pair(pname.str(), pstring.str());
                }
            }

            // Version
            auto version = current_block->get_item<version_item>();
            if (version != nullptr) {
                info_pair("version", std::to_string(version->major) + "." + std::to_string(version->minor));
                if (version->otp_rows.size() > 0) {
                    info_pair("rollback version", std::to_string(version->rollback));
                    std::stringstream rows;
                    for (const auto row : version->otp_rows) { rows << hex_string(row, 3) << " "; }
                    info_pair("rollback rows", rows.str());
                }
            }

            if (verbose_metadata) {
                // Load Map
                // todo what should this really report
                auto load_map = current_block->get_item<load_map_item>();
                if (load_map != nullptr) {
                    for (unsigned int i=0; i < load_map->entries.size(); i++) {
                        std::stringstream ss;
                        auto e = load_map->entries[i];
                        if (e.storage_address == 0) {
                            ss << "Clear 0x" << std::hex << e.runtime_address;
                            ss << "->0x" << std::hex << e.runtime_address + e.size;
                        } else if (e.storage_address != e.runtime_address) {
                            if (is_address_initialized(rp2350_address_ranges_flash, e.runtime_address)) {
                                ss << "ERROR: COPY TO FLASH NOT PERMITTED ";
                            }
                            ss << "Copy 0x" << std::hex << e.storage_address;
                            ss << "->0x" << std::hex << e.storage_address + e.size;
                            ss << " to 0x" << std::hex << e.runtime_address;
                            ss << "->0x" << std::hex << e.runtime_address + e.size;
                        } else {
                            ss << "Load 0x" << std::hex << e.storage_address;
                            ss << "->0x" << std::hex << e.storage_address + e.size;
                        }
                        info_pair("load map entry " + std::to_string(i), ss.str());
                    }
                }

                // Rolling Window Delta
                auto rwd = current_block->get_item<rolling_window_delta_item>();
                if (rwd != nullptr) {
                    info_pair("rolling window delta", hex_string(rwd->addr));
                }

                // Vector Table
                auto vtor = current_block->get_item<vector_table_item>();
                if (vtor != nullptr) {
                    info_pair("vector table", hex_string(vtor->addr));
                }

                // Entry Point
                auto entry_point = current_block->get_item<entry_point_item>();
                if (entry_point != nullptr) {
                    std::stringstream ss;
                    ss << "EP " << hex_string(entry_point->ep);
                    ss << ", SP " << hex_string(entry_point->sp);
                    if (entry_point->splim_set) ss << ", SPLIM " << hex_string(entry_point->splim);
                    info_pair("entry point", ss.str());
                }
            }

            // Hash and Sig
            if (hash_verified != none) {
                info_pair("hash", hash_verified == passed ? "verified" : "incorrect");
                if (verbose_metadata) {
                    std::shared_ptr<hash_value_item> hash_value = current_block->get_item<hash_value_item>();
                    assert(hash_value != nullptr); // verify_block would return none if it's not present
                    std::stringstream val;
                    for(uint8_t i : hash_value->hash_bytes) {
                        val << hex_string(i, 2, false, true);
                    }
                    info_pair("hash value", val.str());
                }
            }
            if (sig_verified != none) {
                info_pair("signature", sig_verified == passed ? "verified" : "incorrect");
                if (verbose_metadata) {
                    std::shared_ptr<signature_item> signature = current_block->get_item<signature_item>();
                    assert(signature != nullptr); // verify_block would return none if it's not present
                    std::stringstream sig;
                    for(uint8_t i : signature->signature_bytes) {
                        sig << hex_string(i, 2, false, true);
                    }
                    info_pair("signature value", sig.str());
                    std::stringstream pkey;
                    for(uint8_t i : signature->public_key_bytes) {
                        pkey << hex_string(i, 2, false, true);
                    }
                    info_pair("public key", pkey.str());
                }
            }
        };

        // establish core groups and their order
        if (!settings.info.show_basic && !settings.info.all && !settings.info.show_metadata && !settings.info.show_pins && !settings.info.show_device && !settings.info.show_debug && !settings.info.show_build) {
            settings.info.show_basic = true;
        }
        if (settings.info.show_debug && !settings.info.show_device) {
            settings.info.show_device = true;
        }
        auto program_info = group("Program Information", settings.info.show_basic || settings.info.all);
        auto no_metadata_info = group("Metadata Blocks", false);
        vector<group> metadata_info;
        #define MAX_METADATA_BLOCKS 10
        for (int i=1; i <= MAX_METADATA_BLOCKS; i++) {
            // These groups are enabled later, depending on how many metadata blocks the binary has
            metadata_info.push_back(group("Metadata Block " + std::to_string(i), false));
        }
        auto pin_info = group("Fixed Pin Information", settings.info.show_pins || settings.info.all);
        auto build_info = group("Build Information", settings.info.show_build || settings.info.all);
        auto device_info = group("Device Information", (settings.info.show_device || settings.info.all) & raw_access.is_device());
        // select them up front to impose order
        select_group(program_info);
        select_group(pin_info);
        select_group(build_info);
        for (auto mb : metadata_info) {
            select_group(mb);
        }
        select_group(device_info);
        binary_info_header hdr;
        try {
            auto bi_access = get_bi_access(raw_access);
            bool has_binary_info = find_binary_info(*bi_access, hdr);
            if (has_binary_info) {
                auto access = remapped_memory_access(*bi_access, hdr.reverse_copy_mapping);
                auto visitor = bi_visitor{};
                map<string, string> output;
                map<unsigned int, vector<string>> pins;

                map<pair<int, uint32_t>, pair<string, unsigned int>> named_feature_groups;
                map<string, vector<string>> named_feature_group_values;

                string program_name, program_build_date, program_version, program_url, program_description;
                string pico_board, sdk_version, boot2_name;
                vector<string> program_features, build_attributes;

                uint32_t binary_end = 0;

                // do a pass first to find named groups
                visitor.named_group(
                        [&](int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id, const string &label,
                            unsigned int flags) {
                            if (parent_tag != BINARY_INFO_TAG_RASPBERRY_PI)
                                return;
                            if (parent_id != BINARY_INFO_ID_RP_PROGRAM_FEATURE)
                                return;
                            named_feature_groups[std::make_pair(group_tag, group_id)] = std::make_pair(label, flags);
                        });

                visitor.visit(access, hdr);

                visitor = bi_visitor{};
                visitor.id_and_int([&](int tag, uint32_t id, uint32_t value) {
                    if (tag != BINARY_INFO_TAG_RASPBERRY_PI)
                        return;
                    if (id == BINARY_INFO_ID_RP_BINARY_END) binary_end = value;
                });
                visitor.id_and_string([&](int tag, uint32_t id, const string &value) {
                    const auto &nfg = named_feature_groups.find(std::make_pair(tag, id));
                    if (nfg != named_feature_groups.end()) {
                        named_feature_group_values[nfg->second.first].push_back(value);
                        return;
                    }
                    if (tag != BINARY_INFO_TAG_RASPBERRY_PI)
                        return;
                    if (id == BINARY_INFO_ID_RP_PROGRAM_NAME) program_name = value;
                    else if (id == BINARY_INFO_ID_RP_PROGRAM_VERSION_STRING) program_version = value;
                    else if (id == BINARY_INFO_ID_RP_PROGRAM_BUILD_DATE_STRING) program_build_date = value;
                    else if (id == BINARY_INFO_ID_RP_PROGRAM_URL) program_url = value;
                    else if (id == BINARY_INFO_ID_RP_PROGRAM_DESCRIPTION) program_description = value;
                    else if (id == BINARY_INFO_ID_RP_PROGRAM_FEATURE) program_features.push_back(value);
                    else if (id == BINARY_INFO_ID_RP_PROGRAM_BUILD_ATTRIBUTE) build_attributes.push_back(value);
                    else if (id == BINARY_INFO_ID_RP_PICO_BOARD) pico_board = value;
                    else if (id == BINARY_INFO_ID_RP_SDK_VERSION) sdk_version = value;
                    else if (id == BINARY_INFO_ID_RP_BOOT2_NAME) boot2_name = value;
                });
                visitor.ptr_int32_t_with_name([&](int tag, uint32_t id, const string &label, int32_t value) {
                    const auto &nfg = named_feature_groups.find(std::make_pair(tag, id));
                    if (nfg != named_feature_groups.end()) {
                        std::stringstream ss;
                        ss << label << " = " << value;
                        named_feature_group_values[nfg->second.first].push_back(ss.str());
                        return;
                    }
                });
                visitor.ptr_string_t_with_name([&](int tag, uint32_t id, const string &label, const string &value) {
                    const auto &nfg = named_feature_groups.find(std::make_pair(tag, id));
                    if (nfg != named_feature_groups.end()) {
                        std::stringstream ss;
                        ss << label << " = \"" << value << "\"";
                        named_feature_group_values[nfg->second.first].push_back(ss.str());
                        return;
                    }
                });
                visitor.pin([&](unsigned int pin, const string &name) {
                    pins[pin].push_back(name);
                });

                // some simple info in --all for now.. split out into separate section later
                vector<std::function<void()>> deferred;
                visitor.block_device([&](memory_access &access, binary_info_block_device_t &bi_bdev) {
                    if (settings.info.all) {
                        std::stringstream ss;
                        ss << hex_string(bi_bdev.address) << "-" << hex_string(bi_bdev.address + bi_bdev.size) <<
                           " (" << ((bi_bdev.size + 1023) / 1024) << "K): " << read_string(access, bi_bdev.name);
                        string s = ss.str();
                        deferred.emplace_back([&select_group, &program_info, &info_pair, s]() {
                            select_group(program_info);
                            info_pair("embedded drive", s);
                        });
                    }
                });

                visitor.visit(access, hdr);

                if (settings.info.show_basic || settings.info.all) {
                    select_group(program_info);
                    info_pair("name", program_name);
                    info_pair("version", program_version);
                    info_pair("web site", program_url);
                    info_pair("description", program_description);
                    info_pair("features", cli::join(program_features, "\n"));
                    for (const auto &e: named_feature_groups) {
                        const auto &name = e.second.first;
                        const auto &flags = e.second.second;
                        const auto &values = named_feature_group_values[name];
                        if (!values.empty() || (flags & BI_NAMED_GROUP_SHOW_IF_EMPTY)) {
                            info_pair(name, cli::join(values, (flags & BI_NAMED_GROUP_SEPARATE_COMMAS) ? ", " : "\n"));
                        }
                    }
                    if (access.get_binary_start())
                        info_pair("binary start", hex_string(access.get_binary_start()));
                    if (binary_end)
                        info_pair("binary end", hex_string(binary_end));

                    for (auto &f: deferred) {
                        f();
                    }
                }
                if (settings.info.show_pins || settings.info.all) {
                    select_group(pin_info);
                    int first_pin = -1;
                    string last_label;
                    for (auto i = pins.begin(); i != pins.end(); i++) {
                        auto j = i;
                        j++;
                        if (j == pins.end() || j->first != i->first + 1 || j->second != i->second) {
                            std::sort(i->second.begin(), i->second.end());
                            auto label = cli::join(i->second, ", ");
                            if (first_pin < 0) {
                                info_pair(std::to_string(i->first), label);
                            } else {
                                info_pair(std::to_string(first_pin) + "-" + std::to_string(i->first), label);
                                first_pin = -1;
                            }
                        } else if (first_pin < 0) {
                            first_pin = (int) i->first;
                        }
                    }
                }
                if (settings.info.show_build || settings.info.all) {
                    select_group(build_info);
                    info_pair("sdk version", sdk_version);
                    info_pair("pico_board", pico_board);
                    info_pair("boot2_name", boot2_name);
                    info_pair("build date", program_build_date);
                    info_pair("build attributes", cli::join(build_attributes, "\n"));
                }
            }
            vector<uint8_t> bin;
            if (settings.info.show_metadata || settings.info.all) {
                uint32_t read_size = 0x1000;
                DEBUG_LOG("Reading from %x size %x\n", raw_access.get_binary_start(), read_size);
                bin = raw_access.read_vector<uint8_t>(raw_access.get_binary_start(), read_size, true);
                std::unique_ptr<block> first_block = find_first_block(bin, raw_access.get_binary_start());
                if (first_block) {
                    // verify stuff
                    get_more_bin_cb more_cb = [&raw_access](std::vector<uint8_t> &bin, uint32_t new_size) {
                        DEBUG_LOG("Now reading from %x size %x\n", raw_access.get_binary_start(), new_size);
                        bin = raw_access.read_vector<uint8_t>(raw_access.get_binary_start(), new_size, true);
                    };
                    auto all_blocks = get_all_blocks(bin, raw_access.get_binary_start(), first_block, more_cb);

                    int block_i = 0;
                    select_group(metadata_info[block_i++], true);
                    info_metadata(bin, first_block.get(), true);
                    for (auto &block : all_blocks) {
                        select_group(metadata_info[block_i++], true);
                        info_metadata(bin, block.get(), true);
                    }
                } else {
                    // This displays that there are no metadata blocks
                    select_group(no_metadata_info, true);
                }
            }
            std::unique_ptr<block> best_block = find_best_block(raw_access, bin);
            if (best_block && (settings.info.show_basic || settings.info.all)) {
                select_group(program_info);
                info_metadata(bin, best_block.get());
            } else if (!best_block && has_binary_info && get_model(raw_access) == rp2350) {
                fos << "WARNING: Binary on RP2350 device does not contain a block loop - this binary will not boot\n";
            }
        } catch (std::invalid_argument &e) {
            fos << "Error reading binary info\n";
    #if HAS_LIBUSB
        } catch (picoboot::command_failure &e) {
            if (e.get_code() != PICOBOOT_NOT_PERMITTED) throw;
            info_pair("flash size", "not determined due to access permissions");
    #endif
        }
    #if HAS_LIBUSB
        std::vector<std::pair<string,string>> device_state_pairs;
        if ((settings.info.show_device || settings.info.all) && raw_access.is_device()) {
            select_group(device_info);
            model_t model = get_model(raw_access);
            uint8_t rom_version;
            raw_access.read_raw(0x13, rom_version);
            if (model == rp2040) {
                info_pair("type", "RP2040");
                if (settings.info.show_debug || settings.info.all) {
                    switch (rom_version) {
                        case 1:
                            info_pair("revision", "B0");
                            break;
                        case 2:
                            info_pair("revision", "B1");
                            break;
                        case 3:
                            info_pair("revision", "B2");
                            break;
                        default:
                            info_pair("revision", "Unknown");
                            break;
                    }
                }
            } else if (model == rp2350) {
                info_pair("type", "RP2350");
                assert(con);
                struct picoboot_get_info_cmd info_cmd;
                info_cmd.bType = PICOBOOT_GET_INFO_SYS,
                info_cmd.dParams[0] = (uint32_t) (settings.info.show_debug || settings.info.all ?
                                                          SYS_INFO_CHIP_INFO | SYS_INFO_CRITICAL |
                                                          SYS_INFO_BOOT_RANDOM |
                                                          SYS_INFO_CPU_INFO | SYS_INFO_FLASH_DEV_INFO | SYS_INFO_BOOT_INFO :
                                                          SYS_INFO_CHIP_INFO | SYS_INFO_CRITICAL |
                                                          SYS_INFO_CPU_INFO | SYS_INFO_FLASH_DEV_INFO);
                uint32_t word_buf[64];
                auto version = get_rp2350_version(raw_access);
                if (settings.info.show_debug || settings.info.all) {
                    switch (version) {
                        case rp2350_a2:
                            info_pair("revision", "A2");
                            break;
                        default:
                            info_pair("revision", "Unknown");
                            break;
                    }
                }
                con->get_info(&info_cmd, (uint8_t *) word_buf, sizeof(word_buf));
                uint32_t *data = word_buf;
                unsigned int word_count = *data++;
                unsigned int included = *data++;
                if (included & SYS_INFO_CHIP_INFO) {
                    // package_id, device_id, wafer_id
                    struct picoboot_otp_cmd otp_cmd;
                    uint16_t num_gpios = 0;
                    otp_cmd.wRow = OTP_DATA_NUM_GPIOS_ROW;
                    otp_cmd.wRowCount = 1;
                    otp_cmd.bEcc = 1;
                    con->otp_read(&otp_cmd, (uint8_t *)&num_gpios, sizeof(num_gpios));
                    if (num_gpios == 30) {
                        info_pair("package", "QFN60");
                    } else if (num_gpios == 48) {
                        info_pair("package", "QFN80");
                    } else {
                        info_pair("package", "unknown");
                    }
                    // Not correct on A2
                    // info_pair("package", data[0] ? "QFN60" : "QFN80");
                    info_pair("chipid", hex_string(data[1] | (uint64_t)data[2] << 32, 16));
                    data += 3;
                }
                unsigned int critical = 0;
                if (included & SYS_INFO_CRITICAL) {
                    critical = *data++;
                }
                unsigned int cpu_info = 0;
                if (included & SYS_INFO_CPU_INFO) {
                    cpu_info = *data++;
                }
                if (included & SYS_INFO_FLASH_DEV_INFO) {
                    unsigned int flash_dev_info = *data++;
                    info_pair("flash devinfo", hex_string(flash_dev_info, 4));
                }
                if (included & SYS_INFO_CPU_INFO) {
                    info_pair("current cpu", cpu_name((uint8_t) cpu_info));
                }
                if (included & SYS_INFO_CRITICAL) {
                    std::vector<string> supported_cpus;
                    if (!(critical & OTP_CRITICAL_ARM_DISABLE_BITS)) supported_cpus.emplace_back("ARM");
                    if (!(critical &
                            (OTP_CRITICAL_RISCV_DISABLE_BITS | OTP_CRITICAL_SECURE_BOOT_ENABLE_BITS)))
                        supported_cpus.emplace_back("RISC-V");
                    info_pair("available cpus", cli::join(supported_cpus, ", "));
                    if (supported_cpus.size() > 1) {
                        info_pair("default cpu",
                                    (critical & OTP_CRITICAL_DEFAULT_ARCHSEL_BITS) ? "RISC-V" : "ARM");
                    }
                    info_pair("secure boot",
                                std::to_string((bool) (critical & OTP_CRITICAL_SECURE_BOOT_ENABLE_BITS)));
                    info_pair("debug enable",
                                std::to_string((bool) !(critical & OTP_CRITICAL_DEBUG_DISABLE_BITS)));
                    info_pair("secure debug enable",
                                std::to_string((bool) !(critical & OTP_CRITICAL_SECURE_DEBUG_DISABLE_BITS)));
                }
                if (included & SYS_INFO_BOOT_RANDOM) {
                    char buf[40];
                    snprintf(buf, sizeof(buf), "%08x:%08x:%08x:%08x", data[0], data[1], data[2], data[3]);
                    data += 4;
                    info_pair("boot_random", buf);
                }
//                    if (included & SYS_INFO_NONCE) {
//                        info_pair("nonce", hex_string(*(int64_t *) data, 16));
//                        data += 2;
//                    }
                if (included & SYS_INFO_BOOT_INFO) {
                    uint32_t boot_word = *data++;
                    unsigned int boot_type = (uint8_t)(boot_word>>8);
                    info_pair("boot type", boot_type_string(boot_type));
                    int8_t boot_partition = (int8_t)(boot_word >> 16);
                    uint8_t tbyb_and_update = (uint8_t)(boot_word >> 24);
                    info_pair("last booted partition", boot_partition_string(boot_partition));
                    if (!(tbyb_and_update & 0x80)) {
                        if (tbyb_and_update & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING) info_pair("explicit buy pending", "true");
                        if (tbyb_and_update & BOOT_TBYB_AND_UPDATE_FLAG_OTHER_ERASED) info_pair("other slot/partition erased", "true");
                        if (tbyb_and_update & BOOT_TBYB_AND_UPDATE_FLAG_OTP_VERSION_APPLIED) info_pair("OTP version applied", "true");
                    }
                    uint32_t diagnostics = *data++;
                    if ((int8_t)boot_word != BOOT_PARTITION_NONE) {
                        info_pair("diagnostic source", boot_partition_string((int8_t)boot_word));
                        info_pair("last boot diagnostics", hex_string(diagnostics, 8));
                    }
                    uint32_t p0 = *data++;
                    uint32_t p1 = *data++;
                    // todo remove these; not particularly useful except for debugging
                    if (boot_type & ~BOOT_TYPE_CHAINED_FLAG) {
                        info_pair("reboot param 0", hex_string(p0));
                        info_pair("reboot param 1", hex_string(p1));
                    }
                }
                if (settings.info.show_debug || settings.info.all) {
                    info_pair("rom gitrev", hex_string(get_rom_git_revision(raw_access)));
                }
                select_group(device_info);
            }

            try {
                int32_t size_guess = guess_flash_size(raw_access);
                if (size_guess > 0) {
                    info_pair("flash size", std::to_string(size_guess/1024) + "K");
                    if (model == rp2040) {
                        uint64_t flash_id = 0;
                        con->flash_id(flash_id);
                        info_pair("flash id", hex_string(flash_id, 16, true, true));
                    }
                }
            } catch (picoboot::command_failure &e) {
                if (e.get_code() == PICOBOOT_NOT_PERMITTED) {
                    info_pair("flash size", "not determined due to access permissions");
                } else {
                    throw;
                }
            }

            // not sure how interesting this is given the chip revision which is correlated
            // info_pair("ROM version", std::to_string(rom_version));
        }
    #endif
        bool first = true;
        int fr_col = fos.first_column();
        // Standardise indent for whole info printout
        int tab = 0;
        for(const auto& group : groups) {
            if (group.enabled) {
                const auto& info = infos[group.name];
                if (!info.empty()) {
                    tab = std::max(tab, group.min_tab);
                    for(const auto& item : info) {
                        tab = std::max(tab, 3 + (int)item.first.length()); // +3 for ":  "
                    }
                }
            }
        }
        for(const auto& group : groups) {
            if (group.enabled) {
                const auto& info = infos[group.name];
                fos.first_column(fr_col);
                fos.hanging_indent(0);
                if (!first) {
                    fos.wrap_hard();
                } else {
                    first = false;
                }
                fos << group.name << "\n";
                fos.first_column(fr_col + 1);
                if (info.empty()) {
                   fos << "none\n";
                } else {
                    for(const auto& item : info) {
                        fos.first_column(fr_col + 1);
                        fos << (item.first + ":");
                        fos.first_column(fr_col + 1 + tab);
                        fos << (item.second + "\n");
                    }
                }
            }
        }
        fos.flush();
    } catch (not_mapped_exception&) {
        std::cout << "\nfailed to read memory\n";
    }
}

void config_guts(memory_access &raw_access) {
    binary_info_header hdr;
    int int_value;
    bool not_int = false;
    string string_value;

    if (!settings.config.value.empty()) {
        string_value = settings.config.value;
        if (!get_int(settings.config.value, int_value)) {
            not_int = true;
        }
    }

    auto bi_access = get_bi_access(raw_access);
    if (find_binary_info(*bi_access, hdr)) {
        auto access = remapped_memory_access(*bi_access, hdr.reverse_copy_mapping);
        auto visitor = bi_visitor{};

        map<pair<int, uint32_t>, pair<string, unsigned int>> named_feature_groups;
        map<string, vector<pair<string, int>>> named_feature_group_ints;
        map<string, vector<pair<string, string>>> named_feature_group_strings;

        // do a pass first to find named groups
        visitor.named_group(
            [&](int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id, const string &label,
                unsigned int flags) {
                if (parent_tag != BINARY_INFO_TAG_RASPBERRY_PI)
                    return;
                if (parent_id != BINARY_INFO_ID_RP_PROGRAM_FEATURE)
                    return;
                if (!settings.config.group.empty() && label != settings.config.group)
                    return;
                named_feature_groups[std::make_pair(group_tag, group_id)] = std::make_pair(label, flags);
            });

        visitor.visit(access, hdr);

        int fr_col = fos.first_column();
        if (settings.config.value.empty()) {
            visitor = bi_visitor{};
            visitor.ptr_int32_t_with_name([&](int tag, uint32_t id, const string &label, int32_t value) {
                const auto &nfg = named_feature_groups.find(std::make_pair(tag, id));
                if (nfg != named_feature_groups.end()) {
                    named_feature_group_ints[nfg->second.first].push_back({label, value});
                    return;
                } else if (settings.config.group.empty()) {
                    named_feature_group_ints[""].push_back({label, value});
                }
            });

            visitor.ptr_string_t_with_name([&](int tag, uint32_t id, const string &label, const string &value) {
                const auto &nfg = named_feature_groups.find(std::make_pair(tag, id));
                if (nfg != named_feature_groups.end()) {
                    named_feature_group_strings[nfg->second.first].push_back({label, value});
                    return;
                } else if (settings.config.group.empty()) {
                    named_feature_group_strings[""].push_back({label, value});
                }
            });

            visitor.visit(access, hdr);

            std::set<string> group_names;
            for (auto kv : named_feature_group_ints)
                group_names.insert(kv.first);
            for (auto kv : named_feature_group_strings)
                group_names.insert(kv.first);

            for (auto n : group_names) {
                auto ints = named_feature_group_ints[n];
                auto strings = named_feature_group_strings[n];
                fos.first_column(fr_col);
                if (!n.empty()) {
                    fos << n << ":\n";
                    fos.first_column(fr_col + 1);
                }
                for (auto val : ints) {
                    fos << val.first << " = " << val.second << "\n";
                }
                for (auto val : strings) {
                    fos << val.first << " = \"" << val.second << "\"\n";
                }
            }
        } else {
            auto modifier = bi_modifier{};

            if (!not_int) {
                modifier.ptr_int32_t_with_name([&](int tag, uint32_t id, const string &label, int32_t value, int32_t& new_value) -> bool {
                    const auto &nfg = named_feature_groups.find(std::make_pair(tag, id));
                    if (nfg == named_feature_groups.end() && !settings.config.group.empty()) {
                        // Group specified, and this isn't in that group
                        return false;
                    }
                    if (settings.config.key != label)
                        return false;
                    fos << label << " = " << value << "\n";
                    new_value = int_value;
                    fos << "setting " << label << " -> " << new_value << "\n";
                    return true;
                });
            }

            modifier.ptr_string_t_with_name([&](int tag, uint32_t id, const string &label, const string &value, string& new_value) -> bool {
                const auto &nfg = named_feature_groups.find(std::make_pair(tag, id));
                if (nfg == named_feature_groups.end() && !settings.config.group.empty()) {
                    // Group specified, and this isn't in that group
                    return false;
                }
                if (settings.config.key != label)
                    return false;
                fos << label << " = \"" << value << "\"\n";
                new_value = string_value;
                fos << "setting " << label << " -> \"" << new_value << "\"\n";
                return true;
            });

            modifier.visit(access, hdr);
        }
    }
}

string missing_device_string(bool wasRetry, bool requires_rp2350 = false) {
    char b[256];
    const char* device_name = requires_rp2350 ? "RP2350" : "RP-series";
    if (wasRetry) {
        strcpy(b, "Despite the reboot attempt, no ");
    } else {
        strcpy(b, "No ");
    }
    char *buf = b + strlen(b);
    int buf_len = b + sizeof(b) - buf;
    if (settings.address != -1) {
        if (settings.bus != -1) {
            snprintf(buf, buf_len, "accessible %s device in BOOTSEL mode was found at bus %d, address %d.", device_name, settings.bus, settings.address);
        } else {
            snprintf(buf, buf_len, "accessible %s devices in BOOTSEL mode were found with address %d.", device_name, settings.address);
        }
    } else if (settings.bus != -1) {
        snprintf(buf, buf_len, "accessible %s devices in BOOTSEL mode were found found on bus %d.", device_name, settings.bus);
    } else if (!settings.ser.empty()) {
        snprintf(buf, buf_len, "accessible %s devices in BOOTSEL mode were found found with serial number %s.", device_name, settings.ser.c_str());
    } else {
        snprintf(buf, buf_len, "accessible %s devices in BOOTSEL mode were found.", device_name);
    }
    return b;
}

bool help_command::execute(device_map &devices) {
    assert(false);
    return false;
}

uint32_t get_access_family_id(memory_access &file_access) {
    uint32_t family_id = 0;
    vector<uint8_t> bin;
    std::unique_ptr<block> best_block = find_best_block(file_access, bin);
    if (best_block == NULL) {
        // No block, so RP2040 or absolute
        if (file_access.get_binary_start() == FLASH_START) {
            vector<uint8_t> checksum_data = {};
            file_access.read_into_vector(FLASH_START, 252, checksum_data);
            uint32_t checksum = file_access.read_int(FLASH_START + 252);
            if (checksum == calc_checksum(checksum_data)) {
                // Checksum is correct, so RP2040
                DEBUG_LOG("Detected family ID %s due to boot2 checksum\n", family_name(RP2040_FAMILY_ID).c_str());
                return RP2040_FAMILY_ID;
            } else {
                // Checksum incorrect, so absolute
                DEBUG_LOG("Assumed family ID %s\n", family_name(ABSOLUTE_FAMILY_ID).c_str());
                return ABSOLUTE_FAMILY_ID;
            }
        } else {
            // no_flash RP2040 binaries have no checksum
            DEBUG_LOG("Assumed family ID %s\n", family_name(RP2040_FAMILY_ID).c_str());
            return RP2040_FAMILY_ID;
        }
    }
    auto first_item = best_block->items[0].get();
    if (first_item->type() != PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE) {
        // This will apply for partition tables
        DEBUG_LOG("Assumed family ID %s due to block with no IMAGE_DEF\n", family_name(ABSOLUTE_FAMILY_ID).c_str());
        return ABSOLUTE_FAMILY_ID;
    }
    auto image_def = dynamic_cast<image_type_item*>(first_item);
    if (image_def->image_type() == type_exe) {
        if (image_def->chip() == chip_rp2040) {
            family_id = RP2040_FAMILY_ID;
        } else if (image_def->chip() == chip_rp2350) {
            if (image_def->cpu() == cpu_riscv) {
                family_id = RP2350_RISCV_FAMILY_ID;
            } else if (image_def->cpu() == cpu_arm) {
                if (image_def->security() == sec_s) {
                    family_id = RP2350_ARM_S_FAMILY_ID;
                } else if (image_def->security() == sec_ns) {
                    family_id = RP2350_ARM_NS_FAMILY_ID;
                } else {
                    fail(ERROR_INCOMPATIBLE, "Cannot autodetect UF2 family: Unsupported security level %x\n", image_def->security());
                }
            } else {
                fail(ERROR_INCOMPATIBLE, "Cannot autodetect UF2 family: Unsupported cpu %x\n", image_def->cpu());
            }
        } else {
            fail(ERROR_INCOMPATIBLE, "Cannot autodetect UF2 family: Unsupported chip %x\n", image_def->chip());
        }
    } else if (image_def->image_type() == type_data) {
        family_id = DATA_FAMILY_ID;
    } else {
        fail(ERROR_INCOMPATIBLE, "Cannot autodetect UF2 family: Unsupported image type %x\n", image_def->image_type());
    }

    return family_id;
}

uint32_t get_family_id(uint8_t file_idx) {
    uint32_t family_id = 0;
    if (settings.family_id) {
        family_id = settings.family_id;
    } else if (get_file_type_idx(file_idx) == filetype::elf || get_file_type_idx(file_idx) == filetype::bin) {
        auto file_access = get_file_memory_access(file_idx);
        family_id = get_access_family_id(file_access);
    } else if (get_file_type_idx(file_idx) == filetype::uf2) {
        auto file = get_file_idx(ios::in|ios::binary, file_idx);
        uf2_block block;
        file->read((char*)&block, sizeof(block));
        #if SUPPORT_A2
        // ignore the absolute block
        if (check_abs_block(block)) {
            DEBUG_LOG("Ignoring RP2350-E10 absolute block\n");
            file->read((char*)&block, sizeof(block));
        }
        #endif
        family_id = block.file_size;
    } else {
        // todo this can be done - need to add block search for bin files
        fail(ERROR_FORMAT, "Cannot autodetect UF2 family - must specify the family\n");
    }
    DEBUG_LOG("Detected family ID %s\n", family_name(family_id).c_str());;
    return family_id;
}

#if HAS_LIBUSB
std::shared_ptr<vector<tuple<uint32_t, uint32_t>>> get_partitions(picoboot::connection &con) {
    picoboot_memory_access raw_access(con);
    if (get_model(raw_access) != rp2350) {
        // Not an rp2350, so no partitions
        return nullptr;
    }

#if SUPPORT_A2
    con.exit_xip();
#endif

    uint8_t loc_flags_id_buf[256];
    uint8_t family_id_name_buf[256];
    uint32_t *loc_flags_id_buf_32 = (uint32_t *)loc_flags_id_buf;
    uint32_t *family_id_name_buf_32 = (uint32_t *)family_id_name_buf;
    picoboot_get_info_cmd cmd;
    cmd.bType = PICOBOOT_GET_INFO_PARTTION_TABLE;
    cmd.dParams[0] = PT_INFO_PT_INFO | PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_PARTITION_ID;
    con.get_info(&cmd, loc_flags_id_buf, sizeof(loc_flags_id_buf));
    unsigned int lfi_pos = 0;
    unsigned int words = loc_flags_id_buf_32[lfi_pos++];
    unsigned int included_fields = loc_flags_id_buf_32[lfi_pos++];
    assert(included_fields == cmd.dParams[0]);
    unsigned int partition_count = loc_flags_id_buf[lfi_pos * 4];
    unsigned int has_pt = loc_flags_id_buf[lfi_pos * 4 + 1];
    lfi_pos++;
    resident_partition_t unpartitioned = *(resident_partition_t *) &loc_flags_id_buf_32[lfi_pos];
    lfi_pos += 2;

    vector<tuple<uint32_t, uint32_t>> ret;

    if (!has_pt || !partition_count) {
        // there is no partition table, or it is empty
        return nullptr;
    }

    if (has_pt) {
        auto rp2350_version = get_rp2350_version(raw_access);
        for (unsigned int i = 0; i < partition_count; i++) {
            uint32_t location_and_permissions = loc_flags_id_buf_32[lfi_pos++];
            uint32_t flags_and_permissions = loc_flags_id_buf_32[lfi_pos++];
            uint64_t id;
            if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_HAS_ID_BITS) {
                id = loc_flags_id_buf_32[lfi_pos] | ((uint64_t) loc_flags_id_buf_32[lfi_pos + 1] << 32u);
                lfi_pos += 2;
            }
            ret.push_back(std::make_tuple(
                ((location_and_permissions >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB) & 0x1fffu) * 4096,
                (((location_and_permissions >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB) & 0x1fffu) + 1) * 4096
            ));
            if ((location_and_permissions ^ flags_and_permissions) &
                PICOBIN_PARTITION_PERMISSIONS_BITS) {
                printf("PARTITION TABLE PERMISSION MISMATCH!\n");
                return nullptr;
            }
        }
    }

    return std::make_shared<vector<tuple<uint32_t, uint32_t>>>(ret);
}
#endif

bool config_command::execute(device_map &devices) {
    fos.first_column(0); fos.hanging_indent(0);

    if (!settings.filenames[0].empty()) {
        auto raw_access = get_file_memory_access(0, true);
        fos << "File " << settings.filenames[0] << ":\n\n";
        config_guts(raw_access);
        return false;
    }
#if HAS_LIBUSB
    int size = devices[dr_vidpid_bootrom_ok].size();
    if (size) {
        if (size > 1) {
            fos << "Multiple RP-series devices in BOOTSEL mode found:\n";
        }
        for (auto handles : devices[dr_vidpid_bootrom_ok]) {
            selected_model = std::get<0>(handles);
            fos.first_column(0); fos.hanging_indent(0);
            if (size > 1) {
                auto s = bus_device_string(std::get<1>(handles), std::get<0>(handles));
                string dashes;
                std::generate_n(std::back_inserter(dashes), s.length() + 1, [] { return '-'; });
                fos << "\n" << s << ":\n" << dashes << "\n";
            }
            picoboot::connection connection(std::get<2>(handles), std::get<0>(handles));
            picoboot_memory_access access(connection);
            // Enable auto-erase
            access.erase = true;
            auto partitions = get_partitions(connection);
            vector<uint32_t> starts;
            if (partitions) {
                for (auto range : *partitions) {
                    starts.push_back(std::get<0>(range));
                }
                for (unsigned int i=0; i < starts.size(); i++) {
                    uint32_t start = starts[i];
                    fos.first_column(0); fos.hanging_indent(0);
                    fos << "\nPartition " << i << "\n";
                    fos.first_column(1);
                    partition_memory_access part_access(access, start);
                    config_guts(part_access);
                }
            } else {
                config_guts(access);
            }
        }
    } else {
        fail(ERROR_NO_DEVICE, missing_device_string(false));
    }
#endif
    return false;
}

bool info_command::execute(device_map &devices) {
    fos.first_column(0); fos.hanging_indent(0);
    if (!settings.filenames[0].empty()) {
        uint32_t next_id = 0;
        auto access = get_file_memory_access(0, false, &next_id);
        uint32_t id = 0;
        id = get_family_id(0);
        if (id == RP2040_FAMILY_ID) {
            access.set_model(rp2040);
        } else if (id >= RP2350_ARM_S_FAMILY_ID && id <= RP2350_ARM_NS_FAMILY_ID) {
            access.set_model(rp2350);
        }
        if (next_id) {
            next_id = id;
            while (next_id) {
                fos.first_column(0); fos.hanging_indent(0);
                std::stringstream s;
                s << "File " << settings.filenames[0] << " family ID " << family_name(next_id) << ":";
                if (next_id != id) {
                    string dashes;
                    std::generate_n(std::back_inserter(dashes), s.str().length() + 1, [] { return '-'; });
                    fos << "\n" << dashes << "\n";
                }
                fos << s.str() << "\n\n";
                auto tmp_access = get_file_memory_access(0, false, &next_id);
                info_guts(tmp_access, nullptr);
            }
        } else {
            if (get_file_type() == filetype::uf2) {
                fos << "File " << settings.filenames[0] << " family ID " << family_name(id) << ":\n\n";
            } else {
                fos << "File " << settings.filenames[0] << ":\n\n";
            }
            info_guts(access, nullptr);
        }
        return false;
    }
#if HAS_LIBUSB
    int size = devices[dr_vidpid_bootrom_ok].size();
    if (size) {
        if (size > 1) {
            fos << "Multiple RP-series devices in BOOTSEL mode found:\n";
        }
        for (auto handles : devices[dr_vidpid_bootrom_ok]) {
            selected_model = std::get<0>(handles);
            fos.first_column(0); fos.hanging_indent(0);
            if (size > 1) {
                auto s = bus_device_string(std::get<1>(handles), std::get<0>(handles));
                string dashes;
                std::generate_n(std::back_inserter(dashes), s.length() + 1, [] { return '-'; });
                fos << "\n" << s << ":\n" << dashes << "\n";
            }
            picoboot::connection connection(std::get<2>(handles), std::get<0>(handles));
            picoboot_memory_access access(connection);
            auto partitions = get_partitions(connection);
            vector<uint32_t> starts;
            if (partitions) {
                // Check if bootloader is present, based on presence of binary info
                binary_info_header hdr;
                auto bi_access = get_bi_access(access);
                bool has_bootloader = find_binary_info(*bi_access, hdr);

                // Don't show device, until all partitions done
                bool device = settings.info.show_device || settings.info.all;
                bool debug = settings.info.show_debug || settings.info.all;
                if (settings.info.all) {
                    settings.info.show_basic = true;
                    settings.info.show_pins = true;
                    settings.info.show_build = true;
                    settings.info.show_metadata = true;
                    settings.info.all = false;
                }
                if ((settings.info.show_basic || settings.info.show_pins || settings.info.show_build || settings.info.show_metadata) || !(settings.info.show_device || settings.info.show_debug)) {
                    settings.info.show_device = false;
                    settings.info.show_debug = false;
                    for (auto range : *partitions) {
                        starts.push_back(std::get<0>(range));
                    }
                    if (has_bootloader && std::none_of(starts.cbegin(), starts.cend(), [](int i) { return i == 0; })) {
                        // Print bootloader info, only if bootloader is present and not in a partition
                        fos.first_column(0); fos.hanging_indent(0);
                        fos << "\nBootloader\n";
                        fos.first_column(1);
                        partition_memory_access part_access(access, 0);
                        info_guts(part_access, &connection);
                    }
                    for (unsigned int i=0; i < starts.size(); i++) {
                        uint32_t start = starts[i];
                        fos.first_column(0); fos.hanging_indent(0);
                        fos << "\nPartition " << i << "\n";
                        fos.first_column(1);
                        partition_memory_access part_access(access, start);
                        info_guts(part_access, &connection);
                    }
                }
                if (device || debug) {
                    fos.first_column(0); fos.hanging_indent(0);
                    fos << "\n";
                    settings.info.show_basic = false;
                    settings.info.show_pins = false;
                    settings.info.show_build = false;
                    settings.info.show_metadata = false;
                    settings.info.show_device = device;
                    settings.info.show_debug = debug;
                    info_guts(access, &connection);
                }
            } else {
                info_guts(access, &connection);
            }
        }
    } else {
        fail(ERROR_NO_DEVICE, missing_device_string(false));
    }
#endif
    return false;
}

#if HAS_LIBUSB
static picoboot::connection get_single_bootsel_device_connection(device_map& devices, bool exclusive = true) {
    assert(devices[dr_vidpid_bootrom_ok].size() == 1);
    auto device = devices[dr_vidpid_bootrom_ok][0];
    selected_model = std::get<0>(device);
    libusb_device_handle *rc = std::get<2>(device);
    if (!rc) fail(ERROR_USB, "Unable to connect to device");
    return picoboot::connection(rc, std::get<0>(device), exclusive);
}

static picoboot::connection get_single_rp2350_bootsel_device_connection(device_map& devices, bool exclusive = true) {
    auto con = get_single_bootsel_device_connection(devices, exclusive);
    // todo amy we may have a different VID PID?
    picoboot_memory_access raw_access(con);
    if (get_model(raw_access) != rp2350) {
        fail(ERROR_INCOMPATIBLE, "RP2350 command cannot be used with a non RP2350 device");
    }
    return con;
}
#endif

struct progress_bar {
    explicit progress_bar(string new_prefix, int width = 30) : width(width) {
        // Align all bars with the longest possible prefix string
        auto longest_mem = std::max_element(
            std::begin(memory_names), std::end(memory_names),
            [] (const auto & p1, const auto & p2) {
                return p1.second.length() < p2.second.length();
            }
        );
        string extra_space(string("Loading into " + longest_mem->second + ": ").length() - new_prefix.length(), ' ');
        prefix = new_prefix + extra_space;
        progress(0);
    }

    void progress(int _percent) {
        if (_percent != percent) {
            percent = _percent;
            unsigned int len = (width * percent) / 100;
            std::cout << prefix << "[" << string(len, '=') << string(width-len, ' ') << "]  " << std::to_string(percent) << "%\r" << std::flush;
        }
    }

    void progress(long dividend, long divisor) {
        progress(divisor ? (int)((100 * dividend) / divisor) : 100);
    }

    ~progress_bar() {
        std::cout << "\n";
    }

    std::string prefix;
    int percent = -1;
    int width;
};

#if HAS_LIBUSB
vector<range> get_coalesced_ranges(iostream_memory_access &file_access, model_t model) {
    auto rmap = file_access.get_rmap();
    auto ranges = rmap.ranges();
    std::sort(ranges.begin(), ranges.end(), [](const range& a, const range &b) {
        return a.from < b.from;
    });
    // coalesce all the contiguous ranges
    for(auto i = ranges.begin(); i < ranges.end(); ) {
        if (i != ranges.end() - 1) {
            uint32_t erase_size;
            // we want to coalesce flash sectors together (this ends up creating ranges that may have holes)
            if( get_memory_type(i->from, model) == flash ) {
                erase_size = FLASH_SECTOR_ERASE_SIZE;
            } else {
                erase_size = 1;
            }
            if (i->to / erase_size == (i+1)->from / erase_size) {
                i->to = (i+1)->to;
                i = ranges.erase(i+1) - 1;
                continue;
            }
        }
        i++;
    }
    return ranges;
}

bool save_command::execute(device_map &devices) {
    auto con = get_single_bootsel_device_connection(devices);
    picoboot_memory_access raw_access(con);

    uint32_t end = 0;
    uint32_t binary_end = 0;
    binary_info_header hdr;
    uint32_t start = FLASH_START;
    if (!settings.save.all) {
        if (settings.range_set) {
            if (get_file_type() == filetype::uf2) {
                start = settings.from & ~(PAGE_SIZE - 1);
                end = (settings.to + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
            } else {
                start = settings.from;
                end = settings.to;
                // Set offset for verifying
                settings.offset = start;
                settings.offset_set = true;
            }
            if (end <= start) {
                fail(ERROR_ARGS, "Save range is invalid/empty");
            }
        } else {
            if (find_binary_info(raw_access, hdr)) {
                auto access = remapped_memory_access(raw_access, hdr.reverse_copy_mapping);
                auto visitor = bi_visitor{};
                visitor.id_and_int([&](int tag, uint32_t id, uint32_t value) {
                    if (tag != BINARY_INFO_TAG_RASPBERRY_PI)
                        return;
                    if (id == BINARY_INFO_ID_RP_BINARY_END) binary_end = value;
                });
                visitor.visit(access, hdr);
            }
            end = binary_end;
            vector<uint8_t> bin;
            std::unique_ptr<block> last_block = find_last_block(raw_access, bin);
            if (last_block != nullptr) {
                uint32_t new_end = last_block->physical_addr + (last_block->to_words().size())*4;
                DEBUG_LOG("Adjusting end to max of %x %x\n", end, new_end);
                end = MAX(end, new_end);
            }
            if (end == 0) {
                fail(ERROR_NOT_POSSIBLE,
                     "Cannot determine the binary size, so cannot save the program only, try --all.");
            }
        }
    } else {
        end = FLASH_START + guess_flash_size(raw_access);
        if (end <= FLASH_START) {
            fail(ERROR_NOT_POSSIBLE, "Cannot determine the flash size, so cannot save the entirety of flash, try --range.");
        }
    }

    model_t model = get_model(raw_access);
    enum memory_type t1 = get_memory_type(start , model);
    enum memory_type t2 = get_memory_type(end, model);
    if (t1 != t2 || t1 == invalid || t1 == sram_unstriped) {
        fail(ERROR_NOT_POSSIBLE, "Save range crosses unmapped memory");
    }
    uint32_t size = end - start;

    std::function<void(FILE *out, const uint8_t *buffer, unsigned int size, unsigned int offset)> writer256 = [](FILE *out, const uint8_t *buffer, unsigned int size, unsigned int offset) { assert(false); };
    uf2_block block;
    memset(&block, 0, sizeof(block));
    switch (get_file_type()) {
        case filetype::bin:
//            if (start != FLASH_START) {
//                fail(ERROR_ARGS, "range must start at 0x%08x for saving as a BIN file", FLASH_START);
//            }
            writer256 = [](FILE *out, const uint8_t *buffer, unsigned int actual_size, unsigned int offset) {
                fseek(out, offset, SEEK_SET);
                if (1 != fwrite(buffer, actual_size, 1, out)) {
                    fail_write_error();
                }
            };
            break;
        case filetype::elf:
            fail(ERROR_ARGS, "Save to ELF file is not supported");
            break;
        case filetype::uf2:
            block.magic_start0 = UF2_MAGIC_START0;
            block.magic_start1 = UF2_MAGIC_START1;
            block.flags = UF2_FLAG_FAMILY_ID_PRESENT;
            block.payload_size = PAGE_SIZE;
            block.num_blocks = (size + PAGE_SIZE - 1)/PAGE_SIZE;
            block.file_size = settings.family_id ? settings.family_id : get_access_family_id(raw_access);
            block.magic_end = UF2_MAGIC_END;
            writer256 = [&](FILE *out, const uint8_t *buffer, unsigned int size, unsigned int offset) {
                static_assert(512 == sizeof(block), "");
                block.target_addr = start + offset;
                block.block_no = offset / PAGE_SIZE;
                assert(size <= PAGE_SIZE);
                memcpy(block.data, buffer, size);
                if (size < PAGE_SIZE) memset(block.data + size, 0, PAGE_SIZE);
                if (1 != fwrite(&block, sizeof(block), 1, out)) {
                    fail_write_error();
                }
            };
            break;
        default:
            throw command_failure(-1, "Unsupported output file type");
    }
    FILE *out = fopen(settings.filenames[0].c_str(), "wb");
    if (out) {
        try {
            vector<uint8_t> buf;
            {
                progress_bar bar("Saving file: ");
                for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
                    bar.progress(addr-start, end-start);
                    uint32_t this_size = std::min(PAGE_SIZE, end - addr);
                    raw_access.read_into_vector(addr, this_size, buf);
                    writer256(out, buf.data(), this_size, addr - start);
                }
                bar.progress(100);
            }
            fseek(out, 0, SEEK_END);
            std::cout << "Wrote " << ftell(out) << " bytes to " << settings.filenames[0].c_str() << "\n";
            fclose(out);
        } catch (std::exception &) {
            fclose(out);
            throw;
        }
    }

    if (settings.save.verify) {
        auto file_access = get_file_memory_access(0);
        model_t model = get_model(raw_access);
        auto ranges = get_coalesced_ranges(file_access, model);
        for (auto mem_range : ranges) {
            enum memory_type type = get_memory_type(mem_range.from, model);
            bool ok = true;
            {
                progress_bar bar("Verifying " + memory_names[type] + ": ");
                uint32_t batch_size = FLASH_SECTOR_ERASE_SIZE;
                vector<uint8_t> file_buf;
                vector<uint8_t> device_buf;
                uint32_t pos = mem_range.from;
                for (uint32_t base = mem_range.from; base < mem_range.to && ok; base += batch_size) {
                    uint32_t this_batch = std::min(std::min(mem_range.to, end) - base, batch_size);
                    // note we pass zero_fill = true in case the file has holes, but this does
                    // mean that the verification will fail if those holes are not filled with zeros
                    // on the device
                    file_access.read_into_vector(base, this_batch, file_buf, true);
                    raw_access.read_into_vector(base, this_batch, device_buf);
                    assert(file_buf.size() == device_buf.size());
                    for (unsigned int i = 0; i < this_batch; i++) {
                        if (file_buf[i] != device_buf[i]) {
                            pos = base + i;
                            printf("Unmatch file %x, device %x, pos %x\n", file_buf[i], device_buf[i], pos);
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        pos = base + this_batch;
                    }
                    bar.progress(pos - mem_range.from, mem_range.to - mem_range.from);
                }
            }
            if (ok) {
                std::cout << "  OK\n";
            } else {
                std::cout << "  FAILED\n";
                fail(ERROR_VERIFICATION_FAILED, "The device contents did not match the saved file");
            }
        }
    }
    return false;
}

bool erase_command::execute(device_map &devices) {
    auto con = get_single_bootsel_device_connection(devices);
    picoboot_memory_access raw_access(con);

    uint32_t end = 0;
    uint32_t binary_end = 0;
    binary_info_header hdr;
    uint32_t start = FLASH_START;
    if (settings.load.partition >= 0) {
        auto partitions = get_partitions(con);
        if (!partitions) {
            fail(ERROR_NOT_POSSIBLE, "There is no partition table on the device");
        }
        if (settings.load.partition >= partitions->size()) {
            fail(ERROR_NOT_POSSIBLE, "There are only %d partitions on the device", partitions->size());
        }
        start = std::get<0>((*partitions)[settings.load.partition]);
        end = std::get<1>((*partitions)[settings.load.partition]);
        printf("Erasing partition %d:\n", settings.load.partition);
        printf("  %08x->%08x\n", start, end);
        start += FLASH_START;
        end += FLASH_START;
        if (end <= start) {
            fail(ERROR_ARGS, "Erase range is invalid/empty");
        }
    } else if (settings.range_set) {
        start = settings.from & ~(FLASH_SECTOR_ERASE_SIZE - 1);
        end = (settings.to + (FLASH_SECTOR_ERASE_SIZE - 1)) & ~(FLASH_SECTOR_ERASE_SIZE - 1);
        if (end <= start) {
            fail(ERROR_ARGS, "Erase range is invalid/empty");
        }
    } else {
        end = FLASH_START + guess_flash_size(raw_access);
        if (end <= FLASH_START) {
            fail(ERROR_NOT_POSSIBLE, "Cannot determine the flash size, so cannot erase the entirety of flash, try --range.");
        }
    }

    model_t model = get_model(raw_access);
    enum memory_type t1 = get_memory_type(start , model);
    enum memory_type t2 = get_memory_type(end, model);
    if (t1 != flash || t1 != t2) {
        fail(ERROR_NOT_POSSIBLE, "Erase range not all in flash");
    }
    uint32_t size = end - start;

    {
        progress_bar bar("Erasing: ");
        for (uint32_t addr = start; addr < end; addr += FLASH_SECTOR_ERASE_SIZE) {
            bar.progress(addr-start, end-start);
            con.flash_erase(addr, FLASH_SECTOR_ERASE_SIZE);
        }
        bar.progress(100);
    }
    std::cout << "Erased " << size << " bytes\n";
    return false;
}
#endif

#if HAS_LIBUSB
bool get_target_partition(picoboot::connection &con, uint32_t* start = nullptr, uint32_t* end = nullptr) {
#if SUPPORT_A2
    con.exit_xip();
#endif

    uint8_t loc_flags_id_buf[256];
    uint32_t *loc_flags_id_buf_32 = (uint32_t *)loc_flags_id_buf;
    picoboot_get_info_cmd cmd;
    cmd.bType = PICOBOOT_GET_INFO_UF2_TARGET_PARTITION;
    cmd.dParams[0] = settings.family_id;
    con.get_info(&cmd, loc_flags_id_buf, sizeof(loc_flags_id_buf));
    assert(loc_flags_id_buf_32[0] == 3);
    if ((int)loc_flags_id_buf_32[1] < 0) {
        printf("Family ID %s cannot be downloaded anywhere\n", family_name(settings.family_id).c_str());
        return false;
    } else {
        if (loc_flags_id_buf_32[1] == PARTITION_TABLE_NO_PARTITION_INDEX) {
            printf("Family ID %s can be downloaded in absolute space:\n", family_name(settings.family_id).c_str());
        } else {
            printf("Family ID %s can be downloaded in partition %d:\n", family_name(settings.family_id).c_str(), loc_flags_id_buf_32[1]);
        }
        uint32_t location_and_permissions = loc_flags_id_buf_32[2];
        uint32_t saddr = ((location_and_permissions >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB) & 0x1fffu) * 4096;
        uint32_t eaddr = (((location_and_permissions >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB) & 0x1fffu) + 1) * 4096;
        printf("  %08x->%08x\n", saddr, eaddr);
        if (start) *start = saddr;
        if (end) *end = eaddr;
        return true;
    }
}

bool load_guts(picoboot::connection con, iostream_memory_access &file_access) {
    picoboot_memory_access raw_access(con);
    range flash_binary_range(FLASH_START, FLASH_END_RP2350); // pick biggest (rp2350) here for now
    bool flash_binary_end_unknown = true;
    if (settings.load.no_overwrite_force) settings.load.no_overwrite = true;
    if (settings.load.no_overwrite) {
        binary_info_header hdr;
        if (find_binary_info(raw_access, hdr)) {
            auto access = remapped_memory_access(raw_access, hdr.reverse_copy_mapping);
            auto visitor = bi_visitor{};
            visitor.id_and_int([&](int tag, uint32_t id, uint32_t value) {
                if (tag != BINARY_INFO_TAG_RASPBERRY_PI)
                    return;
                if (id == BINARY_INFO_ID_RP_BINARY_END) {
                    flash_binary_range.to = value;
                    flash_binary_end_unknown = false;
                }
            });
            visitor.visit(access, hdr);
        }
    }
    model_t model = get_model(raw_access);
    auto ranges = get_coalesced_ranges(file_access, model);
    bool uses_flash = false;
    uint32_t flash_min = std::numeric_limits<uint32_t>::max();
    uint32_t flash_max = std::numeric_limits<uint32_t>::min();
    for (auto mem_range : ranges) {
        enum memory_type t1 = get_memory_type(mem_range.from, model);
        enum memory_type t2 = get_memory_type(mem_range.to, model);
        if (t1 != t2 || t1 == invalid || t1 == rom || t1 == sram_unstriped) {
            fail(ERROR_FORMAT, "File to load contained an invalid memory range 0x%08x-0x%08x", mem_range.from,
                 mem_range.to);
        }
        if (t1 == flash) {
            uses_flash = true;
            flash_min = std::min(flash_min, mem_range.from);
            flash_max = std::max(flash_max, mem_range.to);
        }
        if (settings.load.no_overwrite && mem_range.intersects(flash_binary_range)) {
            if (flash_binary_end_unknown) {
                if (!settings.load.no_overwrite_force) {
                    fail(ERROR_NOT_POSSIBLE, "-n option specified, but the size/presence of an existing flash binary could not be detected; aborting. Consider using the -N option");
                }
            } else {
                fail(ERROR_NOT_POSSIBLE, "-n option specified, and the loaded data range clashes with the existing flash binary range %08x->%08x",
                     flash_binary_range.from, flash_binary_range.to);
            }
        }
    }
    if (uses_flash) {
        assert(flash_max > flash_min);
        uint32_t flash_data_size = flash_max - flash_min;
        assert(flash_min >= FLASH_START);
        uint32_t flash_start_offset = flash_min - FLASH_START;
        uint32_t size_guess = guess_flash_size(raw_access);
        if (size_guess > 0) {
            // Skip check when targeting PSRAM, which is anything above 0x11000000
            if (flash_start_offset < FLASH_END_RP2040 && (flash_start_offset + flash_data_size) > size_guess) {
                if (flash_start_offset) {
                    fail(ERROR_NOT_POSSIBLE, "File size 0x%x starting at 0x%x is too big to fit in flash size 0x%x", flash_data_size, flash_start_offset, size_guess);
                } else {
                    fail(ERROR_NOT_POSSIBLE, "File size 0x%x is too big to fit in flash size 0x%x", flash_data_size, size_guess);
                }
            }
        }
        if (settings.partition_size > 0) {
            if (flash_data_size > settings.partition_size) {
                fail(ERROR_NOT_POSSIBLE, "File size 0x%x is too big to fit in partition size 0x%x", flash_data_size, settings.partition_size);
            }
        }
    }
    for (auto mem_range : ranges) {
        enum memory_type type = get_memory_type(mem_range.from, model);
        // new scope for progress bar
        {
            progress_bar bar("Loading into " + memory_names[type] + ": ");
            uint32_t batch_size = FLASH_SECTOR_ERASE_SIZE;
            bool ok = true;
            vector<uint8_t> file_buf;
            vector<uint8_t> device_buf;
            for (uint32_t base = mem_range.from; base < mem_range.to && ok;) {
                uint32_t this_batch = std::min(mem_range.to - base, batch_size);
                if (type == flash) {
                    // we have to erase an entire page, so then fill with zeros
                    range aligned_range(base & ~(FLASH_SECTOR_ERASE_SIZE - 1),
                                        (base & ~(FLASH_SECTOR_ERASE_SIZE - 1)) + FLASH_SECTOR_ERASE_SIZE);
                    range read_range(base, base + this_batch);
                    read_range.intersect(aligned_range);
                    file_access.read_into_vector(read_range.from, read_range.to - read_range.from, file_buf, true); // zero fill to cope with holes
                    // zero padding up to FLASH_SECTOR_ERASE_SIZE
                    file_buf.insert(file_buf.begin(), read_range.from - aligned_range.from, 0);
                    file_buf.insert(file_buf.end(), aligned_range.to - read_range.to, 0);
                    assert(file_buf.size() == FLASH_SECTOR_ERASE_SIZE);

                    bool skip = false;
                    if (settings.load.update) {
                        vector<uint8_t> read_device_buf;
                        raw_access.read_into_vector(aligned_range.from, batch_size, read_device_buf);
                        skip = file_buf == read_device_buf;
                    }
                    if (!skip) {
                        con.exit_xip();
                        con.flash_erase(aligned_range.from, FLASH_SECTOR_ERASE_SIZE);
                        raw_access.write_vector(aligned_range.from, file_buf);
                    }
                    base = read_range.to; // about to add batch_size
                } else {
                    file_access.read_into_vector(base, this_batch, file_buf);
                    raw_access.write_vector(base, file_buf);
                    base += this_batch;
                }
                bar.progress(base - mem_range.from, mem_range.to - mem_range.from);
            }
        }
    }
    for (auto mem_range : ranges) {
        enum memory_type type = get_memory_type(mem_range.from, model);
        if (settings.load.verify) {
            bool ok = true;
            {
                progress_bar bar("Verifying " + memory_names[type] + ": ");
                uint32_t batch_size = FLASH_SECTOR_ERASE_SIZE;
                vector<uint8_t> file_buf;
                vector<uint8_t> device_buf;
                uint32_t pos = mem_range.from;
                for (uint32_t base = mem_range.from; base < mem_range.to && ok; base += batch_size) {
                    uint32_t this_batch = std::min(mem_range.to - base, batch_size);
                    // note we pass zero_fill = true in case the file has holes, but this does
                    // mean that the verification will fail if those holes are not filled with zeros
                    // on the device
                    file_access.read_into_vector(base, this_batch, file_buf, true);
                    raw_access.read_into_vector(base, this_batch, device_buf);
                    assert(file_buf.size() == device_buf.size());
                    for (unsigned int i = 0; i < this_batch; i++) {
                        if (file_buf[i] != device_buf[i]) {
                            pos = base + i;
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        pos = base + this_batch;
                    }
                    bar.progress(pos - mem_range.from, mem_range.to - mem_range.from);
                }
            }
            if (ok) {
                std::cout << "  OK\n";
            } else {
                std::cout << "  FAILED\n";
                fail(ERROR_VERIFICATION_FAILED, "The device contents did not match the file");
            }
        }
    }
    if (settings.load.execute) {
        uint32_t start = file_access.get_binary_start();
        if (!start) {
            fail(ERROR_FORMAT, "Cannot execute as file does not contain a valid RP2 executable image");
        }
        if (get_model(raw_access) == rp2350) {
            struct picoboot_reboot2_cmd cmd;
            auto mt = get_memory_type(start, model);
            if (mt == flash) {
                cmd.dParam0 = settings.offset;
                cmd.dFlags = REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE;
                DEBUG_LOG(">>> using flash update boot of %08x\n", cmd.dParam0);
            } else {
                cmd.dParam0 = start;
                unsigned int end;
                switch (mt) {
                    case sram:
                        end = SRAM_END_RP2350;
                        break;
                    case xip_sram:
                        end = XIP_SRAM_END_RP2350;
                        break;
                    default:
                        end = SRAM_END_RP2350;
                }
                cmd.dParam1 = end - start;
                cmd.dFlags = REBOOT2_FLAG_REBOOT_TYPE_RAM_IMAGE;
                DEBUG_LOG(">>> using flash update boot of %08x\n", cmd.dParam0);
            }
            cmd.dDelayMS = 500,
            con.reboot2(&cmd);
        } else {
            con.reboot(flash == get_memory_type(start, model) ? 0 : start,
                       model == rp2040 ? SRAM_END_RP2040 : SRAM_END_RP2350, 500);
        }
        std::cout << "\nThe device was rebooted to start the application.\n";
        return true;
    }
    return false;
}

bool load_command::execute(device_map &devices) {
    auto con = get_single_bootsel_device_connection(devices);
    picoboot_memory_access raw_access(con);
    auto tmp_file_access = get_file_memory_access(0);
    if (settings.load.partition >= 0) {
        auto partitions = get_partitions(con);
        if (!partitions) {
            fail(ERROR_NOT_POSSIBLE, "There is no partition table on the device");
        }
        if (settings.load.partition >= partitions->size()) {
            fail(ERROR_NOT_POSSIBLE, "There are only %d partitions on the device", partitions->size());
        }
        uint32_t start = std::get<0>((*partitions)[settings.load.partition]);
        uint32_t end = std::get<1>((*partitions)[settings.load.partition]);
        printf("Downloading into partition %d:\n", settings.load.partition);
        printf("  %08x->%08x\n", start, end);
        settings.offset = start + FLASH_START;
        settings.offset_set = true;
        settings.partition_size = end - start;
    } else if (!settings.load.ignore_pt && !settings.offset_set && tmp_file_access.get_binary_start() == FLASH_START) {
        uint32_t family_id = get_family_id(0);
        settings.family_id = family_id;
        uint32_t start;
        uint32_t end;
        if (get_model(raw_access) != rp2040) {
            if (get_target_partition(con, &start, &end)) {
                settings.offset = start + FLASH_START;
                settings.offset_set = true;
                settings.partition_size = end - start;
            } else {
                // Check if partition table is present, for correct error message
                auto partitions = get_partitions(con);
                if (!partitions) {
                    fail(ERROR_NOT_POSSIBLE, "This file cannot be loaded onto a device with no partition table");
                } else {
                    fail(ERROR_NOT_POSSIBLE, "This file cannot be loaded into the partition table on the device");
                }
            }
        }
    }
    auto file_access = get_file_memory_access(0);
    if (settings.offset_set && get_file_type() != filetype::bin && get_model(raw_access) == rp2040) {
        fail(ERROR_ARGS, "Offset only valid for BIN files");
    }
    bool ret = load_guts(con, file_access);
    return ret;
}
#endif

#if HAS_MBEDTLS
bool encrypt_command::execute(device_map &devices) {
    bool isElf = false;
    bool isBin = false;
    if (get_file_type() == filetype::elf) {
        isElf = true;
    } else if (get_file_type() == filetype::bin) {
        isBin = true;
    } else {
        fail(ERROR_ARGS, "Can only sign ELFs or BINs");
    }

    if (get_file_type_idx(1) != get_file_type()) {
        fail(ERROR_ARGS, "Can only sign to same file type");
    }

    if (get_file_type_idx(2) != filetype::bin) {
        fail(ERROR_ARGS, "Can only read AES key from BIN file");
    }

    if (settings.seal.sign && settings.filenames[3].empty()) {
        fail(ERROR_ARGS, "missing key file for signing after encryption");
    }

    if (!settings.filenames[3].empty() && get_file_type_idx(3) != filetype::pem) {
        fail(ERROR_ARGS, "Can only read pem keys");
    }


    auto aes_file = get_file_idx(ios::in|ios::binary, 2);
    
    private_t aes_key;
    aes_file->read((char*)aes_key.bytes, sizeof(aes_key.bytes));


    private_t private_key = {};
    public_t public_key = {};

    if (settings.seal.sign) read_keys(settings.filenames[3], &public_key, &private_key);

    if (isElf) {
        elf_file source_file(settings.verbose);
        elf_file *elf = &source_file;
        elf->read_file(get_file(ios::in|ios::binary));

        std::unique_ptr<block> first_block = find_first_block(elf);
        if (!first_block) {
            fail(ERROR_FORMAT, "No first block found");
        }
        elf->editable = false;
        block new_block = place_new_block(elf, first_block);
        elf->editable = true;

        encrypt(elf, &new_block, aes_key, public_key, private_key, settings.seal.hash, settings.seal.sign);

        auto out = get_file_idx(ios::out|ios::binary, 1);
        elf->write(out);
        out->close();
    } else if (isBin) {
        auto binfile = get_file_memory_access(0);
        auto rmap = binfile.get_rmap();
        auto ranges = rmap.ranges();
        assert(ranges.size() == 1);
        auto bin_start = ranges[0].from;
        auto bin_size = ranges[0].len();

        vector<uint8_t> bin = binfile.read_vector<uint8_t>(bin_start, bin_size, false);

        std::unique_ptr<block> first_block = find_first_block(bin, bin_start);
        if (!first_block) {
            fail(ERROR_FORMAT, "No first block found");
        }
        auto bin_cp = bin;
        block new_block = place_new_block(bin_cp, bin_start, first_block);

        auto enc_data = encrypt(bin, bin_start, bin_start, &new_block, aes_key, public_key, private_key, settings.seal.hash, settings.seal.sign);

        auto out = get_file_idx(ios::out|ios::binary, 1);
        out->write((const char *)enc_data.data(), enc_data.size());
        out->close();
    } else {
        fail(ERROR_ARGS, "Must be ELF or BIN");
    }

    return false;
}

#if HAS_MBEDTLS
void sign_guts_elf(elf_file* elf, private_t private_key, public_t public_key) {
    std::unique_ptr<block> first_block = find_first_block(elf);
    if (!first_block) {
        fail(ERROR_FORMAT, "No first block found");
    }

    block new_block = place_new_block(elf, first_block);

    if (settings.seal.major_version || settings.seal.minor_version || settings.seal.rollback_version) {
        std::shared_ptr<version_item> version = new_block.get_item<version_item>();
        if (version != nullptr) {
            // Use existing major and minor versions, if not being overridden
            if (settings.seal.major_version == 0) settings.seal.major_version = version->major;
            if (settings.seal.minor_version == 0) settings.seal.minor_version = version->minor;
            new_block.items.erase(std::find(new_block.items.begin(), new_block.items.end(), version));
        }
        if (settings.seal.rollback_version) {
            if (!settings.seal.sign) {
                fail(ERROR_INCOMPATIBLE, "You must sign the binary if adding a rollback version");
            }
            version = std::make_shared<version_item>(settings.seal.major_version, settings.seal.minor_version, settings.seal.rollback_version, settings.seal.rollback_rows);
        } else {
            version = std::make_shared<version_item>(settings.seal.major_version, settings.seal.minor_version);
        }
        new_block.items.push_back(version);
    }

    // Add entry point when signing Arm images
    std::shared_ptr<image_type_item> image_type = new_block.get_item<image_type_item>();
    if (settings.seal.sign && image_type != nullptr && image_type->image_type() == type_exe && image_type->cpu() == cpu_arm) {
        std::shared_ptr<entry_point_item> entry_point = new_block.get_item<entry_point_item>();
        if (entry_point == nullptr) {
            std::shared_ptr<vector_table_item> vtor = new_block.get_item<vector_table_item>();
            uint32_t vtor_loc = 0x10000000;
            if (vtor != nullptr) {
                vtor_loc = vtor->addr;
            } else {
                if (elf->header().entry >= SRAM_START) {
                    vtor_loc = 0x20000000;
                } else if (elf->header().entry >= XIP_SRAM_START_RP2350) {
                    vtor_loc = 0x13ffc000;
                } else {
                    vtor_loc = 0x10000000;
                    std::shared_ptr<rolling_window_delta_item> rwd = new_block.get_item<rolling_window_delta_item>();
                    if (rwd != nullptr) {
                        vtor_loc += rwd->addr;
                    }
                }
            }
            auto segment = elf->segment_from_physical_address(vtor_loc);
            auto content = elf->content(*segment);
            auto offset = vtor_loc - segment->physical_address();
            uint32_t ep;
            memcpy(&ep, content.data() + offset + 4, sizeof(ep));
            uint32_t sp;
            memcpy(&sp, content.data() + offset, sizeof(sp));
            DEBUG_LOG("Adding entry_point_item: ep %08x, sp %08x\n", ep, sp);
            entry_point = std::make_shared<entry_point_item>(ep, sp);
            new_block.items.push_back(entry_point);
        }
    }

    hash_andor_sign(
        elf, &new_block, public_key, private_key,
        settings.seal.hash, settings.seal.sign,
        settings.seal.clear_sram
    );
}

vector<uint8_t> sign_guts_bin(iostream_memory_access in, private_t private_key, public_t public_key, uint32_t bin_start, uint32_t bin_size) {
    vector<uint8_t> bin = in.read_vector<uint8_t>(bin_start, bin_size, false);

    std::unique_ptr<block> first_block = find_first_block(bin, bin_start);
    if (!first_block) {
        fail(ERROR_FORMAT, "No first block found");
    }

    block new_block = place_new_block(bin, bin_start, first_block);

    if (settings.seal.major_version || settings.seal.minor_version || settings.seal.rollback_version) {
        std::shared_ptr<version_item> version = new_block.get_item<version_item>();
        if (version != nullptr) {
            // Use existing major and minor versions, if not being overridden
            if (settings.seal.major_version == 0) settings.seal.major_version = version->major;
            if (settings.seal.minor_version == 0) settings.seal.minor_version = version->minor;
            new_block.items.erase(std::find(new_block.items.begin(), new_block.items.end(), version));
        }
        if (settings.seal.rollback_version) {
            if (!settings.seal.sign) {
                fail(ERROR_INCOMPATIBLE, "You must sign the binary if adding a rollback version");
            }
            version = std::make_shared<version_item>(settings.seal.major_version, settings.seal.minor_version, settings.seal.rollback_version, settings.seal.rollback_rows);
        } else {
            version = std::make_shared<version_item>(settings.seal.major_version, settings.seal.minor_version);
        }
        new_block.items.push_back(version);
    }

    // Add entry point when signing Arm images
    std::shared_ptr<image_type_item> image_type = new_block.get_item<image_type_item>();
    if (settings.seal.sign && image_type != nullptr && image_type->image_type() == type_exe && image_type->cpu() == cpu_arm) {
        std::shared_ptr<entry_point_item> entry_point = new_block.get_item<entry_point_item>();
        if (entry_point == nullptr) {
            std::shared_ptr<vector_table_item> vtor = new_block.get_item<vector_table_item>();
            uint32_t vtor_loc = bin_start;
            if (vtor != nullptr) {
                vtor_loc = vtor->addr;
            }
            auto offset = vtor_loc - bin_start;
            uint32_t ep;
            memcpy(&ep, bin.data() + offset + 4, sizeof(ep));
            uint32_t sp;
            memcpy(&sp, bin.data() + offset, sizeof(sp));
            DEBUG_LOG("Adding entry_point_item: ep %08x, sp %08x\n", ep, sp);
            entry_point = std::make_shared<entry_point_item>(ep, sp);
            new_block.items.push_back(entry_point);
        }
    }

    auto sig_data = hash_andor_sign(
        bin, bin_start, bin_start,
        &new_block, public_key, private_key,
        settings.seal.hash, settings.seal.sign,
        settings.seal.clear_sram
    );

    return sig_data;
}
#endif

bool seal_command::execute(device_map &devices) {
    bool isElf = false;
    bool isBin = false;
    bool isUf2 = false;
    if (get_file_type() == filetype::elf) {
        isElf = true;
    } else if (get_file_type() == filetype::bin) {
        isBin = true;
    } else if (get_file_type() == filetype::uf2) {
        isUf2 = true;
    } else {
        fail(ERROR_ARGS, "Can only sign ELFs, BINs or UF2s");
    }

    if (get_file_type_idx(1) != get_file_type()) {
        fail(ERROR_ARGS, "Can only sign to same file type");
    }

    if (settings.seal.sign && settings.filenames[2].empty()) {
        fail(ERROR_ARGS, "missing key file for signing");
    }

    if (!settings.filenames[2].empty() && get_file_type_idx(2) != filetype::pem) {
        fail(ERROR_ARGS, "Can only read pem keys");
    }

    if (settings.seal.rollback_version) {
        bool defaulted = false;
        if (!settings.seal.rollback_rows.size()) {
            settings.seal.rollback_rows.push_back(OTP_DATA_DEFAULT_BOOT_VERSION0_ROW);
            settings.seal.rollback_rows.push_back(OTP_DATA_DEFAULT_BOOT_VERSION1_ROW);
            defaulted = true;
        }
        int num_rows = settings.seal.rollback_rows.size();
        if (num_rows < (settings.seal.rollback_version / 24) + 1) {
            fail(
                ERROR_ARGS, "Rollback version %d requires %d rows - only %d %s",
                settings.seal.rollback_version, (settings.seal.rollback_version / 24) + 1,
                num_rows, defaulted ? "set by default" : "specified"
            );
        }
        std::sort(settings.seal.rollback_rows.begin(), settings.seal.rollback_rows.end());
        for (int i=0; i < num_rows - 1; i++) {
            if (settings.seal.rollback_rows[i+1] < settings.seal.rollback_rows[i] + 3) {
                fail(
                    ERROR_ARGS, "Rollback rows are RBIT3, so must be three rows apart - %x and %x are too close",
                    settings.seal.rollback_rows[i], settings.seal.rollback_rows[i+1]
                );
            }
        }
    }


    private_t private_key = {};
    public_t public_key = {};

    if (settings.seal.sign) read_keys(settings.filenames[2], &public_key, &private_key);

    if (isElf) {
        elf_file source_file(settings.verbose);
        elf_file *elf = &source_file;
        elf->read_file(get_file(ios::in|ios::binary));
        sign_guts_elf(elf, private_key, public_key);

        auto out = get_file_idx(ios::out|ios::binary, 1);
        elf->write(out);
        out->close();
    } else if (isBin) {
        auto access = get_file_memory_access(0);
        auto rmap = access.get_rmap();
        auto ranges = rmap.ranges();
        assert(ranges.size() == 1);
        auto bin_start = ranges[0].from;
        auto bin_size = ranges[0].len();

        auto sig_data = sign_guts_bin(access, private_key, public_key, bin_start, bin_size);
        auto out = get_file_idx(ios::out|ios::binary, 1);
        out->write((const char *)sig_data.data(), sig_data.size());
        out->close();
    } else if (isUf2) {
        auto access = get_file_memory_access(0);
        auto rmap = access.get_rmap();
        auto ranges = rmap.ranges();
        auto bin_start = ranges.front().from;
        auto bin_size = ranges.back().to - bin_start;
        auto family_id = get_family_id(0);

        auto sig_data = sign_guts_bin(access, private_key, public_key, bin_start, bin_size);
        auto tmp = std::make_shared<std::stringstream>();
        tmp->write(reinterpret_cast<const char*>(sig_data.data()), sig_data.size());
        auto out = get_file_idx(ios::out|ios::binary, 1);
        bin2uf2(tmp, out, bin_start, family_id, settings.uf2.abs_block_loc);
        out->close();
    } else {
        fail(ERROR_ARGS, "Must be ELF or BIN");
    }

    if (settings.seal.sign) {
        message_digest_t pub_sha256;
        sha256_buffer(public_key.bytes, sizeof(public_key.bytes), &pub_sha256);
        DEBUG_LOG("PUBLIC KEY SHA256 ");
        for(uint8_t i : pub_sha256.bytes) {
            DEBUG_LOG("%02x", i);
        }
        DEBUG_LOG("\n");

        if (!settings.filenames[3].empty()) {
            if (get_file_type_idx(3) != filetype::json) {
                fail(ERROR_ARGS, "Can only output OTP json");
            }
            auto check_json_file = std::ifstream(settings.filenames[3]);
            json otp_json;
            if (check_json_file.good()) {
                otp_json = json::parse(check_json_file);
                DEBUG_LOG("Appending to existing otp json\n");
                check_json_file.close();
            }
            auto json_out = get_file_idx(ios::out, 3);

            // Add otp bootkey rows
            for (int i = 0; i < 32; ++i) {
                otp_json["bootkey0"][i] = pub_sha256.bytes[i];
            }

            // Add otp fields to enable secure boot
            otp_json["crit1"]["secure_boot_enable"] = 1;
            otp_json["boot_flags1"]["key_valid"] = 1;

            *json_out << std::setw(4) << otp_json << std::endl;
            json_out->close();
        }
    }

    if (!settings.quiet) {
        auto access = get_file_memory_access(1);
        uint32_t id = 0;
        id = get_family_id(1);
        if (id == RP2040_FAMILY_ID) {
            access.set_model(rp2040);
        } else if (id >= RP2350_ARM_S_FAMILY_ID && id <= RP2350_ARM_NS_FAMILY_ID) {
            access.set_model(rp2350);
        }
        fos << "Output File " << settings.filenames[1] << ":\n\n";
        settings.info.show_basic = true;
        info_guts(access, nullptr);
    }

    return false;
}
#endif

bool link_command::execute(device_map &devices) {
    if (get_file_type() != filetype::bin) {
        fail(ERROR_ARGS, "Can only link to BINs");
    }

    if (__builtin_popcount(settings.link.align) != 1) {
        fail(ERROR_ARGS, "Can only pad to powers of 2");
    }

    vector<uint8_t> output;
    vector<std::unique_ptr<block>> first_blocks;
    for (size_t i=1; i < settings.filenames.size(); i++) {
        if (settings.filenames[i].empty()) break;
        if (get_file_type_idx(i) != filetype::bin) {
            fail(ERROR_ARGS, "Can only link BINs");
        }

        auto access = get_file_memory_access(i);
        auto rmap = access.get_rmap();
        auto ranges = rmap.ranges();
        assert(ranges.size() == 1);
        auto bin_start = ranges[0].from;
        auto bin_size = ranges[0].len();

        vector<uint8_t> bin = access.read_vector<uint8_t>(bin_start, bin_size, false);

        std::unique_ptr<block> first_block = find_first_block(bin, bin_start);
        if (!first_block) {
            fail(ERROR_FORMAT, "No first block found");
        }

        first_blocks.push_back(std::move(first_block));
    }

    for (size_t i=0; i < first_blocks.size(); i++) {
        auto access = get_file_memory_access(i+1);
        auto rmap = access.get_rmap();
        auto ranges = rmap.ranges();
        assert(ranges.size() == 1);
        auto bin_start = ranges[0].from;
        auto bin_size = ranges[0].len();

        vector<uint8_t> bin = access.read_vector<uint8_t>(bin_start, bin_size, false);

        std::unique_ptr<block> first_block = find_first_block(bin, bin_start);
        if (!first_block) {
            fail(ERROR_FORMAT, "No first block found");
        }

        auto last_block = get_last_block(bin, bin_start, first_block);
        if (last_block == nullptr || last_block->get_item<image_type_item>() == nullptr) {
            // Use first block instead of last block, as last block doesn't have an image_def
            first_block.swap(last_block);
            fos_verbose << "Using first block, as last block has no image_def\n";
        }

        // Use last block items in new block
        block new_block = place_new_block(bin, bin_start, first_block);
        new_block.items.clear();
        std::copy(last_block->items.begin(),
            last_block->items.end(),
            std::back_inserter(new_block.items));

        if (output.size() > 0) {
            // Add rwd to block, if required
            fos_verbose << "Adding rwd, as output is size " << hex_string(output.size()) << "\n";
            std::shared_ptr<rolling_window_delta_item> rwd = std::make_shared<rolling_window_delta_item>(output.size());
            new_block.items.push_back(rwd);

            if (last_block->get_item<image_type_item>()->cpu() == cpu_arm && last_block->get_item<vector_table_item>() == nullptr) {
                // Add vtor too
                fos_verbose << "Adding vtor too\n";
                std::shared_ptr<vector_table_item> vtor = std::make_shared<vector_table_item>(bin_start);
                new_block.items.push_back(vtor);
            }
        }

        // Link new block to next first block
        if (i+1 != first_blocks.size()) {
            auto next_first_block_rel = (first_blocks[i+1]->physical_addr - bin_start) + (settings.link.align - bin.size() % settings.link.align);
            new_block.next_block_rel = next_first_block_rel;
        } else {
            auto next_first_block_rel = (first_blocks[0]->physical_addr - bin_start) - (output.size() + bin.size());
            new_block.next_block_rel = next_first_block_rel;
        }

        // Add new block to bin
        fos_verbose << "Size before block: " << hex_string(bin.size()) << "\n";
        auto tmp = new_block.to_words();
        std::vector<uint8_t> data = words_to_lsb_bytes(tmp.begin(), tmp.end());
        bin.insert(bin.end(), data.begin(), data.end());
        fos_verbose << "Size after block: " << hex_string(bin.size()) << "\n";

        // Pad 0s in between binaries
        fos_verbose << "Size before padding: " << hex_string(bin.size()) << "\n";
        bin.resize((bin.size() + settings.link.align -1) & ~(settings.link.align -1), 0);
        fos_verbose << "Size after padding: " << hex_string(bin.size()) << "\n";

        // Copy into output
        std::copy(bin.begin(),
              bin.end(),
              std::back_inserter(output));
    }

    auto out = get_file_idx(ios::out|ios::binary, 0);
    out->write((const char *)output.data(), output.size());
    out->close();

    return false;
}

#if HAS_LIBUSB
bool verify_command::execute(device_map &devices) {
    auto file_access = get_file_memory_access(0);
    auto con = get_single_bootsel_device_connection(devices);
    picoboot_memory_access raw_access(con);
    model_t model = get_model(raw_access);
    if (settings.offset_set && get_file_type() != filetype::bin && get_model(raw_access) == rp2040) {
        fail(ERROR_ARGS, "Offset only valid for BIN files");
    }
    auto ranges = get_coalesced_ranges(file_access, model);
    if (settings.range_set) {
        range filter(settings.from, settings.to);
        for(auto& range : ranges) {
            range.intersect(filter);
        }
    }
    ranges.erase(std::remove_if(ranges.begin(), ranges.end(), std::mem_fn(&range::empty)), ranges.end());
    if (ranges.empty()) {
        std::cout << "No ranges to verify.\n";
    } else {
        for (auto mem_range : ranges) {
            enum memory_type t1 = get_memory_type(mem_range.from, model);
            enum memory_type t2 = get_memory_type(mem_range.to, model);
            if (t1 != t2 || t1 == invalid || t1 == sram_unstriped) {
                fail(ERROR_NOT_POSSIBLE, "invalid memory range for verification %08x-%08x", mem_range.from, mem_range.to);
            } else {
                bool ok = true;
                uint32_t pos = mem_range.from;
                {
                    progress_bar bar("Verifying " + memory_names[t1] + ": ");
                    vector<uint8_t> file_buf;
                    vector<uint8_t> device_buf;
                    uint32_t batch_size = 1024;
                    for(uint32_t base = mem_range.from; base < mem_range.to && ok; base += batch_size) {
                        uint32_t this_batch = std::min(mem_range.to - base, batch_size);
                        // note we pass zero_fill = true in case the file has holes, but this does
                        // mean that the verification will fail if those holes are not filled with zeros
                        // on the device
                        file_access.read_into_vector(base, this_batch, file_buf, true);
                        raw_access.read_into_vector(base, this_batch, device_buf);
                        assert(file_buf.size() == device_buf.size());
                        for(unsigned int i=0;i<this_batch;i++) {
                            if (file_buf[i] != device_buf[i]) {
                                pos = base + i;
                                ok = false;
                                break;
                            }
                        }
                        if (ok) {
                            pos = base + this_batch;
                        }
                        bar.progress(pos - mem_range.from, mem_range.to - mem_range.from);
                    }
                }
                if (ok) {
                    std::cout << "  OK\n";
                } else {
                    std::cout << "  First mismatch at " << hex_string(pos) << "\n";
                    uint32_t display_from = (pos - 15) & ~15;
                    uint32_t display_to = display_from + 48;
                    range valid(display_from, display_to);
                    valid.intersect(mem_range);
                    vector<uint8_t> file_buf;
                    vector<uint8_t> device_buf;
                    file_access.read_into_vector(valid.from, valid.to - valid.from, file_buf);
                    raw_access.read_into_vector(valid.from, valid.to - valid.from, device_buf);
                    assert(file_buf.size() == device_buf.size());
                    for(unsigned int l=0;l<3;l++, display_from+=16) {
                        range this_range(display_from, display_from + 16);
                        this_range.intersect(mem_range);
                        if (this_range.empty()) continue;

                        fos.first_column(4);
                        fos.hanging_indent(0);
                        fos << hex_string(display_from);
                        fos.first_column(15);
                        for(int w=0;w<2;w++) {
                            const auto& buf = w ? device_buf : file_buf;
                            std::stringstream line;
                            line << "| ";
                            for (unsigned int p = 0; p < 16; p++) {
                                if (valid.contains(display_from + p)) {
                                    line << hex_string(buf[display_from + p - valid.from], 2, false) << " ";
                                } else {
                                    line << "   ";
                                }
                            }
                            fos << line.str() << "\n";
                        }

                        std::stringstream line;
                        line << "| "; // fos ignores leading whitespace!
                        for (unsigned int p = 0; p < 16; p++) {
                            if (valid.contains(display_from + p) &&
                                file_buf[display_from + p - valid.from] != device_buf[display_from + p - valid.from]) {
                               line << "~~ ";
                            } else {
                               line << "   ";
                            }
                        }
                        fos << line.str() << "\n\n";
                    }
                    fail(ERROR_VERIFICATION_FAILED, "The device contents did not match the file");
                }
            }
        }
    }
    return false;
}
#endif

bool get_json_bcd(json value, int& out) {
    int tmp = 0;
    if (value.is_number_float()) {
        tmp = round(100.0 * value.template get<double>());
    } else if (get_json_int(value, tmp)) {
        tmp *= 100;
    } else {
        return false;
    }
    if (tmp > 9999) {
        return false;
    }

    int rev = 0;
    out = 0;
    int shift = tmp >= 1000 ? 12 : 8;

    // Reverse the number
    while (tmp > 0) {
        rev = rev * 10 + (tmp % 10);
        tmp /= 10;
    }

    // Output each remainder
    while (rev > 0) {
        uint8_t n = (rev % 10) & 0xf;
        out |= n << shift;

        rev /= 10;
        shift -= 4;
    }
    return true;
}

bool utf8_to_utf16(string utf8, vector<uint16_t>& utf16) {
    // Check valid utf8, and check len
    bool unicode = false;
    bool done = true;
    unsigned int ex_len = 0;
    vector<char> cur;
    for (char c : utf8) {
        if (done) {
            if ((c & 0x80) == 0) {
                ex_len = 1;
            } else if ((c & 0xE0) == 0xC0) {
                ex_len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                ex_len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                ex_len = 4;
            } else {
                fail(ERROR_INCOMPATIBLE, "%x is not a valid first character for a unicode string", c);
            }
            done = false;
            if (ex_len > 1) {
                unicode = true;
            }
        } else {
            if ((c & 0xC0) != 0x80) {
                fail(ERROR_INCOMPATIBLE, "%x is not a valid character in a unicode sequence", c);
            }
        }
        cur.push_back(c);
        if (cur.size() == ex_len) {
            done = true;
            switch (ex_len) {
                case 1: {
                    utf16.push_back(cur[0] & 0x7F);
                    break;
                }
                case 2: {
                    utf16.push_back((cur[0] & 0x1F) << 6 | (cur[1] & 0x3F));
                    break;
                }
                case 3: {
                    utf16.push_back((cur[0] & 0x0F) << 12 | (cur[1] & 0x3F) << 6 | (cur[2] & 0x3F));
                    break;
                }
                case 4: {
                    int32_t cp = (cur[0] & 0x07) << 18 | (cur[1] & 0x3F) << 12 | (cur[2] & 0x3F) << 6 | (cur[3] & 0x3F);
                    cp -= 0x10000;
                    utf16.push_back(0xD800 + ((cp >> 10) & 0x3FF));
                    utf16.push_back(0xDC00 + (cp & 0x3FF));
                    break;
                }
            }
            cur.clear();
        }
    }
    return unicode;
}

bool get_json_strdef(json value, int& out, vector<uint16_t>& data, uint8_t max_strlen) {
    if (!value.is_string()) {
        return false;
    }

    uint8_t max_size = 0;

    // Decode as if unicode into UTF-16
    vector<uint16_t> tmp;
    std::setlocale(LC_ALL, "en_US.utf8");
    string str = value;
    bool unicode = utf8_to_utf16(str, tmp);

    if (tmp.size() > max_strlen) {
        DEBUG_LOG("String is too long (%d) - max length is %d\n", (int)tmp.size(), (int)max_strlen);
        return false;
    }

    out = (data.size() << 8) | (tmp.size() & 0x7f) | (unicode ? 0x80 : 0);
    if (unicode) {
        data.insert(data.end(), tmp.begin(), tmp.end());
    } else {
        size_t data_old_size = data.size();
        data.resize(data.size() + ceil(tmp.size()/2.0));
        vector<uint8_t> new_tmp;
        for (auto x : tmp) new_tmp.push_back(x);
        memcpy(data.data() + data_old_size, new_tmp.data(), new_tmp.size());
    }
    return true;
}

otp_field *select_field(uint32_t offset, const std::string& field_selector) {
    return nullptr;
}

struct otp_match {
    explicit otp_match() = default;
    uint32_t reg_row;
    uint32_t mask = 0xffffffff; // 0xffffffff means unknown
    const otp_reg *reg = nullptr;
    const otp_field *field = nullptr;
};

bool get_mask(const std::string& sel, uint32_t &mask, int max_bit) {
    auto dash = sel.find_first_of('-');
    int from = 0, to = 0;
    if (dash != string::npos) {
        bool ok = get_int(sel.substr(0, dash), from) &&
                  get_int(sel.substr(dash+1), to);
        if (!ok || to < from || from < 0 || to >= max_bit) {
            fail(ERROR_ARGS, "Invalid bit-range in selector: %s; expect 'm-n' where m >= 0, n >= m and n <= %d", max_bit-1);
        }
        mask = (2u << to) - (1u << from);
        return true;
    }
    return false;
}

void init_matches(const otp_reg *reg, uint32_t reg_row, const std::string& field_sel, int max_bit,
                  std::function<void(otp_match)> func, bool fuzzy = true) {
    if (!reg) {
        auto f = otp_regs.find(reg_row);
        if (f != otp_regs.end()) {
            reg = &f->second;
        }
    }
    std::vector<otp_match> matches;
    otp_match m;
    m.reg_row = reg_row;
    m.reg = reg;
    m.field = nullptr;
    m.mask = 0; // not matching field
    if (field_sel.empty()) {
        if (reg) m.mask = reg->mask;
        else m.mask = 0xffffffff;
    } else {
        int from = 0;
        if (!get_mask(field_sel, m.mask, max_bit)) {
            if (get_int(field_sel, from)) {
                if (from < 0 || from >= max_bit) {
                    fail(ERROR_ARGS, "Invalid bit-index in selector: %s; expect value from 0 to %d", field_sel.c_str(), max_bit - 1);
                }
                m.mask = 1u << from;
            } else if (reg) {
                // field name
                auto upper_field = uppercase(field_sel);
                for(const auto &f : reg->fields) {
                    if (f.upper_name.find(upper_field) != string::npos && fuzzy) {
                        m.field = &f;
                        m.mask = f.mask;
                        func(m);
                        m.mask = 0;
                    } else if (f.upper_name == upper_field) {
                        m.field = &f;
                        m.mask = f.mask;
                        func(m);
                        m.mask = 0;
                    }
                }
            }
        }
    }
    if (m.mask) func(m);
}

std::map<std::pair<uint32_t,uint32_t>, otp_match> filter_otp(std::vector<string> selectors, int max_bit, bool fuzzy) {
    // inefficient but who cares!?
    std::map<std::pair<uint32_t,uint32_t>, otp_match> matches;
    auto match_adder = [&matches](const otp_match &m) {
        matches.emplace(std::make_pair(m.reg_row, m.mask), m);
    };
    for(const auto &sel : selectors) {
        std::string reg_sel;
        std::string field_sel;
        auto period = sel.find_first_of('.');
        if (period == string::npos) {
            reg_sel = sel;
        } else {
            reg_sel = sel.substr(0, period);
            field_sel = sel.substr(period+1);
        }
        int reg_row = 0;
        if (get_int(reg_sel, reg_row)) {
            // absolute offset
            if (reg_row < 0 || reg_row >= OTP_ROW_COUNT) {
                fail(ERROR_ARGS, "Invalid selector %s; absolute row number must be even and between 0 and 0x%x", sel.c_str(), OTP_ROW_COUNT);
            }
            init_matches(nullptr, reg_row, field_sel, max_bit, match_adder);
        } else {
            auto colon = reg_sel.find_first_of(':');
            if (colon != string::npos) {
                int single_page = 0;
                auto page_sel = reg_sel.substr(0, colon);
                std::vector<int> pages;
                if (page_sel.empty()) {
                    pages.reserve(OTP_PAGE_COUNT);
                    std::generate_n(std::back_insert_iterator<std::vector<int>>(pages), OTP_PAGE_COUNT,
                                    [n = 0]() mutable { return n++; });
                } else if (!get_int(page_sel, single_page) || single_page < 0 || single_page >= OTP_PAGE_COUNT) {
                    fail(ERROR_ARGS, "Invalid selector %s; expected valid page number (0-%d) before ':'", sel.c_str(),
                         OTP_PAGE_COUNT - 1);
                } else {
                    pages.push_back(single_page);
                }
                std::string offset_sel = reg_sel.substr(colon + 1);
                int page_row = 0;
                if (offset_sel.empty()) {
                    for(auto page : pages) {
                        for (reg_row = page * OTP_PAGE_ROWS; reg_row < (page + 1) * OTP_PAGE_ROWS; reg_row++) {
                            init_matches(nullptr, reg_row, field_sel, max_bit, match_adder);
                        }
                    }
                } else if (!get_int(offset_sel, page_row) || page_row < 0 || page_row >= OTP_PAGE_ROWS) {
                    fail(ERROR_ARGS, "Invalid selector %s; page row number must be even and between 0 and 0x%x", sel.c_str(), OTP_PAGE_ROWS);
                } else {
                    for(auto page : pages) {
                        init_matches(nullptr, page * OTP_PAGE_ROWS + page_row, field_sel, max_bit, match_adder);
                    }
                }
            } else {
                auto upper = uppercase(reg_sel);
                for(const auto &e : otp_regs) {
                    if (e.second.upper_name.find(upper, 0) != string::npos && fuzzy) {
                        init_matches(&e.second, e.second.row, field_sel, max_bit, match_adder);
                    } else if (e.second.upper_name == upper || e.second.upper_name == "OTP_DATA_" + upper) {
                        init_matches(&e.second, e.second.row, field_sel, max_bit, match_adder, false);
                    }
                }
            }
        }
    }
    return matches;
}

// todo we could make this popcount at the cost of having this not be Armv6m or adding the popcount instruction to varmulet for bootrom
static uint32_t even_parity(uint32_t input) {
    return __builtin_popcount(input) & 1;
}

// In: 16-bit unsigned integer. Out: 22-bit unsigned integer.
uint32_t __noinline otp_calculate_ecc(uint16_t x) {
    // Source: db_shf40_ap_ab.pdf, page 25, "TABLE 9: PARITY BIT GENERATION MAP
    // FOR 16 BIT USER DATA (X24 SHF MACROCELL)"
    // https://drive.google.com/drive/u/1/folders/1jgU3tZt2BDeGkWUFhi6KZAlaYUpGrFaG
    uint32_t p0 = even_parity(x & 0b1010110101011011);
    uint32_t p1 = even_parity(x & 0b0011011001101101);
    uint32_t p2 = even_parity(x & 0b1100011110001110);
    uint32_t p3 = even_parity(x & 0b0000011111110000);
    uint32_t p4 = even_parity(x & 0b1111100000000000);
    uint32_t p5 = even_parity(x) ^ p0 ^ p1 ^ p2 ^ p3 ^ p4;
    uint32_t p = p0 | (p1 << 1) | (p2 << 2) | (p3 << 3) | (p4 << 4) | (p5 << 5);
    return x | (p << 16);
}

#if HAS_LIBUSB
static void hack_init_otp_regs(picoboot::connection& con) {
    // build map of OTP regs by offset
    if (settings.otp.extra_files.size() > 0) {
        DEBUG_LOG("Using extra OTP files:\n");
        for (auto file : settings.otp.extra_files) {
            DEBUG_LOG("%s\n", file.c_str());
        }
    }
    init_otp(otp_regs, settings.otp.extra_files);
    picoboot_memory_access raw_access(con);
}
bool otp_get_command::execute(device_map &devices) {
    auto con = get_single_rp2350_bootsel_device_connection(devices);
    hack_init_otp_regs(con);
    auto matches = filter_otp(settings.otp.selectors, settings.otp.ecc ? 16 : 24, settings.otp.fuzzy);
    uint32_t last_reg_row = 1; // invalid
    bool first = true;
    char buf[512];
    uint32_t raw_buffer[OTP_PAGE_ROWS];
    memset(raw_buffer, 0xaa, sizeof(raw_buffer));
    int indent0 = settings.otp.list_pages ? 18 : 8;
    uint32_t last_page = -1;
    picoboot_memory_access raw_access(con);
    for (const auto& e : matches) {
        const auto &m = e.second;
        bool do_ecc = settings.otp.ecc;
        int redundancy = settings.otp.redundancy;
        uint32_t corrected_val = 0;
        if (m.reg_row / OTP_PAGE_ROWS != last_page) {
            // todo pre-check page lock
            struct picoboot_otp_cmd otp_cmd;
            if (m.reg_row / OTP_PAGE_ROWS >= 62) {
                // Read individual rows for lock words
                otp_cmd.wRow = m.reg_row;
                otp_cmd.wRowCount = 1;
                otp_cmd.bEcc = 0;
                con.otp_read(&otp_cmd, (uint8_t *)&(raw_buffer[m.reg_row % OTP_PAGE_ROWS]), sizeof(raw_buffer[0]));
            } else {
                // Otherwise read a page at a time
                last_page = m.reg_row / OTP_PAGE_ROWS;
                otp_cmd.wRow = last_page * OTP_PAGE_ROWS;
                otp_cmd.wRowCount = OTP_PAGE_ROWS;
                otp_cmd.bEcc = 0;
                con.otp_read(&otp_cmd, (uint8_t *)raw_buffer, sizeof(raw_buffer));
            }
        }
        if (m.reg_row != last_reg_row) {
            last_reg_row = m.reg_row;
            // Write out header for row
            fos.first_column(0);
            if (!first) fos.wrap_hard();
            first = false;
            fos.hanging_indent(7);
            snprintf(buf, sizeof(buf), "ROW 0x%04x", m.reg_row);
            fos << buf;
            if (settings.otp.list_pages) {
                snprintf(buf, sizeof(buf), " (0x%02x:0x%02x)", m.reg_row / OTP_PAGE_ROWS,
                         m.reg_row % OTP_PAGE_ROWS);
                fos << buf;
            }
            if (m.reg) {
                fos << ": " << m.reg->name;
                if (settings.otp.list_no_descriptions) {
                    if (m.reg->ecc) {
                        fos << " (ECC)";
                    } else if (m.reg->crit) {
                        fos << " (CRIT)";
                    } else if (m.reg->redundancy) {
                        fos << " (RBIT-" << m.reg->redundancy << ")";
                    }
                }
                if (m.reg->seq_length) {
                    fos << " (Part " << (m.reg->seq_index + 1) << "/" << m.reg->seq_length << ")";
                }
                // Set the ecc and redundancy, unless the user specified values
                do_ecc |= (m.reg->ecc && !settings.otp.raw);
                if (redundancy < 0) redundancy = m.reg->redundancy;
            }
            fos << "\n";
            if (m.reg && !settings.otp.list_no_descriptions && !m.reg->description.empty()) {
                fos.first_column(indent0);
                fos.hanging_indent(0);
                fos << "\"" << m.reg->description << "\"";
                fos.first_column(0);
                fos << "\n";
            }
            fos.first_column(4);
            fos.hanging_indent(10);
            uint32_t raw_value = raw_buffer[m.reg_row % OTP_PAGE_ROWS];
            char raw_buf[16 * 1024];
            uint8_t buf_pos = 0;
            buf_pos += snprintf(raw_buf+buf_pos, sizeof(raw_buf), "RAW_VALUE=0x%06x", raw_value);
            for (int i=1; i < std::max(redundancy, 1); i++) {
                raw_value = raw_buffer[(m.reg_row % OTP_PAGE_ROWS) + i];
                buf_pos += snprintf(raw_buf+buf_pos, sizeof(raw_buf) - buf_pos, ";0x%06x", raw_value);
                if (3 == (raw_value >> 22)) {
                    raw_value ^= 0xffffff;
                    snprintf(buf, sizeof(buf), "(flipping raw value to 0x%08x)", raw_value);
                    fos << buf;
                }
            }
            if (do_ecc) {
                corrected_val = otp_calculate_ecc(raw_value &0xffff);
                snprintf(buf, sizeof(buf), "\nVALUE 0x%06x\n", corrected_val);
                fos << buf;
                // todo more clarity over ECC settigns
                // todo recovery
                if (corrected_val != raw_value) {
                    fos << raw_buf;
                    fos << " (WARNING - ECC IS INVALID)";
                }
            } else if (redundancy > 0) {
                uint8_t sets[24] = {};
                uint8_t clears[24] = {};
                bool diff = false;
                bool crit = m.reg ? m.reg->crit : false;
                for (int i=0; i < redundancy; i++) {
                    raw_value = raw_buffer[(m.reg_row % OTP_PAGE_ROWS) + i];
                    for (int b=0; b < 24; b++) raw_value & (1 << b) ? sets[b]++ : clears[b]++;
                }
                for (int b=0; b < 24; b++){
                    if(sets[b] >= clears[b] || (crit && sets[b] >= 3)) corrected_val |= (1 << b);
                    if (sets[b] && clears[b]) diff = true;
                }
                snprintf(buf, sizeof(buf), "\nVALUE 0x%06x\n", corrected_val);
                if (diff) {
                    fos << raw_buf;
                    fos << " (WARNING - REDUNDANT ROWS AREN'T EQUAL)";
                }
                fos << buf;
            } else {
                corrected_val = raw_value;
                snprintf(buf, sizeof(buf), "\nVALUE 0x%06x\n", corrected_val);
                fos << buf;
            }
            fos << "\n";
        }
        if (m.reg) {
            for (const auto &f: m.reg->fields) {
                if (f.mask & m.mask) {
                    fos.first_column(4);
                    fos.hanging_indent(10);
                    // todo should we care if the fields overlap - i think this is fine for here in list
                    int low = __builtin_ctz(f.mask);
                    int high = 31 - __builtin_clz(f.mask);
                    fos << "field " << f.name;
                    if (low == high) {
                        fos << " (bit " << low << ")";
                    } else {
                        fos << " (bits " << low << "-" << high << ")";
                    }
                    snprintf(buf, sizeof(buf), " = %x\n", (corrected_val & f.mask) >> low);
                    fos << buf;
                    if (!settings.otp.list_no_descriptions && !f.description.empty()) {
                        fos.first_column(indent0);
                        fos.hanging_indent(0);
                        fos << "\"" << f.description << "\"";
                        fos.first_column(0);
                        fos << "\n";
                    }
                }
            }
        }
    #if ENABLE_DEBUG_LOG
        if (do_ecc) {
            struct picoboot_otp_cmd otp_cmd;
            otp_cmd.wRow = m.reg_row;
            otp_cmd.bEcc = 1;
            otp_cmd.wRowCount = 1;
            uint16_t val = 0xaaaa;
            con.otp_read(&otp_cmd, (uint8_t *)&val, sizeof(val));
            snprintf(buf, sizeof(buf), "EXTRA ECC READ: %04x\n", val);
            fos << buf;
        }
    #endif
    }
    return false;
}
#endif

#if HAS_LIBUSB
bool otp_dump_command::execute(device_map &devices) {
    auto con = get_single_rp2350_bootsel_device_connection(devices, false);
    // todo pre-check page lock
    struct picoboot_otp_cmd otp_cmd;
    otp_cmd.wRow = 0;
    otp_cmd.wRowCount = OTP_ROW_COUNT;
    otp_cmd.bEcc = settings.otp.ecc && !settings.otp.raw;
    vector<uint8_t> raw_buffer;
    raw_buffer.resize(otp_cmd.wRowCount * (otp_cmd.bEcc ? 2 : 4));
    picoboot_memory_access raw_access(con);
    con.otp_read(&otp_cmd, raw_buffer.data(), raw_buffer.size());
    fos.first_column(0);
    char buf[256];
    for(int i=0;i<OTP_ROW_COUNT;i+=8) {
        snprintf(buf, sizeof(buf), "%04x: ", i);
        fos << buf;
        for (int j = i; j < i + 8; j++) {
            if (otp_cmd.bEcc) {
                snprintf(buf, sizeof(buf), "%04x, ", ((uint16_t *) raw_buffer.data())[j]);
            } else {
                snprintf(buf, sizeof(buf), "%08x, ", ((uint32_t *) raw_buffer.data())[j]);
            }
            fos << buf;
        }
        fos << "\n";
    }
    return false;
}
#endif

#if HAS_LIBUSB
bool partition_info_command::execute(device_map &devices) {
    auto con = get_single_rp2350_bootsel_device_connection(devices, false);

#if SUPPORT_A2
    con.exit_xip();
#endif

    uint8_t loc_flags_id_buf[256];
    uint8_t family_id_name_buf[256];
    uint32_t *loc_flags_id_buf_32 = (uint32_t *)loc_flags_id_buf;
    uint32_t *family_id_name_buf_32 = (uint32_t *)family_id_name_buf;
    picoboot_get_info_cmd cmd;
    cmd.bType = PICOBOOT_GET_INFO_PARTTION_TABLE;
    cmd.dParams[0] = PT_INFO_PT_INFO | PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_PARTITION_ID;
    con.get_info(&cmd, loc_flags_id_buf, sizeof(loc_flags_id_buf));
    unsigned int lfi_pos = 0;
    unsigned int words = loc_flags_id_buf_32[lfi_pos++];
    unsigned int included_fields = loc_flags_id_buf_32[lfi_pos++];
    assert(included_fields == cmd.dParams[0]);
    unsigned int partition_count = loc_flags_id_buf[lfi_pos * 4];
    unsigned int has_pt = loc_flags_id_buf[lfi_pos * 4 + 1];
    lfi_pos++;
    resident_partition_t unpartitioned = *(resident_partition_t *) &loc_flags_id_buf_32[lfi_pos];
    lfi_pos += 2;

    if (!has_pt) {
        printf("there is no partition table\n");
    } else if (!partition_count) {
        printf("the partition table is empty\n");
    }
    printf("un-partitioned_space : ");
    fos << str_permissions(unpartitioned.permissions_and_flags);
    std::vector<std::string> family_ids;
    insert_default_families(unpartitioned.permissions_and_flags, family_ids);
    printf(", uf2 { %s }\n", cli::join(family_ids, ", ").c_str());

    if (has_pt) {
        picoboot_memory_access raw_access(con);
        auto rp2350_version = get_rp2350_version(raw_access);
        printf("partitions:\n");
        for (unsigned int i = 0; i < partition_count; i++) {
            uint32_t location_and_permissions = loc_flags_id_buf_32[lfi_pos++];
            uint32_t flags_and_permissions = loc_flags_id_buf_32[lfi_pos++];
            uint64_t id = 0;
            if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_HAS_ID_BITS) {
                id = loc_flags_id_buf_32[lfi_pos] | ((uint64_t) loc_flags_id_buf_32[lfi_pos + 1] << 32u);
                lfi_pos += 2;
            }
            printf("  %d", i);
            if ((flags_and_permissions & PICOBIN_PARTITION_FLAGS_LINK_TYPE_BITS) ==
                PICOBIN_PARTITION_FLAGS_LINK_TYPE_AS_BITS(A_PARTITION)) {
                printf("(B w/ %d) ", (flags_and_permissions & PICOBIN_PARTITION_FLAGS_LINK_VALUE_BITS)
                        >> PICOBIN_PARTITION_FLAGS_LINK_VALUE_LSB);
            } else if ((flags_and_permissions & PICOBIN_PARTITION_FLAGS_LINK_TYPE_BITS) ==
                    PICOBIN_PARTITION_FLAGS_LINK_TYPE_AS_BITS(OWNER_PARTITION)) {
                    printf("(A ob/ %d)", (flags_and_permissions & PICOBIN_PARTITION_FLAGS_LINK_VALUE_BITS)
                            >> PICOBIN_PARTITION_FLAGS_LINK_VALUE_LSB);
            } else {
                // b_partition doesn't work without the PT loaded
                printf("(A)      ");
            }
            printf(" %08x->%08x",
                   ((location_and_permissions >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB) & 0x1fffu) * 4096,
                   (((location_and_permissions >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB) & 0x1fffu) + 1) *
                   4096);
            if ((location_and_permissions ^ flags_and_permissions) &
                PICOBIN_PARTITION_PERMISSIONS_BITS) {
                printf(" (PERMISSION MISMATCH)");
                return -1;
            }
            unsigned int p = location_and_permissions & flags_and_permissions;
            fos << str_permissions(p);
            if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_HAS_ID_BITS) {
                printf(", id=%016" PRIx64, id);
            }
            uint32_t num_extra_families =
                    (flags_and_permissions & PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_BITS)
                            >> PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_LSB;
            family_ids.clear();
            insert_default_families(flags_and_permissions, family_ids);
            if (num_extra_families | (flags_and_permissions & PICOBIN_PARTITION_FLAGS_HAS_NAME_BITS)) {
                cmd.dParams[0] = PT_INFO_SINGLE_PARTITION | PT_INFO_PARTITION_FAMILY_IDS | PT_INFO_PARTITION_NAME |
                                (i << 24);
                con.get_info(&cmd, family_id_name_buf, sizeof(family_id_name_buf));
                unsigned int got = family_id_name_buf_32[0];
                assert(family_id_name_buf_32[1] == (cmd.dParams[0] & 0xffffffu));
                assert((flags_and_permissions & PICOBIN_PARTITION_FLAGS_HAS_NAME_BITS) ||
                       got == num_extra_families + 1);
                for (unsigned int j = 1; j < num_extra_families + 1; j++) {
                    family_ids.emplace_back(hex_string(family_id_name_buf_32[j + 1]));
                }
                if (flags_and_permissions & PICOBIN_PARTITION_FLAGS_HAS_NAME_BITS) {
                    uint8_t *bytes = &family_id_name_buf[(num_extra_families + 2) * 4];
                    printf(", \"");
                    for (int l = *(bytes++) & 0x7f; l > 0; l--) {
                        putchar(*bytes++);
                    }
                    putchar('"');
                }
            }
            printf(", uf2 { %s }", cli::join(family_ids, ", ").c_str());
            printf(", arm_boot %d", !(flags_and_permissions & PICOBIN_PARTITION_FLAGS_IGNORED_DURING_ARM_BOOT_BITS));
            printf(", riscv_boot %d", !(flags_and_permissions & PICOBIN_PARTITION_FLAGS_IGNORED_DURING_RISCV_BOOT_BITS));
            printf("\n");
        }
    }
    if (settings.family_id) {
        get_target_partition(con);
    }
    return false;
}
#endif

uint32_t permissions_to_flags(json permissions) {
    uint32_t ret = 0;
    if (permissions.contains("secure")) {
        string perms = permissions["secure"];
        if (perms.find("r") != string::npos) ret |= PICOBIN_PARTITION_PERMISSION_S_R_BITS;
        if (perms.find("w") != string::npos) ret |= PICOBIN_PARTITION_PERMISSION_S_W_BITS;
    }
    if (permissions.contains("nonsecure")) {
        string perms = permissions["nonsecure"];
        if (perms.find("r") != string::npos) ret |= PICOBIN_PARTITION_PERMISSION_NS_R_BITS;
        if (perms.find("w") != string::npos) ret |= PICOBIN_PARTITION_PERMISSION_NS_W_BITS;
    }
    if (permissions.contains("bootloader")) {
        string perms = permissions["bootloader"];
        if (perms.find("r") != string::npos) ret |= PICOBIN_PARTITION_PERMISSION_NSBOOT_R_BITS;
        if (perms.find("w") != string::npos) ret |= PICOBIN_PARTITION_PERMISSION_NSBOOT_W_BITS;
    }
    return ret;
}

uint32_t families_to_flags(std::vector<string> families) {
    uint32_t ret = 0;
    for (auto family : families) {
        if (family == data_family_name) {
            ret |= PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_DATA_BITS;
        } else if (family == absolute_family_name) {
            ret |= PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_ABSOLUTE_BITS;
        } else if (family == rp2040_family_name) {
            ret |= PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_RP2040_BITS;
        } else if (family == rp2350_arm_s_family_name) {
            ret |= PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_RP2350_ARM_S_BITS;
        } else if (family == rp2350_arm_ns_family_name) {
            ret |= PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_RP2350_ARM_NS_BITS;
        } else if (family == rp2350_riscv_family_name) {
            ret |= PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_RP2350_RISCV_BITS;
        }
    }
    return ret;
}

bool partition_create_command::execute(device_map &devices) {
    if (get_file_type_idx(0) != filetype::json) {
        fail(ERROR_ARGS, "json must be a json file\n");
    }
    if (settings.filenames[2].empty()) {
        if (!(get_file_type_idx(1) == filetype::bin || get_file_type_idx(1) == filetype::uf2)) {
            fail(ERROR_ARGS, "output must be a BIN/UF2\n");
        }
    } else {
        if (get_file_type_idx(2) != filetype::elf) {
        fail(ERROR_ARGS, "bootloader must be an ELF\n");
    }
    }

    auto file = get_file(ios::in);
    json pt_json = json::parse(*file.get());
    file->close();

    auto partitions = pt_json["partitions"];

    elf_file source_file(settings.verbose);
    elf_file *elf = &source_file;
    std::shared_ptr<block> pt_block;
    if (!settings.filenames[2].empty()) {
        elf->read_file(get_file_idx(ios::in|ios::binary, 2));
        std::unique_ptr<block> first_block = find_first_block(elf);
        if (!first_block) {
            fail(ERROR_FORMAT, "No first block found");
        }

        block new_block = place_new_block(elf, first_block);
        new_block.items.erase(new_block.items.begin(), new_block.items.end());
        pt_block = std::make_shared<block>(new_block);
    } else {
        pt_block = std::make_shared<block>(FLASH_START);
    }

    uint32_t unpartitioned_flags = permissions_to_flags(pt_json["unpartitioned"]["permissions"]) | families_to_flags(pt_json["unpartitioned"]["families"]);
    partition_table_item pt(unpartitioned_flags, settings.partition.singleton);

#if SUPPORT_A2
    if (!(unpartitioned_flags & PICOBIN_PARTITION_FLAGS_ACCEPTS_DEFAULT_FAMILY_ABSOLUTE_BITS)) {
        fail(ERROR_INCOMPATIBLE, "Unpartitioned space must accept the absolute family, for the RP2350-E10 fix to work");
    }
#endif

    uint32_t cur_pos = 2;

    for (auto p : partitions) {
        partition_table_item::partition new_p;
        uint32_t start = cur_pos;
        if (p.contains("start")) get_json_int(p["start"], start);
        int size; get_json_int(p["size"], size);

        if (start >= 4096 || size >= 4096) {
            if (start == cur_pos) start *= 0x1000;
            if (start % 0x1000 || size % 0x1000) {
                fail(ERROR_INCOMPATIBLE, "Partition table start (%dK) and size (%dK) must be 4K aligned", start/1024, size/1024);
            }
            start /= 0x1000;
            size /= 0x1000;
        }

        cur_pos = start + size;
    #if SUPPORT_A2
        if (start <= (settings.uf2.abs_block_loc - FLASH_START)/0x1000 && start + size > (settings.uf2.abs_block_loc - FLASH_START)/0x1000) {
            fail(ERROR_INCOMPATIBLE, "The address %lx cannot be in a partition for the RP2350-E10 fix to work", settings.uf2.abs_block_loc);
        }
    #endif
        new_p.first_sector = start;
        new_p.last_sector = start + size - 1;
        new_p.permissions = permissions_to_flags(p["permissions"]) >> PICOBIN_PARTITION_PERMISSIONS_LSB;
        new_p.flags = families_to_flags(p["families"]);
        uint32_t id = 0;
        auto id_getter = family_id("family_id").set(id);
        for (string family : p["families"]) {
            DEBUG_LOG("Checking %s\n", family.c_str());
            auto ret = id_getter.action(family);
            if (ret.size() > 0) {
                fail(ERROR_FORMAT, "Could not parse family ID from %s: %s", family.c_str(), ret.c_str());
            }
            DEBUG_LOG("Got ID %08x\n", id);
            if (id < RP2040_FAMILY_ID || id > FAMILY_ID_MAX) {
                DEBUG_LOG("Adding extra family\n");
                new_p.extra_families.push_back(id);
            }
            if (new_p.extra_families.size() > PICOBIN_PARTITION_MAX_EXTRA_FAMILIES) {
                fail(ERROR_NOT_POSSIBLE, "Too many extra families - max permitted is %d", PICOBIN_PARTITION_MAX_EXTRA_FAMILIES);
            }
            DEBUG_LOG("Num extra families %d %x\n", (int)new_p.extra_families.size(), (int)(new_p.extra_families.size() << PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_LSB));
            new_p.flags |= new_p.extra_families.size() << PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_LSB;
        }
        DEBUG_LOG("Permissions %08x\n", new_p.permissions);
        if (p.contains("link")) {
            string link_type = p["link"][0];
            int link_value = p["link"][1];
            if (link_type == "a") {
                new_p.flags |= PICOBIN_PARTITION_FLAGS_LINK_TYPE_AS_BITS(A_PARTITION);
            } else if (link_type == "owner") {
                new_p.flags |= PICOBIN_PARTITION_FLAGS_LINK_TYPE_AS_BITS(OWNER_PARTITION);
            } else if (link_type == "none") {

            } else {
                fail(ERROR_INCOMPATIBLE, "Link type \"%s\" is not recognised\n", link_type.c_str());
            }
            new_p.flags |= (link_value << PICOBIN_PARTITION_FLAGS_LINK_VALUE_LSB) & PICOBIN_PARTITION_FLAGS_LINK_VALUE_BITS;
        }
        if (p.contains("name")) { new_p.name = p["name"]; new_p.flags |= PICOBIN_PARTITION_FLAGS_HAS_NAME_BITS; }
        if (p.contains("id")) {
            if (get_json_int(p["id"], new_p.id)) {new_p.flags |= PICOBIN_PARTITION_FLAGS_HAS_ID_BITS;}
            else {string p_id = p["id"]; fail(ERROR_INCOMPATIBLE, "Partition ID \"%s\" is not a valid 64bit integer\n", p_id.c_str());}
        }

        if(p.contains("no_reboot_on_uf2_download")) new_p.flags |= PICOBIN_PARTITION_FLAGS_UF2_DOWNLOAD_NO_REBOOT_BITS;
        if(p.contains("ab_non_bootable_owner_affinity")) new_p.flags |= PICOBIN_PARTITION_FLAGS_UF2_DOWNLOAD_AB_NON_BOOTABLE_OWNER_AFFINITY;
        if(p.contains("ignored_during_riscv_boot")) new_p.flags |= PICOBIN_PARTITION_FLAGS_IGNORED_DURING_RISCV_BOOT_BITS;
        if(p.contains("ignored_during_arm_boot")) new_p.flags |= PICOBIN_PARTITION_FLAGS_IGNORED_DURING_ARM_BOOT_BITS;
        pt.partitions.push_back(new_p);
    }

    pt_block->items.push_back(std::make_shared<partition_table_item>(pt));

    if (pt_json.contains("version")) {
        version_item v(pt_json["version"][0], pt_json["version"][1]);
        pt_block->items.push_back(std::make_shared<version_item>(v));
    }

    // todo workaround for this not being set
    settings.partition.sign = !settings.filenames[3].empty();
    if (settings.partition.hash || settings.partition.sign) {
    #if HAS_MBEDTLS
        DEBUG_LOG(
            "%s%s%s partition table\n",
            settings.partition.hash ? "Hashing" : "",
            (settings.partition.hash && settings.partition.sign) ? " and " : "",
            settings.partition.sign ? "Signing" : ""
        );
        private_t private_key = {};
        public_t public_key = {};
        if (settings.partition.sign) {
            read_keys(settings.filenames[3], &public_key, &private_key);
        }
        hash_andor_sign_block(pt_block.get(), public_key, private_key, settings.partition.hash, settings.partition.sign);
    #else
        fail(ERROR_ARGS, "Cannot sign/hash partition table with no mbedtls\n");
    #endif
    }

    auto out = get_file_idx(ios::out|ios::binary, 1);
    auto tmp = pt_block->to_words();
    std::vector<uint8_t> data = words_to_lsb_bytes(tmp.begin(), tmp.end());
    if (settings.filenames[2].empty()) {
        if (get_file_type_idx(1) == filetype::uf2) {
            uint32_t family_id = ABSOLUTE_FAMILY_ID;
            if (settings.family_id) family_id = settings.family_id;
            uint32_t address = settings.offset_set ? settings.offset : FLASH_START;
            auto tmp = std::make_shared<std::stringstream>();
            tmp->write(reinterpret_cast<const char*>(data.data()), data.size());
            bin2uf2(tmp, out, address, family_id);
        } else {
            out->write(reinterpret_cast<const char*>(data.data()), data.size());
        }
    } else {
        elf->append_segment(pt_block->physical_addr, pt_block->physical_addr, data.size(), ".pt");
        auto pt_section = elf->get_section(".pt");
        assert(pt_section);
        assert(pt_section->virtual_address() == pt_block->physical_addr);

        if (pt_section->size < data.size()) {
            fail(ERROR_UNKNOWN, "Partition Table block is too big for elf section\n");
        }
        DEBUG_LOG("Partition Table section requires %lu bytes of padding\n", pt_section->size - data.size());
        while (data.size() < pt_section->size) {
            data.push_back(0);
        }

        elf->content(*pt_section, data);
        elf->write(out);
    }
    out->close();
    return false;
}

#if HAS_LIBUSB
bool uf2_info_command::execute(device_map &devices) {
    auto con = get_single_rp2350_bootsel_device_connection(devices, false);
    uint32_t buf[5];
    picoboot_get_info_cmd cmd;
    cmd.bType = PICOBOOT_GET_INFO_UF2_STATUS;
    con.get_info(&cmd, (uint8_t *)buf, sizeof(buf));
    vector<pair<string,string>> infos;
    auto info_pair = [&](const string &name, const string &value) {
        if (!value.empty()) {
            infos.emplace_back(std::make_pair(name, value));
        }
    };
    assert(buf[0] == 4);
    uint32_t status = (uint16_t)buf[1];
    uint32_t family_id = buf[2];
    uint32_t valid_block_count = buf[3];
    uint32_t total_block_count = buf[4];
    const int uf2_status_all = UF2_STATUS_ABORT_BAD_ADDRESS | UF2_STATUS_ABORT_EXCLUSIVELY_LOCKED | UF2_STATUS_IGNORED_FAMILY | UF2_STATUS_ABORT_WRITE_ERROR | UF2_STATUS_ABORT_REBOOT_FAILED;
    if (status & ~uf2_status_all) {
        fos << "<invalid>\n";
    } else if (!status && (!family_id || !total_block_count)) {
        fos << "no info found\n";
    } else {
        std::vector<string> aborts;
        info_pair("uf2 family", family_name(family_id));
        info_pair("uf2 blocks downloaded", total_block_count ? (std::to_string(valid_block_count) + " / " + std::to_string(total_block_count)) : "none");
        if (status & UF2_STATUS_ABORT_BAD_ADDRESS) aborts.emplace_back("bad address");
        if (status & UF2_STATUS_ABORT_EXCLUSIVELY_LOCKED) aborts.emplace_back("exclusively locked");
        if (status & UF2_STATUS_ABORT_WRITE_ERROR) aborts.emplace_back("write error");
        if (status & UF2_STATUS_ABORT_REBOOT_FAILED) aborts.emplace_back("reboot failed");
        info_pair("ignored un-placeable family(s)", status & UF2_STATUS_IGNORED_FAMILY ? "true" : "false");
        info_pair("abort reason", aborts.empty() ? "none" : cli::join(aborts, ", "));
    }
    int tab = 0;
    for(const auto& item : infos) {
        tab = std::max(tab, 3 + (int)item.first.length()); // +3 for ":  "
    }
    for(const auto& item : infos) {
        fos.first_column(1);
        fos << (item.first + ":");
        fos.first_column(1 + tab);
        fos << (item.second + "\n");
    }
    return false;
}
#endif


#ifndef count_of
#define count_of(x) (sizeof(x) / sizeof((x)[0]))
#endif

bool uf2_convert_command::execute(device_map &devices) {
    if (get_file_type_idx(1) != filetype::uf2) {
        fail(ERROR_ARGS, "Output must be a UF2 file\n");
    }

    uint32_t family_id = get_family_id(0);

    auto in = get_file(ios::in|ios::binary);
    auto out = get_file_idx(ios::out|ios::binary, 1);
    #if SUPPORT_A2
    // RP2350-E10 : add absolute block
    if (settings.uf2.abs_block) {
        fos << "RP2350-E10: Adding absolute block to UF2 targeting " << hex_string(settings.uf2.abs_block_loc) << "\n";
    } else {
        settings.uf2.abs_block_loc = 0;
    }
    #endif
    if (get_file_type() == filetype::elf) {
        uint32_t package_address = settings.offset_set ? settings.offset : 0;
        elf2uf2(in, out, family_id, package_address, settings.uf2.abs_block_loc);
    } else if (get_file_type() == filetype::bin) {
        uint32_t address = settings.offset_set ? settings.offset : FLASH_START;
        bin2uf2(in, out, address, family_id, settings.uf2.abs_block_loc);
    } else {
        fail(ERROR_ARGS, "Convert currently only from ELF/BIN to UF2\n");
    }
    out->close();

    return false;
}


// Dissassembly helpers
string gpiodir(int val) {
    switch(val/4) {
        case 0: return "out";
        case 1: return "oe";
        case 2: return "in";
        default: return "unknown";
    }
}

string gpiohilo(int val) {
    switch(val % 4) {
        case 0: return "lo_" + gpiodir(val);
        case 1: return "hi_" + gpiodir(val);
        default: return "unknown";
    }
}

string gpiopxsc(int val) {
    switch(val) {
        case 0: return "put";
        case 1: return "xor";
        case 2: return "set";
        case 3: return "clr";
        default: return "unknown";
    }
}
    
string gpioxsc2(int val) {
    return gpiopxsc(val - 4) + (val > 4 ? "2" : "");
}

string gpioxsc(int val) {
    return gpiopxsc(val - 4);
}

string cpu_reg(int val) {
    if (val < 0xa) return "r" + std::to_string(val);
    switch (val) {
        case 0xa: return "sl";
        case 0xb: return "fp";
        case 0xc: return "ip";
        case 0xd: return "sp";
        case 0xe: return "lr";
        // PC is not valid for coprocessor operations - the value 15 corresponds to APSR_nzcv instead
        case 0xf: return "APSR_nzcv";
        default: return "unknown";
    }
}


bool coprodis_command::execute(device_map &devices) {
    auto in = get_file(ios::in);

    std::stringstream buffer;
    buffer << in->rdbuf();
    auto contents = buffer.str();

    std::regex instruction(
        R"(([ 0-9a-f]{8}):\s*([0-9a-f]{2})(\s*)([0-9a-f]{2})\s+([0-9a-f]{2})\s*([0-9a-f]{2})\s*(.*))"
    );
    vector<vector<string>> insts = {};
    vector<tuple<string,string,string>> proc_insts;
    while (true)
    {
        std::smatch sm;
        if (!std::regex_search(contents, sm, instruction)) break;
        vector<string> tmp;
        std::copy(sm.begin(), sm.end(), std::back_inserter(tmp));
        insts.push_back(tmp);
        contents = sm.suffix();
    }

    for (vector<string> sm : insts) {
        uint32_t val;
        if (sm.size() < 7) {
            fos << sm[0] << " was too small\n";
            continue;
        }
        if (sm[3].length()) {
            // Clang
            int b0, b1, b2, b3 = 0;
            get_int("0x" + sm[5], b0);
            get_int("0x" + sm[6], b1);
            get_int("0x" + sm[2], b2);
            get_int("0x" + sm[4], b3);
            val = b0 + (b1 << 8) + (b2 << 16) + (b3 << 24);
        } else {
            // GCC
            int b0, b1 = 0;
            get_int("0x" + sm[5] + sm[6], b0);
            get_int("0x" + sm[2] + sm[4], b1);
            val = b0 + (b1 << 16);
        }

        uint32_t mcrbits = val & 0xff100010;
        bool mcr = false;
        uint32_t mccrbits = (val & 0xfff00000) >> 20;
        bool mccr = false;
        uint32_t cdpbits = val & 0xff000010;
        bool cdp = false;
        string inst;

        switch (mcrbits) {
            case 0xee000010:
                mcr = true;
                inst = "mcr";
                break;
            case 0xfe000010:
                mcr = true;
                inst = "mcr2";
                break;
            case 0xee100010:
                mcr = true;
                inst = "mrc";
                break;
            case 0xfe100010:
                mcr = true;
                inst = "mrc2";
                break;
            default:
                break;
        }

        switch (mccrbits) {
            case 0xec4:
                mccr = true;
                inst = "mcrr";
                break;
            case 0xfc4:
                mccr = true;
                inst = "mcrr2";
                break;
            case 0xec5:
                mccr = true;
                inst = "mrrc";
                break;
            case 0xfc5:
                mccr = true;
                inst = "mrrc2";
                break;
            default:
                break;
        }

        switch (cdpbits) {
            case 0xee000000:
                cdp = true;
                inst = "cdp";
                break;
            default:
                break;
        }

        char rep[512] = "";

        if (mcr) {
            uint8_t opc1 = (val >> (16+5)) & 0x7;
            uint8_t CRn = (val >> 16) & 0xf;
            uint8_t Rt = (val >> 12) & 0xf;
            uint8_t coproc = (val >> 8) & 0xf;
            uint8_t opc2 = (val >> 5) & 0x7;
            uint8_t CRm = val & 0xf;

            if (coproc == 0) {
                // GPIO
                if (CRn != 0 || opc1 >= 8) {
//                    fail(ERROR_INCOMPATIBLE,
//                         "Instruction %s %d, #%d, %s, c%d, c%d, #%d is not supported by GPIO Coprocessor",
//                         inst.c_str(), coproc, opc1, cpu_reg(Rt).c_str(), CRn, CRm, opc2
//                    );
                    printf("WARNING: Instruction %s %d, #%d, %s, c%d, c%d, #%d is not supported by GPIO Coprocessor\n",
                         inst.c_str(), coproc, opc1, cpu_reg(Rt).c_str(), CRn, CRm, opc2
                    );
                    continue;
                }
                if (inst == "mcr") {
                    if (opc1 < 4) {
                        snprintf(rep, sizeof(rep), "gpioc_%s_%s %s",
                            gpiohilo(CRm).c_str(), gpiopxsc(opc1).c_str(), cpu_reg(Rt).c_str()
                        );
                    } else {
                        snprintf(rep, sizeof(rep), "gpioc_%s_%s %s",
                            gpiodir(CRm).c_str(), gpioxsc(opc1).c_str(), cpu_reg(Rt).c_str()
                        );
                    }
                } else {
                    snprintf(rep, sizeof(rep), "gpioc_%s_get %s",
                        gpiohilo(CRm).c_str(), cpu_reg(Rt).c_str()
                    );
                }
            } else if (coproc == 4 || coproc == 5) {
                // DCP
                bool ns = coproc == 5;
                if (inst == "mrc" || inst == "mrc2") {
                    bool isP = inst == "mrc2";
                    switch (CRm) {
                        case 0:
                            switch (opc2) {
                                case 0:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sxvd %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                case 1:
                                    snprintf(rep, sizeof(rep), "dcp%s_%scmp %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                default:
                                    continue;
                            }
                            break;
                        case 2:
                            switch (opc2) {
                                case 0:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdfa %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                case 1:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdfs %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                case 2:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdfm %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                case 3:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdfd %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                case 4:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdfq %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                case 5:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdfg %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                default:
                                    continue;
                            }
                            break;
                        case 3:
                            switch (opc2) {
                                case 0:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdic %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                case 1:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sduc %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str()
                                    );
                                    break;
                                default:
                                    continue;
                            }
                            break;
                        default:
                            continue;
                    }
                }
            } else if (coproc == 7) {
                // RCP
                if (inst == "mcr" || inst == "mcr2") {
                    bool delay = inst == "mcr";
                    switch (opc1) {
                        case 0:
                            snprintf(rep, sizeof(rep), "rcp_canary_check %s, 0x%02x (%d), %sdelay",
                                cpu_reg(Rt).c_str(), CRn*16 + CRm, CRn*16 + CRm, delay ? "" : "no"
                            );
                            break;
                        case 1:
                            snprintf(rep, sizeof(rep), "rcp_bvalid %s, %sdelay",
                                cpu_reg(Rt).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 2:
                            snprintf(rep, sizeof(rep), "rcp_btrue %s, %sdelay",
                                cpu_reg(Rt).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 3:
                            snprintf(rep, sizeof(rep), "rcp_bfalse %s, %sdelay",
                                cpu_reg(Rt).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 4:
                            snprintf(rep, sizeof(rep), "rcp_count_set 0x%02x (%d), %sdelay",
                                CRn*16 + CRm, CRn*16 + CRm, delay ? "" : "no"
                            );
                            break;
                        case 5:
                            snprintf(rep, sizeof(rep), "rcp_count_check 0x%02x (%d), %sdelay",
                                CRn*16 + CRm, CRn*16 + CRm, delay ? "" : "no"
                            );
                            break;
                        default:
                            continue;
                    }
                } else {
                    bool delay = inst == "mrc";
                    switch (opc1) {
                        case 0:
                            snprintf(rep, sizeof(rep), "rcp_canary_get %s, 0x%02x (%d), %sdelay",
                                cpu_reg(Rt).c_str(), CRn*16 + CRm, CRn*16 + CRm, delay ? "" : "no"
                            );
                            break;
                        case 1:
                            snprintf(rep, sizeof(rep), "rcp_canary_status %s, %sdelay",
                                cpu_reg(Rt).c_str(), delay ? "" : "no"
                            );
                            break;
                        default:
                            continue;
                    }
                }
            }
        } else if (mccr) {
            uint8_t Rt2 = (val >> 16) & 0xf;
            uint8_t Rt = (val >> 12) & 0xf;
            uint8_t coproc = (val >> 8) & 0xf;
            uint8_t opc1 = (val >> 4) & 0xf;
            uint8_t CRm = val & 0xf;

            if (coproc == 0) {
                // GPIO
                if (opc1 >= 12) { // || CRm not in [0, 4, 8]):
//                    fail(ERROR_INCOMPATIBLE,
//                         "Instruction %s %d, #%d, %s, %s, c%d is not supported by GPIO Coprocessor",
//                         inst.c_str(), coproc, opc1, cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), CRm
//                    );
#ifndef NDEBUG
                    printf("WARNING: Instruction %s %d, #%d, %s, %s, c%d is not supported by GPIO Coprocessor\n",
                         inst.c_str(), coproc, opc1, cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), CRm
                    );
#endif
                    continue;
                }
                if (inst == "mcrr") {
                    if (opc1 < 4) {
                        snprintf(rep, sizeof(rep), "gpioc_hilo_%s_%s %s, %s",
                            gpiodir(CRm).c_str(), gpiopxsc(opc1).c_str(), cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                        );
                    } else if (opc1 < 8) {
                        snprintf(rep, sizeof(rep), "gpioc_bit_%s_%s %s, %s",
                            gpiodir(CRm).c_str(), gpioxsc2(opc1).c_str(), cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                        );
                    } else {
                        snprintf(rep, sizeof(rep), "gpioc_index_%s_%s %s, %s",
                            gpiodir(CRm).c_str(), gpiopxsc(opc1 - 8).c_str(), cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                        );
                    }
                } else {
                    snprintf(rep, sizeof(rep), "gpioc_index_%s_get %s, %s",
                        gpiodir(CRm).c_str(), cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                    );
                }
            } else if (coproc == 4 || coproc == 5) {
                // DCP
                bool ns = coproc == 5;
                if (inst == "mcrr") {
                    switch (opc1) {
                        case 0:
                            switch (CRm) {
                                case 0:
                                    snprintf(rep, sizeof(rep), "dcp%s_wxmd %s, %s",
                                        ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 1:
                                    snprintf(rep, sizeof(rep), "dcp%s_wymd %s, %s",
                                        ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 2:
                                    snprintf(rep, sizeof(rep), "dcp%s_wefd %s, %s",
                                        ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                default:
                                    continue;
                            }
                            break;
                        case 1:
                            switch (CRm) {
                                case 0:
                                    snprintf(rep, sizeof(rep), "dcp%s_wxup %s, %s",
                                        ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 1:
                                    snprintf(rep, sizeof(rep), "dcp%s_wyup %s, %s",
                                        ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 2:
                                    snprintf(rep, sizeof(rep), "dcp%s_wxyu %s, %s",
                                        ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                default:
                                    continue;
                            }
                            break;
                        case 2:
                            snprintf(rep, sizeof(rep), "dcp%s_wxms %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 3:
                            snprintf(rep, sizeof(rep), "dcp%s_wxmo %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 4:
                            snprintf(rep, sizeof(rep), "dcp%s_wxdd %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 5:
                            snprintf(rep, sizeof(rep), "dcp%s_wxdq %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 6:
                            snprintf(rep, sizeof(rep), "dcp%s_wxuc %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 7:
                            snprintf(rep, sizeof(rep), "dcp%s_wxic %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 8:
                            snprintf(rep, sizeof(rep), "dcp%s_wxdc %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 9:
                            snprintf(rep, sizeof(rep), "dcp%s_wxfc %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 10:
                            snprintf(rep, sizeof(rep), "dcp%s_wxfm %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 11:
                            snprintf(rep, sizeof(rep), "dcp%s_wxfd %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 12:
                            snprintf(rep, sizeof(rep), "dcp%s_wxfq %s, %s",
                                ns ? "ns" : "", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        default:
                            continue;
                    }
                } else if (inst == "mrrc" || inst == "mrrc2") {
                    bool isP = inst == "mrrc2";
                    switch (CRm) {
                        case 0:
                            switch (opc1) {
                                case 1:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdda %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 3:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sdds %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 5:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sddm %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 7:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sddd %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 9:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sddq %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 11:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sddg %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                default:
                                    continue;
                            }
                            break;
                        case 1:
                            switch (opc1) {
                                case 1:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sxyh %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 2:
                                    snprintf(rep, sizeof(rep), "dcp%s_%symr %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                case 4:
                                    snprintf(rep, sizeof(rep), "dcp%s_%sxmq %s, %s",
                                        ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                                    );
                                    break;
                                default:
                                    continue;
                            }
                            break;
                        case 4:
                            snprintf(rep, sizeof(rep), "dcp%s_%sxms %s, %s, #0x%01x",
                                ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), opc1
                            );
                            break;
                        case 5:
                            snprintf(rep, sizeof(rep), "dcp%s_%syms %s, %s, #0x%01x",
                                ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), opc1
                            );
                            break;
                        case 8:
                            snprintf(rep, sizeof(rep), "dcp%s_%sxmd %s, %s",
                                ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 9:
                            snprintf(rep, sizeof(rep), "dcp%s_%symd %s, %s",
                                ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        case 10:
                            snprintf(rep, sizeof(rep), "dcp%s_%sefd %s, %s",
                                ns ? "ns" : "", isP ? "p" : "r", cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str()
                            );
                            break;
                        default:
                            continue;
                    }
                }
            } else if (coproc == 7) {
                // RCP
                if (inst == "mcrr" || inst == "mcrr2") {
                    bool delay = inst == "mcrr";
                    switch (opc1) {
                        case 0:
                            snprintf(rep, sizeof(rep), "rcp_b2valid %s, %s, %sdelay",
                                cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 1:
                            snprintf(rep, sizeof(rep), "rcp_b2and %s, %s, %sdelay",
                                cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 2:
                            snprintf(rep, sizeof(rep), "rcp_b2or %s, %s, %sdelay",
                                cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 3:
                            snprintf(rep, sizeof(rep), "rcp_bxorvalid %s, %s, %sdelay",
                                cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 4:
                            snprintf(rep, sizeof(rep), "rcp_bxortrue %s, %s, %sdelay",
                                cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 5:
                            snprintf(rep, sizeof(rep), "rcp_bxorfalse %s, %s, %sdelay",
                                cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 6:
                            snprintf(rep, sizeof(rep), "rcp_ivalid %s, %s, %sdelay",
                                cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 7:
                            snprintf(rep, sizeof(rep), "rcp_iequal %s, %s, %sdelay",
                                cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        case 8:
                            snprintf(rep, sizeof(rep), "rcp_salt_core%d %s, %s, %sdelay",
                                CRm, cpu_reg(Rt).c_str(), cpu_reg(Rt2).c_str(), delay ? "" : "no"
                            );
                            break;
                        default:
                            continue;
                    }
                }
            }
        } else if (cdp) {
            uint8_t opc1 = (val >> 20) & 0xf;
            uint8_t CRn = (val >> 16) & 0xf;
            uint8_t CRd = (val >> 12) & 0xf;
            uint8_t coproc = (val >> 8) & 0xf;
            uint8_t opc2 = (val >> 5) & 0x7;
            uint8_t CRm = val & 0xf;

            if (coproc == 0) {
                // GPIO has no cdp instructions
                continue;
            } else if (coproc == 4|| coproc == 5) {
                // DCP
                bool ns = coproc == 5;
                switch (opc1) {
                    case 0:
                        if (CRm == 0) {
                            snprintf(rep, sizeof(rep), "dcp%s_init", ns ? "ns" : "");
                        } else {
                            snprintf(rep, sizeof(rep), "dcp%s_add0", ns ? "ns" : "");
                        }
                        break;
                    case 1:
                        if (opc2 == 0) {
                            snprintf(rep, sizeof(rep), "dcp%s_add1", ns ? "ns" : "");
                        } else {
                            snprintf(rep, sizeof(rep), "dcp%s_sub1", ns ? "ns" : "");
                        }
                        break;
                    case 2:
                        snprintf(rep, sizeof(rep), "dcp%s_sqr0", ns ? "ns" : "");
                        break;
                    case 8:
                        if (CRm == 2 && opc2 == 0) {
                            snprintf(rep, sizeof(rep), "dcp%s_norm", ns ? "ns" : "");
                        } else if (CRm == 2 && opc2 == 1) {
                            snprintf(rep, sizeof(rep), "dcp%s_nrdf", ns ? "ns" : "");
                        } else if (CRm == 0 && opc2 == 1) {
                            snprintf(rep, sizeof(rep), "dcp%s_nrdd", ns ? "ns" : "");
                        } else if (CRm == 0 && opc2 == 2) {
                            snprintf(rep, sizeof(rep), "dcp%s_ntdc", ns ? "ns" : "");
                        } else if (CRm == 0 && opc2 == 3) {
                            snprintf(rep, sizeof(rep), "dcp%s_nrdc", ns ? "ns" : "");
                        } else {
                            continue;
                        }
                        break;
                    default:
                        continue;
                }
            } else if (coproc == 7) {
                // RCP
                if (opc1 == 0 && CRd == 0 && CRn == 0 && CRm == 0) {
                    snprintf(rep, sizeof(rep), "rcp_panic");
                } else {
                    continue;
                }
            }
        } else {
            continue;
        }

        if (strlen(rep) > 0) {
            tuple<string,string,string> proc = {sm.front(), sm.back(), rep};
            proc_insts.push_back(proc);
        }
    }

    auto out = get_file_idx(ios::out, 1);

    if (proc_insts.size() == 0) {
        *out << buffer.str();
        return false;
    }

    string line;
    fos << "Replacing " << proc_insts.size() << " instructions\n";
    while (getline(buffer, line)) {
        if (!proc_insts.empty() && line == std::get<0>(proc_insts[0])) {
            fos << "\nFound instruction\n";
            fos << line;
            fos << "\n";

            string olds = std::get<1>(proc_insts[0]);
            string news = std::get<2>(proc_insts[0]);

            line.replace(line.find(olds), olds.length(), news);
            fos << "Replaced with\n";
            fos << line;
            fos << "\n";
            proc_insts.erase(proc_insts.begin());
        }
        *out << line << "\n";
    }

    for (auto v : proc_insts) {
        fos << std::get<0>(v) + " : "  + std::get<1>(v) + " : " + std::get<2>(v) + "\n\n\n";
    }

    return false;
}


#if HAS_LIBUSB
static void check_otp_write_error(picoboot::command_failure &e, bool ecc) {
    if (e.get_code() == PICOBOOT_UNSUPPORTED_MODIFICATION) {
        if (ecc) fail(ERROR_NOT_POSSIBLE, "Attempted to modify OTP ECC row(s)\n");
        else     fail(ERROR_NOT_POSSIBLE, "Attempted to clear bits in OTP row(s)\n");
    }
}

bool otp_load_command::execute(device_map &devices) {
    auto con = get_single_rp2350_bootsel_device_connection(devices, false);
    // todo pre-check page lock
    struct picoboot_otp_cmd otp_cmd;
    std::shared_ptr<std::fstream> file = get_file(ios::in|ios::binary);
    if (get_file_type() == filetype::json) {
        hack_init_otp_regs(con);
        json otp_json = json::parse(*file);
        int hex_val = 0;
        // todo validation on json
        for (auto row : otp_json.items()) {
            fos.first_column(0);
            string row_key = row.key();
            auto row_value = row.value();
            fos << row_key << ":\n";

            // Find matching OTP row
            bool is_sequence = false;
            auto row_matches = filter_otp({row_key}, 24, true);
            if (row_matches.size() == 0) {
                fail(ERROR_INCOMPATIBLE, "%s does not match an otp row", row_key.c_str());
            } else if (row_matches.size() != 1) {
                // Check if it is a sequence
                auto row_seq0_matches = filter_otp({row_key + "0"}, 24, false);
                auto row_seq_0_matches = filter_otp({row_key + "_0"}, 24, false);
                if (row_seq0_matches.size() == 1) {
                    row_matches = row_seq0_matches;
                } else if (row_seq_0_matches.size() == 1) {
                    row_matches = row_seq_0_matches;
                }
                else if (row_matches.size() != (*row_matches.begin()).second.reg->seq_length) {
                    for (auto x : row_matches) {
                        DEBUG_LOG("Matches %s\n", x.second.reg->name.c_str());
                    }
                    fail(ERROR_INCOMPATIBLE, "%s matches multiple otp rows or sequences", row_key.c_str());
                }
                is_sequence = true;
            }
            auto row_match = *row_matches.begin();
            auto reg = row_match.second.reg;
            vector<uint8_t> data;
            unsigned int row_size = 0;

            fos.first_column(2);

            if (reg != nullptr) {
                otp_cmd.wRow = row_match.second.reg_row;
                otp_cmd.wRowCount = is_sequence ? reg->seq_length : (reg->redundancy ? reg->redundancy : 1);
                otp_cmd.bEcc = reg->ecc;
                row_size = otp_cmd.bEcc ? 2 : 4;

                // Calculate row value
                if (row_value.is_object()) {
                    // Specified fields
                    struct picoboot_otp_cmd tmp_otp_cmd = otp_cmd;
                    tmp_otp_cmd.wRowCount = 1;
                    tmp_otp_cmd.bEcc = 0;
                    uint32_t old_raw_value;
                    picoboot_memory_access raw_access(con);
                    con.otp_read(&tmp_otp_cmd, (uint8_t *)&old_raw_value, sizeof(old_raw_value));

                    uint32_t reg_value = 0;
                    uint32_t full_mask = 0;
                    for (auto field_val : row_value.items()) {
                        string key = field_val.key();
                        auto value = field_val.value();
                        if (!get_json_int(value, hex_val)) {
                            fail(ERROR_FORMAT, "Values must be integers");
                        }

                        // Find matching OTP field
                        auto field_matches = filter_otp({row_key + "." + key}, 24, false);
                        if (field_matches.size() != 1) {
                            fail(ERROR_INCOMPATIBLE, "%s is not a single otp field", key.c_str());
                        }
                        auto field_match = *field_matches.begin();
                        auto field = field_match.second.field;

                        fos << key << ": " << hex_string(hex_val) << "\n";

                        int low = __builtin_ctz(field->mask);

                        if (hex_val & (~field->mask >> low)) {
                            fail(ERROR_NOT_POSSIBLE, "Value to set does not fit in field: value %06x, mask %06x\n", hex_val, field->mask >> low);
                        }

                        hex_val <<= low;
                        hex_val &= field->mask;
                        full_mask |= field->mask;
                        reg_value |= hex_val;
                    }
                    reg_value |= old_raw_value & ~full_mask;
                    vector<uint8_t> tmp((uint8_t*)(&reg_value), (uint8_t*)(&reg_value) + row_size);
                    data.insert(data.begin(), tmp.begin(), tmp.end());
                } else if (row_value.is_array()) {
                    for (auto val : row_value)  {
                        if (!get_json_int(val, hex_val)) {
                            fail(ERROR_FORMAT, "Values must be integers");
                        }
                        fos << hex_string(hex_val, 2) << ", ";
                        data.push_back(hex_val);
                    }
                    fos << "\n";
                } else {
                    if (!get_json_int(row_value, hex_val)) {
                        fail(ERROR_FORMAT, "Values must be integers");
                    }
                    fos << hex_string(hex_val) << "\n";
                    vector<uint8_t> tmp((uint8_t*)(&hex_val), (uint8_t*)(&hex_val) + row_size);
                    data.insert(data.begin(), tmp.begin(), tmp.end());
                }
            } else {
                // Must be a raw address
                otp_cmd.wRow = row_match.second.reg_row;
                otp_cmd.wRowCount = 1;
                otp_cmd.bEcc = row_value["ecc"].is_boolean() ? (bool)row_value["ecc"] : false;
                row_size = otp_cmd.bEcc ? 2 : 4;

                auto val = row_value["value"];
                if (val.is_array()) {
                    for (auto v : val)  {
                        if (!get_json_int(v, hex_val)) {
                            fail(ERROR_FORMAT, "Values must be integers");
                        }
                        fos << hex_string(hex_val, 2) << ", ";
                        data.push_back(hex_val);
                    }
                    fos << "\n";
                    otp_cmd.wRowCount = data.size() / row_size;
                } else {
                    if (!get_json_int(val, hex_val)) {
                        fail(ERROR_FORMAT, "Values must be integers");
                    }
                    fos << hex_string(hex_val) << "\n";
                    vector<uint8_t> tmp((uint8_t*)(&hex_val), (uint8_t*)(&hex_val) + row_size);
                    data.insert(data.begin(), tmp.begin(), tmp.end());
                    if (get_json_int(row_value["redundancy"], hex_val)) otp_cmd.wRowCount = hex_val;
                }
            }

            if (data.size() % row_size) {
                fail(ERROR_FORMAT, "Data size must be a multiple of selected row data size (%d)", row_size);
            }
            if (data.size() == row_size && otp_cmd.wRowCount > 1) {
                // Repeat the data for each redundant row
                data.resize(otp_cmd.wRowCount * row_size);
                for (int i=1; i < otp_cmd.wRowCount; i++) std::copy_n(data.begin(), row_size, data.begin() + row_size*i);
            }

            if (data.size() != row_size * otp_cmd.wRowCount) {
                fail(ERROR_FORMAT, "Data size must be selected row data size * row count (%d*%d)", row_size, otp_cmd.wRowCount);
            }

            try {
                con.otp_write(&otp_cmd, data.data(), data.size());
            } catch (picoboot::command_failure &e) {
                check_otp_write_error(e, otp_cmd.bEcc);
                throw e;
            }
        }

        // Return now, don't do rest of function
        return false;
    }
    otp_cmd.wRow = settings.otp.row;
    otp_cmd.bEcc = settings.otp.ecc && !settings.otp.raw;
    unsigned int row_size = otp_cmd.bEcc ? 2 : 4;
    file->seekg(0, ios::end);
    uint32_t file_size = file->tellg();
    if (file_size % row_size) {
        fail(ERROR_FORMAT, "File size must be a multiple of selected row data size (%d)", row_size);
    }
    unsigned int rows = file_size / row_size;
    if (rows < 1 || otp_cmd.wRow + rows > OTP_ROW_COUNT) {
        fail(ERROR_FORMAT, "OTP data will not fit starting at row %d\n", otp_cmd.wRow);
    }
    otp_cmd.wRowCount = rows;
    file->seekg(0, ios::beg);

    std::unique_ptr<uint8_t[]> unique_file_buffer(new uint8_t[file_size]());
    uint8_t* file_buffer = unique_file_buffer.get();
    file->read((char*)file_buffer, file_size);
    try {
        con.otp_write(&otp_cmd, (uint8_t *)file_buffer, file_size);
    } catch (picoboot::command_failure &e) {
        check_otp_write_error(e, otp_cmd.bEcc);
        throw e;
    }

    std::unique_ptr<uint8_t[]> unique_verify_buffer(new uint8_t[file_size]());
    uint8_t* verify_buffer = unique_verify_buffer.get();
    picoboot_memory_access raw_access(con);
    con.otp_read(&otp_cmd, (uint8_t *)verify_buffer, sizeof(verify_buffer));
    unsigned int i;
    for(i=0;i<file_size;i++) {
        if (file_buffer[i] != verify_buffer[i]) {
            std::cout << "  Mismatch at row " << hex_string(i/row_size) << "\n";
            break;
        }
    }
    if (i == file_size) {
        std::cout << "  Verified OK\n";
    }
    return false;
}
#endif

bool otp_list_command::execute(device_map &devices) {
    init_otp(otp_regs, settings.otp.extra_files);

    auto matches = filter_otp(settings.otp.selectors.empty() ? std::vector<string>({":"}) : settings.otp.selectors, 24, true);
    uint32_t last_reg_row = 1; // invalid
    bool first = true;
    char buf[512];
    int indent0 = settings.otp.list_pages ? 18 : 8;
    for (const auto& e : matches) {
        const auto &m = e.second;
        if (!m.reg) continue;
        auto write_reg_header_if_necessary = [&] {
            // lazy row header output
            if (m.reg_row != last_reg_row && m.reg) {
                last_reg_row = m.reg_row;
                // Write out header for row
                fos.first_column(0);
                if (!first) fos.wrap_hard();
                first = false;
                fos.hanging_indent(7);
                snprintf(buf, sizeof(buf), "ROW 0x%04x", m.reg_row);
                fos << buf;
                if (settings.otp.list_pages) {
                    snprintf(buf, sizeof(buf), " (0x%02x:0x%02x)", m.reg_row / OTP_PAGE_ROWS, m.reg_row % OTP_PAGE_ROWS);
                    fos << buf;
                }
                fos << ": " << m.reg->name;
                if (settings.otp.list_no_descriptions) {
                    if (m.reg->ecc) {
                        fos << " (ECC)";
                    } else if (m.reg->crit) {
                        fos << " (CRIT)";
                    } else if (m.reg->redundancy) {
                        fos << " (RBIT-" << m.reg->redundancy << ")";
                    }
                }
                if (m.reg->seq_length) {
                    fos << " (Part " << (m.reg->seq_index + 1) << "/" << m.reg->seq_length << ")";
                }
                fos << "\n";
                if (!settings.otp.list_no_descriptions && !m.reg->description.empty()) {
                    fos.first_column(indent0);
                    fos.hanging_indent(0);
                    fos << "\"" << m.reg->description << "\"";
                    fos.first_column(0);
                    fos << "\n";
                }
            }
        };
        if (m.reg->fields.empty()) {
            write_reg_header_if_necessary();
            fos.first_column(4);
            fos << "(row has no sub-fields)\n";
        }
        for(const auto &f : m.reg->fields) {
            if (f.mask & m.mask) {
                write_reg_header_if_necessary();
                // todo should we care if the fields overlap - i think this is fine for here in list
                fos.first_column(4);
                fos.hanging_indent(10);
                int low = __builtin_ctz(f.mask);
                int high = 31 - __builtin_clz(f.mask);
                fos << "field " << f.name ;
                if (low == high) {
                    fos << " (bit " << low << ")\n";
                } else {
                    fos << " (bits " << low << "-" << high << ")\n";
                }
                if ((m.field || settings.otp.list_field_descriptions) && !settings.otp.list_no_descriptions && !f.description.empty()) {
                    // Only print field descriptors if matching a field, or if list_field_descriptions is set
                    fos.first_column(indent0);
                    fos.hanging_indent(0);
                    fos << "\"" << f.description << "\"";
                    fos.first_column(0);
                    fos << "\n";
                }
            }
        }
    }
    return false;
}

#if HAS_LIBUSB
bool otp_set_command::execute(device_map &devices) {
    auto con = get_single_rp2350_bootsel_device_connection(devices, false);
    hack_init_otp_regs(con);
    auto matches = filter_otp(settings.otp.selectors, settings.otp.ecc ? 16 : 24, settings.otp.fuzzy);
    // baing lazy to count
    std::set<uint32_t> unique_rows;
    std::transform(matches.begin(), matches.end(), std::inserter(unique_rows, unique_rows.begin()), [](const auto&e) { return e.first.first; });
    if (unique_rows.empty()) {
        fail(ERROR_ARGS, " no OTP rows matched for writing.");
    }
    if (unique_rows.size() != 1) {
        fail(ERROR_ARGS, " multiple OTP rows matched, so write is not allowed.");
    }
    if (matches.size() != 1) {
        fail(ERROR_ARGS, " multiple OTP fields matched, so write is not allowed.");
    }
    uint32_t mask = 0;
    for(const auto& e : matches) {
        mask |= e.second.mask;
    }
    uint32_t reg_row = *unique_rows.begin();
    char buf[512];
    int indent0 = settings.otp.list_pages ? 18 : 8;
    // todo check page permissions
    // todo write only
    struct picoboot_otp_cmd otp_cmd;
    otp_cmd.wRow = reg_row;
    otp_cmd.wRowCount = 1;
    otp_cmd.bEcc = 0;
    uint32_t old_raw_value;
    picoboot_memory_access raw_access(con);
    con.otp_read(&otp_cmd, (uint8_t *)&old_raw_value, sizeof(old_raw_value));
    fos.first_column(0);
    fos.hanging_indent(7);
    snprintf(buf, sizeof(buf), "ROW 0x%04x", reg_row);
    fos << buf;
    snprintf(buf, sizeof(buf), "  OLD_VALUE=0x%06x", old_raw_value);
    fos << buf;
    const otp_reg *reg = matches.cbegin()->second.reg;
    if (settings.otp.list_pages) {
        snprintf(buf, sizeof(buf), " (0x%02x:0x%02x)", reg_row / OTP_PAGE_ROWS, reg_row % OTP_PAGE_ROWS);
        fos << buf;
    }
    if (reg) {
        fos << ": " << reg->name;
        if (settings.otp.list_no_descriptions) {
            if (reg->ecc) {
                fos << " (ECC)";
            } else if (reg->crit) {
                fos << " (CRIT)";
            } else if (reg->redundancy) {
                fos << " (RBIT-" << reg->redundancy << ")";
            }
        }
        if (reg->seq_length) {
            fos << " (Part " << (reg->seq_index + 1) << "/" << reg->seq_length << ")";
        }
        // Set the ecc and redundancy, unless the user specified values
        settings.otp.ecc |= (reg->ecc && !settings.otp.raw);
        if (settings.otp.redundancy < 0) settings.otp.redundancy = reg->redundancy;
    }
    fos << "\n";
    if (reg && !settings.otp.list_no_descriptions && !reg->description.empty()) {
        fos.first_column(indent0);
        fos.hanging_indent(0);
        fos << "\"" << reg->description << "\"";
        fos.first_column(0);
        fos << "\n";
    }
#if 0
    fos.first_column(4);
    fos.hanging_indent(10);
    fos << buf;
    fos << "\n";
    for (const auto &f: m.reg->fields) {
        if (f.mask & m.mask) {
            fos.first_column(4);
            fos.hanging_indent(10);
            // todo should we care if the fields overlap - i think this is fine for here in list
            int low = __builtin_ctz(f.mask);
            int high = 31 - __builtin_clz(f.mask);
            fos << "field " << f.name;
            if (low == high) {
                fos << " (bit " << low << ")\n";
            } else {
                fos << " (bits " << low << "-" << high << ")\n";
            }
        }
    }
#endif
    // if writing field, only write that field
    const otp_field *field = matches.cbegin()->second.field;
    if (field) {
        fos.first_column(4);
        fos.hanging_indent(10);
        int low = __builtin_ctz(field->mask);
        int high = 31 - __builtin_clz(field->mask);
        fos << "field " << field->name;
        if (low == high) {
            fos << " (bit " << low << ")\n";
        } else {
            fos << " (bits " << low << "-" << high << ")\n";
        }

        if (settings.otp.value & (~field->mask >> low)) {
            fail(ERROR_NOT_POSSIBLE, "Value to set does not fit in field: value %06x, mask %06x\n", settings.otp.value, field->mask >> low);
        }

        settings.otp.value <<= low;
        settings.otp.value &= field->mask;
        settings.otp.value |= old_raw_value & ~field->mask;
    }
    if (settings.otp.ignore_set) {
        // OR with current value, to ignore any already-set bits
        settings.otp.value |= old_raw_value;
    }
    // todo check for clearing bits
    if (old_raw_value && settings.otp.ecc) {
        fail(ERROR_NOT_POSSIBLE, "Cannot modify OTP ECC row(s)\n");
    }
    if (~settings.otp.value & old_raw_value) {
        fail(ERROR_NOT_POSSIBLE, "Cannot clear bits in OTP row(s): current value %06x, new value %06x\n", old_raw_value, settings.otp.value);
    }
    // todo this is currently crappy, incorrect and generally evil
    otp_cmd.bEcc = settings.otp.ecc;
    try {
        if (otp_cmd.bEcc) {
            uint16_t write_value = settings.otp.value;
            con.otp_write(&otp_cmd, (uint8_t *) &write_value, sizeof(write_value));
        } else if (settings.otp.redundancy > 0) {
            otp_cmd.wRowCount = settings.otp.redundancy;
            vector<uint32_t> write_value;
            for (int i=0; i < otp_cmd.wRowCount; i++) write_value.push_back(settings.otp.value);
            con.otp_write(&otp_cmd, (uint8_t *)write_value.data(), write_value.size() * sizeof(uint32_t));
        } else {
            uint32_t write_value = settings.otp.value;
            con.otp_write(&otp_cmd, (uint8_t *)&write_value, sizeof(write_value));
        }
    } catch (picoboot::command_failure &e) {
        check_otp_write_error(e, otp_cmd.bEcc);
        throw e;
    }

    return false;
}

bool otp_permissions_command::execute(device_map &devices) {
    auto con = get_single_rp2350_bootsel_device_connection(devices, false);

    json perms_json = json::parse(*get_file(ios::in|ios::binary));

    auto tmp = std::make_shared<std::stringstream>();
    auto file = get_xip_ram_perms();
    *tmp << file->rdbuf();

    auto program = get_iostream_memory_access<iostream_memory_access>(tmp, filetype::elf, true);
    program.set_model(rp2350);

    settings.config.group = "otp_page_permissions";
    for (auto it = perms_json.begin(); it != perms_json.end(); ++it) {
        std::stringstream ss;
        ss << "page" << it.key();
        settings.config.key = ss.str();
        std::cout << ss.str() << std::endl;
        auto perms = it.value();
        int tmp;
        if (perms.contains("no_key_state")) {
            get_json_int(perms["no_key_state"], tmp);
            settings.otp.lock0 |= (tmp << OTP_DATA_PAGE0_LOCK0_NO_KEY_STATE_LSB);
        }
        if (perms.contains("key_r")) {
            get_json_int(perms["key_r"], tmp);
            settings.otp.lock0 |= (tmp << OTP_DATA_PAGE0_LOCK0_KEY_R_LSB);
        }
        if (perms.contains("key_w")) {
            get_json_int(perms["key_w"], tmp);
            settings.otp.lock0 |= (tmp << OTP_DATA_PAGE0_LOCK0_KEY_W_LSB);
        }
        if (perms.contains("lock_bl")) {
            get_json_int(perms["lock_bl"], tmp);
            settings.otp.lock1 |= (tmp << OTP_DATA_PAGE0_LOCK1_LOCK_BL_LSB);
        }
        if (perms.contains("lock_ns")) {
            get_json_int(perms["lock_ns"], tmp);
            settings.otp.lock1 |= (tmp << OTP_DATA_PAGE0_LOCK1_LOCK_NS_LSB);
        }
        if (perms.contains("lock_s")) {
            get_json_int(perms["lock_s"], tmp);
            settings.otp.lock1 |= (tmp << OTP_DATA_PAGE0_LOCK1_LOCK_S_LSB);
        }
        settings.config.value = hex_string(settings.otp.lock1 << 16 | settings.otp.lock0);
        config_guts(program);
    }

    settings.config.group = "led_config";
    // UART Config
    if (settings.otp.led_pin != -1) {
        settings.config.key = "led";
        settings.config.value = hex_string(settings.otp.led_pin);
        config_guts(program);
    }

#if HAS_MBEDTLS
    private_t private_key = {};
    public_t public_key = {};

    if (settings.seal.sign && settings.filenames[2].empty()) {
        fail(ERROR_ARGS, "missing key file for signing");
    }

    if (!settings.filenames[2].empty() && get_file_type_idx(2) != filetype::pem) {
        fail(ERROR_ARGS, "Can only read pem keys");
    }

    if (settings.seal.sign) read_keys(settings.filenames[2], &public_key, &private_key);

    elf_file source_file(settings.verbose);
    elf_file *elf = &source_file;
    elf->read_file(tmp);
    sign_guts_elf(elf, private_key, public_key);
    auto out = std::make_shared<std::stringstream>();
    elf->write(out);

    auto signed_program = get_iostream_memory_access<iostream_memory_access>(out, filetype::elf, true);
#else
    if (settings.seal.sign) fail(ERROR_NOT_POSSIBLE, "Cannot sign binaries without mbedtls");
    auto signed_program = get_iostream_memory_access<iostream_memory_access>(tmp, filetype::elf, true);
#endif

    settings.load.execute = true;
    load_guts(con, signed_program);

    // todo: read back after reboot (requires lots of stuff)

    return true;
}

enum wl_type {
    wl_value,
    wl_bcd,
    wl_strdef,
    wl_unistrdef
};

void wl_do_field(json json_data, vector<uint16_t>& data, uint32_t& flags, const otp_reg* flags_reg, tuple<string, string, wl_type, uint8_t> field, int idx) {
    auto cat = std::get<0>(field);
    auto sub = std::get<1>(field);
    auto type = std::get<2>(field);
    auto max_strlen = std::get<3>(field);

    int hex_val = 0;

    if (json_data.contains(cat) && json_data[cat].contains(sub)) {
        auto val = json_data[cat][sub];
        switch (type)
        {
        case wl_value:
            if (!get_json_int(val, hex_val)) {
                fail(ERROR_FORMAT, "%s.%s must be an integer", cat.c_str(), sub.c_str());
            }
            break;
        
        case wl_bcd:
            if (!get_json_bcd(val, hex_val)) {
                fail(ERROR_FORMAT, "%s.%s must be a float or integer less than 100", cat.c_str(), sub.c_str());
            }
            break;

        case wl_strdef:
            if (get_json_strdef(val, hex_val, data, max_strlen)) {
                if (hex_val & 0x80) {
                    fail(ERROR_FORMAT, "%s.%s must be an ascii string", cat.c_str(), sub.c_str());
                }
            } else {
                fail(ERROR_FORMAT, "%s.%s must be a string with < %d characters", cat.c_str(), sub.c_str(), max_strlen);
            }
            break;
        
        case wl_unistrdef:
            if (!get_json_strdef(val, hex_val, data, max_strlen)) {
                fail(ERROR_FORMAT, "%s.%s must be a string with < %d characters", cat.c_str(), sub.c_str(), max_strlen);
            }
            break;
        }

        data[idx] = hex_val;
        flags |= 1 << idx;
    }
}

bool otp_white_label_command::execute(device_map &devices) {
    auto con = get_single_rp2350_bootsel_device_connection(devices, false);
    hack_init_otp_regs(con);
    const otp_reg* flags_reg;
    const otp_reg* addr_reg;
    {
        auto matches = filter_otp({"usb_boot_flags"}, 24, false);
        auto row_match = *matches.begin();
        flags_reg = row_match.second.reg;
    }
    {
        auto matches = filter_otp({"usb_white_label_addr"}, 16, false);
        auto row_match = *matches.begin();
        addr_reg = row_match.second.reg;
    }

    json wl_json = json::parse(*get_file(ios::in|ios::binary));

    if (wl_json.contains("device") && !wl_json["device"].contains("config_attributes_max_power")) {
        // Check for separate max_power and attributes
        uint16_t val = 0;
        int hex_val = 0;
        if (wl_json["device"].contains("max_power") && wl_json["device"].contains("attributes")) {
            if (!get_json_int(wl_json["device"]["max_power"], hex_val)) {
                fail(ERROR_FORMAT, "MaxPower must be an integer");
            }
            val |= (hex_val << 8);

            if (!get_json_int(wl_json["device"]["attributes"], hex_val)) {
                fail(ERROR_FORMAT, "Device Attributes must be an integer");
            } else if (hex_val & 0b11111 || ~hex_val & 0x80) {
                fail(ERROR_FORMAT, "Device Attributes must have bit 7 set (0x80), and bits 4-0 clear");
            }
            val |= hex_val;
        } else if (wl_json["device"].contains("max_power") || wl_json["device"].contains("attributes")) {
            fail(ERROR_INCOMPATIBLE, "Must specify both max_power and attributes in the JSON file");
        }
        if (val) {
            fos << "Setting attributes " << hex_string(val, 4) << "\n";
            wl_json["device"]["config_attributes_max_power"] = val;
        }
    }

    vector<uint16_t> data;
    data.resize(16);

    uint32_t flags = 0;
    flags |= (flags_reg->fields.begin() + 1)->mask;

    vector<tuple<string, string, wl_type, uint8_t>> wl_fields = {
        {"device", "vid", wl_value, 0},
        {"device", "pid", wl_value, 0},
        {"device", "bcd", wl_bcd, 0},
        {"device", "lang_id", wl_value, 0},
        {"device", "manufacturer", wl_unistrdef, 30},
        {"device", "product", wl_unistrdef, 30},
        {"device", "serial_number", wl_unistrdef, 30},
        {"device", "config_attributes_max_power", wl_value, 0},
        {"volume", "label", wl_strdef, 11},
        {"scsi", "vendor", wl_strdef, 8},
        {"scsi", "product", wl_strdef, 16},
        {"scsi", "version", wl_strdef, 4},
        {"volume", "redirect_url", wl_strdef, 0x7f},
        {"volume", "redirect_name", wl_strdef, 0x7f},
        {"volume", "model", wl_strdef, 0x7f},
        {"volume", "board_id", wl_strdef, 0x7f},
    };

    for (unsigned int i=0; i < wl_fields.size(); i++) {
        wl_do_field(wl_json, data, flags, flags_reg, wl_fields[i], i);
    }

    struct picoboot_otp_cmd otp_cmd;

    uint16_t struct_row = settings.otp.row ? settings.otp.row : 0x100;

    fos << "Writing white-label data to row " << hex_string(struct_row, 4) << "\n";
    if (fos.last_column() > 8*8 - 1) fos.last_column(8*8 - 1);
    for (auto x : data) {
        fos << hex_string(x, 4) << ", ";
    }
    fos << "\n";

    // Write struct
    otp_cmd.bEcc = true;
    otp_cmd.wRow = struct_row;
    otp_cmd.wRowCount = data.size();

    try {
        con.otp_write(&otp_cmd, (uint8_t*)data.data(), data.size()*sizeof(data[0]));
    } catch (picoboot::command_failure &e) {
        check_otp_write_error(e, otp_cmd.bEcc);
        throw e;
    }

    // Write addr
    otp_cmd.bEcc = true;
    otp_cmd.wRow = addr_reg->row;
    otp_cmd.wRowCount = 1;
    try {
        con.otp_write(&otp_cmd, (uint8_t*)&struct_row, sizeof(struct_row));
    } catch (picoboot::command_failure &e) {
        check_otp_write_error(e, otp_cmd.bEcc);
        throw e;
    }

    // Write flags
    otp_cmd.bEcc = false;
    otp_cmd.wRow = flags_reg->row;
    otp_cmd.wRowCount = flags_reg->redundancy;

    vector<uint32_t> tmp_data = {flags};

    tmp_data.resize(otp_cmd.wRowCount);
    for (int i=1; i < otp_cmd.wRowCount; i++) std::copy_n(tmp_data.begin(), 1, tmp_data.begin() + i);
    try {
        con.otp_write(&otp_cmd, (uint8_t*)tmp_data.data(), tmp_data.size()*sizeof(flags));
    } catch (picoboot::command_failure &e) {
        check_otp_write_error(e, otp_cmd.bEcc);
        throw e;
    }

    return false;
}
#endif


#if HAS_LIBUSB
static int reboot_device(libusb_device *device, libusb_device_handle *dev_handle, bool bootsel, unsigned int disable_mask=0) {
    // ok, the device isn't in USB boot mode, let's try to reboot via vendor interface
    struct libusb_config_descriptor *config;
    int ret = libusb_get_active_config_descriptor(device, &config);
    if (ret) {
        fail(ERROR_USB, "Failed to get descriptor %d\n", ret);
    }
    for (int i = 0; i < config->bNumInterfaces; i++) {
        if (0xff == config->interface[i].altsetting[0].bInterfaceClass &&
            RESET_INTERFACE_SUBCLASS == config->interface[i].altsetting[0].bInterfaceSubClass &&
            RESET_INTERFACE_PROTOCOL == config->interface[i].altsetting[0].bInterfaceProtocol) {
            ret = libusb_claim_interface(dev_handle, i);
            if (ret) {
                fail(ERROR_USB, "Failed to claim interface\n");
            }
            if (bootsel) {
                ret = libusb_control_transfer(dev_handle, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                                              RESET_REQUEST_BOOTSEL, disable_mask, i, nullptr, 0, 2000);
            } else {
                ret = libusb_control_transfer(dev_handle, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                                              RESET_REQUEST_FLASH, 0, i, nullptr, 0, 2000);
            }
//            if (ret != 0 ) {
//                fail(ERROR_UNKNOWN, "Unable to reset the device %d\n", ret);
//            }
            return 0;
//            return ret;
        }
    }
    fail(ERROR_USB, "Unable to locate reset interface on the device");
    return -1;
}

bool reboot_command::execute(device_map &devices) {
    if (settings.force) {
        if (!settings.switch_cpu.empty()) {
            fail(ERROR_ARGS, "--cpu may not be specified for forced reboot");
        }
        selected_model = std::get<0>(devices[dr_vidpid_stdio_usb][0]);
        reboot_device(std::get<1>(devices[dr_vidpid_stdio_usb][0]), std::get<2>(devices[dr_vidpid_stdio_usb][0]), settings.reboot_usb);
        if (!quiet) {
            if (settings.reboot_usb) {
                std::cout << "The device was asked to reboot into BOOTSEL mode.\n";
            } else {
                std::cout << "The device was asked to reboot into application mode.\n";
            }
        }
    } else {
        // not exclusive, because restoring un-exclusive could fail; also if we're rebooting, we don't much
        // care what else is happening.
        auto con = get_single_bootsel_device_connection(devices, false);
        picoboot_memory_access raw_access(con);
        model_t model = get_model(raw_access);
        if (model == rp2350) {
            struct picoboot_reboot2_cmd cmd = {
                    .dFlags = (uint8_t)(settings.reboot_usb ? REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL : REBOOT2_FLAG_REBOOT_TYPE_NORMAL),
                    .dDelayMS = 500,
                    .dParam0 = settings.reboot_usb ? 0u : (unsigned int)settings.reboot_diagnostic_partition,
                    .dParam1 = 0,
            };
            if (!settings.switch_cpu.empty()) {
                if (settings.switch_cpu == "arm")
                    cmd.dFlags |= REBOOT2_FLAG_REBOOT_TO_ARM;
                else if (settings.switch_cpu == "riscv")
                    cmd.dFlags |= REBOOT2_FLAG_REBOOT_TO_RISCV;
                else
                    fail(ERROR_ARGS, "--cpu CPU type must be 'arm' or 'riscv'");
            }
            try {
                con.reboot2(&cmd);
            } catch (picoboot::command_failure &e) {
                if (e.get_code() == PICOBOOT_NOT_PERMITTED) {
                    fail(ERROR_NOT_POSSIBLE, "Unable to reboot - architecture unavailable");
                } else {
                    throw;
                }
            }
        } else if (!settings.reboot_usb) {
            // on RP2040 pass 0 to reboot in to flash
            con.reboot(0, 0, 500);
        } else {
            unsigned int program_base = SRAM_START;
            std::vector<uint32_t> program = {
                    0x20002100, // movs r0, #0;       movs r1, #0
                    0x47104a00, // ldr  r2, [pc, #0]; bx r2
                    bootrom_func_lookup(raw_access, rom_table_code('U', 'B'))
            };

            raw_access.write_vector(program_base, program);
            try {
                con.exec(program_base);
            } catch (picoboot::connection_error &e) {
                // the reset_usb_boot above has a very short delay, so it frequently causes libusb to return
                // fairly unpredictable errors... i think it is best to ignore them, because catching a rare
                // case where the reboot command fails, is probably less important than potentially confusing
                // the user with spurious error messages
            }
        }
        if (!quiet) {
            if (settings.reboot_usb) {
                std::cout << "The device was rebooted into BOOTSEL mode.\n";
            } else {
                std::cout << "The device was rebooted into application mode.\n";
            }
        }
    }
    return true;
}
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/ioctl.h>
#endif

// tsk namespace is polluted on windows
#ifdef _WIN32
#undef min
#undef max

#define _CRT_SECURE_NO_WARNINGS
#endif

static void sleep_ms(int ms) {
#if defined(__unix__) || defined(__APPLE__)
    usleep(ms * 1000);
#else
    Sleep(ms);
#endif
}

void get_terminal_size(int& width, int& height) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    width = (int)(csbi.dwSize.X);
    height = (int)(csbi.dwSize.Y);
#elif defined(__linux__) || defined(__APPLE__)
    winsize w;
        if (ioctl(fileno(stdout), TIOCGWINSZ, &w) == 0) {
        width = (int)(w.ws_col);
        height = (int)(w.ws_row);
    } else {
        // default values if ioctl failed, for example if stdout is not
        // connected to an interactive tty
        width = 80;
        height = 24;
    }
#endif // Windows/Linux
}

void cancelled(int) {
    throw cancelled_exception();
}

int main(int argc, char **argv) {
    int tw=0, th=0;
    get_terminal_size(tw, th);
    if (tw) {
        fos.last_column(std::max(tw, 40));
    }

    int rc = parse(argc, argv);
    if (rc) return rc;
    if (!selected_cmd) {
        return 0;
    }

    if (settings.quiet) {
        fos_ptr = fos_null_ptr;
    }

#if HAS_LIBUSB
    libusb_context *ctx = nullptr;

    // save complicating the grammar
    if (settings.force_no_reboot) settings.force = true;

    struct libusb_device **devs = nullptr;
    device_map devices;
    vector<libusb_device_handle *> to_close;

    try {
        signal(SIGINT, cancelled);
        signal(SIGTERM, cancelled);

        if (settings.reboot_usb && settings.reboot_app_specified) {
            fail(ERROR_ARGS, "Cannot specify both -u and -a reboot options");
        }

        if (selected_cmd->get_device_support() != cmd::none) {
            if (libusb_init(&ctx)) {
                fail(ERROR_USB, "Failed to initialise libUSB\n");
            }
        }

        // we only loop a second time if we want to reboot some devices (which may cause device
        for (int tries = 0; !rc && tries <= MAX_REBOOT_TRIES; tries++) {
            if (ctx) {
                if (libusb_get_device_list(ctx, &devs) < 0) {
                    fail(ERROR_USB, "Failed to enumerate USB devices\n");
                }
                for (libusb_device **dev = devs; *dev; dev++) {
                    if (settings.bus != -1 && settings.bus != libusb_get_bus_number(*dev)) continue;
                    if (settings.address != -1 && settings.address != libusb_get_device_address(*dev)) continue;
                    libusb_device_handle *handle = nullptr;
                    model_t model = unknown;
                    auto result = picoboot_open_device(*dev, &handle, &model, settings.vid, settings.pid, settings.ser.c_str());
                    if (handle) {
                        to_close.push_back(handle);
                    }
                    if (result != dr_error) {
                        devices[result].emplace_back(std::make_tuple(model, *dev, handle));
                    }
                }
            }
            auto supported = selected_cmd->get_device_support();
            switch (supported) {
                case cmd::device_support::zero_or_more:
                    if (!settings.filenames[0].empty()) break;
                    // fall thru
                case cmd::device_support::one:
                    if (devices[dr_vidpid_bootrom_ok].empty() &&
                        (!settings.force || devices[dr_vidpid_stdio_usb].empty())) {
                        if (tries == 0 || tries == MAX_REBOOT_TRIES) {
                            if (tries) {
                                fos << "\n\n";
                            }
                            bool had_note = false;
                            fos << missing_device_string(tries>0, selected_cmd->requires_rp2350());
                            if (tries) {
                                fos << " It is possible the device is not responding, and will have to be manually entered into BOOTSEL mode.\n";
                                had_note = true; // suppress "but:" in this case
                            }
                            fos << "\n";
                            fos.first_column(0);
                            fos.hanging_indent(4);
                            auto printer = [&](enum picoboot_device_result r, const string &description) {
                                if (!had_note && !devices[r].empty()) {
                                    fos << "\nbut:\n\n";
                                    had_note = true;
                                }
                                for (auto d : devices[r]) {
                                    fos << bus_device_string(std::get<1>(d), std::get<0>(d)) << description << "\n";
                                }
                            };
    #if defined(__linux__) || defined(__APPLE__)
                            printer(dr_vidpid_bootrom_cant_connect,
                                    " appears to be in BOOTSEL mode, but picotool was unable to connect. Maybe try 'sudo' or check your permissions.");
                            printer(dr_vidpid_stdio_usb_cant_connect,
                                    " appears to have a USB serial connection, but picotool was unable to connect. Maybe try 'sudo' or check your permissions.");
    #else
                            printer(dr_vidpid_bootrom_cant_connect,
                                    " appears to be in BOOTSEL mode, but picotool was unable to connect. You may need to install a driver via Zadig. See \"Getting started with Raspberry Pi Pico\" for more information");
                            printer(dr_vidpid_stdio_usb_cant_connect,
                                    " appears to have a USB serial connection, but picotool was unable to connect.");
    #endif
                            printer(dr_vidpid_picoprobe,
                                    " appears to be an RP-series PicoProbe device not in BOOTSEL mode.");
                            printer(dr_vidpid_micropython,
                                    " appears to be an RP-series MicroPython device not in BOOTSEL mode.");
                            if (selected_cmd->force_requires_pre_reboot()) {
    #if defined(_WIN32)
                                printer(dr_vidpid_stdio_usb,
                                        " appears to have a USB serial connection, not in BOOTSEL mode. You can force reboot into BOOTSEL mode via 'picotool reboot -f -u' first.");
    #else
                                printer(dr_vidpid_stdio_usb,
                                        " appears to have a USB serial connection, so consider -f (or -F) to force reboot in order to run the command.");
    #endif
                            } else {
                                // special case message for what is actually just reboot (the only command that doesn't require reboot first)
                                printer(dr_vidpid_stdio_usb,
                                        " appears to have a USB serial connection, so consider -f to force the reboot.");
                            }
                            rc = ERROR_NO_DEVICE;
                        } else {
                            // waiting for rebooted device to show up
                            break;
                        }
                    } else if (supported == cmd::device_support::one) {
                        if (devices[dr_vidpid_bootrom_ok].size() > 1 ||
                            (devices[dr_vidpid_bootrom_ok].empty() && devices[dr_vidpid_stdio_usb].size() > 1)) {
                            fail(ERROR_NOT_POSSIBLE, "Command requires a single RP-series device to be targeted.");
                        }
                        if (!devices[dr_vidpid_bootrom_ok].empty()) {
                            settings.force = false; // we have a device, so we're not forcing
                        }
                    } else if (supported == cmd::device_support::zero_or_more && settings.force && !devices[dr_vidpid_bootrom_ok].empty()) {
                        // we have usable devices, so lets use them without force
                        settings.force = false;
                    }
                    fos.first_column(0);
                    fos.hanging_indent(0);
                    break;
                default:
                    break;
            }
            if (!rc) {
                if (settings.force && ctx) { // actually ctx should never be null as we are targeting device if force is set, but still
                    if (devices[dr_vidpid_stdio_usb].size() != 1 && !tries) {
                        fail(ERROR_NOT_POSSIBLE,
                             "Forced command requires a single rebootable RP-series device to be targeted.");
                    }
                    if (selected_cmd->force_requires_pre_reboot()) {
                        if (!tries) {
                            // we reboot into BOOTSEL mode and disable MSC interface (the 1 here)
                            auto &to_reboot = std::get<1>(devices[dr_vidpid_stdio_usb][0]);
                            auto &to_reboot_handle = std::get<2>(devices[dr_vidpid_stdio_usb][0]);
    #if defined(_WIN32)
                            {
                                struct libusb_device_descriptor desc;
                                libusb_get_device_descriptor(to_reboot, &desc);
                                if (desc.idProduct == PRODUCT_ID_RP2040_STDIO_USB) {
                                    fail(ERROR_NOT_POSSIBLE,
                                        "Forced commands do not work with RP2040 on Windows - you can force reboot into BOOTSEL mode via 'picotool reboot -f -u' instead.");
                                }
                            }
    #endif
                            if (settings.ser.empty() && to_reboot_handle) {
                                // store USB serial number, to pick correct device after reboot
                                struct libusb_device_descriptor desc;
                                libusb_get_device_descriptor(to_reboot, &desc);
                                char ser_str[128];
                                libusb_get_string_descriptor_ascii(to_reboot_handle, desc.iSerialNumber, (unsigned char*)ser_str, sizeof(ser_str));
                                if (strcmp(ser_str, "EEEEEEEEEEEEEEEE") != 0) {
                                    // don't store EEs serial number, as that is an RP2040 running a no_flash binary
                                    settings.ser = ser_str;
                                    fos << "Tracking device serial number " << ser_str << " for reboot\n";
                                }
                            }

                            reboot_device(to_reboot, to_reboot_handle, true, 1);
                            fos << "The device was asked to reboot into BOOTSEL mode so the command can be executed.";
                        } else if (tries == 1) {
                            fos << "\nWaiting for device to reboot";
                        } else {
                            fos << "...";
                        }
                        fos.flush();
                        for (const auto &handle : to_close) {
                            libusb_close(handle);
                        }
                        libusb_free_device_list(devs, 1);
                        devs = nullptr;
                        to_close.clear();
                        devices.clear();
                        sleep_ms(1200);

                        // we now clear bus/address filters, because the device may have moved, so the only way we can find it
                        // again is to assume it has the same serial number.
                        settings.address = -1;
                        settings.bus = -1;
                        continue;
                    }
                }
                if (tries) {
                    fos << "\n\n";
                }
                if (!selected_cmd->execute(devices) && tries) {
                    if (settings.force_no_reboot) {
                        fos << "\nThe device has been left accessible, but without the drive mounted; use 'picotool reboot' to reboot into regular BOOTSEL mode or application mode.\n";
                    } else {
                        // can only really do this with one device
                        if (devices[dr_vidpid_bootrom_ok].size() == 1) {
                            reboot_cmd->quiet = true;
                            reboot_cmd->execute(devices);
                            fos << "\nThe device was asked to reboot back into application mode.\n";
                        }
                    }
                }
                break;
            }
        }
    } catch (command_failure &e) {
        std::cout << "ERROR: " << e.what() << "\n";
        rc = e.code();
    } catch (picoboot::command_failure& e) {
        // todo rp2350/rp2040
        string device = "RP-series";
        if (selected_model == rp2040) {
            device = "RP2040";
        } else if (selected_model == rp2350) {
            device = "RP2350";
        }
        std::cout << "ERROR: The " << device << " device returned an error: " << e.what() << "\n";
        rc = ERROR_UNKNOWN;
    } catch (picoboot::connection_error&) {
        // todo rp2350/rp2040
        string device = "RP-series";
        if (selected_model == rp2040) {
            device = "RP2040";
        } else if (selected_model == rp2350) {
            device = "RP2350";
        }
        std::cout << "ERROR: Communication with " << device << " device failed\n";
        rc = ERROR_CONNECTION;
    } catch (cancelled_exception&) {
        rc = ERROR_CANCELLED;
    } catch (std::exception &e) {
        std::cout << "ERROR: " << e.what() << "\n";
        rc = ERROR_UNKNOWN;
    }

    for(const auto &handle : to_close) {
        libusb_close(handle);
    }
    if (devs) libusb_free_device_list(devs, 1);
    if (ctx) libusb_exit(ctx);

#else
    device_map devices;

    if (selected_cmd->get_device_support() != cmd::none) {
        fail(ERROR_USB, "No libUSB\n");
    }
    try {
        rc = selected_cmd->execute(devices);
    } catch (command_failure &e) {
        std::cout << "ERROR: " << e.what() << "\n";
        rc = e.code();
    } catch (cancelled_exception&) {
        rc = ERROR_CANCELLED;
    } catch (std::exception &e) {
        std::cout << "ERROR: " << e.what() << "\n";
        rc = ERROR_UNKNOWN;
    }
#endif

    return rc;
}
