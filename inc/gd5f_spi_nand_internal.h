#ifndef GD5F_SPI_NAND_INTERNAL_H
#define GD5F_SPI_NAND_INTERNAL_H

/* hooks shared by gd5f_spi_nand.c and its feature modules (e.g. gd5f_spi_nand_cr.c); not for application use */

#include "gd5f_spi_nand.h"

/* Internal page-to-cache plus cache-read path; lines is 1, 2, or 4. */
ErrStatus spi_nand_read_lines(uint32_t page, uint16_t column, uint8_t *buf, uint32_t len, uint8_t lines);
/* Continuous cache stream from column zero; caller supplies the device-specific dummy-byte count. */
ErrStatus spi_nand_read_continuous_lines(uint32_t page, uint8_t *buf, uint32_t len,
                                         uint8_t lines, uint8_t dummy_bytes);
/* Send one command with optional TX/RX phases while keeping CS asserted. */
ErrStatus spi_nand_command_transfer(uint8_t command, const uint8_t *tx, uint32_t tx_len,
                                   uint8_t *rx, uint32_t rx_len);
/* Poll C0h until OIP clears or the driver's bounded poll count expires; return the final status. */
uint8_t spi_nand_wait_ready_internal(void);

#endif /* GD5F_SPI_NAND_INTERNAL_H */
