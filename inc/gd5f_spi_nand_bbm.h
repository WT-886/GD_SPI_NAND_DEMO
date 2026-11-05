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
/* Exclusive access required; target must be reserved/disposable. Normal mode and ECC must be enabled.
    Erase target, copy and verify the current LBA main area, then commit A1h. OOB is not copied.
    Pre-commit failure leaves source/mapping unchanged; target may be partially written.
    ERROR after A1h can mean an unknown mapping outcome; inspect A5h before any retry.
    Not a power-loss-atomic transaction: A1h interruption requires device guarantees/recovery. */
ErrStatus spi_nand_bbm_replace(uint16_t lba, uint16_t pba);
void spi_nand_bbm_demo(void);

#endif /* GD5F_SPI_NAND_BBM_H */