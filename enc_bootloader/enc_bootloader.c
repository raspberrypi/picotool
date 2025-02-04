/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include "pico/stdlib.h"
#include "boot/picobin.h"
#include "boot/picoboot.h"
#include "pico/bootrom.h"
#include "hardware/structs/otp.h"

#include "hardware/structs/trng.h"
#include "hardware/structs/sha256.h"

#include "pico/binary_info.h"

#include "config.h"

extern uint32_t rstate_sha[4];

extern void decrypt(uint8_t* key4way, uint8_t* iv, uint8_t(*buf)[16], int nblk);


/*
The following is copied from the A2 boot ROM code at
src/main/arm/varm_boot_path.c with adjustments.
*/

void init_rstate() {
    int i;

    hw_set_bits(&resets_hw->reset, RESETS_RESET_TRNG_BITS | RESETS_RESET_SHA256_BITS);
    hw_clear_bits(&resets_hw->reset, RESETS_RESET_TRNG_BITS | RESETS_RESET_SHA256_BITS);
    // As these are clk_sys-clocked, 1 APB read is sufficient delay (no polling loop)
    (void)*hw_clear_alias(&resets_hw->reset);
    // second read is because I'm feeling generous
    (void)*hw_clear_alias(&resets_hw->reset);

    // RNG is derived by streaming a large number of TRNG ROSC samples
    // into the SHA-256. TRNG_SAMPLE_BLOCKS is the number of SHA-256
    // blocks to hash, each containing 384 samples from the TRNG ROSC:
    const unsigned int TRNG_SAMPLE_BLOCKS = 25;

    // Sample one ROSC bit into EHR every cycle, subject to CPU keeping up.
    // More temporal resolution to measure ROSC phase noise is better, if we
    // use a high quality hash function instead of naive VN decorrelation.
    // (Also more metastability events, which are a secondary noise source)

    // Each half-block (192 samples) takes approx 235 cycles, so 470 cycles/block.
    uintptr_t trng = (uintptr_t)trng_hw;
    sha256_hw_t *sha256 = sha256_hw;
    uint32_t _counter;
    // we use 0xff instead of -1 below to set all bits, so make sure there are no bits above bit 7
    static_assert(0xffffff00u == (
            (TRNG_TRNG_DEBUG_CONTROL_RESERVED_BITS | ~TRNG_TRNG_DEBUG_CONTROL_BITS) &
            (TRNG_RND_SOURCE_ENABLE_RESERVED_BITS | ~TRNG_RND_SOURCE_ENABLE_BITS) &
            (TRNG_RNG_ICR_RESERVED_BITS | ~TRNG_RNG_ICR_BITS) & ~0xffu), "");
    pico_default_asm_volatile (
        "movs r1, #1\n"
        "str r1, [%[trng], %[offset_trng_sw_reset]]\n"
        // Fixed delay is required after TRNG soft reset -- this plus
        // following sha256 write is sufficient:
        "ldr %[counter], [%[trng], %[offset_trng_sw_reset]]\n"
        // (reads as 0 -- initialises counter to 0.)
        // Initialise SHA internal state by writing START bit
        "movw r1, %[sha256_init]\n"
        "str r1, [%[sha256]]\n"
        // This is out of the loop because writing to this register seems to
        // restart the sampling, slowing things down. We don't care if this write
        // is skipped as that would just make sampling take longer.
        "str %[counter], [%[trng], %[offset_sample_cnt1]]\n"
        "adds %[counter], %[iterations] + 1\n"
    "2:\n"
        // TRNG setup is inside loop in case it is skipped. Disable checks and
        // bypass decorrelators, to stream raw TRNG ROSC samples:
        "movs r1, #0xff\n"
        "str r1, [%[trng], %[offset_debug_control]]\n"
        // Start ROSC if it is not already started
        "str r1, [%[trng], %[offset_rnd_source_enable]]\n"
        // Clear all interrupts (including EHR_VLD)
        "str r1, [%[trng], %[offset_rng_icr]]\n"
        // (hoist above polling loop to reduce poll->read delay)
        "movs r0, %[trng]\n"
        "adds r0, %[offset_ehr_data0]\n"
        // Wait for 192 ROSC samples to fill EHR, this should take constant time:
        "movs r2, %[offset_trng_busy]\n"
    "1:\n"
        "ldr  r1, [%[trng], r2]\n"
        "cmp r1, #0\n"
        "bne 1b\n"
        // Check counter and bail out if done -- we always end with a full
        // EHR, which is sufficient time for SHA to complete too.
        "subs %[counter], #1\n"
        "beq 3f\n"
        // r1 should now be 0, and we "check" by using it as the base for the loop count.
        "rnd_to_sha_start:\n"
        // Copy 6 EHR words to SHA-256, plus garbage (RND_SOURCE_ENABLE and
        // SAMPLE_CNT1) which pads us out to half of a SHA-256 block. This
        // means we can avoid checking SHA-256 ready whilst reading EHR, so
        // we restart sampling sooner. (SHA-256 becomes non-ready for 57
        // cycles after each 16 words written.).
        "adds r1, #8\n"
    "1:\n"
        "ldmia r0!, {r2, r3}\n"
        "str r2, [%[sha256], #4]\n"
        "str r3, [%[sha256], #4]\n"
        "subs r1, #2\n"
        "bne 1b\n"
        "rnd_to_sha_end:\n"
        // TRNG is now sampling again, having started after we read the last
        // EHR word. Grab some in-progress SHA bits and use them to modulate
        // the chain length, to reduce chance of injection locking:
        "ldr r2, [%[sha256], #8]\n"
        "str r2, [%[trng], %[offset_trng_config]]\n"
        // Repeat for all blocks
        "b.n 2b\n"
    "3:\n"
        // Done -- turn off rand source as it's a waste of power, and wipe SHA
        // bits left in TRNG config. r1 is known to be 0 (even on RISC-V), so
        // use that.
        "str r1, [%[trng], %[offset_trng_config]]\n"
        "str r1, [%[trng], %[offset_rnd_source_enable]]\n"

            // Function of the actual TRNG pointer we used, and the number of loop iterations
        :
            [counter] "=&l" (_counter),
            // Not actually written, but we tell the compiler to assume such:
            [trng]    "+l"  (trng),
            [sha256]  "+l"  (sha256)
        :
            [iterations]               "i" (TRNG_SAMPLE_BLOCKS * 2),
            [sha256_init]              "i" (SHA256_CSR_RESET | SHA256_CSR_START_BITS),
            [offset_trng_sw_reset]     "i" (TRNG_TRNG_SW_RESET_OFFSET       - TRNG_RNG_IMR_OFFSET),
            [offset_sample_cnt1]       "i" (TRNG_SAMPLE_CNT1_OFFSET         - TRNG_RNG_IMR_OFFSET),
            [offset_debug_control]     "i" (TRNG_TRNG_DEBUG_CONTROL_OFFSET  - TRNG_RNG_IMR_OFFSET),
            [offset_rnd_source_enable] "i" (TRNG_RND_SOURCE_ENABLE_OFFSET   - TRNG_RNG_IMR_OFFSET),
            [offset_rng_icr]           "i" (TRNG_RNG_ICR_OFFSET             - TRNG_RNG_IMR_OFFSET),
            [offset_trng_busy]         "i" (TRNG_TRNG_BUSY_OFFSET           - TRNG_RNG_IMR_OFFSET),
            [offset_ehr_data0]         "i" (TRNG_EHR_DATA0_OFFSET           - TRNG_RNG_IMR_OFFSET),
            [offset_trng_config]       "i" (TRNG_TRNG_CONFIG_OFFSET         - TRNG_RNG_IMR_OFFSET)
        : "r0", "r1", "r2", "r3"
    );
    // No need to wait for SHA, as we polled the EHR one extra time, which
    // takes longer than the SHA.
    for(i=0;i<4;i++) rstate_sha[i]=sha256->sum[i];
}


