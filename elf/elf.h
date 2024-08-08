/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _ELF_H
#define _ELF_H

#include <stdint.h>

#define ELF_MAGIC 0x464c457fu

#define EM_ARM 0x28u
#define EM_RISCV 0xf3u

#define EF_ARM_ABI_FLOAT_HARD 0x00000400u

#define PT_LOAD 0x00000001u

#define PF_X 0x1u
#define PF_W 0x2u
#define PF_R 0x4u

#define SHT_NULL 0x00000000u
#define SHT_PROGBITS 0x00000001u
#define SHT_NOBITS 0x00000008u
#define SHF_ALLOC 0x00000002u

#ifdef __cplusplus
#include <string>
#include <sstream>
#endif

#pragma pack(push, 1)
struct elf_header {
    uint32_t    magic;
    uint8_t     arch_class;
    uint8_t     endianness;
    uint8_t     version;
    uint8_t     abi;
    uint8_t     abi_version;
    uint8_t     _pad[7];
    uint16_t    type;
    uint16_t    machine;
    uint32_t    version2;
};

struct elf32_header {
    struct elf_header common;
    uint32_t    entry;
    uint32_t    ph_offset;
    uint32_t    sh_offset;
    uint32_t    flags;
    uint16_t    eh_size;
    uint16_t    ph_entry_size;
    uint16_t    ph_num;
    uint16_t    sh_entry_size;
    uint16_t    sh_num;
    uint16_t    sh_str_index;
};

struct elf32_ph_entry {
#ifdef __cplusplus
    uint32_t physical_address(void) const {return paddr;};
    uint32_t physical_size(void) const {return filez;};
    uint32_t virtual_address(void) const {return vaddr;};
    uint32_t virtual_size(void) const {return memsz;};
    bool is_load(void) const {return type == 0x1;};
#endif
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filez;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
};

#ifdef __cplusplus
typedef elf32_ph_entry Segment;
inline std::string to_string(const elf32_ph_entry &ph)  {
    std::stringstream ss;
    ss << "segment paddr " << std::hex << ph.paddr << " vaddr " << std::hex << ph.vaddr;
    return ss.str();
}
#endif

struct elf32_sh_entry {
#ifdef __cplusplus
    uint32_t virtual_address(void) const {return addr;};
#endif
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
};

#ifdef __cplusplus
typedef elf32_sh_entry Section;
inline std::string to_string(const elf32_sh_entry &sh)  {
    std::stringstream ss;
    ss << std::hex << sh.addr;
    return ss.str();
}
#endif

struct elf32_sym_entry {
	uint32_t name;
	uint32_t value;
	uint32_t size;
	uint8_t info;
	uint8_t other;
	uint16_t shndx;
};

#pragma pack(pop)

#endif