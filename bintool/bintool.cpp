#include <iostream>
#include <memory>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <random>

#include <tuple>

#include "boot/picobin.h"
#include <map>

#include "elf_file.h"

#if HAS_MBEDTLS
    #include "mbedtls_wrapper.h"
    #include <mbedtls/pk.h>
    #include <mbedtls/ecp.h>
    #include <mbedtls/error.h>
#endif

#include "bintool.h"
#include "metadata.h"
#include "errors.h"

// todo test with a holey binary

template<typename T> void dumper(const char *msg, const T& foop) {
    DEBUG_LOG("%s ", msg);
    for(uint8_t i : foop.bytes) {
        DEBUG_LOG("%02x", i);
    }
    DEBUG_LOG("\n");
}

std::vector<const Segment *> sorted_segs(elf_file *elf) {
    std::vector<const Segment *> phys_sorted_segs;
    std::transform(elf->segments().begin(), elf->segments().end(), std::back_inserter(phys_sorted_segs), [](const Segment &seg) {
        return &seg;
    });
    std::sort(phys_sorted_segs.begin(), phys_sorted_segs.end(), [](const Segment *first, const Segment *second) {
        return first->physical_address() < second->physical_address();
    });
    return phys_sorted_segs;
}

#if HAS_MBEDTLS
int read_keys(const std::string &filename, public_t *public_key, private_t *private_key) {
    mbedtls_pk_context pk_ctx;
    int rc;

    mbedtls_pk_init(&pk_ctx);
    rc = mbedtls_pk_parse_keyfile(&pk_ctx, filename.c_str(), NULL);
    if (rc != 0) {
        char error_string[128];
        mbedtls_strerror(rc, error_string, sizeof(error_string));
        fail(ERROR_FORMAT, "Failed to read key file %s, error %s", filename.c_str(), error_string);
        return -1;
    }
    
    const mbedtls_ecp_keypair *keypair = mbedtls_pk_ec(pk_ctx);
    if (!keypair) {
        fail(ERROR_FORMAT, "Failed to parse key file %s", filename.c_str());
    }
    mbedtls_mpi_write_binary(&keypair->d, reinterpret_cast<unsigned char *>(private_key), 32);
    mbedtls_mpi_write_binary(&keypair->Q.X, reinterpret_cast<unsigned char *>(public_key), 32);
    mbedtls_mpi_write_binary(&keypair->Q.Y, reinterpret_cast<unsigned char *>(public_key) + 32, 32);
    return 0;
}
#endif

#define OTP_KEY_YAML_HEADER \
"include:\n" \
"  - otp/tc_images/base_chipinfo.yml\n" \
"data:\n" \
"  - crit1_secure_boot_enable: [crit, 1]\n" \
"  - crit0_riscv_disable: [crit, 1]\n" \
"  - crit0_arm_disable: [crit, 0]\n" \
"  - BOOT_FLAGS0_SECURE_PARTITION_TABLE: [rbit3, 0]\n" \
"  - BOOT_FLAGS0_DISABLE_AUTO_SWITCH_ARCH: [rbit3, 1]\n" \
"  # - boot_temp_chicken_bit_opt_in_faster_sigcheck_rosc_div: [rbit3, 1]\n" \
"  - boot_flags1_key_valid: [rbit3, 0b0001]\n" \


#if HAS_MBEDTLS
void write_otp_key_yaml(const std::string &filename, message_digest_t pub_sha256) {
    std::ofstream out(filename, std::ios::out | std::ios::trunc);
    out.exceptions(std::fstream::failbit | std::fstream::badbit);
    out << std::string(OTP_KEY_YAML_HEADER);

    // Print public key hash again in the format it is expected to appear in OTP
    for (int i = 0; i < 16; ++i) {
        char row[128];
        snprintf(row, sizeof(row), "  - bootkey0_%-2d: [ecc, 0x%02x%02x]\n", i, pub_sha256.bytes[2 * i + 1], pub_sha256.bytes[2 * i]);
        out << std::string(row);        
    }
}
#endif


std::unique_ptr<block> find_first_block(elf_file *elf) {
    std::unique_ptr<block> first_block;
    for(auto x : sorted_segs(elf)) {
        if (!x->is_load()) continue;
        auto data = elf->content(*x); // x->content(*elf);
        // todo handle alignment (not sure if necessary)
        if ((x->physical_address() & 3) || (x->physical_size() & 3)) {
            fail(ERROR_INCOMPATIBLE, "ELF segments must be word aligned");
        }
        std::vector<uint32_t> words = lsb_bytes_to_words(data.begin(), data.end());
        auto block_begin = std::find(words.begin(), words.end(), PICOBIN_BLOCK_MARKER_START);
        while (block_begin != words.end() && !first_block) {
            DEBUG_LOG("Found possible block at %08x + %08x...", (unsigned int)x->physical_address(), (int)(block_begin - words.begin())*4);
            block_begin++;
            for(auto next_item = block_begin; next_item < words.end(); ) {
                unsigned int size = item::decode_size(*next_item);
                if ((uint8_t)*next_item == PICOBIN_BLOCK_ITEM_2BS_LAST) {
                    if (size == next_item - block_begin) {
                        if (next_item < words.end() && next_item[2] == PICOBIN_BLOCK_MARKER_END) {
                            DEBUG_LOG(" verified block");
                            first_block = block::parse(x->physical_address() + 4*(block_begin - words.begin() - 1),
                                                       next_item + 1, block_begin, block_begin + size);
                            break;
                        }
                    }
                } else {
                    next_item += size;
                }
            }
            DEBUG_LOG("\n");
            block_begin = std::find(block_begin, words.end(), PICOBIN_BLOCK_MARKER_START);
        }
        if (first_block) break;
    }
    if (!first_block) {
        DEBUG_LOG("No block found\n");
        return NULL;
    }
    return first_block;
}


