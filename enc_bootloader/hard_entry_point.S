/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico.h"
#include "pico/asm_helper.S"
#include "pico/platform/cpu_regs.h"
#include "boot/bootrom_constants.h"
#include "hardware/rcp.h"
#include "hardware/regs/bootram.h"
#include "hardware/regs/resets.h"
#include "hard_entry_point.h"

pico_default_asm_setup

#if !PICO_NO_FLASH
#error expected PICO_NO_FLASH
#endif
// ELF entry point:
.type _entry_point,%function
.type _reset_handler,%function
.thumb_func
.global _entry_point, _reset_handler
_entry_point:
_reset_handler:
#if !ALLOW_DEBUGGING
    // note we assume that RCP is already initialized by bootrom
#else
    // just enable the RCP which is fine if it already was (we assume no other co-processors are enabled at this point to save space)
    ldr r0, = PPB_BASE + M33_CPACR_OFFSET
    movs r1, #ARM_CPU_PREFIXED(CPACR_CP7_BITS)
    str r1, [r0]
    // only initialize canary seeds if they haven't been (as to do so twice is a fault)
    mrc p7, #1, apsr_nzcv, c0, c0, #0
    bmi 1f
    // i dont think it much matters what we initialized to, as to have gotten here we must have not
    // gone thru the bootrom (which a secure boot would have)
    mcrr p7, #8, r0, r0, c0
    mcrr p7, #8, r0, r0, c1
    sev
1:
    ldr r0, =__StackTop
    msr msp, r0
    ldr r0, =__vectors
    ldr r1, =(PPB_BASE + ARM_CPU_PREFIXED(VTOR_OFFSET))
    str r0, [r1]
