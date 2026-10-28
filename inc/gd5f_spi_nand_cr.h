#ifndef GD5F_SPI_NAND_CR_H
#define GD5F_SPI_NAND_CR_H

/* Continuous Read for GD5F SPI NAND (devices with cont_read = 1 in nand_table[], e.g. GD5F1GM9)
   - switch with spi_nand_set_continuous_read(); program/erase only in Normal Read mode (enable = 0)
   - one call issues a single 13h and streams len bytes from start_page across page boundaries,
     main area only, no bad block skip */

#include "gd5f_spi_nand.h"

ErrStatus spi_nand_set_continuous_read(uint8_t enable);
ErrStatus spi_nand_continuous_read(uint32_t start_page, uint8_t *buf, uint32_t len);
ErrStatus spi_nand_quad_continuous_read(uint32_t start_page, uint8_t *buf, uint32_t len);

#endif /* GD5F_SPI_NAND_CR_H */
