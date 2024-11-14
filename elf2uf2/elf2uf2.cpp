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
#include <memory>

#include "elf2uf2.h"
#include "errors.h"

// Size of a flash sector in bytes
#define FLASH_SECTOR_ERASE_SIZE 4096u

// Global variable to control verbose mode
static bool verbose;

/**
 * @brief Terminates the program with a read error.
 */
static void fail_read_error() {
    fail(ERROR_READ_FAILED, "Failed to read input file");
}

/**
 * @brief Terminates the program with a write error.
 */
static void fail_write_error() {
    fail(ERROR_WRITE_FAILED, "Failed to write output file");
}

/**
 * @struct page_fragment
 * @brief Represents a fragment of a page with file offset and size.
 */
struct page_fragment {
    /**
     * @brief Constructor for page_fragment.
     * @param file_offset Offset in the input file.
     * @param page_offset Offset within the page.
     * @param bytes Number of bytes in the fragment.
     */
    page_fragment(uint32_t file_offset, uint32_t page_offset, uint32_t bytes)
        : file_offset(file_offset), page_offset(page_offset), bytes(bytes) {}

    uint32_t file_offset;  ///< Offset in the input file.
    uint32_t page_offset;  ///< Offset within the page.
    uint32_t bytes;        ///< Number of bytes in the fragment.
};

/**
 * @brief Checks if an address and the size is within the valid range. 
 * 
 * @param valid_ranges Valid address ranges.
 * @param addr Physical address to check.
 * @param vaddr Virtual address. Only used in verbose mode.
 * @param size Size of the area to check.
 * @param uninitialized Flag indicating if the area is uninitialized.
 * @param ar Reference to an address_range that will be set on success.
 * @return 0 on success, error code otherwise.
 */
int check_address_range(const address_ranges& valid_ranges, uint32_t addr, uint32_t vaddr, uint32_t size, bool uninitialized, address_range &ar) {
    for(const auto& range : valid_ranges) {
        if (range.from <= addr && range.to >= addr + size) {
            if (range.type == address_range::type::NO_CONTENTS && !uninitialized) {
                fail(ERROR_INCOMPATIBLE, "ELF contains memory contents for uninitialized memory at 0x%08x", addr);
            }
            ar = range;
            if (verbose) {
                printf("%s segment 0x%08x->0x%08x (0x%08x->0x%08x)\n", 
                       uninitialized ? "Uninitialized" : "Mapped", 
                       addr, addr + size, vaddr, vaddr + size);
            }
            return 0;
        }
    }
    fail(ERROR_INCOMPATIBLE, "Memory segment 0x%08x->0x%08x is outside of valid address range for device", addr, addr + size);
    return ERROR_INCOMPATIBLE;
}

/**
 * @brief Checks ELF32 program header entries and populates the page fragments map.
 * 
 * @param entries Vector of ELF32 program header entries.
 * @param valid_ranges Valid address ranges.
 * @param pages Map of pages with their fragments.
 * @return 0 on success, error code otherwise.
 */
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

/**
 * @brief Realizes a page by reading the corresponding fragments from the input stream.
 * 
 * @param in Input stream (e.g., ELF file).
 * @param fragments Vector of page fragments.
 * @param buf Buffer to store the page data.
 * @param buf_len Length of the buffer.
 * @return 0 on success, error code otherwise.
 */
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

/**
 * @brief Checks if a specific address is mapped.
 * 
 * @param pages Map of pages with their fragments.
 * @param addr Address to check.
 * @return true if the address is mapped, false otherwise.
 */
static bool is_address_mapped(const std::map<uint32_t, std::vector<page_fragment>>& pages, uint32_t addr) {
    uint32_t page = addr & ~(UF2_PAGE_SIZE - 1);
    if (!pages.count(page)) return false;
    // TODO: Check actual address within page
    return true;
}

