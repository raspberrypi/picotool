/*
 * Copyright (c) 2024 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ELF_FILE_H
#define ELF_FILE_H

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "elf.h"

#include "addresses.h"

class elf_file {
public:
    elf_file(bool verbose = false) : verbose(verbose) {};
    int read_file(std::shared_ptr<std::iostream> file);
    void write(std::shared_ptr<std::iostream> file);

    const elf32_ph_entry& append_segment(uint32_t vaddr, uint32_t paddr, uint32_t size, const std::string &section_name);

    const elf32_header &header(void) const {return eh;}
    const std::vector<elf32_ph_entry> &segments(void) const {return ph_entries;}
    const std::vector<elf32_sh_entry> &sections(void) const {return sh_entries;}

    std::vector<uint8_t> content(const elf32_ph_entry &ph) const;
    std::vector<uint8_t> content(const elf32_sh_entry &sh) const;
    void content(const elf32_ph_entry &ph, const std::vector<uint8_t> &content);
    void content(const elf32_sh_entry &sh, const std::vector<uint8_t> &content);

    uint32_t lowest_section_offset(void) const;
    uint32_t highest_section_offset(void) const;
    const elf32_sh_entry* get_section(const std::string &sh_name);
    uint32_t get_symbol(const std::string &sym_name);
    const std::string section_name(uint32_t sh_name) const;
    const elf32_ph_entry* segment_from_physical_address(uint32_t paddr);
    const elf32_ph_entry* segment_from_virtual_address(uint32_t vaddr);
    void dump(void) const;

    void move_all(int dist);

    static std::vector<uint8_t> read_binfile(std::shared_ptr<std::iostream> file);

    bool editable = true;
private:
    int read_header(void);
    void read_ph(void);
    void read_sh(void);
    void read_sh_data(void);
    void read_ph_data(void);
    void read_bytes(unsigned offset, unsigned length, void *dest);
    uint32_t append_section_name(const std::string &sh_name_str);
    void flatten(void);

private:
    elf32_header eh;
    std::vector<uint8_t> elf_bytes;
    std::vector<elf32_ph_entry> ph_entries;
    std::vector<elf32_sh_entry> sh_entries;
    std::vector<std::vector<uint8_t>> sh_data;
    std::vector<std::vector<uint8_t>> ph_data;
    bool verbose;
};
int rp_check_elf_header(const elf32_header &eh);
int rp_determine_binary_type(const elf32_header &eh, const std::vector<elf32_ph_entry>& entries, address_ranges flash_range, address_ranges ram_range, bool *ram_style);
#endif