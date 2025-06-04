/**
* Copyright (c) 2025 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#define MPU_REGION_RAM 0
#define MPU_REGION_SCRATCH_X 1
#define MPU_REGION_SCRATCH_Y_DATA 2
#define MPU_REGION_SCRATCH_Y_CODE 3

#define MPU_REGION_FLASH 7

#define STEP_RUNTIME_CLOCKS_INIT      16
#define STEP_RUNTIME_CLOCKS_INIT2     17
#define STEP_RUNTIME_CLOCKS_INIT3     18
#define STEP_RUNTIME_CLOCKS_INIT4     19
#define STEP_RUNTIME_CLOCKS_INIT5     20
#define STEP_RUNTIME_CLOCKS_INIT_DONE 21
#define STEP_MAIN                     22
// note decryot: expects count to be 23
