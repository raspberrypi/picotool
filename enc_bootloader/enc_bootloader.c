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
#include "hardware/structs/mpu.h"
#include "pico/binary_info.h"

#include "hardware/clocks.h"
#include "hardware/structs/rosc.h"
#include "hardware/rcp.h"

#include "hard_entry_point.h"
#include "config.h"

extern void decrypt(uint8_t* key4way, uint8_t* IV_OTPsalt, uint8_t* IV_public, uint8_t(*buf)[16], int nblk);

// These just have to be higher than the actual frequency, to prevent overclocking unused peripherals
#define ROSC_HZ 300*MHZ
#define OTHER_CLK_DIV 30

// Enable calibration of the ROSC using the XOSC - if disabled it runs with the fixed ~110MHz ROSC configuration
#define XOSC_CALIBRATION 0

#if CK_JITTER

static inline bool has_glitchless_mux(clock_handle_t clock) {
    return clock == clk_sys || clock == clk_ref;
}

static __force_inline void clock_configure_internal(clock_handle_t clock, uint32_t src, uint32_t auxsrc, uint32_t actual_freq, uint32_t div) {
    clock_hw_t *clock_hw = &clocks_hw->clk[clock];

    // If increasing divisor, set divisor before source. Otherwise set source
    // before divisor. This avoids a momentary overspeed when e.g. switching
    // to a faster source and increasing divisor to compensate.
    if (div > clock_hw->div)
        clock_hw->div = div;

    // If switching a glitchless slice (ref or sys) to an aux source, switch
    // away from aux *first* to avoid passing glitches when changing aux mux.
    // Assume (!!!) glitchless source 0 is no faster than the aux source.
    if (has_glitchless_mux(clock) && src == CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX) {
        hw_clear_bits(&clock_hw->ctrl, CLOCKS_CLK_REF_CTRL_SRC_BITS);
        while (!(clock_hw->selected & 1u))
            tight_loop_contents();
    }
    // If no glitchless mux, cleanly stop the clock to avoid glitches
    // propagating when changing aux mux. Note it would be a really bad idea
    // to do this on one of the glitchless clocks (clk_sys, clk_ref).
    else {
        // Disable clock. On clk_ref and clk_sys this does nothing,
        // all other clocks have the ENABLE bit in the same position.
        if (clock != clk_sys && clock != clk_ref) {
            pico_default_asm_volatile("b not_there\n"); // this branch should be elided by compiler in inlined func
            hw_clear_bits(&clock_hw->ctrl, CLOCKS_CLK_GPOUT0_CTRL_ENABLE_BITS);
        }
        // if (configured_freq[clock] > 0) {
        //     // Delay for 3 cycles of the target clock, for ENABLE propagation.
        //     // Note XOSC_COUNT is not helpful here because XOSC is not
        //     // necessarily running, nor is timer...
        //     uint delay_cyc = configured_freq[clk_sys] / configured_freq[clock] + 1;
        //     busy_wait_at_least_cycles(delay_cyc * 3);
        // }
    }

    // Set aux mux first, and then glitchless mux if this clock has one
    hw_write_masked(&clock_hw->ctrl,
        (auxsrc << CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB),
        CLOCKS_CLK_SYS_CTRL_AUXSRC_BITS
    );

    if (has_glitchless_mux(clock)) {
        hw_write_masked(&clock_hw->ctrl,
            src << CLOCKS_CLK_REF_CTRL_SRC_LSB,
            CLOCKS_CLK_REF_CTRL_SRC_BITS
        );
        while (!(clock_hw->selected & (1u << src)))
            tight_loop_contents();
    }

    // Enable clock. On clk_ref and clk_sys this does nothing,
    // all other clocks have the ENABLE bit in the same position.
    if (clock != clk_sys && clock != clk_ref) {
        pico_default_asm_volatile("b not_there\n"); // code should me omitted
        // hw_set_bits(&clock_hw->ctrl, CLOCKS_CLK_GPOUT0_CTRL_ENABLE_BITS);
    }

    // Now that the source is configured, we can trust that the user-supplied
    // divisor is a safe value.
    clock_hw->div = div;
    //configured_freq[clock] = actual_freq;
}

static __force_inline void clock_configure_int_divider_inline(clock_handle_t clock, uint32_t src, uint32_t auxsrc, uint32_t src_freq, uint32_t int_divider) {
    clock_configure_internal(clock, src, auxsrc, src_freq / int_divider, int_divider << CLOCKS_CLK_GPOUT0_DIV_INT_LSB);
}

