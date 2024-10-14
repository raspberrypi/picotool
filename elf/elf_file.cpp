/*
 * Copyright (c) 2024 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

#include "elf.h"
#include "elf_file.h"
#include "errors.h"

#include "portable_endian.h"

// tsk namespace is polluted on windows
#ifdef _WIN32
#undef min
#undef max

#define _CRT_SECURE_NO_WARNINGS
#endif

void eh_he(elf32_header &eh) {
    // Swap to host endianness
    eh.common.magic    = le32toh(eh.common.magic);
    eh.common.type     = le16toh(eh.common.type);
    eh.common.machine  = le16toh(eh.common.machine);
    eh.common.version2 = le32toh(eh.common.version2);
    eh.entry           = le32toh(eh.entry);
    eh.ph_offset       = le32toh(eh.ph_offset);
    eh.sh_offset       = le32toh(eh.sh_offset);
    eh.flags           = le32toh(eh.flags);
    eh.eh_size         = le16toh(eh.eh_size);
    eh.ph_entry_size   = le16toh(eh.ph_entry_size);
    eh.ph_num          = le16toh(eh.ph_num);
    eh.sh_entry_size   = le16toh(eh.sh_entry_size);
    eh.sh_num          = le16toh(eh.sh_num);
    eh.sh_str_index    = le16toh(eh.sh_str_index);
}
void eh_le(elf32_header &eh) {
    // Swap to little endianness
    eh.common.magic    = htole32(eh.common.magic);
    eh.common.type     = htole16(eh.common.type);
    eh.common.machine  = htole16(eh.common.machine);
    eh.common.version2 = htole32(eh.common.version2);
    eh.entry           = htole32(eh.entry);
    eh.ph_offset       = htole32(eh.ph_offset);
    eh.sh_offset       = htole32(eh.sh_offset);
    eh.flags           = htole32(eh.flags);
    eh.eh_size         = htole16(eh.eh_size);
    eh.ph_entry_size   = htole16(eh.ph_entry_size);
    eh.ph_num          = htole16(eh.ph_num);
    eh.sh_entry_size   = htole16(eh.sh_entry_size);
    eh.sh_num          = htole16(eh.sh_num);
    eh.sh_str_index    = htole16(eh.sh_str_index);
}
void ph_he(elf32_ph_entry &ph) {
    // Swap to host endianness
    ph.type     = le32toh(ph.type);
    ph.offset   = le32toh(ph.offset);
    ph.vaddr    = le32toh(ph.vaddr);
    ph.paddr    = le32toh(ph.paddr);
    ph.filez    = le32toh(ph.filez);
    ph.memsz    = le32toh(ph.memsz);
    ph.flags    = le32toh(ph.flags);
    ph.align    = le32toh(ph.align);
}
void ph_le(elf32_ph_entry &ph) {
    // Swap to little endianness
    ph.type     = htole32(ph.type);
    ph.offset   = htole32(ph.offset);
    ph.vaddr    = htole32(ph.vaddr);
    ph.paddr    = htole32(ph.paddr);
    ph.filez    = htole32(ph.filez);
    ph.memsz    = htole32(ph.memsz);
    ph.flags    = htole32(ph.flags);
    ph.align    = htole32(ph.align);
}
void sh_he(elf32_sh_entry &sh) {
    // Swap to host endianness
    sh.name         = le32toh(sh.name);
    sh.type         = le32toh(sh.type);
    sh.flags        = le32toh(sh.flags);
    sh.addr         = le32toh(sh.addr);
    sh.offset       = le32toh(sh.offset);
    sh.size         = le32toh(sh.size);
    sh.link         = le32toh(sh.link);
    sh.info         = le32toh(sh.info);
    sh.addralign    = le32toh(sh.addralign);
    sh.entsize      = le32toh(sh.entsize);
}
void sh_le(elf32_sh_entry &sh) {
    // Swap to little endianness
    sh.name         = htole32(sh.name);
    sh.type         = htole32(sh.type);
    sh.flags        = htole32(sh.flags);
    sh.addr         = htole32(sh.addr);
    sh.offset       = htole32(sh.offset);
    sh.size         = htole32(sh.size);
    sh.link         = htole32(sh.link);
    sh.info         = htole32(sh.info);
    sh.addralign    = htole32(sh.addralign);
    sh.entsize      = htole32(sh.entsize);
}
void sym_he(elf32_sym_entry &sym) {
    // Swap to host endianness
    sym.name    = le32toh(sym.name);
    sym.value   = le32toh(sym.value);
    sym.size    = le32toh(sym.size);
    sym.info    = le32toh(sym.info);
    sym.other   = le32toh(sym.other);
    sym.shndx   = le32toh(sym.shndx);
}
void sym_le(elf32_sym_entry &sym) {
    // Swap to little endianness
    sym.name    = htole32(sym.name);
    sym.value   = htole32(sym.value);
    sym.size    = htole32(sym.size);
    sym.info    = htole32(sym.info);
    sym.other   = htole32(sym.other);
    sym.shndx   = htole32(sym.shndx);
}

// Checks whether an ELF header is compatible with RP2040 / RP3050
// Returns zero on success
int rp_check_elf_header(const elf32_header &eh) {
    if (eh.common.magic != ELF_MAGIC) {
        fail(ERROR_FORMAT, "Not an ELF file");
    }
    if (eh.common.version != 1 || eh.common.version2 != 1) {
        fail(ERROR_FORMAT, "Unrecognized ELF version");
    }
    if (eh.common.arch_class != 1 || eh.common.endianness != 1) {
        fail(ERROR_INCOMPATIBLE, "Require 32 bit little-endian ELF");
    }
    if (eh.eh_size != sizeof(struct elf32_header)) {
        fail(ERROR_FORMAT, "Invalid ELF32 format");
    }
    if (eh.common.machine != EM_ARM && eh.common.machine != EM_RISCV) {
        fail(ERROR_FORMAT, "Not an Arm or RISC-V executable");
    }
    // Accept either ELFOSABI_NONE or ELFOSABI_GNU for EI_OSABI. Compilers may
    // set the OS/ABI field to ELFOSABI_GNU when they use GNU features, such as
    // the SHF_GNU_RETAIN section flag, but the binary is still compatible.
    if (eh.common.abi != 0 /* NONE */ && eh.common.abi != 3 /* GNU */) {
        fail(ERROR_INCOMPATIBLE, "Unrecognized ABI");
    }
    // todo amy not sure if this should be expected or not - we have HARD float in clang only for now
    if (eh.flags & EF_ARM_ABI_FLOAT_HARD) {
//        fail(ERROR_INCOMPATIBLE, "HARD-FLOAT not supported");
    }
    return 0;
}

