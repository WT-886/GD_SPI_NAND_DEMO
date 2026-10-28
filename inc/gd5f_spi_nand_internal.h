#ifndef GD5F_SPI_NAND_INTERNAL_H
#define GD5F_SPI_NAND_INTERNAL_H

/* hooks shared by gd5f_spi_nand.c and its feature modules (e.g. gd5f_spi_nand_cr.c); not for application use */

#include "gd5f_spi_nand.h"

/* page read to cache (13h) + read from cache with 1/2/4 data lines; len may exceed one page in Continuous Read */
ErrStatus spi_nand_read_lines(uint32_t page, uint16_t column, uint8_t *buf, uint32_t len, uint8_t lines);

#endif /* GD5F_SPI_NAND_INTERNAL_H */