void runtime_init_clocks(void) {
    // Disable resus that may be enabled from previous software
    clocks_hw->resus.ctrl = 0;

    bi_decl(bi_ptr_int32(0, 0, rosc_div, 2)); // default divider 2
    bi_decl(bi_ptr_int32(0, 0, rosc_drive, 0x7777)); // default drives of 0b111 (0x7)

    // Bump up ROSC speed to ~110MHz
    rosc_hw->freqa = 0; // reset the drive strengths (note password not needed for this)
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

    // Not used with FREQ_RANGE_VALUE_HIGH, but should still be set to the maximum drive
    rosc_hw->freqb = (ROSC_FREQB_PASSWD_VALUE_PASS << ROSC_FREQB_PASSWD_LSB) |
            ROSC_FREQB_DS7_BITS | ROSC_FREQB_DS6_BITS | ROSC_FREQB_DS5_BITS | ROSC_FREQB_DS4_BITS;

#if RC_COUNT
    rcp_count_check_nodelay(STEP_RUNTIME_CLOCKS_INIT);
#endif
#if HARDENING
    // force reload
    rosc_hw_t *rosc_hw2;
    pico_default_asm_volatile(
        "ldr %0, =%c1\n"
        : "=&r" (rosc_hw2) : "i" (ROSC_BASE), "r" (rosc_hw)); // note include rosc_hw to use a different register
    uint32_t c = (*(volatile uint32_t *)&rosc_drive) | ROSC_FREQA_DS1_RANDOM_BITS | ROSC_FREQA_DS0_RANDOM_BITS;
    rcp_iequal_nodelay(c, *(io_ro_16 *)(&rosc_hw2->freqa));
    rcp_iequal_nodelay(ROSC_FREQB_DS7_BITS | ROSC_FREQB_DS6_BITS | ROSC_FREQB_DS5_BITS | ROSC_FREQB_DS4_BITS, *(io_ro_16 *)(&rosc_hw2->freqb));
    rcp_iequal_nodelay((ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB) | ROSC_CTRL_FREQ_RANGE_VALUE_HIGH, rosc_hw2->ctrl);
#endif
#if RC_COUNT
    rcp_count_check_nodelay(STEP_RUNTIME_CLOCKS_INIT2);
#endif
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
    clock_configure_int_divider_inline(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC,
                    ROSC_HZ,    // this doesn't have to be accurate
                    1);

#if RC_COUNT
    rcp_count_check_nodelay(STEP_RUNTIME_CLOCKS_INIT3);
#endif

    // CLK_REF = ROSC / OTHER_CLK_DIV - this and other clocks aren't really used, so just need to be set to a low enough frequency
    clock_configure_int_divider_inline(clk_ref,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH,
                    0,
                    ROSC_HZ,
                    OTHER_CLK_DIV);

#if RC_COUNT
    rcp_count_check_nodelay(STEP_RUNTIME_CLOCKS_INIT4);
#endif
    // we don't need any other clocks
#if HARDENING
    static_assert(CLOCKS_CLK_REF_CTRL_OFFSET == clk_ref * sizeof(clock_hw_t), "");
    static_assert(clk_sys > clk_ref, "");
    // prevent GCC from re-using ptr
    io_ro_32 *clock_ref_ctrl;
    pico_default_asm_volatile(
        "ldr %0, =%c1\n"
        : "=r" (clock_ref_ctrl) : "i" (CLOCKS_BASE + clk_ref * sizeof(clock_hw_t)));
    // check clock_sys
    rcp_iequal_nodelay(clock_ref_ctrl[(clk_sys - clk_ref) * sizeof(clock_hw_t) / 4],
        (CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC << CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB) |
        (CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX << CLOCKS_CLK_SYS_CTRL_SRC_LSB));
    // check clock_ref
    rcp_iequal_nodelay(*clock_ref_ctrl, 0);
#endif
#if RC_COUNT
    rcp_count_check_nodelay(STEP_RUNTIME_CLOCKS_INIT5);
#endif
}
#endif


bi_decl(bi_ptr_int32(0, 0, otp_key_page, 29));