// Determine binary type (flash or ram)
int rp_determine_binary_type(const elf32_header &eh, const std::vector<elf32_ph_entry>& entries, address_ranges flash_range, address_ranges ram_range, bool *ram_style) {
    for(const auto &entry : entries) {
        if (entry.type == PT_LOAD && entry.memsz) {
            unsigned int mapped_size = std::min(entry.filez, entry.memsz);
            if (mapped_size) {
                // we back convert the entrypoint from a VADDR to a PADDR to see if it originates in flash, and if
                // so call THAT a flash binary.
                if (eh.entry >= entry.vaddr && eh.entry < entry.vaddr + mapped_size) {
                    uint32_t effective_entry = eh.entry + entry.paddr - entry.vaddr;
                    if (is_address_initialized(ram_range, effective_entry)) {
                        *ram_style = true;
                        return 0;
                    } else if (is_address_initialized(flash_range, effective_entry)) {
                        *ram_style = false;
                        return 0;
                    }
                }
            }
        }
    }
    fail(ERROR_INCOMPATIBLE, "entry point is not in mapped part of file");
    return ERROR_INCOMPATIBLE;
}

void elf_file::read_bytes(unsigned offset, unsigned length, void *dest) {
    if (offset + length > elf_bytes.size()) {
        fail(ERROR_FORMAT, "ELF File Read from 0x%x with size 0x%x exceeds the file size 0x%x", offset, length, elf_bytes.size());
    }
    memcpy(dest, &elf_bytes[offset], length);
}

int elf_file::read_header(void) {
    read_bytes(0, sizeof(eh), &eh);
    eh_he(eh);  // swap to Host for processing
    return rp_check_elf_header(eh);
}