#endif

    ldr r0, =PPB_BASE + M33_MPU_CTRL_OFFSET
    adr r6, mpu_regions
    // set region 7 (flash)
    ldmia r6!, {r1, r2, r3, r4}
    stmia r0!, {r1, r2, r3, r4}
    // sp should have low 3 bits == 0 (which is all the bits RNR is)
    str sp, [r0, #M33_MPU_RNR_OFFSET - (M33_MPU_CTRL_OFFSET + 16)]
#if HARDENING
    sub lr, lr // we don't need lr and it is unlikely to be something that ww will read from memory if this is skipped
    ldr r1, [r0, #M33_MPU_RNR_OFFSET - (M33_MPU_CTRL_OFFSET + 16)]
    // RNR should read back as zero
    rcp_iequal_nodelay r1, lr
    adds r0, r1
#endif
    subs r0, #8
#ifdef HARDENING
    rcp_iequal_nodelay lr, r1
#endif
    ldmia r6!, {r1, r2, r3, r4, r5, r8, r9, r10}
    stmia r0!, {r1, r2, r3, r4, r5, r8, r9, r10}
#if HARDENING
    ldr r1, = PPB_BASE + M33_MPU_RLAR_A3_OFFSET + 4
    rcp_iequal_nodelay r0, r1
    adr r7, mpu_regions + 48
    rcp_iequal_nodelay r6, r7
    // check this again for good measure
    // todo can remove if we do some tt checks
    rcp_iequal_nodelay r1, r0
    // todo this is less useful, because if we laoded garbage into the MPU it may cause a fault anyway,
    //      however it is only one bit for enabling, so we definitely should do some tt to prove it is enabled
    rcp_iequal_nodelay r7, r6
#endif

    // todo is this also part of ALLOW_DEBUGGING (is it done by LOAD_MAP?, or indeed by later code)
    // Zero out the BSS
    ldr r1, =__bss_start__
    ldr r2, =__bss_end__
    movs r0, #0
    b bss_fill_test
bss_fill_loop:
    stm r1!, {r0}
bss_fill_test:
    cmp r1, r2
    bne bss_fill_loop
#if HARDENING
    rcp_iequal_nodelay r1, r2
#endif
    // runtime_init is inlined here, to avoid a bunch of ROP attackable functions (note
    // runtime_run_initializers is particularly bad as it calls a list of function pointers)
    //
    // can revisit this when we have a hardened SDK option
#if 0
    bl runtime_init
#else
#if !PICO_RP2350
#error RP2350 init only supported
#endif
    rcp_count_set_nodelay STEP_RUNTIME_CLOCKS_INIT
    // runtime_init_install_stack_guard
    ldr r1, =__StackBottom
    // todo harden
    msr msplim, r1

    // runtime_init_early_resets - note we actually reset more than the standard runtime_init
    // as we know more about our environment
    ldr r1, =RESETS_BASE + RESETS_RESET_OFFSET + REG_ALIAS_SET_BITS
#if ALLOW_DEBUGGING
    ldr r0, =~(RESETS_RESET_SYSCFG_BITS | RESETS_RESET_PLL_SYS_BITS | RESETS_RESET_PLL_USB_BITS) // include USB PLL in case we are running from it
#else
    ldr r0, =~(RESETS_RESET_SYSCFG_BITS | RESETS_RESET_PLL_SYS_BITS)
#endif
    str r0, [r1]

    bl runtime_init_clocks
    rcp_count_check_nodelay STEP_RUNTIME_CLOCKS_INIT_DONE

    // note: there is no runtime_init_post_clocks_reset as there are no peripherals that need turning on
    // all we really care about is lock BOOTROM_LOCK_ENABLE for now, because we don't want bootrom locking enabled
    // and without it, the bootrom will ignore the reset
    ldr r1, =BOOTRAM_BASE + BOOTRAM_BOOTLOCK0_OFFSET + BOOTROM_LOCK_ENABLE * 4
    str r1, [r1] // any write unlocks

    // note: there is no runtime_init_bootrom_reset as we will have ome in via the bootrom
    // todo however think about somehow being watchdogged back in?

    // note there is no runtime_init_per_corebootrom_reset as it is a no-op on Arm
#endif

    bl main
#if ALLOW_DEBUGGING
    bkpt #0
#endif
    rcp_panic
#if HARDENING
    rcp_panic
//#if DOUBLE_HARDENING
    rcp_panic
//#endif
#endif

.p2align 2
.global data_cpy_table
data_cpy_table:
.word 0

#define MPU_REGION_RW_XN(n, rbar, rlar) \
   .word rbar + M33_MPU_RBAR_XN_BITS + (0 << M33_MPU_RBAR_AP_LSB), \
    (rlar) + M33_MPU_RLAR_EN_BITS + 0x10 // note 0x10 will be written but not read back

#define MPU_REGION_RO_XN(n, rbar, rlar) \
   .word rbar + M33_MPU_RBAR_XN_BITS + (2 << M33_MPU_RBAR_AP_LSB), \
    (rlar) + M33_MPU_RLAR_EN_BITS + 0x10 // note 0x10 will be written but not read back

#define MPU_REGION_RO(n, rbar, rlar) \
   .word rbar + (2 << M33_MPU_RBAR_AP_LSB), \
    (rlar) + M33_MPU_RLAR_EN_BITS + 0x10 // note 0x10 will be written but not read back

.p2align 2
mpu_regions:
    /* ctrl */ .word M33_MPU_CTRL_PRIVDEFENA_BITS | M33_MPU_CTRL_ENABLE_BITS
    /* rnr */  .word 7 // set for thie initial load
#if MPU_REGION_FLASH != 7 || \
    MPU_REGION_RAM != 0 || \
    MPU_REGION_SCRATCH_X != 1 || \
    MPU_REGION_SCRATCH_Y_DATA != 2
    MPU_REGION_SCRATCH_Y_CODE != 3
#error MPU regions should be in order
#endif
    // todo what about XIP_CACHE binaries?
    MPU_REGION_RO_XN(MPU_REGION_FLASH,
                     XIP_BASE,
                     XIP_END - 0x20)
mpu_regions_middle:
    MPU_REGION_RW_XN(MPU_REGION_RAM,
                     SRAM_BASE,
                     SRAM_SCRATCH_X_BASE - 0x20)
    MPU_REGION_RO(MPU_REGION_SCRATCH_X,
                     SRAM_SCRATCH_X_BASE,
                     SRAM_SCRATCH_Y_BASE - 0x20)
    MPU_REGION_RW_XN(MPU_REGION_SCRATCH_Y_DATA,
                     SRAM_SCRATCH_Y_BASE,
                     __text_start - 0x20)
    MPU_REGION_RO(MPU_REGION_SCRATCH_Y_CODE,
                     __text_start,
                     __data_start__ - 0x20)
mpu_regions_end:

.if mpu_regions_middle - mpu_regions != 16
.err unexpected region size
.endif
.if mpu_regions_end - mpu_regions_middle != 32
.err unexpected region size
.endif