std::unique_ptr<block> find_first_block(std::vector<uint8_t> bin, uint32_t storage_addr) {
    std::unique_ptr<block> first_block;

    std::vector<uint32_t> words = lsb_bytes_to_words(bin.begin(), bin.end());
    auto block_begin = std::find(words.begin(), words.end(), PICOBIN_BLOCK_MARKER_START);

    while (block_begin != words.end() && !first_block) {
        DEBUG_LOG("Possible block at %08x + %08x\n", storage_addr, (int)(block_begin - words.begin())*4);
        block_begin++;
        for(auto next_item = block_begin; next_item < words.end(); ) {
            unsigned int size = item::decode_size(*next_item);
            if ((uint8_t)*next_item == PICOBIN_BLOCK_ITEM_2BS_LAST) {
                if (size == next_item - block_begin) {
                    if (next_item < words.end() && next_item[2] == PICOBIN_BLOCK_MARKER_END) {
                        DEBUG_LOG("is a valid block\n");
                        first_block = block::parse(storage_addr + 4*(block_begin - words.begin() - 1),
                                                    next_item + 1, block_begin, block_begin + size);
                        break;
                    } else {
                        printf("WARNING: Invalid block found at 0x%x - no block end marker\n",
                            (int)(block_begin - words.begin())*4
                        );
                    }
                } else {
                    printf("WARNING: Invalid block found at 0x%x - incorrect last item size of %d, expected %d\n",
                        (int)(block_begin - words.begin())*4, size, (int)(next_item - block_begin)
                    );
                }
                // Invalid block, so find the next one
                next_item = words.end();
            } else {
                next_item += size;
            }
        }
        block_begin = std::find(block_begin, words.end(), PICOBIN_BLOCK_MARKER_START);
    }
    if (!first_block) {
        DEBUG_LOG("NO BLOCK FOUND\n");
        return nullptr;
    }
    return first_block;
}


void set_next_block(elf_file *elf, std::unique_ptr<block> &first_block, uint32_t highest_address) {
    // todo this isn't right, but virtual should be physical for now
    auto seg = elf->segment_from_physical_address(first_block->physical_addr);
    if (seg == nullptr) {
        fail(ERROR_NOT_POSSIBLE, "The ELF file does not contain the next block address %x", first_block->physical_addr);
    }
    std::vector<uint8_t> content = elf->content(*seg);
    uint32_t offset = first_block->physical_addr + first_block->next_block_rel_index * 4 - seg->physical_address();
    uint32_t delta = highest_address - first_block->physical_addr;
    // todo this assumes a 2 word NEXT_BLOCK_ADDR type for now
    content[offset] = delta & 0xff;
    content[offset+1] = (delta >> 8) & 0xff;
    content[offset+2] = (delta >> 16) & 0xff;
    content[offset+3] = (delta >> 24) & 0xff;
    DEBUG_LOG("defaulting next_block_addr at %08x to %08x\n",
        (int)first_block->physical_addr + first_block->next_block_rel_index * 4,
        (int)(highest_address));
    first_block->next_block_rel = delta;
    elf->content(*seg, content);
}


void set_next_block(std::vector<uint8_t> &bin, uint32_t storage_addr, std::unique_ptr<block> &first_block, uint32_t highest_address) {
    // todo this isn't right, but virtual should be physical for now
    uint32_t offset = first_block->physical_addr + first_block->next_block_rel_index * 4 - storage_addr;
    uint32_t delta = highest_address - first_block->physical_addr;
    // todo this assumes a 2 word NEXT_BLOCK_ADDR type for now
    bin[offset] = delta & 0xff;
    bin[offset+1] = (delta >> 8) & 0xff;
    bin[offset+2] = (delta >> 16) & 0xff;
    bin[offset+3] = (delta >> 24) & 0xff;
    DEBUG_LOG("defaulting next_block_addr at %08x to %08x\n",
        (int)first_block->physical_addr + first_block->next_block_rel_index * 4,
        (int)(highest_address));
    first_block->next_block_rel = delta;
}


