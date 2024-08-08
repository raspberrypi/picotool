/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/regs/otp_data.h"

#include "pico/binary_info.h"


// Very rough - seems to be broken in xip_sram
#define sleep_ms(x) for(int i=0; i<x*20000; i++) asm volatile("nop");


int main() {
    bi_decl(bi_program_feature_group(0x1234, 0x5678, "led_config"));
    bi_decl(bi_ptr_int32(0x1234, 0x5678, led, PICO_DEFAULT_LED_PIN));

    gpio_init(led);
    gpio_set_dir(led, GPIO_OUT);
    gpio_put(led, true);

    bi_decl(bi_program_feature_group(0x1111, 0x2222, "otp_page_permissions"));

    bi_decl(bi_ptr_int32(0x1111, 0x2222, page0, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page1, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page2, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page3, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page4, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page5, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page6, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page7, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page8, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page9, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page10, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page11, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page12, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page13, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page14, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page15, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page16, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page17, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page18, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page19, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page20, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page21, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page22, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page23, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page24, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page25, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page26, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page27, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page28, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page29, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page30, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page31, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page32, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page33, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page34, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page35, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page36, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page37, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page38, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page39, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page40, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page41, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page42, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page43, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page44, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page45, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page46, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page47, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page48, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page49, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page50, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page51, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page52, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page53, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page54, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page55, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page56, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page57, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page58, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page59, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page60, 0));
    bi_decl(bi_ptr_int32(0x1111, 0x2222, page61, 0));

    uint32_t pages[62] = {
        page0,
        page1,
        page2,
        page3,
        page4,
        page5,
        page6,
        page7,
        page8,
        page9,
        page10,
        page11,
        page12,
        page13,
        page14,
        page15,
        page16,
        page17,
        page18,
        page19,
        page20,
        page21,
        page22,
        page23,
        page24,
        page25,
        page26,
        page27,
        page28,
        page29,
        page30,
        page31,
        page32,
        page33,
        page34,
        page35,
        page36,
        page37,
        page38,
        page39,
        page40,
        page41,
        page42,
        page43,
        page44,
        page45,
        page46,
        page47,
        page48,
        page49,
        page50,
        page51,
        page52,
        page53,
        page54,
        page55,
        page56,
        page57,
        page58,
        page59,
        page60,
        page61,
    };

    otp_cmd_t cmd;
    static __attribute__((aligned(4))) uint8_t otp_buffer[8];
    for (int i=0; i < 62; i++) {
        if (pages[i] == 0) {
            continue;
        }
        uint8_t perms0 = pages[i] & 0xff;
        uint8_t perms1 = (pages[i] >> 16) & 0xff;

        // printf("Unlocking\n");
        // for (int i=0; i<4; i++) {
        //     uint32_t key_i = ((i*2+1) << 24) | ((i*2+1) << 16) |
        //                     (i*2 << 8) | i*2;
        //     otp_hw->crt_key_w[i] = key_i;
        //     printf("%08x\n", key_i);
        // }
        cmd.flags = (OTP_CMD_ROW_BITS & (OTP_DATA_PAGE0_LOCK0_ROW + i*2)) | OTP_CMD_WRITE_BITS;

        otp_buffer[0] = perms0;
        otp_buffer[1] = perms0;
        otp_buffer[2] = perms0;
        otp_buffer[3] = 0;
        otp_buffer[4] = perms1;
        otp_buffer[5] = perms1;
        otp_buffer[6] = perms1;
        otp_buffer[7] = 0;

        int8_t num = rom_func_otp_access(otp_buffer, sizeof(otp_buffer), cmd);

        sleep_ms(100);
    }

    gpio_put(led, false);

    reset_usb_boot(0, 0);
}
