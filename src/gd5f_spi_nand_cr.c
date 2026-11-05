#include "gd5f_spi_nand_cr.h"
#include "gd5f_spi_nand_internal.h"
#include <stddef.h>

ErrStatus spi_nand_set_continuous_read(uint8_t enable)
{
    const spi_nand_info_t *nand = spi_nand_get_info();
    uint8_t reg;

    /* only parts with the Continuous Read register layout have NR in B0h bit3 (BPL on standard parts) */
    if((NULL == nand) || !nand->cont_read) {
        return ERROR;
    }

    reg = spi_nand_get_feature(SPI_NAND_REG_FEATURE) & SPI_NAND_FEAT_RW_MASK;
    reg = enable ? (uint8_t)(reg & ~SPI_NAND_CR_FEAT_NR) : (uint8_t)(reg | SPI_NAND_CR_FEAT_NR);
    spi_nand_set_feature(SPI_NAND_REG_FEATURE, reg);

    return ((spi_nand_get_feature(SPI_NAND_REG_FEATURE) & SPI_NAND_CR_FEAT_NR) == (reg & SPI_NAND_CR_FEAT_NR)) ? SUCCESS : ERROR;
}

static uint8_t continuous_read_dummy_bytes(void)
{
    const uint8_t reg = spi_nand_get_feature(SPI_NAND_CR_REG_BLOCK_LOCK);

    /* CRDC selects the device's 32- or 24-clock pre-data interval. */
    return (reg & SPI_NAND_CR_LOCK_CRDC) ? SPI_NAND_CR_CRDC1_DUMMY_BYTES : SPI_NAND_CR_CRDC0_DUMMY_BYTES;
}

/* Continuous Read starts at byte 0 and continues across pages while CS stays low. */
ErrStatus spi_nand_continuous_read(uint32_t start_page, uint8_t *buf, uint32_t len)
{
    return spi_nand_read_lines(start_page, 0U, buf, len, 1U);
}

ErrStatus spi_nand_quad_continuous_read(uint32_t start_page, uint8_t *buf, uint32_t len)
{
    return spi_nand_read_continuous_lines(start_page, buf, len, 4U, continuous_read_dummy_bytes());
}
