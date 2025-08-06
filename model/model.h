/*
* Copyright (c) 2025 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "boot/uf2.h"
#include "boot/picoboot.h"

// Unreadable ROM data
#include "rp2350_a2_rom_end.h"
#include "rp2350_a3_rom_end.h"
#include "rp2350_a4_rom_end.h"

enum memory_type {
    rom,
    flash,
    sram,
    sram_unstriped,
    xip_sram,
    invalid,
};

typedef enum {
    rp2040,
    rp2350,
    unknown
} chip_t;

typedef enum {
    rp2040_b0,
    rp2040_b1,
    rp2040_b2,
    rp2350_a2,
    rp2350_a3,
    rp2350_a4,
    unknown_revision
} chip_revision_t;

#ifdef __cplusplus

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include "addresses.h"
#include "errors.h"

static std::string chip_name(chip_t chip) {
    std::string device = "RP-series";
    if (chip == rp2040) {
        device = "RP2040";
    } else if (chip == rp2350) {
        device = "RP2350";
    }
    return device;

}

// details of a specific chip/version. the most fleshed out version is derived from an actual device connection (usually by
// looking at the bootrom), however "stock" versions can be created from family IDs for example
class model_info {
public:
    model_info(chip_t chip, std::string name, uint32_t rom_end, std::set<picoboot_cmd_id> picoboot_cmds = {}) : _chip(chip), _name(std::move(name)),
        _rom_end(rom_end), _picoboot_cmds(std::move(picoboot_cmds)) {}
    chip_t chip() const { return _chip; }
    chip_revision_t chip_revision() const { return _chip_revision; }
    void set_chip_revision(chip_revision_t revision) { _chip_revision = revision; }
    uint32_t family_id() const { return _family_id; }
    void set_family_id(uint32_t family_id) { _family_id = family_id; }
    uint32_t rom_start() const { return ROM_START; }
    uint32_t rom_end() const { return _rom_end; }
    virtual enum memory_type get_memory_type(uint32_t addr) {
        if (addr >= rom_start() && addr <= rom_end()) {
            return rom;
        }
        return invalid;
    }

    virtual bool supports_picoboot_cmd(picoboot_cmd_id cmd) const {
        return _picoboot_cmds.find(cmd) != _picoboot_cmds.end();
    }

    virtual bool supports_picoboot_cmd(picoboot_cmd_id cmd, std::string& failed_device_name) const {
        if (!supports_picoboot_cmd(cmd)) {
            failed_device_name = name();
            return false;
        }
        return true;
    }
    bool supports_picoboot_cmds(std::set<picoboot_cmd_id> cmds, std::string& failed_device_name) const {
        for (auto cmd : cmds) {
            if (!supports_picoboot_cmd(cmd, failed_device_name)) {
                return false;
            }
        }
        return true;
    }
    std::string name() const { return _name; }
    virtual std::string revision_name() const {
        return "Unknown";
    }

    virtual bool supports_partition_table() { return false; }
    virtual bool supports_otp_v2() { return false; }
    virtual bool requires_block_loop() { return false; }
    virtual int rom_table_version() { fail(ERROR_NOT_POSSIBLE, "unknown rom table version"); return 0; }
    virtual uint32_t flash_start() { fail(ERROR_NOT_POSSIBLE, "unknown flash start"); return 0; }
    virtual uint32_t flash_end() { fail(ERROR_NOT_POSSIBLE, "unknown flash end"); return 0; }
    virtual uint32_t sram_start() { fail(ERROR_NOT_POSSIBLE, "unknown sram start"); return 0; }
    virtual uint32_t sram_end() { fail(ERROR_NOT_POSSIBLE, "unknown sram end"); return 0; }
    virtual uint32_t xip_sram_start() { fail(ERROR_NOT_POSSIBLE, "unknown xip sram start"); return 0; }
    virtual uint32_t xip_sram_end() { fail(ERROR_NOT_POSSIBLE, "unknown xip sram end"); return 0; }
    virtual uint32_t unreadable_rom_start() { return 0xffffffff; }
    virtual uint32_t unreadable_rom_end() { return 0xffffffff; }
    virtual const unsigned char *unreadable_rom_data() { return nullptr; }
private:
    std::string _name;
    chip_revision_t _chip_revision;
    uint32_t _rom_end;
    std::set<picoboot_cmd_id> _picoboot_cmds;
    chip_t _chip;
    uint32_t _family_id;
};

class model_unknown : public model_info {
public:
    // note we allow a small amount of ROM so that we can read the id bytes
    model_unknown() : model_info(unknown, chip_name(unknown), 0x100) {

    }
};

class model_rp : public model_info {
protected:
    model_rp(chip_t chip, std::string name, uint32_t rom_end, std::set<picoboot_cmd_id> picoboot_cmds = {}) : model_info(chip, std::move(name), rom_end, std::move(picoboot_cmds)) {}

public:
    enum memory_type get_memory_type(uint32_t addr) override {
        if (addr >= flash_start() && addr <= flash_end()) {
            return flash;
        }
        if (addr >= sram_start() && addr <= sram_end()) {
            return sram;
        }
        if (addr >= xip_sram_start() && addr <= xip_sram_end()) {
            return xip_sram;
        }
        return model_info::get_memory_type(addr);
    }

    uint32_t sram_start() override {
        return SRAM_START;
    }

    uint32_t flash_start() override {
        return FLASH_START;
    }

};

class model_rp_generic : public model_rp {
public:
    // allow large memory regions for generic model
    model_rp_generic() : model_rp(unknown, chip_name(unknown), 0x100, {}) {}

    uint32_t xip_sram_start() override {
        return std::min({XIP_SRAM_START_RP2040, XIP_SRAM_START_RP2350});
    }

    uint32_t xip_sram_end() override {
        return std::max({XIP_SRAM_END_RP2040, XIP_SRAM_END_RP2350});
    }

    uint32_t sram_end() override {
        return std::max({SRAM_END_RP2040, SRAM_END_RP2350});
    }

    uint32_t flash_end() override {
        return std::max({FLASH_END_RP2040, FLASH_END_RP2350});
    }
};

class model_rp2040 : public model_rp {
public:
    model_rp2040() : model_rp(rp2040, chip_name(rp2040), ROM_END_RP2040, {
                PC_EXCLUSIVE_ACCESS,
                PC_REBOOT,
                PC_FLASH_ERASE,
                PC_READ,
                PC_WRITE,
                PC_EXIT_XIP,
                PC_ENTER_CMD_XIP,
                PC_EXEC,
                PC_VECTORIZE_FLASH,
            }) {
                set_family_id(RP2040_FAMILY_ID);
            }

    enum memory_type get_memory_type(uint32_t addr) override {
        if (addr >= MAIN_RAM_BANKED_START && addr <= MAIN_RAM_BANKED_END) {
            return sram_unstriped;
        }
        return model_rp::get_memory_type(addr);
    }

    int rom_table_version() override {
        return 1;
    }

    uint32_t xip_sram_start() override {
        return XIP_SRAM_START_RP2040;
    }

    uint32_t xip_sram_end() override {
        return XIP_SRAM_END_RP2040;
    }

    uint32_t sram_end() override {
        return SRAM_END_RP2040;
    }

    uint32_t flash_end() override {
        return FLASH_END_RP2040;
    }

    virtual std::string revision_name() const override{
        switch (chip_revision()) {
            case rp2040_b0:
                return "B0";
            case rp2040_b1:
                return "B1";
            case rp2040_b2:
                return "B2";
            default:
                return model_rp::revision_name();
        }
    }

};

class model_rp2350 : public model_rp {
public:
    model_rp2350() : model_rp2350(ROM_END_RP2350) {}
    explicit model_rp2350(uint32_t rom_end) : model_rp(rp2350, rom_end > 0x8000 ? "RP2350(64k)" : "RP2350", rom_end, {
        PC_EXCLUSIVE_ACCESS,
        PC_REBOOT,
        PC_FLASH_ERASE,
        PC_READ,
        PC_WRITE,
        PC_EXIT_XIP,
        PC_ENTER_CMD_XIP,
        PC_EXEC,
        PC_REBOOT2,
        PC_GET_INFO,
        PC_OTP_READ,
        PC_OTP_WRITE,
    }) {}

    bool supports_partition_table() override { return true; }
    bool requires_block_loop() override { return true; }

    int rom_table_version() override {
        return 2;
    }

    uint32_t xip_sram_start() override {
        return XIP_SRAM_START_RP2350;
    }

    uint32_t xip_sram_end() override {
        return XIP_SRAM_END_RP2350;
    }

    uint32_t sram_end() override {
        return SRAM_END_RP2350;
    }

    uint32_t flash_end() override {
        return FLASH_END_RP2350;
    }

    uint32_t unreadable_rom_start() override {
        return rom_end() - 0x200;
    }

    uint32_t unreadable_rom_end() override {
        return rom_end();
    }

    const unsigned char *unreadable_rom_data() override {
        switch (chip_revision()) {
            case rp2350_a2:
                return rp2350_a2_rom_end;
            case rp2350_a3:
                return rp2350_a3_rom_end;
            case rp2350_a4:
                return rp2350_a4_rom_end;
            default:
                return nullptr;
        }
    }

    virtual std::string revision_name() const override{
        switch (chip_revision()) {
            case rp2350_a2:
                return "A2";
            case rp2350_a3:
                return "A3";
            case rp2350_a4:
                return "A4";
            default:
                return model_rp::revision_name();
        }
    }
};

class model_rp2350_arm_s : public model_rp2350 {
public:
    model_rp2350_arm_s() : model_rp2350() {
        set_family_id(RP2350_ARM_S_FAMILY_ID);
    }
};

class model_rp2350_arm_ns : public model_rp2350 {
public:
    model_rp2350_arm_ns() : model_rp2350() {
        set_family_id(RP2350_ARM_NS_FAMILY_ID);
    }
};

class model_rp2350_riscv : public model_rp2350 {
public:
    model_rp2350_riscv() : model_rp2350() {
        set_family_id(RP2350_RISCV_FAMILY_ID);
    }
};

struct models {
    static std::shared_ptr<model_info> unknown;
    static std::shared_ptr<model_info> largest;
};

typedef std::shared_ptr<model_info> model_t;

// inclusive of ends
static inline enum memory_type get_memory_type(uint32_t addr, const model_t& model) {
    return model->get_memory_type(addr);
}

static model_t model_from_family(uint32_t family_id) {
    if (family_id == RP2040_FAMILY_ID) {
        return std::make_shared<model_rp2040>();
    } else if (family_id >= RP2350_ARM_S_FAMILY_ID && family_id <= RP2350_ARM_NS_FAMILY_ID) {
        return std::make_shared<model_rp2350>();
    }
    return models::unknown;
}

// const address_ranges rp2040_address_ranges_flash {
//     address_range(FLASH_START, FLASH_END_RP2040, address_range::type::CONTENTS),
//     address_range(SRAM_START, SRAM_END_RP2040, address_range::type::NO_CONTENTS),
//     address_range(MAIN_RAM_BANKED_START, MAIN_RAM_BANKED_END, address_range::type::NO_CONTENTS)
// };
//
// const address_ranges rp2350_address_ranges_flash {
//     address_range(FLASH_START, FLASH_END_RP2350, address_range::type::CONTENTS),
//     address_range(SRAM_START, SRAM_END_RP2350, address_range::type::NO_CONTENTS),
//     address_range(MAIN_RAM_BANKED_START, MAIN_RAM_BANKED_END, address_range::type::NO_CONTENTS)
// };


static address_ranges address_ranges_flash(const model_t& model) {
    address_ranges ranges;
    ranges.emplace_back(model->flash_start(), model->flash_end(), address_range::type::CONTENTS);
    ranges.emplace_back(model->sram_start(), model->sram_end(), address_range::type::NO_CONTENTS);
    if (model->chip() == rp2040) {
        ranges.emplace_back(MAIN_RAM_BANKED_START, MAIN_RAM_BANKED_END, address_range::type::NO_CONTENTS);
    }
    return ranges;
}


//
// const address_ranges rp2040_address_ranges_ram {
//     address_range(SRAM_START, SRAM_END_RP2040, address_range::type::CONTENTS),
//     address_range(XIP_SRAM_START_RP2040, XIP_SRAM_END_RP2040, address_range::type::CONTENTS),
//     address_range(ROM_START, ROM_END_RP2040, address_range::type::IGNORE) // for now we ignore the bootrom if present
// };
//
// const address_ranges rp2350_address_ranges_ram {
//     address_range(SRAM_START, SRAM_END_RP2350, address_range::type::CONTENTS),
//     address_range(XIP_SRAM_START_RP2350, XIP_SRAM_END_RP2350, address_range::type::CONTENTS),
//     address_range(ROM_START, ROM_END_RP2350, address_range::type::IGNORE) // for now we ignore the bootrom if present
// };

static address_ranges address_ranges_ram(const model_t& model) {
    address_ranges ranges;
    ranges.emplace_back(model->sram_start(), model->sram_end(), address_range::type::CONTENTS);
    ranges.emplace_back(model->xip_sram_start(), model->xip_sram_end(), address_range::type::CONTENTS);
    ranges.emplace_back(model->rom_start(), model->rom_end(), address_range::type::IGNORE);
    return ranges;
}

static bool contains_unreadable_rom(uint32_t addr, uint32_t size, const model_t& model) {
    return (addr >= model->unreadable_rom_start() && addr < model->unreadable_rom_end()) ||
            (addr + size > model->unreadable_rom_start() && addr + size <= model->unreadable_rom_end()) ||
            (addr < model->unreadable_rom_start() && addr + size > model->unreadable_rom_end());
}

#endif