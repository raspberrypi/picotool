/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _ELF2UF2_H
#define _ELF2UF2_H


#include <cstdio>
#include <fstream>

#include "boot/uf2.h"

#include "elf_file.h"

// we require 256 (as this is the page size supported by the device)
#define LOG2_PAGE_SIZE 8u
#define UF2_PAGE_SIZE (1u << LOG2_PAGE_SIZE)


bool check_abs_block(uf2_block block);
int bin2uf2(std::shared_ptr<std::iostream> in, std::shared_ptr<std::iostream> out, uint32_t address, uint32_t family_id, uint32_t abs_block_loc=0);
int elf2uf2(std::shared_ptr<std::iostream> in, std::shared_ptr<std::iostream> out, uint32_t family_id, uint32_t package_addr=0, uint32_t abs_block_loc=0);


#endif