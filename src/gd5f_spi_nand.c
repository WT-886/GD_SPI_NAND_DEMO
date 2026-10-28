#include "gd5f_spi_nand.h"
#include "gd5f_spi_nand_internal.h"
#include <stddef.h>

#define CMD_RESET               0xFFU
#define CMD_READ_ID             0x9FU
#define CMD_GET_FEATURE         0x0FU
#define CMD_SET_FEATURE         0x1FU
#define CMD_WRITE_ENABLE        0x06U
#define CMD_PAGE_READ           0x13U
#define CMD_READ_CACHE          0x03U
#define CMD_READ_CACHE_DUAL     0x3BU
#define CMD_READ_CACHE_QUAD     0x6BU
#define CMD_PROGRAM_LOAD        0x02U
#define CMD_PROGRAM_LOAD_QUAD   0x32U
#define CMD_PROGRAM_EXECUTE     0x10U
#define CMD_BLOCK_ERASE         0xD8U

#define DUMMY_BYTE              0xFFU

/* SPI_QCTL.IO23_DRV: drive IO2(WP#)/IO3(HOLD#) high while not in quad mode */
#define SPI_QCTL_IO23_DRV       BIT(2)

#define CS_LOW()                gpio_bit_reset(SPI_NAND_GPIO_PORT, SPI_NAND_CS_PIN)
#define CS_HIGH()               gpio_bit_set(SPI_NAND_GPIO_PORT, SPI_NAND_CS_PIN)

/* add new parts here */
static const spi_nand_info_t nand_table[] = {
    /* mid    did    name        page   spare  ppb  blocks row col dummy cont_read */
    { 0xC8U, 0x91U, "GD5F1GM9", 2048U, 128U, 64U, 1024U, 3U, 2U, 8U,    1U },
    { 0xC8U, 0x55U, "GD5F1GQ4", 2048U, 128U, 64U, 1024U, 3U, 2U, 8U,    0U },
};

/* used when the ID is not in nand_table[]: common 1Gb 2K-page geometry */
static const spi_nand_info_t nand_default =
    { 0x00U, 0x00U, "unknown",  2048U, 128U, 64U, 1024U, 3U, 2U, 8U,    0U };

static const spi_nand_info_t *nand_info = NULL;

static uint8_t xfer(uint8_t byte)
{
    while(RESET == spi_i2s_flag_get(SPI_NAND_SPI, SPI_FLAG_TBE));
    spi_i2s_data_transmit(SPI_NAND_SPI, byte);
    while(RESET == spi_i2s_flag_get(SPI_NAND_SPI, SPI_FLAG_RBNE));
    return (uint8_t)spi_i2s_data_receive(SPI_NAND_SPI);
}

static void send_address(uint32_t addr, uint8_t bytes)
{
    while(bytes--) {
        xfer((uint8_t)(addr >> (8U * bytes)));
    }
}

static void send_row_address(uint32_t page)
{
    send_address(page, nand_info->row_addr_bytes);
}

static void wait_idle(void)
{
    while(SET == spi_i2s_flag_get(SPI_NAND_SPI, SPI_FLAG_TRANS));
}

static void quad_enter(uint8_t read)
{
    /* QCTL must only change while the SPI is idle */
    wait_idle();
    spi_quad_enable(SPI_NAND_SPI);
    if(read) {
        spi_quad_read_enable(SPI_NAND_SPI);
    } else {
        spi_quad_write_enable(SPI_NAND_SPI);
    }
}

static void quad_exit(void)
{
    wait_idle();
    spi_quad_disable(SPI_NAND_SPI);
}

static void write_enable(void)
{
    CS_LOW();
    xfer(CMD_WRITE_ENABLE);
    CS_HIGH();
}

static uint8_t read_feature(uint8_t reg)
{
    uint8_t value;

    CS_LOW();
    xfer(CMD_GET_FEATURE);
    xfer(reg);
    value = xfer(DUMMY_BYTE);
    CS_HIGH();

    return value;
}

/* returns the last status (C0h); OIP is still set if the device never became ready */
static uint8_t wait_ready(void)
{
    uint8_t status;
    uint32_t t = SPI_NAND_BUSY_TIMEOUT;

    do {
        status = read_feature(SPI_NAND_REG_STATUS);
    } while((status & SPI_NAND_STAT_OIP) && --t);

    return status;
}