// Stop the compiler from constant-folding a hardware base pointer into the
// pointers to individual registers, in cases where constant folding has
// produced redundant 32-bit pointer literals that could have been load/store
// offsets. (Note typeof(ptr+0) gives non-const, for +r constraint.) E.g.
//     uart_hw_t *uart0 = __get_opaque_ptr(uart0_hw);
#define __get_opaque_ptr(ptr) ({ \
    typeof((ptr)+0) __opaque_ptr = (ptr); \
    asm ("" : "+r"(__opaque_ptr)); \
    __opaque_ptr; \
})

// The function lock_key() is called from decrypt() after key initialisation is complete and before decryption begins.
// That is a suitable point to lock the OTP area where key information is stored.
void lock_key() {
    io_rw_32 *sw_lock = __get_opaque_ptr(&otp_hw->sw_lock[0]) + otp_key_page;
#if HARDENING
    // prevent compiler from re-using sw_lock pointer
    io_rw_32 *sw_lock2;
    pico_default_asm(
        "ldr %0, =%1\n"
        : "=r" (sw_lock2)
        : "i" (OTP_BASE + OTP_SW_LOCK0_OFFSET)
    );
    sw_lock2 += otp_key_page;
    sw_lock[0] = 0xf;
    sw_lock[1] = 0xf;
    uint32_t v = sw_lock2[1];
    v = (v << 4) | sw_lock2[0];
    uint32_t ff1;
    pico_default_asm_volatile(
        "movs %0, #0xff"
        : "=r" (ff1)
    );
    uint32_t ff2;
    pico_default_asm_volatile(
        "movw %0, #0xff"
        : "=r" (ff2)
    );
    rcp_iequal(v, ff1);
#if RC_COUNT
    rcp_count_check_nodelay(31);
#endif
    rcp_iequal(ff2, v);
#else
    sw_lock[0] = 0xf;
    sw_lock[1] = 0xf;
#if RC_COUNT
    rcp_count_check_nodelay(31);
#endif
#endif
}

static __force_inline void lock_all(void) {
    io_rw_32 *sw_lock = __get_opaque_ptr(&otp_hw->sw_lock[0]) + otp_key_page;
#if HARDENING
    // we only actually need to lock page 2 but we lock and check 0, 1, 2 anyway
    // prevent compiler from re-using sw_lock pointer
    io_rw_32 *sw_lock2;
    pico_default_asm(
        "ldr %0, =%1\n"
        : "=r" (sw_lock2)
        : "i" (OTP_BASE + OTP_SW_LOCK0_OFFSET)
    );
    sw_lock2 += otp_key_page;
    sw_lock[0] = 0xf;
    sw_lock[1] = 0xf;
    uint32_t v = sw_lock2[1];
    sw_lock[2] = 0xf;
    v = (v << 4) | sw_lock2[0];
    v = (v << 4) | sw_lock2[2];
    uint32_t fff1;
    pico_default_asm_volatile(
        "movw %0, #0xfff"
        : "=r" (fff1)
    );
    uint32_t fff2;
    pico_default_asm_volatile(
        "movw %0, #0xfff"
        : "=r" (fff2)
    );
    rcp_iequal(v, fff1);
    rcp_iequal(fff2, v);
#else
    sw_lock[0] = 0xf;
    sw_lock[1] = 0xf;
    sw_lock[2] = 0xf;
#endif

}
#define PIN_R PICO_DEFAULT_LED_PIN
#define PIN_G 6

#ifndef USE_LED
#if ALLOW_DEBUGGING
#define USE_LED 1
#endif
#endif