block place_new_block(elf_file *elf, std::unique_ptr<block> &first_block) {
    uint32_t highest_ram_address = 0;
    uint32_t highest_flash_address = 0;
    bool no_flash = false;

    for(const auto &seg : elf->segments()) {
        // std::cout << &seg << " " << to_string(seg) << std::endl;
        const uint32_t paddr = seg.physical_address();
        const uint32_t psize = seg.physical_size();
        if (paddr >= 0x20000000 && paddr < 0x20080000) {
            highest_ram_address = std::max(paddr + psize, highest_ram_address);
        } else if (paddr >= 0x10000000 && paddr < 0x20000000) {
            highest_flash_address = std::max(paddr + psize, highest_flash_address);
        }
    }

    if (highest_flash_address == 0) {
        no_flash = true;
    }
    uint32_t highest_address = no_flash ? highest_ram_address : highest_flash_address;

    DEBUG_LOG("RAM %08x ", highest_ram_address);
    if (no_flash) {
        DEBUG_LOG("NO FLASH\n");
    } else {
        DEBUG_LOG("FLASH %08x\n", highest_flash_address);
    }

    int32_t loop_start_rel = 0;
    uint32_t new_block_addr = 0;
    if (!first_block->next_block_rel) {
        set_next_block(elf, first_block, highest_address);
        loop_start_rel = -first_block->next_block_rel;
        new_block_addr = first_block->physical_addr + first_block->next_block_rel;
    } else {
        DEBUG_LOG("There is already a block loop\n");
        uint32_t next_block_addr = first_block->physical_addr + first_block->next_block_rel;
        std::unique_ptr<block> new_first_block;
        while (true) {
            auto segment = elf->segment_from_physical_address(next_block_addr);
            if (segment == nullptr) {
                fail(ERROR_NOT_POSSIBLE, "The ELF file does not contain the next block address %x", next_block_addr);
            }
            auto data = elf->content(*segment);
            auto offset = next_block_addr - segment->physical_address();
            std::vector<uint32_t> words = lsb_bytes_to_words(data.begin() + offset, data.end());
            if (words.front() != PICOBIN_BLOCK_MARKER_START) {
                fail(ERROR_UNKNOWN, "Block loop is not valid - no block found at %08x\n", (int)(next_block_addr));
            }
            words.erase(words.begin());
            DEBUG_LOG("Checking block at %x\n", next_block_addr);
            for(auto next_item = words.begin(); next_item < words.end(); ) {
                unsigned int size = item::decode_size(*next_item);
                if ((uint8_t)*next_item == PICOBIN_BLOCK_ITEM_2BS_LAST) {
                    if (size == next_item - words.begin()) {
                        if (next_item < words.end() && next_item[2] == PICOBIN_BLOCK_MARKER_END) {
                            DEBUG_LOG("is a valid block\n");
                            new_first_block = block::parse(next_block_addr, next_item + 1, words.begin(), words.begin() + size);
                            break;
                        }
                    }
                } else {
                    next_item += size;
                }
            }
            if (new_first_block->physical_addr + new_first_block->next_block_rel == first_block->physical_addr) {
                DEBUG_LOG("Found last block in block loop\n");
                break;
            } else {
                DEBUG_LOG("Continue looping\n");
                next_block_addr = new_first_block->physical_addr + new_first_block->next_block_rel;
                new_first_block.reset();
            }
        }
        set_next_block(elf, new_first_block, highest_address);
        new_block_addr = new_first_block->physical_addr + new_first_block->next_block_rel;
        loop_start_rel = first_block->physical_addr - new_block_addr;
    }
    if (highest_address != new_block_addr) {
        fail(ERROR_UNKNOWN, "Next block not at highest address %08x %08x\n", (int)highest_address, (int)(new_block_addr));
    }

    // loop back to first block
    block new_block(new_block_addr, loop_start_rel);
    // copt the existing block
    std::copy(first_block->items.begin(),
              first_block->items.end(),
              std::back_inserter(new_block.items));

    return new_block;
}


std::unique_ptr<block> get_last_block(std::vector<uint8_t> &bin, uint32_t storage_addr, std::unique_ptr<block> &first_block, get_more_bin_cb more_cb) {
    uint32_t next_block_addr = first_block->physical_addr + first_block->next_block_rel;
    std::unique_ptr<block> new_first_block;
    uint32_t read_size = PICOBIN_MAX_BLOCK_SIZE;
    while (true) {
        auto offset = next_block_addr - storage_addr;
        if (offset + read_size > bin.size() && more_cb != nullptr) {
            more_cb(bin, offset + read_size);
        }
        std::vector<uint32_t> words = lsb_bytes_to_words(bin.begin() + offset, bin.end());
        if (words.front() != PICOBIN_BLOCK_MARKER_START) {
            fail(ERROR_UNKNOWN, "Block loop is not valid - no block found at %08x\n", (int)(next_block_addr));
        }
        words.erase(words.begin());
        DEBUG_LOG("Checking block at %x\n", next_block_addr);
        DEBUG_LOG("Starts with %x %x %x %x\n", words[0], words[1], words[2], words[3]);
        for(auto next_item = words.begin(); next_item < words.end(); ) {
            unsigned int size = item::decode_size(*next_item);
            if ((uint8_t)*next_item == PICOBIN_BLOCK_ITEM_2BS_LAST) {
                if (size == next_item - words.begin()) {
                    if (next_item < words.end() && next_item[2] == PICOBIN_BLOCK_MARKER_END) {
                        DEBUG_LOG("is a valid block\n");
                        new_first_block = block::parse(next_block_addr, next_item + 1, words.begin(), words.begin() + size);
                        break;
                    }
                }
            } else {
                next_item += size;
            }
        }
        if (new_first_block->physical_addr + new_first_block->next_block_rel == first_block->physical_addr) {
            DEBUG_LOG("Found last block in block loop\n");
            break;
        } else {
            DEBUG_LOG("Continue looping\n");
            next_block_addr = new_first_block->physical_addr + new_first_block->next_block_rel;
            new_first_block.reset();
        }
    }
    return new_first_block;
}