static void bus_init(void)
{
    const uint32_t af_pins = SPI_NAND_SCK_PIN | SPI_NAND_IO0_PIN | SPI_NAND_IO1_PIN |
                             SPI_NAND_IO2_PIN | SPI_NAND_IO3_PIN;
    spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(SPI_NAND_GPIO_CLK);
    rcu_periph_clock_enable(SPI_NAND_SPI_CLK);

    gpio_af_set(SPI_NAND_GPIO_PORT, SPI_NAND_GPIO_AF, af_pins);
    gpio_mode_set(SPI_NAND_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  SPI_NAND_SCK_PIN | SPI_NAND_IO0_PIN | SPI_NAND_IO1_PIN);
    gpio_mode_set(SPI_NAND_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP,
                  SPI_NAND_IO2_PIN | SPI_NAND_IO3_PIN);
    gpio_output_options_set(SPI_NAND_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, af_pins);

    gpio_mode_set(SPI_NAND_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_NAND_CS_PIN);
    gpio_output_options_set(SPI_NAND_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI_NAND_CS_PIN);
    CS_HIGH();

    spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode          = SPI_MASTER;
    spi_init_struct.frame_size           = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    spi_init_struct.nss                  = SPI_NSS_SOFT;
    spi_init_struct.prescale             = SPI_NAND_PRESCALE;
    spi_init_struct.endian               = SPI_ENDIAN_MSB;
    spi_init(SPI_NAND_SPI, &spi_init_struct);
    if(SPI_NAND_IO23_DRIVE) {
        SPI_QCTL(SPI_NAND_SPI) |= SPI_QCTL_IO23_DRV;
    }
    spi_enable(SPI_NAND_SPI);
}

const spi_nand_info_t *spi_nand_init(void)
{
    uint16_t id;
    uint8_t reg;

    bus_init();

    CS_LOW();
    xfer(CMD_RESET);
    CS_HIGH();
    wait_ready();

    nand_info = &nand_default;
    id = spi_nand_read_id();
    for(uint32_t i = 0U; i < sizeof(nand_table) / sizeof(nand_table[0]); i++) {
        if((nand_table[i].mid == (uint8_t)(id >> 8)) && (nand_table[i].did == (uint8_t)id)) {
            nand_info = &nand_table[i];
            break;
        }
    }

    /* all blocks are locked after power-up: clear BP2..BP0 / INV / CMP */
    reg = spi_nand_get_feature(SPI_NAND_REG_PROTECTION) & SPI_NAND_PROT_RW_MASK;
    spi_nand_set_feature(SPI_NAND_REG_PROTECTION, reg & (uint8_t)~(SPI_NAND_PROT_BP_MASK | SPI_NAND_PROT_INV | SPI_NAND_PROT_CMP));

    reg = spi_nand_get_feature(SPI_NAND_REG_FEATURE) & SPI_NAND_FEAT_RW_MASK;
    spi_nand_set_feature(SPI_NAND_REG_FEATURE, reg | SPI_NAND_FEAT_QE | SPI_NAND_FEAT_ECC_EN);

    return nand_info;
}

const spi_nand_info_t *spi_nand_get_info(void)
{
    return nand_info;
}

uint16_t spi_nand_read_id(void)
{
    uint16_t id;

    CS_LOW();
    xfer(CMD_READ_ID);
    xfer(0x00U);
    id = (uint16_t)((uint16_t)xfer(DUMMY_BYTE) << 8);
    id |= xfer(DUMMY_BYTE);
    CS_HIGH();

    return id;
}

uint8_t spi_nand_get_feature(uint8_t reg)
{
    return read_feature(reg);
}

void spi_nand_set_feature(uint8_t reg, uint8_t value)
{
    CS_LOW();
    xfer(CMD_SET_FEATURE);
    xfer(reg);
    xfer(value);
    CS_HIGH();
}

ErrStatus spi_nand_block_erase(uint32_t block)
{
    write_enable();

    CS_LOW();
    xfer(CMD_BLOCK_ERASE);
    send_row_address(block * nand_info->pages_per_block);
    CS_HIGH();

    return (wait_ready() & (SPI_NAND_STAT_OIP | SPI_NAND_STAT_E_FAIL)) ? ERROR : SUCCESS;
}

