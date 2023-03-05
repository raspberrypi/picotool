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
#include <csignal>
#include <cstdio>
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
#include <ctime>

#include "boot/uf2.h"
#include "picoboot_connection_cxx.h"
#include "pico/binary_info.h"
#include "pico/stdio_usb/reset_interface.h"
#include "elf.h"

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

// tsk namespace is polluted on windows
#ifdef _WIN32
#undef min
#undef max

#define _CRT_SECURE_NO_WARNINGS
#undef ERROR_CANCELLED
#endif

#define ERROR_ARGS -1
#define ERROR_FORMAT -2
#define ERROR_INCOMPATIBLE -3
#define ERROR_READ_FAILED -4
#define ERROR_WRITE_FAILED -5
#define ERROR_USB -6
#define ERROR_NO_DEVICE -7
#define ERROR_NOT_POSSIBLE -8
#define ERROR_CONNECTION -9
#define ERROR_CANCELLED -10
#define ERROR_VERIFICATION_FAILED -11
#define ERROR_UNKNOWN -99

using std::string;
using std::vector;
using std::pair;
using std::map;

typedef map<enum picoboot_device_result,vector<pair<libusb_device *, libusb_device_handle *>>> device_map;

typedef unsigned int uint;

auto memory_names = map<enum memory_type, string>{
        {memory_type::sram, "RAM"},
        {memory_type::sram_unstriped, "Unstriped RAM"},
        {memory_type::flash, "Flash"},
        {memory_type::xip_sram, "XIP RAM"},
        {memory_type::rom, "ROM"}
};

static string tool_name = "picotool";

static string hex_string(int value, int width=8, bool prefix=true) {
    std::stringstream ss;
    if (prefix) ss << "0x";
    ss << std::setfill('0') << std::setw(width) << std::hex << value;
    return ss.str();
}

std::array<std::array<string, 30>, 10> pin_functions{{
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

auto bus_device_string = [](struct libusb_device *device) {
    return string("Device at bus ") + std::to_string(libusb_get_bus_number(device)) + ", address " + std::to_string(libusb_get_device_address(device));
};

enum class filetype {bin, elf, uf2};

struct command_failure : std::exception {
    command_failure(int code, string s) : c(code), s(std::move(s)) {}

    const char *what() const noexcept override {
        return s.c_str();
    }

    int code() const { return c; }
private:
    int c;
    string s;
};

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

static void __noreturn fail(int code, string msg) {
    throw command_failure(code, std::move(msg));
}

static void __noreturn fail(int code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    static char error_msg[512];
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);
    fail(code, string(error_msg));
}

// ranges should not overlap
template <typename T> struct range_map {
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
private:
    map<uint32_t, pair<uint32_t, T>> m;
};

using cli::group;
using cli::option;
using cli::integer;
using cli::hex;
using cli::value;

struct cmd {
    explicit cmd(string name) : _name(std::move(name)) {}
    enum device_support { none, one, zero_or_more };
    virtual group get_cli() = 0;
    virtual string get_doc() const = 0;
    virtual device_support get_device_support() { return one; }
    virtual bool force_requires_pre_reboot() { return true; }
    // return true if the command caused a reboot
    virtual bool execute(device_map& devices) = 0;
    const string& name() { return _name; }
private:
    string _name;
};

struct _settings {
    string filename;
    string file_type;
    uint32_t binary_start = FLASH_START;
    int bus=-1;
    int address=-1;
    uint32_t offset = 0;
    uint32_t from = 0;
    uint32_t to = 0;
    bool offset_set = false;
    bool range_set = false;
    bool reboot_usb = false;
    bool reboot_app_specified = false;
    bool force = false;
    bool force_no_reboot = false;

    struct {
        bool show_basic = false;
        bool all = false;
        bool show_pins = false;
        bool show_device = false;
        bool show_build = false;
    } info;

    struct {
        bool verify = false;
        bool execute = false;
        bool no_overwrite = false;
        bool no_overwrite_force = false;
        bool update = false;
    } load;

    struct {
        bool all = false;
    } save;

    struct {
        bool semantic = false;
    } version;
};
_settings settings;
std::shared_ptr<cmd> selected_cmd;

auto device_selection =
    (
        (option("--bus") & integer("bus").min_value(0).max_value(255).set(settings.bus)
            .if_missing([] { return "missing bus number"; })) % "Filter devices by USB bus number" +
        (option("--address") & integer("addr").min_value(1).max_value(127).set(settings.address)
            .if_missing([] { return "missing address"; })) % "Filter devices by USB device address"
#if !defined(_WIN32)
        + option('f', "--force").set(settings.force) % "Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the command (unless the command itself is a 'reboot') the device will be rebooted back to application mode" +
                option('F', "--force-no-reboot").set(settings.force_no_reboot) % "Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the RPI-RP2 drive mounted"
#endif
    ).min(0).doc_non_optional(true);

auto file_types = (option ('t', "--type") & value("type").set(settings.file_type))
            % "Specify file type (uf2 | elf | bin) explicitly, ignoring file extension";

auto file_selection =
        (
            value("filename").with_exclusion_filter([](const string &value) {
                    return value.find_first_of('-') == 0;
                }).set(settings.filename) % "The file name" +
            file_types
        );

struct info_command : public cmd {
    info_command() : cmd("info") {}
    bool execute(device_map& devices) override;
    device_support get_device_support() override {
        if (settings.filename.empty())
            return zero_or_more;
        else
            return none;
    }

    group get_cli() override {
        return (
            (
                option('b', "--basic").set(settings.info.show_basic) % "Include basic information. This is the default" +
                option('p', "--pins").set(settings.info.show_pins) % "Include pin information" +
                option('d', "--device").set(settings.info.show_device) % "Include device information" +
                option('l', "--build").set(settings.info.show_build) % "Include build attributes" +
                option('a', "--all").set(settings.info.all) % "Include all information"
            ).min(0).doc_non_optional(true) % "Information to display" +
            (
                device_selection % "To target one or more connected RP2040 device(s) in BOOTSEL mode (the default)" |
                file_selection % "To target a file"
            ).major_group("TARGET SELECTION").min(0).doc_non_optional(true)
         );
    }

    string get_doc() const override {
         return "Display information from the target device(s) or file.\nWithout any arguments, this will display basic information for all connected RP2040 devices in BOOTSEL mode";
    }
};

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
        return false;
    }

    device_support get_device_support() override {
        return device_support::none;
    }

    group get_cli() override {
        return group(
                option('s', "--semantic").set(settings.version.semantic) % "Output semantic version number only"
        );
    }

    string get_doc() const override {
        return "Display picotool version";
    }
};