std::vector<std::unique_ptr<block>> get_all_blocks(std::vector<uint8_t> &bin, uint32_t storage_addr, std::unique_ptr<block> &first_block, get_more_bin_cb more_cb) {
    uint32_t next_block_addr = first_block->physical_addr + first_block->next_block_rel;
    std::vector<std::unique_ptr<block>> all_blocks;
    uint32_t read_size = PICOBIN_MAX_BLOCK_SIZE;
    while (true) {
        std::unique_ptr<block> new_first_block;
        auto offset = next_block_addr - storage_addr;
        if (offset + read_size > bin.size() && more_cb != nullptr) {
            more_cb(bin, offset + read_size);
        }
        std::vector<uint32_t> words = lsb_bytes_to_words(bin.begin() + offset, bin.end());
        words.erase(words.begin());
        DEBUG_LOG("Checking block at %x\n", next_block_addr);
        DEBUG_LOG("Starts with %x %x %x %x\n", words[0], words[1], words[2], words[3]);
        for(auto next_item = words.begin(); next_item < words.end(); ) {
            unsigned int size = item::decode_size(*next_item);
            if ((uint8_t)*next_item == PICOBIN_BLOCK_ITEM_2BS_LAST) {
                if (size == next_item - words.begin()) {
                    if (next_item < words.end() && next_item[2] == PICOBIN_BLOCK_MARKER_END) {
                        DEBUG_LOG("is a valid block\n");
                        new_first_block = block::parse(next_block_addr, next_item + 1, words.begin(), words.begin() + size);
                        break;
                    }
                }
            } else {
                next_item += size;
            }
        }
        if (new_first_block->physical_addr + new_first_block->next_block_rel == first_block->physical_addr) {
            DEBUG_LOG("Found last block in block loop\n");
            all_blocks.push_back(std::move(new_first_block));
            break;
        } else {
            DEBUG_LOG("Continue looping\n");
            next_block_addr = new_first_block->physical_addr + new_first_block->next_block_rel;
            all_blocks.push_back(std::move(new_first_block));
        }
    }
    return all_blocks;
}


block place_new_block(std::vector<uint8_t> &bin, uint32_t storage_addr, std::unique_ptr<block> &first_block) {
    uint32_t highest_ram_address = 0;
    uint32_t highest_flash_address = 0;
    bool no_flash = false;

    const uint32_t paddr = storage_addr;
    const uint32_t psize = bin.size();
    if (paddr >= 0x20000000 && paddr < 0x20080000) {
        highest_ram_address = std::max(paddr + psize, highest_ram_address);
    } else if (paddr >= 0x10000000 && paddr < 0x20000000) {
        highest_flash_address = std::max(paddr + psize, highest_flash_address);
    }

    if (highest_flash_address == 0) {
        no_flash = true;
    }
    uint32_t highest_address = no_flash ? highest_ram_address : highest_flash_address;

    if (no_flash) {
        DEBUG_LOG("RAM %08x ", highest_ram_address);
        DEBUG_LOG("NO FLASH\n");
    } else {
        DEBUG_LOG("FLASH %08x\n", highest_flash_address);
    }

    int32_t loop_start_rel = 0;
    uint32_t new_block_addr = 0;
    if (!first_block->next_block_rel) {
        set_next_block(bin, storage_addr, first_block, highest_address);
        loop_start_rel = -first_block->next_block_rel;
        new_block_addr = first_block->physical_addr + first_block->next_block_rel;
    } else {
        DEBUG_LOG("Ooh, there is already a block loop - lets find it's end\n");
        auto new_first_block = get_last_block(bin, storage_addr, first_block);
        set_next_block(bin, storage_addr, new_first_block, highest_address);
        new_block_addr = new_first_block->physical_addr + new_first_block->next_block_rel;
        loop_start_rel = first_block->physical_addr - new_block_addr;
    }
    if (highest_address != new_block_addr) {
        fail(ERROR_UNKNOWN, "Next block not at highest address %08x %08x\n", (int)highest_address, (int)(new_block_addr));
    }

    // loop back to first block
    block new_block(new_block_addr, loop_start_rel);
    // copt the existing block
    std::copy(first_block->items.begin(),
              first_block->items.end(),
              std::back_inserter(new_block.items));

    return new_block;
}


