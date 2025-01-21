/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include "pico/stdlib.h"
#include "boot/picobin.h"
#include "pico/bootrom.h"
#include "hardware/structs/otp.h"

#include "pico/binary_info.h"

#include "config.h"

extern void remap();
extern uint32_t gen_rand_sha();
extern void init_key(uint8_t *key);
extern void gen_lut_sbox();
extern int  ctr_crypt_s(uint8_t*iv,uint8_t*buf,int nblk);

extern uint8_t rkey_s[480];
extern uint8_t lut_a[256];
extern uint8_t lut_b[256];
extern uint32_t lut_a_map[1];
extern uint32_t lut_b_map[1];
extern uint32_t rstate_sha[4],rstate_lfsr[2];

void resetrng() {
    uint32_t f0,f1;
    uint32_t boot_random[4];
    rom_get_boot_random(boot_random);
    do f0=boot_random[0]; while(f0==0);   // make sure we don't initialise the LFSR to zero
    f1=boot_random[1];
    rstate_sha[0]=f0&0xffffff00;         // bottom byte must be zero (or 4) for SHA, representing "out of data"
    rstate_sha[1]=f1;
    rstate_sha[2]=0x41414141;
    rstate_sha[3]=0x41414141;
    rstate_lfsr[0]=f0;                   // must be nonzero for non-degenerate LFSR
    rstate_lfsr[1]=0x1d872b41;           // constant that defines LFSR
#if GEN_RAND_SHA
    reset_block(RESETS_RESET_SHA256_BITS);
    unreset_block(RESETS_RESET_SHA256_BITS);
#endif
}

static void init_lut_map() {
    int i;
    for(i=0;i<256;i++) lut_b[i]=gen_rand_sha()&0xff, lut_a[i]^=lut_b[i];
    lut_a_map[0]=0;
    lut_b_map[0]=0;
    remap();
}

static void init_aes() {
    resetrng();
    gen_lut_sbox();
    init_lut_map();
}

int main() {
    // Unlock OTP page with hardcoded key
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

    init_aes();
    // Read key directly from OTP - guarded reads will throw a bus fault if there are any errors
    uint16_t* otp_data = (uint16_t*)OTP_DATA_GUARDED_BASE;

    init_key((uint8_t*)&(otp_data[(OTP_CMD_ROW_BITS & (otp_key_page * 0x40))]));
    otp_hw->sw_lock[otp_key_page] = 0xf;
    ctr_crypt_s(iv, (void*)data_start_addr, data_size/16);

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
