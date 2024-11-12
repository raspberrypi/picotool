/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <map>
#include <set>
#include <vector>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <cstring>
#include <memory>

#include "elf2uf2.h"
#include "errors.h"

#define FLASH_SECTOR_ERASE_SIZE 4096u

static bool verbose;

static void fail_read_error() {
    fail(ERROR_READ_FAILED, "Failed to read input file");
}

static void fail_write_error() {
    fail(ERROR_WRITE_FAILED, "Failed to write output file");
}

struct page_fragment {
    page_fragment(uint32_t file_offset, uint32_t page_offset, uint32_t bytes) : file_offset(file_offset), page_offset(page_offset), bytes(bytes) {}
    uint32_t file_offset;
    uint32_t page_offset;
    uint32_t bytes;
};

int check_address_range(const address_ranges& valid_ranges, uint32_t addr, uint32_t vaddr, uint32_t size, bool uninitialized, address_range &ar) {
    for(const auto& range : valid_ranges) {
        if (range.from <= addr && range.to >= addr + size) {
            if (range.type == address_range::type::NO_CONTENTS && !uninitialized) {
                fail(ERROR_INCOMPATIBLE, "ELF contains memory contents for uninitialized memory at %p", addr);
            }
            ar = range;
            if (verbose) {
                printf("%s segment %08x->%08x (%08x->%08x)\n", uninitialized ? "Uninitialized" : "Mapped", addr,
                   addr + size, vaddr, vaddr+size);
            }
            return 0;
        }
    }
    fail(ERROR_INCOMPATIBLE, "Memory segment %08x->%08x is outside of valid address range for device", addr, addr+size);
    return ERROR_INCOMPATIBLE;
}

int check_elf32_ph_entries(const std::vector<elf32_ph_entry>& entries, const address_ranges& valid_ranges, std::map<uint32_t, std::vector<page_fragment>>& pages) {
    for(const auto & entry : entries) {
        if (entry.type == PT_LOAD && entry.memsz) {
            address_range ar;
            int rc;
            unsigned int mapped_size = std::min(entry.filez, entry.memsz);
            if (mapped_size) {
                rc = check_address_range(valid_ranges, entry.paddr, entry.vaddr, mapped_size, false, ar);
                if (rc) return rc;
                // we don't download uninitialized, generally it is BSS and should be zero-ed by crt0.S, or it may be COPY areas which are undefined
                if (ar.type != address_range::type::CONTENTS) {
                    if (verbose) printf("  ignored\n");
                    continue;
                }
                unsigned int addr = entry.paddr;
                unsigned int remaining = mapped_size;
                unsigned int file_offset = entry.offset;
                while (remaining) {
                    unsigned int off = addr & (UF2_PAGE_SIZE - 1);
                    unsigned int len = std::min(remaining, UF2_PAGE_SIZE - off);
                    auto &fragments = pages[addr - off]; // list of fragments
                    // note if filesz is zero, we want zero init which is handled because the
                    // statement above creates an empty page fragment list
                    // check overlap with any existing fragments
                    for (const auto &fragment : fragments) {
                        if ((off < fragment.page_offset + fragment.bytes) !=
                            ((off + len) <= fragment.page_offset)) {
                            fail(ERROR_FORMAT, "In memory segments overlap");
                        }
                    }
                    fragments.push_back(
                            page_fragment{file_offset,off,len});
                    addr += len;
                    file_offset += len;
                    remaining -= len;
                }
            }
            if (entry.memsz > entry.filez) {
                // we have some uninitialized data too
                rc = check_address_range(valid_ranges, entry.paddr + entry.filez, entry.vaddr + entry.filez, entry.memsz - entry.filez, true,
                                         ar);
                if (rc) return rc;
            }
        }
    }
    return 0;
}

int realize_page(std::shared_ptr<std::iostream> in, const std::vector<page_fragment> &fragments, uint8_t *buf, unsigned int buf_len) {
    assert(buf_len >= UF2_PAGE_SIZE);
    for(auto& frag : fragments) {
        assert(frag.page_offset < UF2_PAGE_SIZE && frag.page_offset + frag.bytes <= UF2_PAGE_SIZE);
        in->seekg(frag.file_offset, in->beg);
        if (in->fail()) {
            fail_read_error();
        }
        in->read((char*)buf + frag.page_offset, frag.bytes);
        if (in->fail()) {
            fail_read_error();
        }
    }
    return 0;
}

static bool is_address_mapped(const std::map<uint32_t, std::vector<page_fragment>>& pages, uint32_t addr) {
    uint32_t page = addr & ~(UF2_PAGE_SIZE - 1);
    if (!pages.count(page)) return false;
    // todo check actual address within page
    return true;
}

