#ifndef GD5F_SPI_NAND_H
#define GD5F_SPI_NAND_H

#include "gd32f4xx.h"

/* ---------------- board wiring (default: GD32450Z-EVAL, SPI5 on GPIOG) ---------------- */
#ifndef SPI_NAND_SPI
#define SPI_NAND_SPI                SPI5            /* only SPI5 supports quad mode on GD32F4xx */
#define SPI_NAND_SPI_CLK            RCU_SPI5
#define SPI_NAND_PRESCALE           SPI_PSC_4       /* PCLK2/4: 25 MHz with the default 100 MHz APB2 */
#define SPI_NAND_IO23_DRIVE         1U              /* 1: drive IO2/IO3 high in single-line mode (SPI_QCTL.IO23_DRV) */

#define SPI_NAND_GPIO_PORT          GPIOG
#define SPI_NAND_GPIO_CLK           RCU_GPIOG
#define SPI_NAND_GPIO_AF            GPIO_AF_5
#define SPI_NAND_SCK_PIN            GPIO_PIN_13
#define SPI_NAND_IO0_PIN            GPIO_PIN_14     /* MOSI */
#define SPI_NAND_IO1_PIN            GPIO_PIN_12     /* MISO */
#define SPI_NAND_IO2_PIN            GPIO_PIN_10     /* WP#  */
#define SPI_NAND_IO3_PIN            GPIO_PIN_11     /* HOLD# */
#define SPI_NAND_CS_PIN             GPIO_PIN_9
#endif

/* largest page + spare among supported devices, used to size user buffers */
#define SPI_NAND_MAX_PAGE_SIZE      4096U

/* status reads waiting for OIP=0 before an operation is reported as failed */
#define SPI_NAND_BUSY_TIMEOUT       20000U

/* ---------------- feature registers (GD5F1GM9 datasheet Table 6-1) ---------------- */
#define SPI_NAND_REG_PROTECTION     0xA0U
#define SPI_NAND_REG_FEATURE        0xB0U
#define SPI_NAND_REG_STATUS         0xC0U
#define SPI_NAND_REG_DRIVE          0xD0U
#define SPI_NAND_REG_STATUS2        0xF0U

/* A0h */
#define SPI_NAND_PROT_BRWD          BIT(7)
#define SPI_NAND_PROT_BP_MASK       (BIT(5) | BIT(4) | BIT(3))
#define SPI_NAND_PROT_INV           BIT(2)
#define SPI_NAND_PROT_CMP           BIT(1)
#define SPI_NAND_PROT_RW_MASK       0xBEU           /* reserved bits must be written as 0 */
/* B0h */
#define SPI_NAND_FEAT_OTP_PRT       BIT(7)
#define SPI_NAND_FEAT_OTP_EN        BIT(6)
#define SPI_NAND_FEAT_ECC_EN        BIT(4)
#define SPI_NAND_FEAT_NR            BIT(3)
#define SPI_NAND_FEAT_QE            BIT(0)
#define SPI_NAND_FEAT_RW_MASK       0xD9U
/* C0h */
#define SPI_NAND_STAT_BBLS          BIT(6)
#define SPI_NAND_STAT_ECCS_MASK     (BIT(5) | BIT(4))
#define SPI_NAND_STAT_ECCS_UNCORR   BIT(5)          /* ECCS[1:0] = 10b */
#define SPI_NAND_STAT_P_FAIL        BIT(3)
#define SPI_NAND_STAT_E_FAIL        BIT(2)
#define SPI_NAND_STAT_WEL           BIT(1)
#define SPI_NAND_STAT_OIP           BIT(0)
/* D0h */
#define SPI_NAND_DRV_DS_SHIFT       5U
#define SPI_NAND_DRV_DS_MASK        (BIT(6) | BIT(5))
#define SPI_NAND_DRV_DLP_EN         BIT(3)
#define SPI_NAND_DRV_DC             BIT(2)
#define SPI_NAND_DRV_RW_MASK        0x6CU

/* device geometry, looked up by ID at init */
typedef struct {
    uint8_t     mid;
    uint8_t     did;
    const char *name;
    uint16_t    page_size;          /* main area, bytes */
    uint16_t    spare_size;         /* spare area, bytes */
    uint16_t    pages_per_block;
    uint16_t    block_count;
    uint8_t     row_addr_bytes;     /* page address bytes for 13h/10h/D8h */
    uint8_t     col_addr_bytes;     /* column address bytes for 03h/3Bh/6Bh/02h/32h */
    uint8_t     read_dummy_clocks;  /* dummy clocks after column address in 03h/3Bh/6Bh */
    uint8_t     cont_read;          /* 1: supports Continuous Read (B0h.NR = 0) */
} spi_nand_info_t;

/* init SPI + GPIO, reset device, unlock blocks, enable QE; unknown IDs get a default 1Gb geometry */
const spi_nand_info_t *spi_nand_init(void);
const spi_nand_info_t *spi_nand_get_info(void);
/* raw ID: manufacturer ID in high byte, device ID in low byte */
uint16_t spi_nand_read_id(void);

uint8_t spi_nand_get_feature(uint8_t reg);
void spi_nand_set_feature(uint8_t reg, uint8_t value);

/* page = absolute page index (block * pages_per_block + page_in_block) */
ErrStatus spi_nand_block_erase(uint32_t block);
ErrStatus spi_nand_page_program(uint32_t page, uint16_t column, const uint8_t *buf, uint16_t len);
ErrStatus spi_nand_quad_page_program(uint32_t page, uint16_t column, const uint8_t *buf, uint16_t len);
/* return ERROR when ECC reports an uncorrectable error */
ErrStatus spi_nand_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len);
/* dual output read from cache (3Bh), sampled through the SPI5 quad receiver */
ErrStatus spi_nand_dual_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len);
ErrStatus spi_nand_quad_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len);

#endif /* GD5F_SPI_NAND_H */
