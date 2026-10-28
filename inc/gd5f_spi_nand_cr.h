#ifndef GD5F_SPI_NAND_CR_H
#define GD5F_SPI_NAND_CR_H

/* Continuous Read for GD5F SPI NAND (devices with cont_read = 1 in nand_table[], e.g. GD5F1GM9)
   - call spi_nand_set_continuous_read(0) after spi_nand_init(): NR survives an MCU reset, only power cycle clears it
   - program/erase only in Normal Read mode (enable = 0)
   - one call issues a single 13h and streams len bytes from start_page across page boundaries,
     main area only, no bad block skip */

#include "gd5f_spi_nand.h"

/* ---------------- Continuous Read parts (e.g. GD5F1GM9), see datasheet Features Settings table ---------------- */
#define SPI_NAND_CR_REG_BLOCK_LOCK  0x60U           /* extra configuration (R/W), not present on standard parts */
#define SPI_NAND_CR_REG_BFT         0x10U           /* ECC bit-flip threshold (R/W), not present on standard parts */

/* B0h */
#define SPI_NAND_CR_FEAT_NR         BIT(3)          /* 0 = Continuous Read, 1 = Normal Read (default) */
/* C0h */
#define SPI_NAND_CR_STAT_BBLS       BIT(6)          /* BBLS, see datasheet register table */
/* D0h */
#define SPI_NAND_CR_DRV_DLP_EN      BIT(3)          /* data learning pattern enable */
#define SPI_NAND_CR_DRV_DC          BIT(2)          /* dummy cycle configuration */
#define SPI_NAND_CR_DRV_RW_MASK     0x6CU           /* D0h writable bits: DS_S1/DS_S0/DLP-EN/DC */
/* F0h */
#define SPI_NAND_CR_STAT2_CBSY      BIT(0)          /* cache busy */
/* 60h (bit0 must always be 0) */
#define SPI_NAND_CR_LOCK_BPL        BIT(3)          /* block protection lock */
#define SPI_NAND_CR_LOCK_CRDC       BIT(2)          /* CRDC, see datasheet register table */
#define SPI_NAND_CR_LOCK_AL         BIT(1)          /* AL, see datasheet register table */
#define SPI_NAND_CR_LOCK_RW_MASK    0x0EU           /* writable bits of 60h */
/* 10h */
#define SPI_NAND_CR_BFT_MASK        (BIT(7) | BIT(6) | BIT(5) | BIT(4))  /* BFT3..BFT0: bit-flip threshold */

ErrStatus spi_nand_set_continuous_read(uint8_t enable);
ErrStatus spi_nand_continuous_read(uint32_t start_page, uint8_t *buf, uint32_t len);
ErrStatus spi_nand_quad_continuous_read(uint32_t start_page, uint8_t *buf, uint32_t len);

#endif /* GD5F_SPI_NAND_CR_H */