int main() {
    // This works around E21 by locking down the page with a key,
    // as there is no way to enter a key over the picoboot interface.
    // Unlocks the OTP page with hardcoded key [0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
    for (int i=0; i<4; i++) {
        uint32_t key_i = ((i*2+1) << 24) | ((i*2+1) << 16) |
                         (i*2 << 8) | i*2;
        otp_hw->crt_key_w[i] = key_i;
    }

    bi_decl(bi_program_feature_group(0x1111, 0x2222, "encryption_config"));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, data_start_addr, 0x20000000));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, data_size, 0x78000));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, iv0, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, iv1, 1));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, iv2, 2));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, iv3, 3));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, otp_key_page, 30));

    // Initialise IV from binary info words
    uint8_t iv[16];
    memcpy(iv, (void*)&iv0, sizeof(iv0));
    memcpy(iv + 4, (void*)&iv1, sizeof(iv1));
    memcpy(iv + 8, (void*)&iv2, sizeof(iv2));
    memcpy(iv + 12, (void*)&iv3, sizeof(iv3));

    // Initialise random state
    init_rstate();

    // Read key directly from OTP - guarded reads will throw a bus fault if there are any errors
    uint16_t* otp_data = (uint16_t*)OTP_DATA_GUARDED_BASE;
    decrypt((uint8_t*)&(otp_data[(OTP_CMD_ROW_BITS & (otp_key_page * 0x40))]), iv, (void*)data_start_addr, data_size/16);
    otp_hw->sw_lock[otp_key_page] = 0xf;

    // Increase stack limit by 0x100
    pico_default_asm_volatile(
            "mrs r0, msplim\n"
            "subs r0, 0x100\n"
            "msr msplim, r0"
            :::"r0");

    // Chain into decrypted image
    rom_chain_image(
        (uint8_t*)0x20080200, // AES Code & workspace from 0x20080030 -> 0x20081500
        4 * 1024,
        data_start_addr,
        data_size
    );

    __breakpoint();
}
