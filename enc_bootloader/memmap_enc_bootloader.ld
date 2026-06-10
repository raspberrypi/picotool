/* Heavily customised using overrides in memmap_enc_bootloader folder */

INCLUDE "memmap_no_flash.incl"

/* Extra assertions */
ASSERT(__StackBottom >= __scratch_y_end__, "stack collision in scratch_y")
ASSERT(chaff==0x20081000, "Chaff array must be located at 0x20081000")

/* Provide symbols for picotool */
__enc_bootloader_start = ORIGIN(RAM_START);
__enc_bootloader_end = ORIGIN(RAM) + LENGTH(RAM);
