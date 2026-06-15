/* Use XIP_CACHE as RAM, but still put stack in scratch x/y */
RAM_ORIGIN = XIP_RAM_ORIGIN_DEFAULT;
RAM_LENGTH = XIP_RAM_LENGTH_DEFAULT;

/* Other than that, normal no_flash binary */
INCLUDE "memmap_no_flash.incl"

/* Provide symbols for picotool */
__enc_bootloader_start = ORIGIN(RAM);
__enc_bootloader_end = ORIGIN(RAM) + LENGTH(RAM);