// Flattens the data in the section array the elf_bytes blob
void elf_file::flatten(void) {
    elf_bytes.resize(sizeof(eh));
    auto eh_out = eh;
    eh_le(eh_out);    // swap to LE for writing
    memcpy(&elf_bytes[0], &eh_out, sizeof(eh_out));

    elf_bytes.resize(std::max(eh.ph_offset + sizeof(elf32_ph_entry) * eh.ph_num, elf_bytes.size()));
    auto ph_entries_out = ph_entries;
    for (auto ph : ph_entries_out) {
        ph_le(ph);  // swap to LE for writing
    }
    memcpy(&elf_bytes[eh.ph_offset], &ph_entries_out[0], sizeof(elf32_ph_entry) * eh.ph_num);

    elf_bytes.resize(std::max(eh.sh_offset + sizeof(elf32_sh_entry) * eh.sh_num, elf_bytes.size()));
    auto sh_entries_out = sh_entries;
    for (auto sh : sh_entries_out) {
        sh_le(sh);  // swap to LE for writing
    }
    memcpy(&elf_bytes[eh.sh_offset], &sh_entries_out[0], sizeof(elf32_sh_entry) * eh.sh_num);

    int idx = 0;
    for (const auto &sh : sh_entries) {
        if (sh.size && sh.type != SHT_NOBITS) {
            elf_bytes.resize(std::max(sh.offset + sh.size, (uint32_t)elf_bytes.size()));
            memcpy(&elf_bytes[sh.offset], &sh_data[idx][0], sh.size);
        }
        idx++;
    }

    idx = 0;
    for (const auto &ph : ph_entries) {
        if (ph.filez) {
            elf_bytes.resize(std::max(ph.offset + ph.filez, (uint32_t)elf_bytes.size()));
            memcpy(&elf_bytes[ph.offset], &ph_data[idx][0], ph.filez);
        }
        idx++;
    }
    if (verbose) printf("Elf file size %zu\n", elf_bytes.size());
}

void elf_file::write(std::shared_ptr<std::iostream> out) {
    flatten();
    out->exceptions(std::iostream::failbit | std::iostream::badbit);
    if (verbose) printf("Writing %lu bytes to file\n", elf_bytes.size());
    out->write(reinterpret_cast<const char*>(&elf_bytes[0]), elf_bytes.size());
}

void elf_file::read_sh(void) {
    if (verbose) printf("%s sh offset %u #entries %d\n", __func__, eh.sh_offset, eh.sh_num);
    if (eh.sh_num) {
        sh_entries.resize(eh.sh_num);
        read_bytes(eh.sh_offset, sizeof(elf32_sh_entry) * eh.sh_num, &sh_entries[0]);
        for (auto sh : sh_entries) {
            sh_he(sh);  // swap to Host for processing
        }
    }
}

// Read the section data from the internal byte array into discrete sections.
// This is used after modifying segments but before inserting new segments
void elf_file::read_sh_data(void) {
    int sh_idx = 0;
    sh_data.resize(eh.sh_num);
    for (const auto &sh: sh_entries) {
        if (sh.size && sh.type != SHT_NOBITS) {
            sh_data[sh_idx].resize(sh.size);
            read_bytes(sh.offset, sh.size, &sh_data[sh_idx][0]);
        }
        sh_idx++;
    }
}

void elf_file::read_ph_data(void) {
    int ph_idx = 0;
    ph_data.resize(eh.ph_num);
    for (const auto &ph: ph_entries) {
        if (ph.filez) {
            ph_data[ph_idx].resize(ph.filez);
            read_bytes(ph.offset, ph.filez, &ph_data[ph_idx][0]);
        }
        ph_idx++;
    }
}

const std::string elf_file::section_name(uint32_t sh_name) const {
    if (!eh.sh_str_index || eh.sh_str_index > eh.sh_num)
        return "";

    if (sh_name > sh_data[eh.sh_str_index].size())
        return "";

    const char * str =(const char *) &sh_data[eh.sh_str_index][0];
    return &str[sh_name];
}

const elf32_sh_entry* elf_file::get_section(const std::string &sh_name) {
    for (unsigned int i = 0; i < sh_entries.size(); i++) {
        if (section_name(sh_entries[i].name) == sh_name) {
            return &sh_entries[i];
        }
    }
    return NULL;
}

uint32_t elf_file::get_symbol(const std::string &sym_name) {
    auto sym_tab = get_section(".symtab");
    auto str_tab = get_section(".strtab");
    if (!sym_tab || !str_tab) {
        return 0;
    }
    auto data = content(*sym_tab);
    auto strings = content(*str_tab);
    const char * str =(const char *) strings.data();
    for (unsigned int i=0; i < sym_tab->size / sizeof(elf32_sym_entry); i++) {
        elf32_sym_entry sym;
        memcpy(&sym, data.data() + i*sizeof(elf32_sym_entry), sizeof(elf32_sym_entry));
        sym_he(sym);    // swap to Host for processing
        if (&str[sym.name] == sym_name) {
            return sym.value;
        }
    }
    return 0;
}

