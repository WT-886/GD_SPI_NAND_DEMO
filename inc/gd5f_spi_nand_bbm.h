#ifndef GD5F_SPI_NAND_BBM_H
#define GD5F_SPI_NAND_BBM_H

#include "gd5f_spi_nand.h"

#define SPI_NAND_BBM_LINK_COUNT       20U
#define SPI_NAND_BBM_LINK_AVAILABLE   0U
#define SPI_NAND_BBM_LINK_VALID       2U
#define SPI_NAND_BBM_LINK_INVALID     3U

typedef struct {
    uint16_t lba;
    uint16_t pba;
    uint8_t status;
} spi_nand_bbm_link_t;

ErrStatus spi_nand_bbm_read_links(spi_nand_bbm_link_t *links, uint8_t *links_full);
ErrStatus spi_nand_bbm_replace(uint16_t lba, uint16_t pba);
void spi_nand_bbm_demo(void);

#endif /* GD5F_SPI_NAND_BBM_H */