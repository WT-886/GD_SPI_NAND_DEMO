#ifndef GD5F_SPI_NAND_BAD_BLOCK_TEST_H
#define GD5F_SPI_NAND_BAD_BLOCK_TEST_H

#include "gd5f_spi_nand.h"

/* Scan existing block markers; count=0 scans from first_block to the end.
   Existing chip BBM applies. ERROR means invalid range or bad/unreadable markers. */
ErrStatus spi_nand_bad_block_scan(const spi_nand_info_t *nand, uint32_t first_block, uint32_t count);

#endif /* GD5F_SPI_NAND_BAD_BLOCK_TEST_H */