#if HAS_MBEDTLS
void hash_andor_sign_block(block *new_block, const public_t public_key, const private_t private_key, bool hash_value, bool sign, std::vector<uint8_t> to_hash) {
    std::shared_ptr<hash_def_item> hash_def = std::make_shared<hash_def_item>(PICOBIN_HASH_SHA256);
    new_block->items.push_back(hash_def);

    // hash everything up to an including the hash def (todo really we shuold use the value from the hashdef when
    // we're all done)
    auto tmp_words = new_block->to_words();
    DEBUG_LOG("hash 0 + %08x\n", (int)(tmp_words.size()-3)*4);
    if (new_block->items[0]->type() == PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE) {
        if (((image_type_item *)new_block->items[0].get())->flags & 0x8000) {
            DEBUG_LOG("CLEARING TBYB FLAG\n");
            assert(tmp_words[1] & 0x80000000);
            tmp_words[1] &= ~0x80000000;
        }
    }
    auto block_hashed_contents = words_to_lsb_bytes(tmp_words.begin(), tmp_words.end() - 3); // remove stuff at end
    std::copy(block_hashed_contents.begin(), block_hashed_contents.end(), std::back_inserter(to_hash));

    message_digest_t sha256;
    sha256_buffer(to_hash.data(), to_hash.size(), &sha256);
    dumper("SHA256", sha256);

    if (sign) {
        // dumper("PRIVATE KEY", private_key);
        dumper("PUBLIC KEY", public_key);

        uint8_t entropy[32];
        std::random_device rand{};
        assert(rand.max() - rand.min() >= 256);
        for(auto &e : entropy) {
            e = rand();
        }

        signature_t sig;
        sign_sha256(entropy, sizeof(entropy), &sha256, &public_key, &private_key, &sig);
        dumper("SIG", sig);

        uint32_t err = verify_signature_secp256k1(&sig, &public_key, &sha256);
        if (err) {
            fail(ERROR_VERIFICATION_FAILED, "Signature verification failed");
        }

        std::shared_ptr<signature_item> signature = std::make_shared<signature_item>(PICOBIN_SIGNATURE_SECP256K1);
        signature->public_key_bytes = std::vector<uint8_t>(public_key.bytes, public_key.bytes + sizeof(public_key.bytes));
        signature->signature_bytes = std::vector<uint8_t>(sig.bytes, sig.bytes + sizeof(sig.bytes));
        new_block->items.push_back(signature);
    }

    if (hash_value) {
        std::shared_ptr<hash_value_item> hash = std::make_shared<hash_value_item>();
        hash->hash_bytes = std::vector<uint8_t>(sha256.bytes, sha256.bytes + sizeof(sha256.bytes));
        new_block->items.push_back(hash);
    }
}


std::vector<uint8_t> get_lm_hash_data(elf_file *elf, block *new_block, bool clear_sram = false) {
    std::vector<uint8_t> to_hash;
    std::shared_ptr<load_map_item> load_map = new_block->get_item<load_map_item>();
    if (load_map == nullptr) {
        std::vector<load_map_item::entry> entries;
        if (clear_sram) {
            // todo tidy up this way of hashing the uint32_t
            std::vector<uint32_t> sram_size_vec = {SRAM_END_RP2350 - SRAM_START};
            entries.push_back({
                0x0,
                SRAM_START,
                sram_size_vec[0]
            });
            auto sram_size_data = words_to_lsb_bytes(sram_size_vec.begin(), sram_size_vec.end());
            std::copy(sram_size_data.begin(), sram_size_data.end(), std::back_inserter(to_hash));
            DEBUG_LOG("CLEAR %08x + %08x\n", (int)SRAM_START, (int)sram_size_vec[0]);
        }
        for(const auto &seg : sorted_segs(elf)) {
            if (!seg->is_load()) continue;
            const auto data = elf->content(*seg);
            // std::cout << "virt = " << std::hex << seg->virtual_address() << " + " << std::hex << seg->virtual_size() << ", phys = " << std::hex << seg->physical_address() << " + " << std::hex << seg->physical_size() << std::endl;
            if (data.size() != seg->physical_size()) {
                fail(ERROR_INCOMPATIBLE, "Elf segment physical size (%x) does not match data size in file (%x)", seg->physical_size(), data.size());
            }
            if (seg->physical_size() && seg->physical_address() < new_block->physical_addr) {
                std::copy(data.begin(), data.end(), std::back_inserter(to_hash));
                DEBUG_LOG("HASH %08x + %08x\n", (int)seg->physical_address(), (int)seg->physical_size());
                entries.push_back(
                    {
                        (uint32_t)seg->physical_address(),
                        (uint32_t)seg->virtual_address(),
                        (uint32_t)seg->physical_size()
                    });
            }
        }

        load_map = std::make_shared<load_map_item>(false, entries);
        new_block->items.push_back(load_map);
    } else {
        DEBUG_LOG("Already has load map, so hashing that\n");
        // todo hash existing load map
        for(const auto &entry : load_map->entries) {
            std::vector<uint8_t> data;
            uint32_t current_storage_address = entry.storage_address;
            while (data.size() < entry.size) {
                auto seg = elf->segment_from_physical_address(current_storage_address);
                if (seg == nullptr) {
                    fail(ERROR_NOT_POSSIBLE, "The ELF file does not contain the storage address %x", current_storage_address);
                }
                const auto new_data = elf->content(*seg);

                uint32_t offset = current_storage_address - seg->physical_address();

                std::copy(new_data.begin()+offset, new_data.end(), std::back_inserter(data));
                current_storage_address += new_data.size();
            }
            data.resize(entry.size);
            std::copy(data.begin(), data.end(), std::back_inserter(to_hash));
            DEBUG_LOG("HASH %08x + %08x\n", (int)entry.storage_address, (int)data.size());
        }
    }

    return to_hash;
}


