#pragma once

#include <functional>

#if HAS_MBEDTLS
    #include "mbedtls_wrapper.h"
#endif
#include "elf_file.h"
#include "metadata.h"

typedef enum verified_t {
    none,
    failed,
    passed
} verified_t;

// Common
#if HAS_MBEDTLS
    int read_keys(const std::string &filename, public_t *public_key, private_t *private_key);
    void hash_andor_sign_block(block *new_block, const public_t public_key, const private_t private_key, bool hash_value, bool sign, std::vector<uint8_t> to_hash = {});
#endif

// Elfs
std::unique_ptr<block> find_first_block(elf_file *elf);
block place_new_block(elf_file *elf, std::unique_ptr<block> &first_block);
#if HAS_MBEDTLS
    int hash_andor_sign(elf_file *elf, block *new_block, const public_t public_key, const private_t private_key, bool hash_value, bool sign, bool clear_sram = false);
    int encrypt(elf_file *elf, block *new_block, const private_t aes_key, const public_t public_key, const private_t private_key, bool hash_value, bool sign);
#endif

// Bins
typedef std::function<void(std::vector<uint8_t> &bin, uint32_t size)> get_more_bin_cb;
std::unique_ptr<block> find_first_block(std::vector<uint8_t> bin, uint32_t storage_addr);
std::unique_ptr<block> get_last_block(std::vector<uint8_t> &bin, uint32_t storage_addr, std::unique_ptr<block> &first_block, get_more_bin_cb more_cb = nullptr);
std::vector<std::unique_ptr<block>> get_all_blocks(std::vector<uint8_t> &bin, uint32_t storage_addr, std::unique_ptr<block> &first_block, get_more_bin_cb more_cb = nullptr);
block place_new_block(std::vector<uint8_t> &bin, uint32_t storage_addr, std::unique_ptr<block> &first_block);
uint32_t calc_checksum(std::vector<uint8_t> bin);
#if HAS_MBEDTLS
    std::vector<uint8_t> hash_andor_sign(std::vector<uint8_t> bin, uint32_t storage_addr, uint32_t runtime_addr, block *new_block, const public_t public_key, const private_t private_key, bool hash_value, bool sign, bool clear_sram = false);
    std::vector<uint8_t> encrypt(std::vector<uint8_t> bin, uint32_t storage_addr, uint32_t runtime_addr, block *new_block, const private_t aes_key, const public_t public_key, const private_t private_key, bool hash_value, bool sign);
    void verify_block(std::vector<uint8_t> bin, uint32_t storage_addr, uint32_t runtime_addr, block *block, verified_t &hash_verified, verified_t &sig_verified);
#endif
