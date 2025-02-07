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

#include "hardware/clocks.h"
#include "hardware/xosc.h"
#include "hardware/structs/rosc.h"

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

    rosc_hw->random = rstate_sha[0];
}

// These just have to be higher than the actual frequency, to prevent overclocking unused peripherals
#define ROSC_HZ 300*MHZ
#define OTHER_CLK_DIV 30

void runtime_init_clocks(void) {
    // Disable resus that may be enabled from previous software
    clocks_hw->resus.ctrl = 0;

    bi_decl(bi_ptr_int32(0, 0, rosc_div, 2)); // default divider 2
    bi_decl(bi_ptr_int32(0, 0, rosc_drive, 0x0000)); // default drives of 0
    bi_decl(bi_ptr_int32(0, 0, xosc_mhz, 12)); // xosc freq in MHz
    bi_decl(bi_ptr_int32(0, 0, clk_mhz, 150)); // xosc freq in MHz

    // Bump up ROSC speed to ~90MHz
    rosc_hw->freqa = 0; // reset the drive strengths
    rosc_hw->div = rosc_div | ROSC_DIV_VALUE_PASS; // set divider
    // Increment the freqency range one step at a time - this is safe provided the current config is not TOOHIGH
    // because ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM | ROSC_CTRL_FREQ_RANGE_VALUE_HIGH == ROSC_CTRL_FREQ_RANGE_VALUE_HIGH
    static_assert(ROSC_CTRL_FREQ_RANGE_VALUE_LOW | ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM == ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM);
    static_assert(ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM | ROSC_CTRL_FREQ_RANGE_VALUE_HIGH == ROSC_CTRL_FREQ_RANGE_VALUE_HIGH);
    hw_set_bits(&rosc_hw->ctrl, ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM);
    hw_set_bits(&rosc_hw->ctrl, ROSC_CTRL_FREQ_RANGE_VALUE_HIGH);

    // Enable rosc randomisation
    rosc_hw->freqa = (ROSC_FREQA_PASSWD_VALUE_PASS << ROSC_FREQA_PASSWD_LSB) |
            rosc_drive | ROSC_FREQA_DS1_RANDOM_BITS | ROSC_FREQA_DS0_RANDOM_BITS; // enable randomisation

    // Not used with FREQ_RANGE_VALUE_HIGH, but should still be set
    rosc_hw->freqb = (ROSC_FREQB_PASSWD_VALUE_PASS << ROSC_FREQB_PASSWD_LSB) |
            rosc_drive;

    // Calibrate ROSC frequency if XOSC present - otherwise just configure
    uint32_t rosc_freq_mhz = 0;
    if (xosc_mhz) {
        xosc_init();
        // CLK_REF = XOSC
        clock_configure_int_divider(clk_ref,
                        CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                        0,
                        xosc_mhz * MHZ,
                        1);
        // CLK_SYS = CLK_REF
        clock_configure_int_divider(clk_sys,
                        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC,   // leave the aux source on ROSC
                        xosc_mhz * MHZ,
                        1);

        // Max out rosc
        rosc_hw->div = 1 | ROSC_DIV_VALUE_PASS;
        rosc_hw->freqa = (ROSC_FREQA_PASSWD_VALUE_PASS << ROSC_FREQA_PASSWD_LSB) |
                ROSC_FREQA_DS3_BITS | ROSC_FREQA_DS2_BITS |
                ROSC_FREQA_DS1_RANDOM_BITS | ROSC_FREQA_DS0_RANDOM_BITS; // enable randomisation
        rosc_hw->freqb = (ROSC_FREQB_PASSWD_VALUE_PASS << ROSC_FREQB_PASSWD_LSB) |
                ROSC_FREQB_DS7_LSB | ROSC_FREQB_DS6_LSB | ROSC_FREQB_DS5_LSB | ROSC_FREQB_DS4_LSB;

        // Wait for ROSC to be stable
        while(!(rosc_hw->status & ROSC_STATUS_STABLE_BITS)) {
            tight_loop_contents();
        }

        rosc_freq_mhz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC_PH) / KHZ;
    }
    if (rosc_freq_mhz > clk_mhz) {
        // CLK SYS = ROSC divided down to clk_mhz, according to XOSC calibration
        clock_configure_mhz(clk_sys,
                        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC,
                        rosc_freq_mhz,
                        clk_mhz);
    } else { // Either ROSC is running too slowly, or there was no XOSC to calibrate it against
        // CLK SYS = ROSC directly, as it's running slowly enough
        clock_configure_int_divider(clk_sys,
                        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC,
                        ROSC_HZ,    // this doesn't have to be accurate
                        1);
    }

    // Configure other clocks - none of these need to be accurate

    // CLK_REF = ROSC / OTHER_CLK_DIV - this and other clocks aren't really used, so just need to be set to a low enough frequency
    clock_configure_int_divider(clk_ref,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH,
                    0,
                    ROSC_HZ,
                    OTHER_CLK_DIV);

    // CLK USB (not used)
    clock_configure_int_divider(clk_usb,
                    0, // No GLMUX
                    CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH,
                    ROSC_HZ,
                    OTHER_CLK_DIV);

    // CLK ADC (not used)
    clock_configure_int_divider(clk_adc,
                    0, // No GLMUX
                    CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH,
                    ROSC_HZ,
                    OTHER_CLK_DIV);

    // CLK PERI Used as reference clock for UART and SPI serial. (not used)
    clock_configure_int_divider(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    ROSC_HZ,
                    OTHER_CLK_DIV);

    // CLK_HSTX Transmit bit clock for the HSTX peripheral. (not used)
    clock_configure_int_divider(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                    ROSC_HZ,
                    OTHER_CLK_DIV);
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

    bi_decl(bi_ptr_int32(0, 0, data_start_addr, 0x20000000));
    bi_decl(bi_ptr_int32(0, 0, data_size, 0x78000));
    bi_decl(bi_ptr_int32(0, 0, iv0, 0));
    bi_decl(bi_ptr_int32(0, 0, iv1, 1));
    bi_decl(bi_ptr_int32(0, 0, iv2, 2));
    bi_decl(bi_ptr_int32(0, 0, iv3, 3));
    bi_decl(bi_ptr_int32(0, 0, otp_key_page, 30));

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