std::vector<uint8_t> get_lm_hash_data(std::vector<uint8_t> bin, uint32_t storage_addr, uint32_t runtime_addr, block *new_block, bool clear_sram = false) {
    std::vector<uint8_t> to_hash;
    std::shared_ptr<load_map_item> load_map = new_block->get_item<load_map_item>();
    if (load_map == nullptr) {
        to_hash.insert(to_hash.begin(), bin.begin(), bin.end());
        std::vector<load_map_item::entry> entries;
        if (clear_sram) {
            // todo gate this clearing of SRAM
            std::vector<uint32_t> sram_size_vec = {0x00082000};
            assert(sram_size_vec[0] % 4 == 0);
            entries.push_back({
                0x0,
                0x20000000,
                sram_size_vec[0]
            });
            auto sram_size_data = words_to_lsb_bytes(sram_size_vec.begin(), sram_size_vec.end());
            to_hash.insert(to_hash.begin(), sram_size_data.begin(), sram_size_data.end());
        }
        DEBUG_LOG("HASH %08x + %08x\n", (int)storage_addr, (int)bin.size());
        entries.push_back(
            {
                (uint32_t)storage_addr,
                (uint32_t)runtime_addr,
                (uint32_t)bin.size()
            });

        load_map = std::make_shared<load_map_item>(false, entries);
        new_block->items.push_back(load_map);
    } else {
        DEBUG_LOG("Already has load map, so hashing that\n");
        // todo hash existing load map
        for(const auto &entry : load_map->entries) {
            if (entry.storage_address == 0) {
                std::copy(
                    (uint8_t*)&entry.size,
                    (uint8_t*)&entry.size + sizeof(entry.size),
                    std::back_inserter(to_hash));
                DEBUG_LOG("HASH CLEAR %08x + %08x\n", (int)entry.storage_address, (int)entry.size);
            } else {
                uint32_t rel_addr = entry.storage_address - storage_addr;
                std::copy(
                    bin.begin() + rel_addr,
                    bin.begin() + rel_addr + entry.size,
                    std::back_inserter(to_hash));
                DEBUG_LOG("HASH %08x + %08x\n", (int)entry.storage_address, (int)entry.size);
            }
        }
    }

    return to_hash;
}


int hash_andor_sign(elf_file *elf, block *new_block, const public_t public_key, const private_t private_key, bool hash_value, bool sign, bool clear_sram) {
    std::vector<uint8_t> to_hash = get_lm_hash_data(elf, new_block, clear_sram);

    hash_andor_sign_block(new_block, public_key, private_key, hash_value, sign, to_hash);
    
    auto tmp = new_block->to_words();
    std::vector<uint8_t> data = words_to_lsb_bytes(tmp.begin(), tmp.end());

    // If multiple signature segments, need different names
    std::string sigx = ".sigx";
    if (elf->get_section(sigx) != NULL) {
        sigx += '0';
    }
    while (elf->get_section(sigx) != NULL) {
        sigx[5] += 1;
        if (sigx[5] > '9') fail(ERROR_INCOMPATIBLE, "Only compatible with up to 10 sigx blocks"); // very unlikely anyone has more than 10 sigx blocks - that would be silly
    }
    elf->append_segment(new_block->physical_addr, new_block->physical_addr, data.size(), sigx);
    auto sig_section = elf->get_section(sigx);
    assert(sig_section);
    assert(sig_section->virtual_address() == new_block->physical_addr);

    if (sig_section->size < data.size()) {
        fail(ERROR_UNKNOWN, "Block is too big for elf section\n");
    }
    while (data.size() < sig_section->size) {
        data.push_back(0);
    }

    elf->content(*sig_section, data);

    return 0;
}


std::vector<uint8_t> hash_andor_sign(std::vector<uint8_t> bin, uint32_t storage_addr, uint32_t runtime_addr, block *new_block, const public_t public_key, const private_t private_key, bool hash_value, bool sign, bool clear_sram) {
    std::vector<uint8_t> to_hash = get_lm_hash_data(bin, storage_addr, runtime_addr, new_block, clear_sram);

    hash_andor_sign_block(new_block, public_key, private_key, hash_value, sign, bin);
    
    auto tmp = new_block->to_words();
    std::vector<uint8_t> data = words_to_lsb_bytes(tmp.begin(), tmp.end());

    bin.insert(bin.end(), data.begin(), data.end());

    return bin;
}


