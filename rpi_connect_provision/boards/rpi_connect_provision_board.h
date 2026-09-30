/*
 * Copyright (c) 2025 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

// Board for the provisioning image, which runs from RAM on any Pico 2 W style
// board: a Pico 2 W, but RP2350B with the maximum flash size, so it can reach
// partitions anywhere in flash on bigger parts (and use all 48 GPIOs). Pins
// that differ per board (UART, LED, wireless chip) are set at runtime through
// binary info instead.

#ifndef _BOARDS_RPI_CONNECT_PROVISION_BOARD_H
#define _BOARDS_RPI_CONNECT_PROVISION_BOARD_H

// --- FLASH ---
// 16MB is the most a single chip select can address
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// --- CYW43 ---
// Wireless chip pins are set at runtime from binary info, for boards that use
// different pins to the Pico 2 W
#ifndef CYW43_PIN_WL_DYNAMIC
#define CYW43_PIN_WL_DYNAMIC 1
#endif

// --- FPGA ---
// Detect the FPGA at runtime, so the clocks (and so the UART baud rate) are
// correct there too - otherwise the PLLs are assumed and clk_peri is wrong
#ifndef PICO_NO_FPGA_CHECK
#define PICO_NO_FPGA_CHECK 0
#endif

#include "boards/pico2_w.h"

// --- RP2350 VARIANT ---
// pico2_w.h sets this unconditionally
#undef PICO_RP2350A
#define PICO_RP2350A 0

#endif
