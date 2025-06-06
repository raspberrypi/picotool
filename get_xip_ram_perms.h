/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <memory>
#include <fstream>

std::shared_ptr<std::iostream> get_xip_ram_perms();