void verify_block(std::vector<uint8_t> bin, uint32_t storage_addr, uint32_t runtime_addr, block *block, verified_t &hash_verified, verified_t &sig_verified) {
    std::shared_ptr<load_map_item> load_map = block->get_item<load_map_item>();
    std::shared_ptr<hash_def_item> hash_def = block->get_item<hash_def_item>();
    hash_verified = none;
    sig_verified = none;
    if (load_map == nullptr || hash_def == nullptr) {
        return;
    }
    std::vector<uint8_t> to_hash = get_lm_hash_data(bin, storage_addr, runtime_addr, block, false);

    // auto it = std::find(block->items.begin(), block->items.end(), hash_def);
    // assert (it != block->items.end());
    // int index = it - block->items.begin();

    // hash everything specified in the hash def
    auto tmp_words = block->to_words();
    tmp_words.resize(hash_def->block_words_to_hash);
    DEBUG_LOG("hash 0 + %08x\n", (int)(tmp_words.size())*4);
    if (block->items[0]->type() == PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE) {
        if (((image_type_item *)block->items[0].get())->flags & 0x8000) {
            DEBUG_LOG("CLEARING TBYB FLAG\n");
            assert(tmp_words[1] & 0x80000000);
            tmp_words[1] &= ~0x80000000;
        }
    }
    auto block_hashed_contents = words_to_lsb_bytes(tmp_words.begin(), tmp_words.end());
    std::copy(block_hashed_contents.begin(), block_hashed_contents.end(), std::back_inserter(to_hash));

    message_digest_t sha256;
    message_digest_t block_sha256;
    sha256_buffer(to_hash.data(), to_hash.size(), &sha256);
    dumper("SHA256", sha256);

    std::shared_ptr<hash_value_item> hash_value = block->get_item<hash_value_item>();
    if (hash_value != nullptr) {
        memcpy(block_sha256.bytes, hash_value->hash_bytes.data(), hash_value->hash_bytes.size());
        if (std::equal(hash_value->hash_bytes.begin(), hash_value->hash_bytes.end(), sha256.bytes)) {
            DEBUG_LOG("It's a match!\n");
            hash_verified = passed;
        } else {
            hash_verified = failed;
        }
    }

    std::shared_ptr<signature_item> signature = block->get_item<signature_item>();
    if (signature != nullptr) {
        public_t public_key = {};
        memcpy(public_key.bytes, signature->public_key_bytes.data(), signature->public_key_bytes.size());
        dumper("PUBLIC KEY", public_key);

        signature_t sig {
            .bytes = {},
            .der = {},
            .der_len = 0,
        };
        memcpy(sig.bytes, signature->signature_bytes.data(), signature->signature_bytes.size());
        dumper("SIG", sig);

        uint32_t err = verify_signature_secp256k1(&sig, &public_key, &sha256);
        if (err) {
            sig_verified = failed;
        } else {
            DEBUG_LOG("It's a match!\n");
            sig_verified = passed;
        }
    }
}