/**
 * @brief Generates an absolute UF2 block. The absolute block is required to work around Errata E10 in the RP2350 datasheet.
 * 
 * @param abs_block_loc Target address for the absolute block.
 * @return Generated UF2 block.
 */
uf2_block gen_abs_block(uint32_t abs_block_loc) {
    uf2_block block;
    block.magic_start0 = UF2_MAGIC_START0;
    block.magic_start1 = UF2_MAGIC_START1;
    block.flags = UF2_FLAG_FAMILY_ID_PRESENT;
    block.payload_size = UF2_PAGE_SIZE;
    block.num_blocks = 2;
    block.file_size = ABSOLUTE_FAMILY_ID;
    block.magic_end = UF2_MAGIC_END;
    block.target_addr = abs_block_loc;
    block.block_no = 0;
    memset(block.data, 0, sizeof(block.data));
    memset(block.data, 0xef, UF2_PAGE_SIZE);
    return block;
}

/**
 * @brief Checks if a UF2 block is an absolute block.
 * 
 * @param block UF2 block to check.
 * @return true if the block is an absolute block, false otherwise.
 */
bool check_abs_block(uf2_block block) {
    return std::all_of(block.data, block.data + UF2_PAGE_SIZE, [](uint8_t i) { return i == 0xef; }) &&
        block.magic_start0 == UF2_MAGIC_START0 &&
        block.magic_start1 == UF2_MAGIC_START1 &&
        block.flags == UF2_FLAG_FAMILY_ID_PRESENT &&
        block.payload_size == UF2_PAGE_SIZE &&
        block.num_blocks == 2 &&
        block.file_size == ABSOLUTE_FAMILY_ID &&
        block.magic_end == UF2_MAGIC_END &&
        block.block_no == 0;
}