uf2_block gen_abs_block(uint32_t abs_block_loc) {
    uf2_block block;
    block.magic_start0 = UF2_MAGIC_START0;
    block.magic_start1 = UF2_MAGIC_START1;
    block.flags = UF2_FLAG_FAMILY_ID_PRESENT | UF2_FLAG_EXTENSION_FLAGS_PRESENT;
    block.payload_size = UF2_PAGE_SIZE;
    block.num_blocks = 2;
    block.file_size = ABSOLUTE_FAMILY_ID;
    block.magic_end = UF2_MAGIC_END;
    block.target_addr = abs_block_loc;
    block.block_no = 0;
    memset(block.data, 0, sizeof(block.data));
    memset(block.data, 0xef, UF2_PAGE_SIZE);
    *(uint32_t*)&(block.data[UF2_PAGE_SIZE]) = UF2_EXTENSION_RP2_IGNORE_BLOCK;
    return block;
}

bool check_abs_block(uf2_block block) {
    return std::all_of(block.data, block.data + UF2_PAGE_SIZE, [](uint8_t i) { return i == 0xef; }) &&
        block.magic_start0 == UF2_MAGIC_START0 &&
        block.magic_start1 == UF2_MAGIC_START1 &&
        (block.flags & ~UF2_FLAG_EXTENSION_FLAGS_PRESENT) == UF2_FLAG_FAMILY_ID_PRESENT &&
        block.payload_size == UF2_PAGE_SIZE &&
        block.num_blocks == 2 &&
        block.file_size == ABSOLUTE_FAMILY_ID &&
        block.magic_end == UF2_MAGIC_END &&
        block.block_no == 0 &&
        !(block.flags & UF2_FLAG_EXTENSION_FLAGS_PRESENT && *(uint32_t*)&(block.data[UF2_PAGE_SIZE]) != UF2_EXTENSION_RP2_IGNORE_BLOCK);
}

int pages2uf2(std::map<uint32_t, std::vector<page_fragment>>& pages, std::shared_ptr<std::iostream> in, std::shared_ptr<std::iostream> out, uint32_t family_id, uint32_t abs_block_loc=0) {
    // RP2350-E10: add absolute block to start of flash UF2s, targeting end of flash by default
    if (family_id != ABSOLUTE_FAMILY_ID && family_id != RP2040_FAMILY_ID && abs_block_loc) {
        uint32_t base_addr = pages.begin()->first;
        address_ranges flash_range = rp2350_address_ranges_flash;
        if (is_address_initialized(flash_range, base_addr)) {
            uf2_block block = gen_abs_block(abs_block_loc);
            out->write((char*)&block, sizeof(uf2_block));
            if (out->fail()) {
                fail_write_error();
            }
        }
    }
    uf2_block block;
    unsigned int page_num = 0;
    block.magic_start0 = UF2_MAGIC_START0;
    block.magic_start1 = UF2_MAGIC_START1;
    block.flags = UF2_FLAG_FAMILY_ID_PRESENT;
    block.payload_size = UF2_PAGE_SIZE;
    block.num_blocks = (uint32_t)pages.size();
    block.file_size = family_id;
    block.magic_end = UF2_MAGIC_END;
    for(auto& page_entry : pages) {
        block.target_addr = page_entry.first;
        block.block_no = page_num++;
        if (verbose) {
            printf("Page %d / %d %08x%s\n", block.block_no, block.num_blocks, block.target_addr,
                   page_entry.second.empty() ? " (padding)": "");
        }
        memset(block.data, 0, sizeof(block.data));
        int rc = realize_page(in, page_entry.second, block.data, sizeof(block.data));
        if (rc) return rc;
        out->write((char*)&block, sizeof(uf2_block));
        if (out->fail()) {
            fail_write_error();
        }
    }
    return 0;
}

int bin2uf2(std::shared_ptr<std::iostream> in, std::shared_ptr<std::iostream> out, uint32_t address, uint32_t family_id, uint32_t abs_block_loc) {
    std::map<uint32_t, std::vector<page_fragment>> pages;

    in->seekg(0, in->end);
    if (in->fail()) {
        fail_read_error();
    }
    int size = in->tellg();
    if (size <= 0) {
        fail_read_error();
    }

    unsigned int addr = address;
    unsigned int remaining = size;
    unsigned int file_offset = 0;
    while (remaining) {
        unsigned int off = addr & (UF2_PAGE_SIZE - 1);
        unsigned int len = std::min(remaining, UF2_PAGE_SIZE - off);
        auto &fragments = pages[addr - off]; // list of fragments
        // note if filesz is zero, we want zero init which is handled because the
        // statement above creates an empty page fragment list
        // check overlap with any existing fragments
        for (const auto &fragment : fragments) {
            if ((off < fragment.page_offset + fragment.bytes) !=
                ((off + len) <= fragment.page_offset)) {
                fail(ERROR_FORMAT, "In memory segments overlap");
            }
        }
        fragments.push_back(
                page_fragment{file_offset,off,len});
        addr += len;
        file_offset += len;
        remaining -= len;
    }

    return pages2uf2(pages, in, out, family_id, abs_block_loc);
}