int main() {
#if HARDENING
    // lock down OTP_DEBUG_EN so later code cannot enable debugger if it was disabled in OTP,
    // and thus be able to force a reset of the OTP while in the debugger, giving access
    // to our secret.

    // note: this is in assembler, since I already had it in hard_entry_point.S, then realized
    // it was desirable for mbedtls version too, and didn't feel like fighting with GCC
    pico_default_asm_volatile(
        "ldr r1, =%[otp_base] + %c[lock_offset]\n"
        "ldr r2, =%[otp_base]\n"
        "adds r2, %[lock_offset]\n"
        "mcrr2 p7, #7, r1, r2, c0\n" // rcp_iequal_nodelay r1, r2
        "movw r0, %[lock_bits]\n"
        "str r0, [r1]\n"
        "ldr r3, [r2]\n"
        "mcrr2 p7, #7, r0, r3, c0\n"  // rcp_iequal_nodelay r0, r3
        :
        : [otp_base] "i" (OTP_BASE),
          [lock_offset] "i" (OTP_DEBUGEN_LOCK_OFFSET),
          [lock_bits] "i" (OTP_DEBUGEN_LOCK_BITS)
        : "r0", "r1", "r2", "r3", "cc"
    );
#else
    otp_hw->debugen_lock = OTP_DEBUGEN_LOCK_BITS;
#endif

#if USE_LED
#if ALLOW_DEBUGGING
    reset_block_mask((1u << RESET_IO_BANK0) | (1u << RESET_PADS_BANK0));
    unreset_block_mask_wait_blocking((1u << RESET_IO_BANK0) | (1u << RESET_PADS_BANK0));
#endif
    gpio_init(PIN_G);
    gpio_set_dir(PIN_G, GPIO_OUT);
    gpio_put(PIN_G, 1);
#endif
#if RC_COUNT
    rcp_count_check_nodelay(STEP_MAIN);
#endif

    bi_decl(bi_ptr_int32(0, 0, data_start_addr, 0x20000000));
    bi_decl(bi_ptr_int32(0, 0, data_size, 0x78000));
    bi_decl(bi_ptr_string(0, 0, iv, "0123456789abcdef", 17));

    // Read key directly from OTP - guarded reads will throw a bus fault if there are any errors
#if ALLOW_DEBUGGING
    uint16_t* otp_data = (uint16_t*)OTP_DATA_BASE;
#else
    uint16_t* otp_data = (uint16_t*)OTP_DATA_GUARDED_BASE;
#endif
    decrypt(
        (uint8_t*)&(otp_data[otp_key_page * 0x40]),
        (uint8_t*)&(otp_data[(otp_key_page + 2) * 0x40]),
        (uint8_t*)iv,
        (void*)data_start_addr,
        data_size/16
    );

    lock_all();

#if ALLOW_DEBUGGING && !defined(NDEBUG)
    // check locks
    io_ro_32 *otp_data_raw = (io_ro_32 *)OTP_DATA_RAW_BASE;
    rcp_iequal(otp_data_raw[otp_key_page * 0x40], -1);
    rcp_iequal(otp_data_raw[otp_key_page * 0x40 + 0x40], -1);
    rcp_iequal(otp_data_raw[otp_key_page * 0x40 + 0x80], -1);
#endif
    // Increase stack space by 0x100
    pico_default_asm_volatile(
            "mrs r0, msplim\n"
            "subs r0, 0x100\n"
            "msr msplim, r0"
            :::"r0");

#if HARDENING
    // this is the only one we want to leave in place
    static_assert(MPU_REGION_FLASH == 7, "");
    // disable regions 0-3 (note rnr should be 0 already)
    // todo we should check here that we decrypted enough
    mpu_hw->rlar = 0;
    mpu_hw->rlar_a1 = 0;
    mpu_hw->rlar_a2 = 0;
    mpu_hw->rlar_a3 = 0;
    // todo make this configurable (if you want MPU disabled)
#if 1
    // disable region 7
    mpu_hw->rnr = MPU_REGION_FLASH;
    mpu_hw->rlar = 0;
    // disable MPU
    mpu_hw->ctrl = 0;
#endif
#endif

#if USE_LED
    gpio_put(PIN_G, 0);
    __compiler_memory_barrier();
#endif
    // Chain into decrypted image
    // todo make sure the image is NOT in flash (perhaps we also disallow execution from flash as an option)
    int chain_result = rom_chain_image(
        (uint8_t*)ROM_CHAIN_WORKSPACE,
        4 * 1024,
        data_start_addr,
        data_size
    );

    chain_result = -chain_result;
#if USE_LED
    gpio_init(PIN_R);
    gpio_set_dir(PIN_R, GPIO_OUT);
    gpio_put(PIN_R, 1);

    
    gpio_put(PIN_G, 0);
    for (int j = 0; j < 10000000; j++) { __nop(); }
    for (int i = 0; i < chain_result; i++) {
        gpio_put(PIN_G, 0);
        for (int j = 0; j < 5000000; j++) { __nop(); }
        gpio_put(PIN_G, 1);
        for (int j = 0; j < 5000000; j++) { __nop(); }
    }
    gpio_put(PIN_G, 0);
#endif

#if ALLOW_DEBUGGING
    rom_connect_internal_flash();
    rom_flash_exit_xip();
    rom_flash_range_erase(0x100000, 0x80000, (1u << 16), 0xd8);
    rom_flash_range_program(0x100000, (void *)data_start_addr, data_size);
    rom_reset_usb_boot(0, 0);
#endif
}