/**
 * @brief Converts page information into UF2 blocks and writes them to the output stream.
 * 
 * @param pages Map of pages with their fragments.
 * @param in Input stream (e.g., ELF or binary file).
 * @param out Output stream for UF2 data.
 * @param family_id Family ID for UF2.
 * @param abs_block_loc Optional absolute block location.
 * @return 0 on success, error code otherwise.
 */
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
    block.num_blocks = static_cast<uint32_t>(pages.size());
    block.file_size = family_id;
    block.magic_end = UF2_MAGIC_END;

    for(auto& page_entry : pages) {
        block.target_addr = page_entry.first;
        block.block_no = page_num++;

        if (verbose) {
            printf("Page %u / %u 0x%08x%s\n", block.block_no, block.num_blocks, block.target_addr,
                   page_entry.second.empty() ? " (padding)" : "");
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

/**
 * @brief Converts a binary file to UF2 format.
 * 
 * @param in Input stream for the binary file.
 * @param out Output stream for UF2 data.
 * @param address Start address in the target memory.
 * @param family_id Family ID for UF2.
 * @param abs_block_loc Optional absolute block location. If set to 0, this is ignored. Any non-zero value will
 *                      be used to add an absolute UF2 block, targeting the specified location.
 * @return 0 on success, error code otherwise.
 */
int bin2uf2(std::shared_ptr<std::iostream> in, std::shared_ptr<std::iostream> out, uint32_t address, uint32_t family_id, uint32_t abs_block_loc) {
    std::map<uint32_t, std::vector<page_fragment>> pages;

    // Determine the size of the input file
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

/**
 * @brief Converts an ELF file to UF2 format.
 * 
 * @param in Input stream for the ELF file.
 * @param out Output stream for UF2 data.
 * @param family_id Family ID for UF2.
 * @param package_addr Optional packaging address.
 * @param abs_block_loc Optional absolute block location.
 * @return 0 on success, error code otherwise.
 */
int elf2uf2(std::shared_ptr<std::iostream> in, std::shared_ptr<std::iostream> out, uint32_t family_id, uint32_t package_addr, uint32_t abs_block_loc) {
    elf_file elf;
    std::map<uint32_t, std::vector<page_fragment>> pages;

    // Read the ELF file
    int rc = elf.read_file(in);
    bool ram_style = false;
    address_ranges valid_ranges = {};
    address_ranges flash_range; address_ranges ram_range;

    // Determine the valid address ranges based on the family ID
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
                printf("Detected %s binary\n", ram_style ? "RAM" : "FLASH");
            }
            valid_ranges = ram_style ? ram_range : flash_range;
            rc = check_elf32_ph_entries(elf.segments(), valid_ranges, pages);
        }
    }

    if (rc) return rc;

    if (pages.empty()) {
        fail(ERROR_INCOMPATIBLE, "The input file has no memory pages");
    }

    // Determine the Thumb bit (for ARM architecture)
    elf32_header eh = elf.header();
    uint32_t thumb_bit = (eh.common.machine == EM_ARM) ? 0x1u : 0x0u;

    if (ram_style) {
        uint32_t expected_ep_main_ram = UINT32_MAX;
        uint32_t expected_ep_xip_sram = UINT32_MAX;

        // Determine expected entry points
        for(auto& page_entry : pages) {
            if ((page_entry.first >= SRAM_START) && (page_entry.first < ram_range[0].to) && (page_entry.first < expected_ep_main_ram)) {
                expected_ep_main_ram = page_entry.first | thumb_bit;
            } else if (((page_entry.first >= ram_range[1].from) && (page_entry.first < ram_range[1].to)) && (page_entry.first < expected_ep_xip_sram)) { 
                expected_ep_xip_sram = page_entry.first | thumb_bit;
            }
        }
        uint32_t expected_ep = (UINT32_MAX != expected_ep_main_ram) ? expected_ep_main_ram : expected_ep_xip_sram;

        // Check entry points for RP2040
        if (eh.entry == expected_ep_xip_sram && family_id == RP2040_FAMILY_ID) {
            fail(ERROR_INCOMPATIBLE, "RP2040 B0/B1/B2 Boot ROM does not support direct entry into XIP_SRAM\n");
        } else if (eh.entry != expected_ep && family_id == RP2040_FAMILY_ID) {
            fail(ERROR_INCOMPATIBLE, "A RP2040 RAM binary should have an entry point at the beginning: 0x%08x (not 0x%08x)\n", expected_ep, eh.entry);
        }

        // Ensure SRAM_START is aligned to a page boundary
        static_assert(0 == (SRAM_START & (UF2_PAGE_SIZE - 1)), "");

        // Currently don't require this as entry point is now at the start, we don't know where reset vector is
        // TODO: Can be re-enabled for RP2350
#if 0
        uint8_t buf[UF2_PAGE_SIZE];
        rc = realize_page(in, pages[SRAM_START], buf, sizeof(buf));
        if (rc) return rc;
        uint32_t sp = ((uint32_t *)buf)[0];
        uint32_t ip = ((uint32_t *)buf)[1];
        if (!is_address_mapped(pages, ip)) {
            fail(ERROR_INCOMPATIBLE, "Vector table at 0x%08x is invalid: reset vector 0x%08x is not in mapped memory",
                 SRAM_START, ip);
        }
        if (!is_address_valid(valid_ranges, sp - 4)) {
            fail(ERROR_INCOMPATIBLE, "Vector table at 0x%08x is invalid: stack pointer 0x%08x is not in RAM",
                        SRAM_START, sp);
        }
#endif
    } else {
        // Fill in empty dummy UF2 pages to align the binary to flash sectors (except for the last sector)
        // This workaround is required because the boot ROM uses the block number for erase sector calculations:
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
                    // Create a dummy page if it does not exist yet. Note that all present pages are first
                    // zeroed before they are filled with any contents, so a dummy page will be all zeros.
                    auto &dummy = pages[page];
                }
            }
        }
    }

    // Optional: Package binary at a specific address
    if (package_addr) {
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
