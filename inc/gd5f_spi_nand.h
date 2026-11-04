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

/* ---------------- feature registers: bits common to all GD5F parts ---------------- */
#define SPI_NAND_REG_PROTECTION     0xA0U           /* block protection (R/W) */
#define SPI_NAND_REG_FEATURE        0xB0U           /* feature / configuration (R/W) */
#define SPI_NAND_REG_STATUS         0xC0U           /* status (read only, WEL set by 06h) */
#define SPI_NAND_REG_DRIVE          0xD0U           /* output driver strength (R/W) */
#define SPI_NAND_REG_STATUS2        0xF0U           /* extended status (read only) */

/* A0h */
#define SPI_NAND_PROT_BRWD          BIT(7)          /* block register write disable: A0h locked while WP# is low */
#define SPI_NAND_PROT_BP_MASK       (BIT(5) | BIT(4) | BIT(3))  /* BP2..BP0: size of the protected area */
#define SPI_NAND_PROT_INV           BIT(2)          /* protect from the bottom instead of the top */
#define SPI_NAND_PROT_CMP           BIT(1)          /* complement: protect the other part of the array */
#define SPI_NAND_PROT_RW_MASK       0xBEU           /* writable bits; reserved bits must be written as 0 */
/* B0h (bit3 differs by family: BPL on standard parts, NR on Continuous Read parts) */
#define SPI_NAND_FEAT_OTP_PRT       BIT(7)          /* lock the OTP area permanently (non-volatile) */
#define SPI_NAND_FEAT_OTP_EN        BIT(6)          /* access the OTP area instead of the main array */
#define SPI_NAND_FEAT_ECC_EN        BIT(4)          /* on-chip ECC enable */
#define SPI_NAND_FEAT_QE            BIT(0)          /* quad enable: allows x4 commands, IO2/IO3 lose WP#/HOLD# */
#define SPI_NAND_FEAT_RW_MASK       0xD9U           /* writable bits; reserved bits must be written as 0 */
/* C0h */
#define SPI_NAND_STAT_ECCS_MASK     (BIT(5) | BIT(4))  /* ECC result of the last page read */
#define SPI_NAND_STAT_ECCS_UNCORR   BIT(5)          /* ECCS[1:0] = 10b: uncorrectable error */
#define SPI_NAND_STAT_P_FAIL        BIT(3)          /* last program failed */
#define SPI_NAND_STAT_E_FAIL        BIT(2)          /* last erase failed */
#define SPI_NAND_STAT_WEL           BIT(1)          /* write enable latch */
#define SPI_NAND_STAT_OIP           BIT(0)          /* operation in progress (busy) */
/* D0h */
#define SPI_NAND_DRV_DS_SHIFT       5U
#define SPI_NAND_DRV_DS_MASK        (BIT(6) | BIT(5))  /* DS_S1/DS_S0: output driver strength */
/* F0h */
#define SPI_NAND_STAT2_ECCSE_MASK   (BIT(5) | BIT(4))  /* extended ECC status, refines ECCS */
#define SPI_NAND_STAT2_BPS          BIT(3)          /* BPS, see datasheet register table */

/* ---------------- standard parts (e.g. GD5F4GQ6), see datasheet Features Settings table ---------------- */
#define SPI_NAND_STD_FEAT_BPL       BIT(3)          /* B0h: block protection lock */
#define SPI_NAND_STD_DRV_RW_MASK    0x60U           /* D0h writable bits: DS_S1/DS_S0 only */
/* Continuous Read parts (e.g. GD5F1GM9) use a different B0h/C0h/D0h/F0h layout: see gd5f_spi_nand_cr.h */

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
    uint8_t     cont_read;          /* 1: Continuous Read register layout (B0h bit3 = NR), see gd5f_spi_nand_cr.h */
    uint8_t     bad_block_marker_page;  /* first in-block page checked for factory bad-block marker */
    uint8_t     bad_block_marker_count; /* number of consecutive marker pages */
    uint16_t    bad_block_marker_offset;/* byte offset in spare area; marker value other than FFh means bad */
    uint8_t     bbm_supported;       /* 1: supports A1h/A5h BBM commands */
} spi_nand_info_t;

typedef enum {
    SPI_NAND_IO_SINGLE = 1U,
    SPI_NAND_IO_DUAL   = 2U,
    SPI_NAND_IO_QUAD   = 4U
} spi_nand_io_mode_t;

/* init SPI + GPIO, reset device, unlock blocks, enable QE; unknown IDs get a fallback geometry */
const spi_nand_info_t *spi_nand_init(void);
const spi_nand_info_t *spi_nand_get_info(void);
/* raw ID: manufacturer ID in high byte, device ID in low byte */
uint16_t spi_nand_read_id(void);

uint8_t spi_nand_get_feature(uint8_t reg);
void spi_nand_set_feature(uint8_t reg, uint8_t value);

/* page = absolute page index (block * pages_per_block + page_in_block) */
ErrStatus spi_nand_block_erase(uint32_t block);
/* erase whole device-addressed blocks; chip BBM may redirect them; all selected data is destroyed */
ErrStatus spi_nand_erase_blocks(uint32_t first_block, uint32_t block_count);
/* SUCCESS: block is good; ERROR: bad marker found or marker read/configuration failed */
ErrStatus spi_nand_block_check_bad(uint32_t block);
ErrStatus spi_nand_page_program(uint32_t page, uint16_t column, const uint8_t *buf, uint16_t len);
ErrStatus spi_nand_quad_page_program(uint32_t page, uint16_t column, const uint8_t *buf, uint16_t len);
/* return ERROR when ECC reports an uncorrectable error */
ErrStatus spi_nand_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len);
/* dual output read from cache (3Bh), sampled through the SPI5 quad receiver */
ErrStatus spi_nand_dual_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len);
ErrStatus spi_nand_quad_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len);

/* address is a device-visible main-area byte offset (OOB excluded); ranges split at page boundaries */
/* caller must erase first and handle bad blocks; partial-page program limits are device-specific */
ErrStatus spi_nand_program_data(uint32_t address, const uint8_t *buf, uint32_t len, spi_nand_io_mode_t mode);
/* no software bad-block skipping or remapping; existing chip-internal BBM mappings still apply */
ErrStatus spi_nand_read_data(uint32_t address, uint8_t *buf, uint32_t len, spi_nand_io_mode_t mode);

#endif /* GD5F_SPI_NAND_H */
