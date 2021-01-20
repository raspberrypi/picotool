/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdexcept>
#include <system_error>
#include <map>
#include <algorithm>
#include "picoboot_connection_cxx.h"

using picoboot::connection;
using picoboot::connection_error;
using picoboot::command_failure;

std::map<enum picoboot_status, const char *> status_code_strings = {
        {picoboot_status::PICOBOOT_OK, "ok"},
        {picoboot_status::PICOBOOT_BAD_ALIGNMENT, "bad address alignment"},
        {picoboot_status::PICOBOOT_INTERLEAVED_WRITE, "interleaved write"},
        {picoboot_status::PICOBOOT_INVALID_ADDRESS, "invalid address"},
        {picoboot_status::PICOBOOT_INVALID_CMD_LENGTH, "invalid cmd length"},
        {picoboot_status::PICOBOOT_INVALID_TRANSFER_LENGTH, "invalid transfer length"},
        {picoboot_status::PICOBOOT_REBOOTING, "rebooting"},
        {picoboot_status::PICOBOOT_UNKNOWN_CMD, "unknown cmd"},
};

const char *command_failure::what() const noexcept {
    auto f = status_code_strings.find((enum picoboot_status) code);
    if (f != status_code_strings.end()) {
        return f->second;
    } else {
        return "<unknown>";
    }
}

template <typename F> void connection::wrap_call(F&& func) {
    int rc = func();
#if 0
    // we should always get a failure if there is an error, hence this is NDEBUG
    if (!rc) {
        struct picoboot_cmd_status status;
        rc = picoboot_cmd_status(device, &status);
        if (!rc) {
            assert(!status.dStatusCode);
        }
    }
#endif
    if (rc) {
        struct picoboot_cmd_status status;
        status.dStatusCode = 0;
        rc = picoboot_cmd_status(device, &status);
        if (!rc) {
            throw command_failure(status.dStatusCode ? (int)status.dStatusCode : PICOBOOT_UNKNOWN_ERROR);
        }
        throw connection_error(rc);
    }
}

void connection::reset() {
    wrap_call([&] { return picoboot_reset(device); });
}

void connection::exclusive_access(uint8_t exclusive) {
    wrap_call([&] { return picoboot_exclusive_access(device, exclusive); });
}

void connection::enter_cmd_xip() {
    wrap_call([&] { return picoboot_enter_cmd_xip(device); });
}

void connection::exit_xip() {
    wrap_call([&] { return picoboot_exit_xip(device); });
}

void connection::reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    wrap_call([&] { return picoboot_reboot(device, pc, sp, delay_ms); });
}

void connection::exec(uint32_t addr) {
    wrap_call([&] { return picoboot_exec(device, addr); });
}

void connection::flash_erase(uint32_t addr, uint32_t len) {
    wrap_call([&] { return picoboot_flash_erase(device, addr, len); });
}

void connection::vector(uint32_t addr) {
    wrap_call([&] { return picoboot_vector(device, addr); });
}

void connection::write(uint32_t addr, uint8_t *buffer, uint32_t len) {
    wrap_call([&] { return picoboot_write(device, addr, buffer, len); });
}

void connection::read(uint32_t addr, uint8_t *buffer, uint32_t len) {
    wrap_call([&] { return picoboot_read(device, addr, buffer, len); });
}
