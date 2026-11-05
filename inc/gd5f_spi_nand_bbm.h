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

/* Read all 20 A5h entries; each status is AVAILABLE, VALID, INVALID, or reserved. */
ErrStatus spi_nand_bbm_read_links(spi_nand_bbm_link_t *links, uint8_t *links_full);
/* Exclusive access required; target must be reserved/disposable. Requires Normal Read and ECC enabled.
   Erases target, copies/verifies the current LBA main area, then commits A1h; OOB is not copied.
   Pre-commit failure leaves source/mapping unchanged, but target may be partially written.
   ERROR after A1h may mean an unknown outcome; inspect A5h before retrying. Not power-loss atomic. */
ErrStatus spi_nand_bbm_replace(uint16_t lba, uint16_t pba);
/* Print the link table and, when enabled, execute the configured demo replacement. */
void spi_nand_bbm_demo(void);

#endif /* GD5F_SPI_NAND_BBM_H */