int elf2uf2(std::shared_ptr<std::iostream> in, std::shared_ptr<std::iostream> out, uint32_t family_id, uint32_t package_addr, uint32_t abs_block_loc) {
    elf_file elf;
    std::map<uint32_t, std::vector<page_fragment>> pages;

    int rc = elf.read_file(in);
    bool ram_style = false;
    address_ranges valid_ranges = {};
    address_ranges flash_range; address_ranges ram_range;
    if (family_id == RP2040_FAMILY_ID) {
        flash_range = rp2040_address_ranges_flash;
        ram_range = rp2040_address_ranges_ram;
    } else {
        flash_range = rp2350_address_ranges_flash;
        ram_range = rp2350_address_ranges_ram;
    }
    if (!rc) {
        rc = rp_determine_binary_type(elf.header(), elf.segments(), flash_range, ram_range, &ram_style);
        if (!rc) {
            if (verbose) {
                if (ram_style) {
                    printf("Detected RAM binary\n");
                } else {
                    printf("Detected FLASH binary\n");
                }
            }
            valid_ranges = ram_style ? ram_range : flash_range;
            rc = check_elf32_ph_entries(elf.segments(), valid_ranges, pages);
        }
    }
    if (rc) return rc;
    if (pages.empty()) {
        fail(ERROR_INCOMPATIBLE, "The input file has no memory pages");
    }
    // No Thumb bit on RISC-V
    elf32_header eh = elf.header();
    uint32_t thumb_bit = eh.common.machine == EM_ARM ? 0x1u : 0x0u;
    if (ram_style) {
        uint32_t expected_ep_main_ram = UINT32_MAX;
        uint32_t expected_ep_xip_sram = UINT32_MAX;
        for(auto& page_entry : pages) {
            if ( ((page_entry.first >= SRAM_START) && (page_entry.first < ram_range[0].to)) && (page_entry.first < expected_ep_main_ram) ) {
                expected_ep_main_ram = page_entry.first | thumb_bit;
            } else if ( ((page_entry.first >= ram_range[1].from) && (page_entry.first < ram_range[1].to)) && (page_entry.first < expected_ep_xip_sram) ) { 
                expected_ep_xip_sram = page_entry.first | thumb_bit;
            }
        }
        uint32_t expected_ep = (UINT32_MAX != expected_ep_main_ram) ? expected_ep_main_ram : expected_ep_xip_sram;
        if (eh.entry == expected_ep_xip_sram && family_id == RP2040_FAMILY_ID) {
            fail(ERROR_INCOMPATIBLE, "RP2040 B0/B1/B2 Boot ROM does not support direct entry into XIP_SRAM\n");
        } else if (eh.entry != expected_ep && family_id == RP2040_FAMILY_ID) {
            fail(ERROR_INCOMPATIBLE, "A RP2040 RAM binary should have an entry point at the beginning: %08x (not %08x)\n", expected_ep, eh.entry);
        }
        static_assert(0 == (SRAM_START & (UF2_PAGE_SIZE - 1)), "");
        // currently don't require this as entry point is now at the start, we don't know where reset vector is
        // todo can be re-enabled for RP2350
#if 0
        uint8_t buf[UF2_PAGE_SIZE];
        rc = realize_page(in, pages[SRAM_START], buf, sizeof(buf));
        if (rc) return rc;
        uint32_t sp = ((uint32_t *)buf)[0];
        uint32_t ip = ((uint32_t *)buf)[1];
        if (!is_address_mapped(pages, ip)) {
            fail(ERROR_INCOMPATIBLE, "Vector table at %08x is invalid: reset vector %08x is not in mapped memory",
                SRAM_START, ip);
        }
        if (!is_address_valid(valid_ranges, sp - 4)) {
            fail(ERROR_INCOMPATIBLE, "Vector table at %08x is invalid: stack pointer %08x is not in RAM",
                        SRAM_START, sp);
        }
#endif
    } else {
        // Fill in empty dummy uf2 pages to align the binary to flash sectors (except for the last sector which we don't
        // need to pad, and choose not to to avoid making all SDK UF2s bigger)
        // That workaround is required because the bootrom uses the block number for erase sector calculations:
        // https://github.com/raspberrypi/pico-bootrom/blob/c09c7f08550e8a36fc38dc74f8873b9576de99eb/bootrom/virtual_disk.c#L205

        std::set<uint32_t> touched_sectors;
        for (auto& page_entry : pages) {
            uint32_t sector = page_entry.first / FLASH_SECTOR_ERASE_SIZE;
            touched_sectors.insert(sector);
        }

        uint32_t last_page = pages.rbegin()->first;
        for (uint32_t sector : touched_sectors) {
            for (uint32_t page = sector * FLASH_SECTOR_ERASE_SIZE; page < (sector + 1) * FLASH_SECTOR_ERASE_SIZE; page += UF2_PAGE_SIZE) {
                if (page < last_page) {
                    // Create a dummy page, if it does not exist yet. note that all present pages are first
                    // zeroed before they are filled with any contents, so a dummy page will be all zeros.
                    auto &dummy = pages[page];
                }
            }
        }
    }

    if (package_addr) {
        // Package binary at address
        uint32_t base_addr = pages.begin()->first;
        int32_t package_delta = package_addr - base_addr;
        if (verbose) printf("Base %x\n", base_addr);

        auto copy_pages = pages;
        pages.clear();
        for (auto page : copy_pages) {
            pages[page.first + package_delta] = page.second;
        }
    }

    return pages2uf2(pages, in, out, family_id, abs_block_loc);
}