int encrypt(elf_file *elf, block *new_block, const private_t aes_key, const public_t public_key, const private_t private_key, bool hash_value, bool sign) {

    std::vector<uint8_t> to_enc = get_lm_hash_data(elf, new_block);

    std::random_device rand{};
    assert(rand.max() - rand.min() >= 256);

    while (to_enc.size() % 16 != 0){
        to_enc.push_back(rand()); // todo maybe better padding? random should be fine though
    }
    DEBUG_LOG("size %08x\n", (int)to_enc.size());

    iv_t iv;
    for(auto &e : iv.bytes) {
        e = rand();
    }

    std::vector<uint8_t> iv_data(iv.bytes, iv.bytes + sizeof(iv.bytes));

    std::vector<uint8_t> enc_data;
    enc_data.resize(to_enc.size());

    aes256_buffer(to_enc.data(), to_enc.size(), enc_data.data(), &aes_key, &iv);

    unsigned int i=0;
    for(const auto &seg : sorted_segs(elf)) {
        if (!seg->is_load()) continue;
        std::vector<uint8_t> data(enc_data.begin() + i, enc_data.begin() + i + seg->physical_size());
        // std::cout << "virt = " << std::hex << seg->virtual_address() << " + " << std::hex << seg->virtual_size() << ", phys = " << std::hex << seg->physical_address() << " + " << std::hex << seg->physical_size() << std::endl;
        if (data.size() != seg->physical_size()) {
            fail(ERROR_INCOMPATIBLE, "Elf segment physical size (%x) does not match data size in file (%x)", seg->physical_size(), data.size());
        }
        if (seg->physical_size() && seg->physical_address() < new_block->physical_addr) {
            DEBUG_LOG("ENCRYPTED %08x + %08x\n", (int)seg->physical_address(), (int)seg->physical_size());
            elf->content(*seg, data);
            i += data.size();
            assert(i <= enc_data.size());
        }
    }
    assert(i <= enc_data.size());
    if (i < enc_data.size()) {
        elf->append_segment(new_block->physical_addr, new_block->physical_addr, enc_data.size() - i, ".enc_pad");
        auto pad_section = elf->get_section(".enc_pad");
        assert(pad_section);
        assert(pad_section->virtual_address() == new_block->physical_addr);

        if (pad_section->size < enc_data.size() - i) {
            fail(ERROR_UNKNOWN, "Block is too big for elf section\n");
        }

        std::vector<uint8_t> pad_data(enc_data.begin() + i, enc_data.end());

        DEBUG_LOG("Adding padding len %d\n", (int)pad_data.size());
        for (auto x : pad_data) DEBUG_LOG("%02x", x);
        DEBUG_LOG("\n");

        elf->content(*pad_section, pad_data);
    }

    block link_block(0x20000000, enc_data.size());
    // ignored_item ign(1, {0});
    std::shared_ptr<image_type_item> image_def = new_block->get_item<image_type_item>();
    link_block.items.push_back(image_def);

    link_block.next_block_rel += (link_block.to_words().size())*4 + iv_data.size();

    auto tmp = link_block.to_words();
    DEBUG_LOG("Link block\n");
    for (auto x : tmp) DEBUG_LOG("%08x", x);
    DEBUG_LOG("\n");

    std::vector<uint8_t> link_data = words_to_lsb_bytes(tmp.begin(), tmp.end());

    elf->move_all(link_data.size() + iv_data.size());

    elf->append_segment(link_block.physical_addr, link_block.physical_addr, link_data.size(), ".enc_link");
    auto link_section = elf->get_section(".enc_link");
    assert(link_section);
    assert(link_section->virtual_address() == link_block.physical_addr);
    if (link_section->size < link_data.size()) {
        fail(ERROR_UNKNOWN, "Block is too big for elf section\n");
    }
    elf->content(*link_section, link_data);

    elf->append_segment(link_block.physical_addr + link_data.size(), link_block.physical_addr + link_data.size(), iv_data.size(), ".enc_iv");
    auto iv_section = elf->get_section(".enc_iv");
    assert(iv_section);
    if (iv_section->size < iv_data.size()) {
        fail(ERROR_UNKNOWN, "Block is too big for elf section\n");
    }
    elf->content(*iv_section, iv_data);

    new_block->physical_addr = link_block.physical_addr + link_block.next_block_rel;
    new_block->next_block_rel = -link_block.next_block_rel;

    std::shared_ptr<load_map_item> load_map = new_block->get_item<load_map_item>();
    if (load_map != nullptr) {
        new_block->items.erase(std::remove(new_block->items.begin(), new_block->items.end(), load_map), new_block->items.end());
    }

    hash_andor_sign(elf, new_block, public_key, private_key, hash_value, sign);

    return 0;
}


std::vector<uint8_t> encrypt(std::vector<uint8_t> bin, uint32_t storage_addr, uint32_t runtime_addr, block *new_block, const private_t aes_key, const public_t public_key, const private_t private_key, bool hash_value, bool sign) {
    std::random_device rand{};
    assert(rand.max() - rand.min() >= 256);

    while (bin.size() % 16 != 0){
        bin.push_back(rand()); // todo maybe better padding? random should be fine though
    }
    DEBUG_LOG("size %08x\n", (int)bin.size());

    iv_t iv;
    for(auto &e : iv.bytes) {
        e = rand();
    }

    std::vector<uint8_t> iv_data(iv.bytes, iv.bytes + sizeof(iv.bytes));

    std::vector<uint8_t> enc_data;
    enc_data.resize(bin.size());

    aes256_buffer(bin.data(), bin.size(), enc_data.data(), &aes_key, &iv);
    std::copy(enc_data.begin(), enc_data.end(), bin.begin());

    block link_block(0x20000000, enc_data.size());
    // ignored_item ign(1, {0});
    std::shared_ptr<image_type_item> image_def = new_block->get_item<image_type_item>();
    link_block.items.push_back(image_def);

    link_block.next_block_rel += (link_block.to_words().size())*4 + iv_data.size();

    auto tmp = link_block.to_words();
    DEBUG_LOG("Link block\n");
    for (auto x : tmp) DEBUG_LOG("%08x", x);
    DEBUG_LOG("\n");

    std::vector<uint8_t> link_data = words_to_lsb_bytes(tmp.begin(), tmp.end());

    bin.insert(bin.begin(), link_data.begin(), link_data.end());

    bin.insert(bin.begin() + link_data.size(), iv_data.begin(), iv_data.end());

    new_block->physical_addr = link_block.physical_addr + link_block.next_block_rel;
    new_block->next_block_rel = -link_block.next_block_rel;

    std::shared_ptr<load_map_item> load_map = new_block->get_item<load_map_item>();
    if (load_map != nullptr) {
        new_block->items.erase(std::remove(new_block->items.begin(), new_block->items.end(), load_map), new_block->items.end());
    }

    return hash_andor_sign(bin, storage_addr, runtime_addr, new_block, public_key, private_key, hash_value, sign);;
}
#endif
