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

extern void decrypt(uint8_t* key4way, uint8_t* IV_OTPsalt, uint8_t* IV_public, uint8_t(*buf)[16], int nblk);

// These just have to be higher than the actual frequency, to prevent overclocking unused peripherals
#define ROSC_HZ 300*MHZ
#define OTHER_CLK_DIV 30

// Enable calibration of the ROSC using the XOSC - if disabled it runs with the fixed ~110MHz ROSC configuration
#define XOSC_CALIBRATION 0

#if CK_JITTER
void runtime_init_clocks(void) {
    // Disable resus that may be enabled from previous software
    clocks_hw->resus.ctrl = 0;

    bi_decl(bi_ptr_int32(0, 0, rosc_div, 2)); // default divider 2
    bi_decl(bi_ptr_int32(0, 0, rosc_drive, 0x7777)); // default drives of 0b111 (0x7)

    // Bump up ROSC speed to ~110MHz
    rosc_hw->freqa = 0; // reset the drive strengths
    rosc_hw->div = rosc_div | ROSC_DIV_VALUE_PASS; // set divider
    // Increment the freqency range one step at a time - this is safe provided the current config is not TOOHIGH
    // because ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM | ROSC_CTRL_FREQ_RANGE_VALUE_HIGH == ROSC_CTRL_FREQ_RANGE_VALUE_HIGH
    static_assert((ROSC_CTRL_FREQ_RANGE_VALUE_LOW | ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM) == ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM);
    static_assert((ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM | ROSC_CTRL_FREQ_RANGE_VALUE_HIGH) == ROSC_CTRL_FREQ_RANGE_VALUE_HIGH);
    hw_set_bits(&rosc_hw->ctrl, ROSC_CTRL_FREQ_RANGE_VALUE_MEDIUM);
    hw_set_bits(&rosc_hw->ctrl, ROSC_CTRL_FREQ_RANGE_VALUE_HIGH);

    // Enable rosc randomisation
    rosc_hw->freqa = (ROSC_FREQA_PASSWD_VALUE_PASS << ROSC_FREQA_PASSWD_LSB) |
            rosc_drive | ROSC_FREQA_DS1_RANDOM_BITS | ROSC_FREQA_DS0_RANDOM_BITS; // enable randomisation

    // Not used with FREQ_RANGE_VALUE_HIGH, but should still be set to the maximum drive
    rosc_hw->freqb = (ROSC_FREQB_PASSWD_VALUE_PASS << ROSC_FREQB_PASSWD_LSB) |
            ROSC_FREQB_DS7_LSB | ROSC_FREQB_DS6_LSB | ROSC_FREQB_DS5_LSB | ROSC_FREQB_DS4_LSB;

#if XOSC_CALIBRATION
    // Calibrate ROSC frequency if XOSC present - otherwise just configure
    bi_decl(bi_ptr_int32(0, 0, xosc_hz, 12000000)); // xosc freq in Hz
    bi_decl(bi_ptr_int32(0, 0, clk_khz, 150 * KHZ)); // maximum clk_sys freq in KHz
    if (xosc_hz) {
        xosc_init();
        // Switch away from ROSC to avoid overclocking
        // CLK_REF = XOSC
        clock_configure_int_divider(clk_ref,
                        CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                        0,
                        xosc_hz,
                        1);
        // CLK_SYS = CLK_REF
        clock_configure_int_divider(clk_sys,
                        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC,   // leave the aux source on ROSC to prevent glitches when we switch back to it
                        xosc_hz,
                        1);

        // Go through configurations until you get below 150MHz
        uint8_t div = 1;
        uint32_t drives = 0x7777;
        while (div < 4) {
            rosc_hw->div = div | ROSC_DIV_VALUE_PASS;
            rosc_hw->freqa = (ROSC_FREQA_PASSWD_VALUE_PASS << ROSC_FREQA_PASSWD_LSB) |
                    drives |
                    ROSC_FREQA_DS1_RANDOM_BITS | ROSC_FREQA_DS0_RANDOM_BITS; // enable randomisation
            
            // Wait for ROSC to be stable
            while(!(rosc_hw->status & ROSC_STATUS_STABLE_BITS)) {
                tight_loop_contents();
            }

            if (frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC_PH) < clk_khz) {
                break;
            }

            if (!drives) div++;
            drives = drives ? 0x0000 : 0x7777;
        }
    }
#endif // XOSC_CALIBRATION
    // CLK SYS = ROSC directly, as it's running slowly enough
    clock_configure_int_divider(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC,
                    ROSC_HZ,    // this doesn't have to be accurate
                    1);

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
#endif


bi_decl(bi_ptr_int32(0, 0, otp_key_page, 29));

// The function lock_key() is called from decrypt() after key initialisation is complete and before decryption begins.
// That is a suitable point to lock the OTP area where key information is stored.
void lock_key() {
    otp_hw->sw_lock[otp_key_page] = 0xf;
    otp_hw->sw_lock[otp_key_page + 1] = 0xf;
}


int main() {
    bi_decl(bi_ptr_int32(0, 0, data_start_addr, 0x20000000));
    bi_decl(bi_ptr_int32(0, 0, data_size, 0x78000));
    bi_decl(bi_ptr_string(0, 0, iv, "0123456789abcdef", 17));

    // Read key directly from OTP - guarded reads will throw a bus fault if there are any errors
    uint16_t* otp_data = (uint16_t*)OTP_DATA_GUARDED_BASE;
    decrypt(
        (uint8_t*)&(otp_data[otp_key_page * 0x40]),
        (uint8_t*)&(otp_data[(otp_key_page + 2) * 0x40]),
        (uint8_t*)iv,
        (void*)data_start_addr,
        data_size/16
    );

    // Lock the IV salt
    otp_hw->sw_lock[otp_key_page + 2] = 0xf;

    // Increase stack limit by 0x100
    pico_default_asm_volatile(
            "mrs r0, msplim\n"
            "subs r0, 0x100\n"
            "msr msplim, r0"
            :::"r0");

    // Chain into decrypted image
    rom_chain_image(
        (uint8_t*)ROM_CHAIN_WORKSPACE,
        4 * 1024,
        data_start_addr,
        data_size
    );

    __breakpoint();
}