static ErrStatus page_program(uint32_t page, uint16_t column, const uint8_t *buf, uint16_t len, uint8_t quad)
{
    write_enable();

    CS_LOW();
    xfer(quad ? CMD_PROGRAM_LOAD_QUAD : CMD_PROGRAM_LOAD);
    send_address(column, nand_info->col_addr_bytes);
    if(quad) {
        quad_enter(0U);
    }
    while(len--) {
        xfer(*buf++);
    }
    CS_HIGH();
    if(quad) {
        quad_exit();
    }

    CS_LOW();
    xfer(CMD_PROGRAM_EXECUTE);
    send_row_address(page);
    CS_HIGH();

    return (wait_ready() & (SPI_NAND_STAT_OIP | SPI_NAND_STAT_P_FAIL)) ? ERROR : SUCCESS;
}

static uint8_t load_page(uint32_t page)
{
    CS_LOW();
    xfer(CMD_PAGE_READ);
    send_row_address(page);
    CS_HIGH();
    return wait_ready();
}

/* lines: 1 = single (03h), 2 = dual (3Bh), 4 = quad (6Bh) */
static void read_cache(uint16_t column, uint8_t *buf, uint32_t len, uint8_t lines)
{
    static const uint8_t cmd[] = { 0U, CMD_READ_CACHE, CMD_READ_CACHE_DUAL, 0U, CMD_READ_CACHE_QUAD };

    CS_LOW();
    xfer(cmd[lines]);
    send_address(column, nand_info->col_addr_bytes);
    /* whole dummy bytes in single mode (8 clocks each) */
    for(uint8_t i = 0U; i < nand_info->read_dummy_clocks / 8U; i++) {
        xfer(DUMMY_BYTE);
    }
    if(lines > 1U) {
        /* SPI5 has no dual mode: dual data is sampled with the quad receiver, IO2/IO3 ignored */
        quad_enter(1U);
        /* remaining dummy clocks in quad mode (2 clocks per byte) */
        for(uint8_t i = 0U; i < (nand_info->read_dummy_clocks % 8U) / 2U; i++) {
            xfer(DUMMY_BYTE);
        }
    }
    if(2U == lines) {
        /* each received byte = 2 clocks x (IO3..IO0); data bits come on IO1:IO0 */
        while(len--) {
            const uint8_t hi = xfer(DUMMY_BYTE);
            const uint8_t lo = xfer(DUMMY_BYTE);
            *buf++ = (uint8_t)((((hi >> 4) & 0x3U) << 6) | ((hi & 0x3U) << 4) |
                               (((lo >> 4) & 0x3U) << 2) | (lo & 0x3U));
        }
    } else {
        while(len--) {
            *buf++ = xfer(DUMMY_BYTE);
        }
    }
    CS_HIGH();
    if(lines > 1U) {
        quad_exit();
    }
}

ErrStatus spi_nand_read_lines(uint32_t page, uint16_t column, uint8_t *buf, uint32_t len, uint8_t lines)
{
    const uint8_t status = load_page(page);

    read_cache(column, buf, len, lines);

    return ((status & SPI_NAND_STAT_OIP) ||
            ((status & SPI_NAND_STAT_ECCS_MASK) == SPI_NAND_STAT_ECCS_UNCORR)) ? ERROR : SUCCESS;
}

ErrStatus spi_nand_page_program(uint32_t page, uint16_t column, const uint8_t *buf, uint16_t len)
{
    return page_program(page, column, buf, len, 0U);
}

ErrStatus spi_nand_quad_page_program(uint32_t page, uint16_t column, const uint8_t *buf, uint16_t len)
{
    return page_program(page, column, buf, len, 1U);
}

ErrStatus spi_nand_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len)
{
    return spi_nand_read_lines(page, column, buf, len, 1U);
}

ErrStatus spi_nand_dual_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len)
{
    return spi_nand_read_lines(page, column, buf, len, 2U);
}

ErrStatus spi_nand_quad_page_read(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len)
{
    return spi_nand_read_lines(page, column, buf, len, 4U);
}