uint32_t elf_file::append_section_name(const std::string &sh_name_str) {
    // Create byte array with new section name
    std::vector<uint8_t> name_bytes(sh_name_str.begin(), sh_name_str.end());
    name_bytes.push_back(0);

    // Append the byte array to section header table remembering the offset
    // of the start of the string for the new section
    elf32_sh_entry &shstrtab = sh_entries[eh.sh_str_index];
    std::vector<uint8_t> &shstrtab_data = sh_data[eh.sh_str_index];
    sh_entries[eh.sh_str_index].size += name_bytes.size();
    uint32_t sh_name = shstrtab_data.size();
    shstrtab_data.insert(shstrtab_data.end(), name_bytes.begin(), name_bytes.end());

    // Move offsets for anything stored after the resized section header table
    for (auto &sh: sh_entries) {
        if (sh.offset > shstrtab.offset)
            sh.offset += name_bytes.size();
    }
    for (auto &ph: ph_entries) {
        if (ph.offset > shstrtab.offset)
            ph.offset += name_bytes.size();
    }
    return sh_name;
}

void elf_file::dump(void) const {
    for (const auto &ph: ph_entries) {
        printf("PH offset %08x vaddr %08x paddr %08x size %08x type %08x\n",
            ph.offset, ph.vaddr, ph.paddr, ph.memsz, ph.type);
    }

    int sh_idx = 0;
    for (const auto &sh: sh_entries) {
        printf("SH[%d] %20s addr %08x offset %08x size %08x type %08x\n",
            sh_idx, section_name(sh.name).c_str(), sh.addr, sh.offset, sh.size, sh.type);
        sh_idx++;
    }
}

void elf_file::move_all(int dist) {
    if (verbose) printf("Incrementing all paddr by %d\n", dist);
    for (auto &ph: ph_entries) {
        ph.paddr += dist;
    }
}

void elf_file::read_ph(void) {
    if (verbose) printf("%s ph offset %u #entries %d\n", __func__, eh.ph_offset, eh.ph_num);
    if (eh.ph_num) {
        ph_entries.resize(eh.ph_num);
        read_bytes(eh.ph_offset, sizeof(elf32_ph_entry) * eh.ph_num, &ph_entries[0]);
        for (auto ph : ph_entries) {
            ph_he(ph);  // swap to Host for processing
        }
    }
}

int elf_file::read_file(std::shared_ptr<std::iostream> file) {
    int rc = 0;
    try {
        elf_bytes = read_binfile(file);
        int rc = read_header();
        if (!rc) {
            read_ph();
            read_sh();
        }
        read_sh_data();
        read_ph_data();
    }
    catch (const std::ios_base::failure &e) {
        std::cerr << "Failed to read elf file" << std::endl;
        rc = -1;
    }
    return rc;
}

uint32_t elf_file::lowest_section_offset(void) const {
    uint32_t offset = eh.sh_offset; // Section header offset is after the data
    for (const auto &sh: sh_entries) {
        if (sh.type != SHT_NULL && sh.offset > 0 && sh.offset < offset) {
            offset = sh.offset;
        }
    }
    return offset;
}

uint32_t elf_file::highest_section_offset(void) const {
    uint32_t offset = 0; // Section header offset is after the data
    for (const auto &sh: sh_entries) {
        if (sh.type != SHT_NULL && sh.offset > 0 && sh.offset >= offset) {
            offset = sh.offset + sh.size;
        }
    }
    return offset;
}

std::vector<uint8_t> elf_file::content(const elf32_ph_entry &ph) const {
    std::vector<uint8_t> content;
    std::copy(elf_bytes.begin() + ph.offset, elf_bytes.begin() + ph.offset + ph.filez, std::back_inserter(content));
    return content;
}

std::vector<uint8_t> elf_file::content(const elf32_sh_entry &sh) const {
    std::vector<uint8_t> content;
    std::copy(elf_bytes.begin() + sh.offset, elf_bytes.begin() + sh.offset + sh.size, std::back_inserter(content));
    return content;
}

