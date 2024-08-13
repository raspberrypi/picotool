#include "hardware/regs/io_qspi.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/ssi.h"
#include "hardware/flash.h"

asm(
    ".macro static_assert value, msg\n"
        ".if !(\\value)\n"
        ".err \\msg\n"
        ".endif\n"
    ".endm\n"

    ".set FLASH_RUID_CMD, 0x4b\n"
    ".set FLASH_RUID_DUMMY_BYTES, 4\n"
    ".set FLASH_RUID_DATA_BYTES, 8\n"
    ".set FLASH_RUID_TOTAL_BYTES, (1 + FLASH_RUID_DUMMY_BYTES + FLASH_RUID_DATA_BYTES)\n"
);

void flash_do_cmd(const uint8_t * txbuf, uint8_t *rxbuf, size_t count);
void __attribute__((naked)) flash_get_unique_id_raw(void) {
    asm(
        ".Lflash_get_unique_id_raw:\n"
            "adr r0, .Ltxbuf\n"
            "adr r1, .Lrxbuf\n"
            "ldr r2, .Lbuflen\n"
            "b flash_do_cmd\n"
        ".Lbuflen:\n"
            ".word FLASH_RUID_TOTAL_BYTES\n"
        ".Ltxbuf:\n"
            ".byte FLASH_RUID_CMD\n"
            ".zero (FLASH_RUID_TOTAL_BYTES - 1)\n"
            ".zero (16 - FLASH_RUID_TOTAL_BYTES)\n"
        ".Lrxbuf:\n"
            ".zero FLASH_RUID_TOTAL_BYTES\n"
            ".zero (16 - FLASH_RUID_TOTAL_BYTES)\n"
        "static_assert ((.Lrxbuf - flash_get_unique_id_raw) == 28), \"rxbuf offset incorrect\"\n"
    );
}

static inline void __attribute__((always_inline)) flash_cs_force(_Bool high) {
    uint32_t field_val = high ?
        IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_HIGH :
        IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_LOW;
    hw_write_masked(&ioqspi_hw->io[1].ctrl,
        field_val << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS
    );
}

void flash_do_cmd(const uint8_t * txbuf, uint8_t *rxbuf, size_t count) {
    flash_cs_force(0);
    size_t tx_remaining = count;
    size_t rx_remaining = count;
    // We may be interrupted -- don't want FIFO to overflow if we're distracted.
    const size_t max_in_flight = 16 - 2;
    while (tx_remaining || rx_remaining) {
        uint32_t flags = ssi_hw->sr;
        bool can_put = !!(flags & SSI_SR_TFNF_BITS);
        bool can_get = !!(flags & SSI_SR_RFNE_BITS);
        if (can_put && tx_remaining && rx_remaining - tx_remaining < max_in_flight) {
            ssi_hw->dr0 = *txbuf++;
            --tx_remaining;
        }
        if (can_get && rx_remaining) {
            *rxbuf++ = (uint8_t)ssi_hw->dr0;
            --rx_remaining;
        }
    }
    flash_cs_force(1);
}
