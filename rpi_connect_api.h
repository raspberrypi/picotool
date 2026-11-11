/**
 * Copyright (c) 2025 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <functional>
#include <string>

// The provisioning calls to the Raspberry Pi Connect API that need no device
// identity key, made from the host. They match those in the SDK's
// pico_rpi_connect, which the provisioning binary makes from the device.
namespace rpi_connect_api {
    extern const char *const default_client_id;

    // Exchange a provisioning auth key for an access token
    std::string auth_key_exchange(const std::string &auth_key, const std::string &serial_number,
                                  const std::string &device_name, const std::string &client_id);

    // Device-code signin: show(url, user_code) is called once the code is
    // issued, and the access token is returned once the user has signed in,
    // or an empty string if cancelled() returns true while waiting
    std::string signin(const std::string &client_id, const std::string &serial_number,
                       const std::function<void(const std::string &, const std::string &)> &show,
                       const std::function<bool()> &cancelled);
}