void elf_file::content(const elf32_ph_entry &ph, const std::vector<uint8_t> &content) {
    if (!editable) return;
    assert(content.size() <= ph.filez);
    if (verbose) printf("Update segment content offset %x content size %zx physical size %x\n", ph.offset, content.size(), ph.filez);
    memcpy(&elf_bytes[ph.offset], &content[0], std::min(content.size(), (size_t) ph.filez));
    read_sh_data(); // Extract the sections after modifying the content
    read_ph_data();
}

void elf_file::content(const elf32_sh_entry &sh, const std::vector<uint8_t> &content) {
    if (!editable) return;
    assert(content.size() <= sh.size);
    if (verbose) printf("Update section content offset %x content size %zx section size %x\n", sh.offset, content.size(), sh.size);
    memcpy(&elf_bytes[sh.offset], &content[0], std::min(content.size(), (size_t) sh.size));
    read_sh_data();  // Extract the sections after modifying the content
    read_ph_data();
}

const elf32_ph_entry* elf_file::segment_from_physical_address(uint32_t paddr) {
    for (int i = 0; i < eh.ph_num; i++) {
        if (paddr >= ph_entries[i].paddr && paddr < ph_entries[i].paddr + ph_entries[i].filez) {
            if (verbose) printf("segment %d contains physical address %x\n", i, paddr);
            return &ph_entries[i];
        }
    }
    return nullptr;
}

const elf32_ph_entry* elf_file::segment_from_virtual_address(uint32_t vaddr) {
    for (int i = 0; i < eh.ph_num; i++) {
        if (vaddr >= ph_entries[i].vaddr && vaddr < ph_entries[i].vaddr + ph_entries[i].memsz) {
            if (verbose) printf("segment %d contains virtual address %x\n", i, vaddr);
            return &ph_entries[i];
        }
    }
    return NULL;
}

// Appends a new segment and section - filled with zeros
// Use content to replace the content
const elf32_ph_entry& elf_file::append_segment(uint32_t vaddr, uint32_t paddr, uint32_t size, const std::string &name) {
    elf32_ph_entry ph;
    read_sh_data(); // Convert the section data back into discreet chunks
    uint32_t sh_name = append_section_name(name);

    ph.type = PT_LOAD;
    ph.flags = PF_R; // Readable segment
    ph.paddr = paddr;
    ph.vaddr = vaddr;
    ph.filez = size;
    ph.memsz = size;
    ph.align = 2;

    if (verbose) {
        std::cout << "new segment " << name <<
            " paddr " << std::hex << paddr <<
            " vaddr " << std::hex << vaddr <<
            " size " << std::hex << size << std::endl;
    }
    ph_entries.push_back(ph);
    eh.ph_num++;

    // There's normally space between the end of the program header table and the start of data to
    // squeeze in another program header. If not, shuffle everything by 4K;
    uint32_t lso = lowest_section_offset();
    if (lso < eh.ph_offset + eh.ph_entry_size * eh.ph_num) {

        // Move the segment offsets
        for (auto &ph: ph_entries) {
            ph.offset += 0x1000;
        }

        // Move section header table and each section offset
        eh.sh_offset += 0x1000;
        for (auto &sh : sh_entries) {
            sh.offset += 0x1000;
        }
    }

    // Append the new signature section
    elf32_sh_entry sh = {};
    sh.name = sh_name;
    sh.type = SHT_PROGBITS;
    sh.flags = SHF_ALLOC;
    sh.addr = ph.vaddr;
    sh.size = size;
    uint32_t hso = highest_section_offset();
    sh.offset = hso;

    // Add the new segment for the signature and point to offset in file for data
    sh_entries.push_back(sh);
    sh_data.push_back(std::vector<uint8_t>(size));
    ph_entries.back().offset = sh.offset;
    ph_data.push_back(std::vector<uint8_t>(size));

    eh.sh_offset = sh.offset + sh.size;
    eh.sh_num++;

    if (verbose) printf("%s sig offset %08x num sections %u\n", __func__, sh.offset, eh.sh_num);
    flatten();
    return ph_entries.back();
}

 std::vector<uint8_t> elf_file::read_binfile(std::shared_ptr<std::iostream> in) {
    std::vector<uint8_t> data;
    in->exceptions(std::iostream::failbit | std::iostream::badbit);
    in->seekg(0, in->end);
    data.resize(in->tellg());
    in->seekg(0, in->beg);
    in->read(reinterpret_cast<char *>(&data[0]), data.size());
    return data;
}
