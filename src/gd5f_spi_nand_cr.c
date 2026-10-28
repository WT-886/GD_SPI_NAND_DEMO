#include "gd5f_spi_nand_cr.h"
#include "gd5f_spi_nand_internal.h"
#include <stddef.h>

ErrStatus spi_nand_set_continuous_read(uint8_t enable)
{
    const spi_nand_info_t *nand = spi_nand_get_info();
    uint8_t reg;

    if((NULL == nand) || !nand->cont_read) {
        return ERROR;
    }

    /* B0h.NR: 0 = Continuous Read, 1 = Normal Read */
    reg = spi_nand_get_feature(SPI_NAND_REG_FEATURE) & SPI_NAND_FEAT_RW_MASK;
    reg = enable ? (uint8_t)(reg & ~SPI_NAND_FEAT_NR) : (uint8_t)(reg | SPI_NAND_FEAT_NR);
    spi_nand_set_feature(SPI_NAND_REG_FEATURE, reg);

    return ((spi_nand_get_feature(SPI_NAND_REG_FEATURE) & SPI_NAND_FEAT_NR) == (reg & SPI_NAND_FEAT_NR)) ? SUCCESS : ERROR;
}

/* column bytes are don't-care in Continuous Read: output starts at byte 0 and runs on while CS stays low */
ErrStatus spi_nand_continuous_read(uint32_t start_page, uint8_t *buf, uint32_t len)
{
    return spi_nand_read_lines(start_page, 0U, buf, len, 1U);
}

ErrStatus spi_nand_quad_continuous_read(uint32_t start_page, uint8_t *buf, uint32_t len)
{
    return spi_nand_read_lines(start_page, 0U, buf, len, 4U);
}