struct reboot_command : public cmd {
    bool quiet;
    reboot_command() : cmd("reboot") {}
    bool execute(device_map &devices) override;

    group get_cli() override {
        return
        (
            option('a', "--application").set(settings.reboot_app_specified) % "Reboot back into the application (this is the default)" +
            option('u', "--usb").set(settings.reboot_usb) % "Reboot back into BOOTSEL mode "
#if defined(_WIN32)
            + option('f', "--force").set(settings.force) % "Force a device not in BOOTSEL mode but running compatible code to reboot."
#endif
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
} reboot_cmd;

vector<std::shared_ptr<cmd>> commands {
        std::shared_ptr<cmd>(new info_command()),
        std::shared_ptr<cmd>(new load_command()),
        std::shared_ptr<cmd>(new save_command()),
        std::shared_ptr<cmd>(new verify_command()),
        std::shared_ptr<cmd>(new reboot_command()),
        std::shared_ptr<cmd>(new version_command()),
        std::shared_ptr<cmd>(new help_command()),
};

template <typename T>
std::basic_string<T> lowercase(const std::basic_string<T>& s)
{
    std::basic_string<T> s2 = s;
    std::transform(s2.begin(), s2.end(), s2.begin(), tolower);
    return std::move(s2);
}

template <typename T>
std::basic_string<T> uppercase(const std::basic_string<T>& s)
{
    std::basic_string<T> s2 = s;
    std::transform(s2.begin(), s2.end(), s2.begin(), toupper);
    return std::move(s2);
}

clipp::formatting_ostream<std::ostream> fos(std::cout);

static void sleep_ms(int ms) {
#if defined(__unix__) || defined(__APPLE__)
    usleep(ms * 1000);
#else
    Sleep(ms);
#endif
}
using cli::option;
using cli::integer;
int parse(const int argc, char **argv) {
    bool help_mode = false;
    bool no_global_header = false;
    bool no_synopsis = false;

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
            section_header(selected_cmd->name());
            fos.first_column(tab);
            fos << selected_cmd->get_doc() << "\n";
        } else if (!selected_cmd && !no_global_header) {
            section_header(tool_name);
            fos.first_column(tab);
            fos << "Tool for interacting with a RP2040 device in BOOTSEL mode, or with a RP2040 binary" << "\n";
        }
        vector<string> synopsis;
        for(auto &c : commands) {
            if (selected_cmd && c != selected_cmd) continue;
            vector<string> cmd_synposis = c->get_cli().synopsys();
            for(auto &s : cmd_synposis) {
                synopsis.push_back(c->name()+" "+s);
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
        if (!selected_cmd) {
            section_header("COMMANDS");
            size_t max = 0;
            for (auto &cmd : commands) {
                max = std::max(cmd->name().size(), max);
            }
            for (auto &cmd : commands) {
                fos.first_column(tab);
                fos << cmd->name();
                fos.first_column((int)(max + tab + 3));
                std::stringstream s;
                s << cmd->get_doc();
                s << "\n";
                fos << s.str();
            }
        } else if (!help_mode) {
            fos.first_column(0);
            fos.hanging_indent(0);
            fos.wrap_hard();
            fos << string("Use \"picotool help ").append(selected_cmd->name()).append("\" for more info\n");
        } else {
            cli::option_map options;
            selected_cmd->get_cli().get_option_help("", "", options);
            for (const auto &major : options.contents.ordered_keys()) {
                section_header(major.empty() ? "OPTIONS" : major);
                for (const auto &minor : options.contents[major].ordered_keys()) {
                    if (!minor.empty()) {
                        fos.first_column(tab);
                        fos.hanging_indent(tab*2);
                        fos << minor << "\n";
                    }
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

    try {
        selected_cmd = find_command(args[0]);
        if (selected_cmd->name() == "help") {
            help_mode = true;
            if (args.size() == 1) {
                selected_cmd = nullptr;
                usage();
                return 0;
            } else {
                selected_cmd = find_command((args[1]));
                usage();
                selected_cmd = nullptr;
                return 0;
            }
        } else if (!selected_cmd) {
            no_synopsis = true;
            no_global_header = true;
            throw cli::parse_error("unknown command '" + args[0] + "'");
        }
        args.erase(args.begin()); // remove the cmd itself
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
SAFE_MAPPING(binary_info_block_device_t);
SAFE_MAPPING(binary_info_pins_with_func_t);
SAFE_MAPPING(binary_info_pins_with_name_t);
SAFE_MAPPING(binary_info_named_group_t);

#define BOOTROM_MAGIC 0x01754d
#define BOOTROM_MAGIC_ADDR 0x00000010
static inline uint32_t rom_table_code(char c1, char c2) {
    return (c2 << 8u) | c1;
}

struct memory_access {
    virtual void read(uint32_t p, uint8_t *buffer, uint size) {
        read(p, buffer, size, false);
    }

    virtual void read(uint32_t, uint8_t *buffer, uint size, bool zero_fill) = 0;

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
    template <typename T> vector<T> read_vector(uint32_t addr, uint count, bool zero_fill = false) {
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

    template <typename T> void read_into_vector(uint32_t addr, uint count, vector<T> &v, bool zero_fill = false) {
        vector<typename raw_type_mapping<T>::access_type> buffer(count);
        if (count) read(addr, (uint8_t *)buffer.data(), count * sizeof(typename raw_type_mapping<T>::access_type), zero_fill);
        v.clear();
        v.reserve(count);
        for(const auto &e : buffer) {
            v.push_back(e);
        }
    }
};

uint32_t bootrom_func_lookup(memory_access& access, uint16_t tag) {
    auto magic = access.read_int(BOOTROM_MAGIC_ADDR);
    magic &= 0xffffff; // ignore bootrom version
    if (magic != BOOTROM_MAGIC) {
        if (!((magic ^ BOOTROM_MAGIC)&0xffff))
            fail(ERROR_INCOMPATIBLE, "Incorrect RP2040 BOOT ROM version");
        else
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
    fail(ERROR_INCOMPATIBLE, "Reboot function not found in BOOT ROM");
}

struct picoboot_memory_access : public memory_access {
    explicit picoboot_memory_access(picoboot::connection &connection) : connection(connection) {}

    bool is_device() override {
        return true;
    }

    uint32_t get_binary_start() override {
        return FLASH_START;
    }

    void read(uint32_t address, uint8_t *buffer, uint size, __unused bool zero_fill) override {
        if (flash == get_memory_type(address)) {
            connection.exit_xip();
        }
        if (rom == get_memory_type(address) && (address+size) >= 0x2000) {
            // read by memcpy instead
            uint program_base = SRAM_START + 0x4000;
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
        } else if (is_transfer_aligned(address) && is_transfer_aligned(address + size)) {
            connection.read(address, (uint8_t *) buffer, size);
        } else {
            if (flash == get_memory_type(address)) {
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

    // note this does not automatically erase flash
    void write(uint32_t address, uint8_t *buffer, uint size) {
        if (flash == get_memory_type(address)) {
            connection.exit_xip();
        }
        if (is_transfer_aligned(address) && is_transfer_aligned(address + size)) {
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
private:
    picoboot::connection& connection;
};


struct file_memory_access : public memory_access {
    file_memory_access(FILE *file, range_map<size_t>& rmap, uint32_t binary_start) : file(file), rmap(rmap), binary_start(binary_start) {

    }

    uint32_t get_binary_start() override {
        return binary_start;
    }

    void read(uint32_t address, uint8_t *buffer, uint32_t size, bool zero_fill) override {
        while (size) {
            uint this_size;
            try {
                auto result = rmap.get(address);
                this_size = std::min(size, result.first.max_offset - result.first.offset);
                assert(this_size);
                fseek(file, result.second + result.first.offset, SEEK_SET);
                fread(buffer, this_size, 1, file);
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

    const range_map<size_t> &get_rmap() {
        return rmap;
    }

    ~file_memory_access() {
        fclose(file);
    }
private:
    FILE *file;
    range_map<size_t> rmap;
    uint32_t binary_start;
};

struct remapped_memory_access : public memory_access {
    remapped_memory_access(memory_access &wrap, range_map<uint32_t> rmap) : wrap(wrap), rmap(rmap) {}

    void read(uint32_t address, uint8_t *buffer, uint size, bool zero_fill) override {
        while (size) {
            auto result = get_remapped(address);
            uint this_size = std::min(size, result.first.max_offset - result.first.offset);
            assert( this_size);
            wrap.read(result.second + result.first.offset, buffer, this_size, zero_fill);
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

static void read_and_check_elf32_header(FILE *in, elf32_header& eh_out) {
    if (1 != fread(&eh_out, sizeof(eh_out), 1, in)) {
        fail(ERROR_FORMAT, "'" + settings.filename +"' is not an ELF file");
    }
    if (eh_out.common.magic != ELF_MAGIC) {
        fail(ERROR_FORMAT, "'" + settings.filename +"' is not an ELF file");
    }
    if (eh_out.common.version != 1 || eh_out.common.version2 != 1) {
        fail(ERROR_FORMAT, "'" + settings.filename +"' has an unsupported  ELF version");
    }
    if (eh_out.common.arch_class != 1 || eh_out.common.endianness != 1) {
        fail(ERROR_INCOMPATIBLE, "'" + settings.filename +"' is not a 32 bit little-endian ELF");
    }
    if (eh_out.eh_size != sizeof(struct elf32_header)) {
        fail(ERROR_FORMAT, "'" + settings.filename + "' is not valid");
    }
    if (eh_out.common.machine != EM_ARM) {
        fail(ERROR_FORMAT, "'" + settings.filename +"' is not an ARM executable");
    }
    if (eh_out.common.abi != 0) {
        fail(ERROR_INCOMPATIBLE, "'" + settings.filename +"' has the wrong architecture");
    }
    if (eh_out.flags & EF_ARM_ABI_FLOAT_HARD) {
        fail(ERROR_INCOMPATIBLE, "'" + settings.filename  +"' has the wrong architecture (hard float)");
    }
}

static void __noreturn fail_read_error() {
    fail(ERROR_READ_FAILED, "Failed to read input file");
}

static void __noreturn fail_write_error() {
    fail(ERROR_WRITE_FAILED, "Failed to write output file");
}

struct binary_info_header {
    vector<uint32_t> bi_addr;
    range_map<uint32_t> reverse_copy_mapping;
};

bool find_binary_info(memory_access& access, binary_info_header &hdr) {
    uint32_t base = access.get_binary_start();
    if (!base) {
        fail(ERROR_FORMAT, "UF2 file does not contain a valid RP2 executable image");
    }
    if (base == FLASH_START) base += 0x100;
    vector<uint32_t> buffer = access.read_vector<uint32_t>(base, 64);
    for(uint i=0;i<64;i++) {
        if (buffer[i] == BINARY_INFO_MARKER_START) {
            if (i + 4 < 64 && buffer[i+4] == BINARY_INFO_MARKER_END) {
                uint32_t from = buffer[i+1];
                uint32_t to = buffer[i+2];
                enum memory_type from_type = get_memory_type(from);
                enum memory_type to_type = get_memory_type(to);
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
    const uint max_length = 512;
    auto v = access.read_vector<char>(addr, max_length, true); // zero fill
    uint length;
    for (length = 0; length < max_length; length++) {
        if (!v[length]) {
            break;
        }
    }
    return string(v.data(), length);
}

struct bi_visitor_base {
    void visit(memory_access& access, const binary_info_header& hdr) {
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
            case BINARY_INFO_TYPE_BLOCK_DEVICE: {
                binary_info_block_device_t value;
                access.read_raw(addr, value);
                block_device(access, value);
                break;
            }
            case BINARY_INFO_TYPE_PINS_WITH_FUNC: {
                binary_info_pins_with_func_t value;
                access.read_raw(addr, value);
                uint type = value.pin_encoding & 7u;
                uint func = (value.pin_encoding >> 3u) & 0xfu;
                if (type == BI_PINS_ENCODING_RANGE) {
                    // todo pin range
                } else if (type == BI_PINS_ENCODING_MULTI) {
                    uint32_t mask = 0;
                    int last = -1;
                    uint work = value.pin_encoding >> 7u;
                    for(int i=0;i<5;i++) {
                        int cur = (int) (work & 0x1fu);
                        mask |= 1u << cur;
                        if (cur == last) break;
                        last = cur;
                        work >>= 5u;
                    }
                    pins(mask, func, "");
                }
                break;
            }
            case BINARY_INFO_TYPE_PINS_WITH_NAME: {
                binary_info_pins_with_name_t value;
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
//        printf("ID=0x%08x int value=%d 0x%08x\n", id, value, value);
    }
    virtual void id_and_string(int tag, uint32_t id, const string& value) {
//        printf("ID=0x%08x int value=%s\n", id, value.c_str());
    }
    virtual void block_device(memory_access& access, binary_info_block_device_t &bi_bdev) {
    }

    virtual void pins(uint32_t pin_mask, int func, string name) {
        if (func != -1) {
            if (func >= (int)pin_functions.size())
                return;
        }
        for(uint i=0; i<30; i++) {
            if (pin_mask & (1u << i)) {
                if (func != -1) {
                    pin(i, pin_functions[func][i]);
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

    virtual void pin(uint i, const string& name) {

    }

    virtual void zero_terminated_bi_list(memory_access& access, const binary_info_core_t &bi_core, uint32_t addr) {
        uint32_t bi_addr;
        access.read_raw<uint32_t>(addr,bi_addr);
        while (bi_addr) {
            visit(access, addr);
            access.read_raw<uint32_t>(addr,bi_addr);
        }
    }

    virtual void named_group(int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id, const string& label, uint flags) {

    }
};

struct bi_visitor : public bi_visitor_base {
    typedef std::function<void(int tag, uint32_t id, uint32_t value)> id_and_int_fn;
    typedef std::function<void(int tag, uint32_t id, const string &value)> id_and_string_fn;
    typedef std::function<void(uint num, const string &label)> pin_fn;
    typedef std::function<void(int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id,
                               const string &label, uint flags)> named_group_fn;
    typedef std::function<void(memory_access &access, binary_info_block_device_t &bi_bdev)> block_device_fn;

    id_and_int_fn _id_and_int;
    id_and_string_fn _id_and_string;
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

    void pin(uint i, const string &name) override {
        if (_pin) _pin(i, name);
    }

    void named_group(int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id, const string &label,
                     uint flags) override {
        if (_named_group) _named_group(parent_tag, parent_id, group_tag, group_id, label, flags);
    }

    void block_device(memory_access &access, binary_info_block_device_t &bi_bdev) override {
        if (_block_device) _block_device(access, bi_bdev);
    }
};

int32_t guess_flash_size(memory_access &access) {
    assert(access.is_device());
    // Check that flash is not erased (TODO should check for second stage)
    auto first_two_pages = access.read_vector<uint8_t>(FLASH_START, 2*PAGE_SIZE);
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
        auto new_pages = access.read_vector<uint8_t>(FLASH_START + size, 2*PAGE_SIZE);
        if (!std::equal(first_two_pages.begin(), first_two_pages.end(), new_pages.begin())) break;
    }
    return size * 2;
}

FILE *get_file(const char *mode) {
    FILE *file = fopen(settings.filename.c_str(), mode);
    if (!file) fail(ERROR_READ_FAILED, "Could not open '%s'", settings.filename.c_str());
    return file;
}

enum filetype get_file_type() {
    auto low = lowercase(settings.filename);
    if (settings.file_type.empty() && low.size() >= 4) {
        if (low.rfind(".uf2") == low.size() - 4) {
            return filetype::uf2;
        } else if (low.rfind(".elf") == low.size() - 4) {
            return filetype::elf;
        } else if (low.rfind(".bin") == low.size() - 4) {
            return filetype::bin;
        }
    } else if (!settings.file_type.empty()) {
        low = lowercase(settings.file_type);
        if (low == "uf2") {
            return filetype::uf2;
        }
        if (low == "bin") {
            return filetype::bin;
        }
        if (low == "elf") {
            return filetype::elf;
        }
        throw cli::parse_error("unsupported file type '" + low + "'");
    }
    throw cli::parse_error("filename '" + settings.filename+ "' does not have a recognized file type (extension)");
}

void build_rmap_elf(FILE *file, range_map<size_t>& rmap) {
    elf32_header eh;
    uint32_t binary_start = 0xffffffff;
    bool had_flash_start = false;
    read_and_check_elf32_header(file, eh);
    if (eh.ph_entry_size != sizeof(elf32_ph_entry)) {
        fail(ERROR_FORMAT, "Invalid ELF32 program header");
    }
    if (eh.ph_num) {
        vector<elf32_ph_entry> entries(eh.ph_num);
        if (fseek(file, eh.ph_offset, SEEK_SET)) {
            return fail_read_error();
        }
        if (eh.ph_num != fread(&entries[0], sizeof(struct elf32_ph_entry), eh.ph_num, file)) {
            fail_read_error();
        }
        for (uint i = 0; i < eh.ph_num; i++) {
             elf32_ph_entry &entry = entries[i];
             if (entry.type == PT_LOAD && entry.memsz) {
                uint mapped_size = std::min(entry.filez, entry.memsz);
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

void build_rmap_uf2(FILE *file, range_map<size_t>& rmap) {
    fseek(file, 0, SEEK_SET);
    uf2_block block;
    uint pos = 0;
    do {
        if (1 != fread(&block, sizeof(uf2_block), 1, file)) {
            if (feof(file)) break;
            fail(ERROR_READ_FAILED, "unexpected end of input file");
        }
        if (block.magic_start0 == UF2_MAGIC_START0 && block.magic_start1 == UF2_MAGIC_START1 &&
            block.magic_end == UF2_MAGIC_END) {
            if (block.flags & UF2_FLAG_FAMILY_ID_PRESENT && block.file_size == RP2040_FAMILY_ID &&
                !(block.flags & UF2_FLAG_NOT_MAIN_FLASH) && block.payload_size == PAGE_SIZE) {
                rmap.insert(range(block.target_addr, block.target_addr + PAGE_SIZE), pos + offsetof(uf2_block, data[0]));
            }
        }
        pos += sizeof(uf2_block);
    } while (true);
}

uint32_t find_binary_start(range_map<size_t>& rmap) {
    range flash(FLASH_START, FLASH_END);
    range sram(SRAM_START, SRAM_END);
    range xip_sram(XIP_SRAM_BASE, XIP_SRAM_END);
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
    if (get_memory_type(binary_start) == invalid) {
        return 0;
    }
    return binary_start;
}

file_memory_access get_file_memory_access() {
    FILE *file = get_file("rb");
    range_map<size_t> rmap;
    uint32_t binary_start;
    try {
        switch (get_file_type()) {
            case filetype::bin:
                fseek(file, 0, SEEK_END);
                binary_start = settings.offset_set ? settings.offset : FLASH_START;
                rmap.insert(range(binary_start, binary_start + ftell(file)), 0);
                break;
            case filetype::elf:
                build_rmap_elf(file, rmap);
                binary_start = find_binary_start(rmap);
                break;
            case filetype::uf2:
                build_rmap_uf2(file, rmap);
                binary_start = find_binary_start(rmap);
                break;
        }
        return file_memory_access(file, rmap, binary_start);
    } catch (std::exception&) {
        fclose(file);
        throw;
    }
}

void info_guts(memory_access &raw_access) {
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
        auto select_group = [&](const group &g1, bool enabled = true) {
            if (std::find_if(groups.begin(), groups.end(), [&](const group &g2) {
                return g1.name == g2.name;
            }) == groups.end()) {
                groups.push_back(g1);
            }
            current_group = g1.name;
        };
        auto info_pair = [&](const string &name, const string &value) {
            if (!value.empty()) {
                assert(!current_group.empty());
                infos[current_group].push_back(std::make_pair(name, value));
            }
        };

        // establish core groups and their order
        if (!settings.info.show_basic && !settings.info.all && !settings.info.show_pins && !settings.info.show_device && !settings.info.show_build) {
            settings.info.show_basic = true;
        }
        auto program_info = group("Program Information", settings.info.show_basic || settings.info.all);
        auto pin_info = group("Fixed Pin Information", settings.info.show_pins || settings.info.all);
        auto build_info = group("Build Information", settings.info.show_build || settings.info.all);
        auto device_info = group("Device Information", (settings.info.show_device || settings.info.all) & raw_access.is_device());
        // select them up front to impose order
        select_group(program_info);
        select_group(pin_info);
        select_group(build_info);
        select_group(device_info);
        binary_info_header hdr;
        if (find_binary_info(raw_access, hdr)) {
            auto access = remapped_memory_access(raw_access, hdr.reverse_copy_mapping);
            auto visitor = bi_visitor{};
            map<string,string> output;
            map<uint,vector<string>> pins;

            map<pair<int, uint32_t>, pair<string, uint>> named_feature_groups;
            map<string, vector<string>> named_feature_group_values;

            string program_name, program_build_date, program_version, program_url, program_description;
            string pico_board, sdk_version, boot2_name;
            vector<string> program_features, build_attributes;
            
            uint32_t binary_end = 0;

            // do a pass first to find named groups
            visitor.named_group([&](int parent_tag, uint32_t parent_id, int group_tag, uint32_t group_id, const string& label, uint flags) {
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
            visitor.id_and_string([&](int tag, uint32_t id, const string& value) {
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
            visitor.pin([&](uint pin, const string &name) {
                pins[pin].push_back(name);
            });

            // some simple info in --all for now.. split out into separate section later
            vector<std::function<void()>> deferred;
            visitor.block_device([&](memory_access& access, binary_info_block_device_t &bi_bdev) {
                if (settings.info.all) {
                    std::stringstream ss;
                    ss << hex_string(bi_bdev.address) << "-" << hex_string(bi_bdev.address + bi_bdev.size) <<
                       " (" << ((bi_bdev.size + 1023)/1024) << "K): " << read_string(access, bi_bdev.name);
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
                for(const auto &e : named_feature_groups) {
                    const auto &name = e.second.first;
                    const auto &flags = e.second.second;
                    const auto &values = named_feature_group_values[name];
                    if (!values.empty() || (flags & BI_NAMED_GROUP_SHOW_IF_EMPTY)) {
                        info_pair(name, cli::join(values, (flags & BI_NAMED_GROUP_SEPARATE_COMMAS) ? ", " : "\n"));
                    }
                }
                if (settings.info.all) {
                    if (access.get_binary_start())
                        info_pair("binary start", hex_string(access.get_binary_start()));
                    if (binary_end)
                        info_pair("binary end", hex_string(binary_end));
                }

                for(auto &f : deferred) {
                    f();
                }
            }
            if (settings.info.show_pins || settings.info.all) {
                select_group(pin_info);
                int first_pin = -1;
                string last_label;
                for(auto i = pins.begin(); i != pins.end(); i++) {
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
                        first_pin = (int)i->first;
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
        if ((settings.info.show_device || settings.info.all) && raw_access.is_device()) {
            select_group(device_info);
            int32_t size_guess = guess_flash_size(raw_access);
            if (size_guess > 0) {
                info_pair("flash size", std::to_string(size_guess/1024) + "K");
            }

            uint8_t rom_version;
            raw_access.read_raw(0x13, rom_version);
            info_pair("ROM version", std::to_string(rom_version));

            // todo add chip revision? requires exec-ing some code on the device now, as picoboot does not give
            //  us direct access to read hardware
        }
        bool first = true;
        for(const auto& group : groups) {
            if (group.enabled) {
                const auto& info = infos[group.name];
                fos.first_column(0);
                fos.hanging_indent(0);
                if (!first) {
                    fos.wrap_hard();
                } else {
                    first = false;
                }
                fos << group.name << "\n";
                fos.first_column(1);
                if (info.empty()) {
                   fos << "none\n";
                } else {
                    int tab = group.min_tab;
                    for(const auto& item : info) {
                        tab = std::max(tab, 3 + (int)item.first.length()); // +3 for ":  "
                    }
                    for(const auto& item : info) {
                        fos.first_column(1);
                        fos << (item.first + ":");
                        fos.first_column(1 + tab);
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

string missing_device_string(bool wasRetry) {
    char b[256];
    if (wasRetry) {
        strcpy(b, "Despite the reboot attempt, no ");
    } else {
        strcpy(b, "No ");
    }
    char *buf = b + strlen(b);
    int buf_len = b + sizeof(b) - buf;
    if (settings.address != -1) {
        if (settings.bus != -1) {
            snprintf(buf, buf_len, "accessible RP2040 device in BOOTSEL mode was found at bus %d, address %d.", settings.bus, settings.address);
        } else {
            snprintf(buf, buf_len, "accessible RP2040 devices in BOOTSEL mode were found with address %d.", settings.address);
        }
    } else {
        if (settings.bus != -1) {
            snprintf(buf, buf_len, "accessible RP2040 devices in BOOTSEL mode were found found on bus %d.", settings.bus);
        } else {
            snprintf(buf, buf_len,"accessible RP2040 devices in BOOTSEL mode were found.");
        }
    }
    return b;
}

bool help_command::execute(device_map &devices) {
    assert(false);
    return false;
}

bool info_command::execute(device_map &devices) {
    fos.first_column(0); fos.hanging_indent(0);
    if (!settings.filename.empty()) {
        auto access = get_file_memory_access();
        fos << "File " << settings.filename << ":\n\n";
        info_guts(access);
        return false;
    }
    int size = devices[dr_vidpid_bootrom_ok].size();
    if (size) {
        if (size > 1) {
            fos << "Multiple RP2040 devices in BOOTSEL mode found:\n";
        }
        for (auto handles : devices[dr_vidpid_bootrom_ok]) {
            fos.first_column(0); fos.hanging_indent(0);
            if (size > 1) {
                auto s = bus_device_string(handles.first);
                string dashes;
                std::generate_n(std::back_inserter(dashes), s.length() + 1, [] { return '-'; });
                fos << "\n" << s << ":\n" << dashes << "\n";
            }
            picoboot::connection connection(handles.second);
            picoboot_memory_access access(connection);
            info_guts(access);
        }
    } else {
        fail(ERROR_NO_DEVICE, missing_device_string(false));
    }
    return false;
}

static picoboot::connection get_single_bootsel_device_connection(device_map& devices, bool exclusive = true) {
    assert(devices[dr_vidpid_bootrom_ok].size() == 1);
    libusb_device_handle *rc = devices[dr_vidpid_bootrom_ok][0].second;
    if (!rc) fail(ERROR_USB, "Unable to connect to device");
    return picoboot::connection(rc, exclusive);
}

struct progress_bar {
    explicit progress_bar(string prefix, int width = 30) : prefix(std::move(prefix)), width(width) {
        progress(0);
    }

    void progress(int _percent) {
        if (_percent != percent) {
            percent = _percent;
            uint len = (width * percent) / 100;
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
            if (binary_end == 0) {
                fail(ERROR_NOT_POSSIBLE,
                     "Cannot determine the binary size, so cannot save the program only, try --all.");
            }
            end = binary_end;
        }
    } else {
        end = FLASH_START + guess_flash_size(raw_access);
        if (end <= FLASH_START) {
            fail(ERROR_NOT_POSSIBLE, "Cannot determine the flash size, so cannot save the entirety of flash, try --range.");
        }
    }

    enum memory_type t1 = get_memory_type(start);
    enum memory_type t2 = get_memory_type(end);
    if (t1 == invalid || t1 != t2) {
        fail(ERROR_NOT_POSSIBLE, "Save range crosses unmapped memory");
    }
    uint32_t size = end - start;

    std::function<void(FILE *out, const uint8_t *buffer, uint size, uint offset)> writer256 = [](FILE *out, const uint8_t *buffer, uint size, uint offset) { assert(false); };
    uf2_block block;
    memset(&block, 0, sizeof(block));
    switch (get_file_type()) {
        case filetype::bin:
//            if (start != FLASH_START) {
//                fail(ERROR_ARGS, "range must start at 0x%08x for saving as a BIN file", FLASH_START);
//            }
            writer256 = [](FILE *out, const uint8_t *buffer, uint actual_size, uint offset) {
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
            block.file_size = RP2040_FAMILY_ID;
            block.magic_end = UF2_MAGIC_END;
            writer256 = [&](FILE *out, const uint8_t *buffer, uint size, uint offset) {
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
    FILE *out = fopen(settings.filename.c_str(), "wb");
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
            std::cout << "Wrote " << ftell(out) << " bytes to " << settings.filename.c_str() << "\n";
            fclose(out);
        } catch (std::exception &) {
            fclose(out);
            throw;
        }
    }
    return false;
}

vector<range> get_coalesced_ranges(file_memory_access &file_access) {
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
            if( get_memory_type(i->from) == flash ) {
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

bool load_command::execute(device_map &devices) {
    if (settings.offset_set && get_file_type() != filetype::bin) {
        fail(ERROR_ARGS, "Offset only valid for BIN files");
    }
    auto file_access = get_file_memory_access();
    auto con = get_single_bootsel_device_connection(devices);
    picoboot_memory_access raw_access(con);
    range flash_binary_range(FLASH_START, FLASH_END);
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
    auto ranges = get_coalesced_ranges(file_access);
    for (auto mem_range : ranges) {
        enum memory_type t1 = get_memory_type(mem_range.from);
        enum memory_type t2 = get_memory_type(mem_range.to);
        if (t1 != t2 || t1 == invalid || t1 == rom) {
            fail(ERROR_FORMAT, "File to load contained an invalid memory range 0x%08x-0x%08x", mem_range.from,
                 mem_range.to);
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
    for (auto mem_range : ranges) {
        enum memory_type type = get_memory_type(mem_range.from);
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
        enum memory_type type = get_memory_type(mem_range.from);
        if (settings.load.verify) {
            bool ok = true;
            {
                progress_bar bar("Verifying " + memory_names[type] + ":    ");
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
                    for (uint i = 0; i < this_batch; i++) {
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
        con.reboot(flash == get_memory_type(start) ? 0 : start, SRAM_END, 500);
        std::cout << "\nThe device was rebooted to start the application.\n";
        return true;
    }
    return false;
}

bool verify_command::execute(device_map &devices) {
    if (settings.offset_set && get_file_type() != filetype::bin) {
        fail(ERROR_ARGS, "Offset only valid for BIN files");
    }
    auto file_access = get_file_memory_access();
    auto con = get_single_bootsel_device_connection(devices);
    picoboot_memory_access raw_access(con);
    auto ranges = get_coalesced_ranges(file_access);
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
            enum memory_type t1 = get_memory_type(mem_range.from);
            enum memory_type t2 = get_memory_type(mem_range.to);
            if (t1 != t2 || t1 == invalid) {
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
                        for(uint i=0;i<this_batch;i++) {
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
                    for(uint l=0;l<3;l++, display_from+=16) {
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
                            for (uint p = 0; p < 16; p++) {
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
                        for (uint p = 0; p < 16; p++) {
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

static int reboot_device(libusb_device *device, bool bootsel, uint disable_mask=0) {
    // ok, the device isn't in USB boot mode, let's try to reboot via vendor interface
    struct libusb_config_descriptor *config;
    int ret = libusb_get_active_config_descriptor(device, &config);
    if (ret) {
        fail(ERROR_USB, "Failed to get descriptor %d\n", ret);
    }
    libusb_device_handle *dev_handle;
    ret = libusb_open(device, &dev_handle);
    if (ret) {
#if _WIN32
        fail(ERROR_USB, "Unable to access device to reboot it; Make sure there is a driver installed via Zadig\n", ret);
#else
        fail(ERROR_USB, "Unable to access device to reboot it; Use sudo or setup a udev rule\n", ret);
#endif
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
}

bool reboot_command::execute(device_map &devices) {
    if (settings.force) {
        reboot_device(devices[dr_vidpid_stdio_usb][0].first, settings.reboot_usb);
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
        if (!settings.reboot_usb) {
            con.reboot(0, SRAM_END, 500);
        } else {
            picoboot_memory_access raw_access(con);
            uint program_base = SRAM_START;
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

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/ioctl.h>
#endif

void get_terminal_size(int& width, int& height) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    width = (int)(csbi.dwSize.X);
    height = (int)(csbi.dwSize.Y);
#elif defined(__linux__) || defined(__APPLE__)
    winsize w;
    ioctl(fileno(stdout), TIOCGWINSZ, &w);
    width = (int)(w.ws_col);
    height = (int)(w.ws_row);
#endif // Windows/Linux
}

void cancelled(int) {
    throw cancelled_exception();
}

int main(int argc, char **argv) {
    libusb_context *ctx = nullptr;

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
        for (int tries = 0; !rc && tries < 2; tries++) {
            if (ctx) {
                if (libusb_get_device_list(ctx, &devs) < 0) {
                    fail(ERROR_USB, "Failed to enumerate USB devices\n");
                }
                for (libusb_device **dev = devs; *dev; dev++) {
                    if (settings.bus != -1 && settings.bus != libusb_get_bus_number(*dev)) continue;
                    if (settings.address != -1 && settings.address != libusb_get_device_address(*dev)) continue;
                    libusb_device_handle *handle = nullptr;
                    auto result = picoboot_open_device(*dev, &handle);
                    if (handle) {
                        to_close.push_back(handle);
                    }
                    if (result != dr_error) {
                        devices[result].push_back(std::make_pair(*dev, handle));
                    }
                }
            }
            auto supported = selected_cmd->get_device_support();
            switch (supported) {
                case cmd::device_support::zero_or_more:
                    if (!settings.filename.empty()) break;
                    // fall thru
                case cmd::device_support::one:
                    if (devices[dr_vidpid_bootrom_ok].empty() &&
                        (!settings.force || devices[dr_vidpid_stdio_usb].empty())) {
                        bool had_note = false;
                        fos << missing_device_string(tries>0);
                        if (tries > 0) {
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
                                fos << bus_device_string(d.first) << description << "\n";
                            }
                        };
#if defined(__linux__) || defined(__APPLE__)
                        printer(dr_vidpid_bootrom_cant_connect,
                                " appears to be a RP2040 device in BOOTSEL mode, but picotool was unable to connect. Maybe try 'sudo' or check your permissions.");
#else
                        printer(dr_vidpid_bootrom_cant_connect,
                                " appears to be a RP2040 device in BOOTSEL mode, but picotool was unable to connect. You may need to install a driver. See \"Getting started with Raspberry Pi Pico\" for more information");
#endif
                        printer(dr_vidpid_picoprobe,
                                " appears to be a RP2040 PicoProbe device not in BOOTSEL mode.");
                        printer(dr_vidpid_micropython,
                                " appears to be a RP2040 MicroPython device not in BOOTSEL mode.");
                        if (selected_cmd->force_requires_pre_reboot()) {
#if defined(_WIN32)
                            printer(dr_vidpid_stdio_usb,
                                    " appears to be a RP2040 device with a USB serial connection, not in BOOTSEL mode. You can force reboot into BOOTSEL mode via 'picotool reboot -f -u' first.");
#else
                            printer(dr_vidpid_stdio_usb,
                                    " appears to be a RP2040 device with a USB serial connection, so consider -f (or -F) to force reboot in order to run the command.");
#endif
                        } else {
                            // special case message for what is actually just reboot (the only command that doesn't require reboot first)
                            printer(dr_vidpid_stdio_usb,
                                    " appears to be a RP2040 device with a USB serial connection, so consider -f to force the reboot.");
                        }
                        rc = ERROR_NO_DEVICE;
                    } else if (supported == cmd::device_support::one) {
                        if (devices[dr_vidpid_bootrom_ok].size() > 1 ||
                            (devices[dr_vidpid_bootrom_ok].empty() && devices[dr_vidpid_stdio_usb].size() > 1)) {
                            fail(ERROR_NOT_POSSIBLE, "Command requires a single RP2040 device to be targeted.");
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
                    if (devices[dr_vidpid_stdio_usb].size() != 1) {
                        fail(ERROR_NOT_POSSIBLE,
                             "Forced command requires a single rebootable RP2040 device to be targeted.");
                    }
                    if (selected_cmd->force_requires_pre_reboot()) {
                        // we reboot into BOOTSEL mode and disable MSC interface (the 1 here)
                        auto &to_reboot = devices[dr_vidpid_stdio_usb][0].first;
                        reboot_device(to_reboot, true, 1);
                        fos << "The device was asked to reboot into BOOTSEL mode so the command can be executed.\n\n";
                        for (const auto &handle : to_close) {
                            libusb_close(handle);
                        }
                        libusb_free_device_list(devs, 1);
                        devs = nullptr;
                        to_close.clear();
                        devices.clear();
                        sleep_ms(1200);

                        // we now clear settings.force, because we expect the device to have rebooted and be available.
                        // we also clear any filters, because the device may have moved, so the only way we can find it
                        // again is to assume it is the only now visible device.
                        settings.force = false;
                        settings.address = -1;
                        settings.bus = -1;
                        continue;
                    }
                }
                if (!selected_cmd->execute(devices) && tries) {
                    if (settings.force_no_reboot) {
                        fos << "\nThe device has been left accessible, but without the drive mounted; use 'picotool reboot' to reboot into regular BOOTSEL mode or application mode.\n";
                    } else {
                        // can only really do this with one device
                        if (devices[dr_vidpid_bootrom_ok].size() == 1) {
                            reboot_cmd.quiet = true;
                            reboot_cmd.execute(devices);
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
        std::cout << "ERROR: The RP2040 device returned an error: " << e.what() << "\n";
        rc = ERROR_UNKNOWN;
    } catch (picoboot::connection_error&) {
        std::cout << "ERROR: Communication with RP2040 device failed\n";
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

    return rc;
}
