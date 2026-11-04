#include "gd5f_spi_nand_bbm.h"
#include "gd5f_spi_nand_internal.h"
#include "gd5f_spi_nand_cr.h"
#include <stdio.h>

#define CMD_WRITE_ENABLE             0x06U
#define CMD_WRITE_BBM                0xA1U
#define CMD_READ_BBM_LINK_TABLE      0xA5U
#define BBM_LBA_VALUE_MASK           0x3FFFU

/* Example from the manual; replacement execution is deliberately disabled by default. */
#ifndef SPI_NAND_BBM_DEMO_LBA
#define SPI_NAND_BBM_DEMO_LBA        7U
#endif
#ifndef SPI_NAND_BBM_DEMO_PBA
#define SPI_NAND_BBM_DEMO_PBA        1010U
#endif
#ifndef SPI_NAND_BBM_DEMO_EXECUTE
#define SPI_NAND_BBM_DEMO_EXECUTE    1U     // 1=ON  0=OFF
#endif

static ErrStatus validate_candidate(uint16_t lba, uint16_t pba,
                                    const spi_nand_bbm_link_t *links, uint8_t links_full)
{
    const spi_nand_info_t *nand = spi_nand_get_info();
    uint8_t available = 0U;

    if((NULL == nand) || !nand->bbm_supported || (lba >= nand->block_count) ||
       (pba >= nand->block_count) || (lba == pba) || links_full ||
       (ERROR == spi_nand_block_check_bad(pba))) {
        return ERROR;
    }

    for(uint32_t i = 0U; i < SPI_NAND_BBM_LINK_COUNT; i++) {
        if(SPI_NAND_BBM_LINK_AVAILABLE == links[i].status) {
            available++;
        } else if((SPI_NAND_BBM_LINK_VALID == links[i].status) &&
                  ((pba == links[i].pba) || (pba == links[i].lba))) {
            return ERROR;
        }
    }
    return available ? SUCCESS : ERROR;
}

ErrStatus spi_nand_bbm_read_links(spi_nand_bbm_link_t *links, uint8_t *links_full)
{
    uint8_t dummy = 0xFFU;
    uint8_t raw[SPI_NAND_BBM_LINK_COUNT * 4U];

    if((NULL == links) || (NULL == links_full)) {
        return ERROR;
    }
    if((NULL == spi_nand_get_info()) || !spi_nand_get_info()->bbm_supported) {
        return ERROR;
    }

    if(ERROR == spi_nand_command_transfer(CMD_READ_BBM_LINK_TABLE, &dummy, 1U, raw, sizeof(raw))) {
        return ERROR;
    }

    for(uint32_t i = 0U; i < SPI_NAND_BBM_LINK_COUNT; i++) {
        const uint16_t raw_lba = (uint16_t)(((uint16_t)raw[i * 4U] << 8) | raw[i * 4U + 1U]);

        links[i].status = (uint8_t)(raw_lba >> 14);
        links[i].lba = raw_lba & BBM_LBA_VALUE_MASK;
        links[i].pba = (uint16_t)(((uint16_t)raw[i * 4U + 2U] << 8) | raw[i * 4U + 3U]);
    }
    *links_full = (spi_nand_get_feature(SPI_NAND_REG_STATUS) & SPI_NAND_CR_STAT_BBLS) ? 1U : 0U;
    return SUCCESS;
}

ErrStatus spi_nand_bbm_replace(uint16_t lba, uint16_t pba)
{
    spi_nand_bbm_link_t links[SPI_NAND_BBM_LINK_COUNT];
    uint8_t links_full;
    uint8_t wel;
    uint8_t command_data[4];
    uint8_t status;

    if(ERROR == spi_nand_bbm_read_links(links, &links_full) ||
       ERROR == validate_candidate(lba, pba, links, links_full)) {
        return ERROR;
    }

    if(ERROR == spi_nand_command_transfer(CMD_WRITE_ENABLE, NULL, 0U, NULL, 0U)) {
        return ERROR;
    }
    wel = spi_nand_get_feature(SPI_NAND_REG_STATUS);
    if(0U == (wel & SPI_NAND_STAT_WEL)) {
        return ERROR;
    }

    command_data[0] = (uint8_t)(lba >> 8);
    command_data[1] = (uint8_t)lba;
    command_data[2] = (uint8_t)(pba >> 8);
    command_data[3] = (uint8_t)pba;
    if(ERROR == spi_nand_command_transfer(CMD_WRITE_BBM, command_data, sizeof(command_data), NULL, 0U)) {
        return ERROR;
    }

    status = spi_nand_wait_ready_internal();
    if(status & SPI_NAND_STAT_OIP) {
        return ERROR;
    }
    if(ERROR == spi_nand_bbm_read_links(links, &links_full)) {
        return ERROR;
    }

    for(uint32_t i = 0U; i < SPI_NAND_BBM_LINK_COUNT; i++) {
        if((SPI_NAND_BBM_LINK_VALID == links[i].status) &&
           (lba == links[i].lba) && (pba == links[i].pba)) {
            return SUCCESS;
        }
    }
    return ERROR;
}

static void print_link_table(const spi_nand_bbm_link_t *links, uint8_t links_full)
{
    uint8_t available = 0U;

    printf("\r\n===== GD5F1GM9 BBM link table =====\r\n");
    printf("BBLS: %s\r\n", links_full ? "full" : "available");
    for(uint32_t i = 0U; i < SPI_NAND_BBM_LINK_COUNT; i++) {
        if(SPI_NAND_BBM_LINK_AVAILABLE == links[i].status) {
            available++;
            printf("link %02lu: available\r\n", (unsigned long)i);
        } else if(SPI_NAND_BBM_LINK_VALID == links[i].status) {
            printf("link %02lu: LBA %u -> PBA %u, status %u\r\n",
                   (unsigned long)i, links[i].lba, links[i].pba, links[i].status);
        } else {
            printf("link %02lu: invalid, LBA %u, PBA %u, status %u\r\n",
                   (unsigned long)i, links[i].lba, links[i].pba, links[i].status);
        }
    }
    printf("Available links: %u/%u\r\n", available, SPI_NAND_BBM_LINK_COUNT);
}

void spi_nand_bbm_demo(void)
{
    spi_nand_bbm_link_t links[SPI_NAND_BBM_LINK_COUNT];
    uint8_t links_full;

    if(ERROR == spi_nand_bbm_read_links(links, &links_full)) {
        printf("BBM: unsupported or link-table read failed\r\n");
        return;
    }

    print_link_table(links, links_full);
    printf("Candidate: LBA %u -> PBA %u\r\n", SPI_NAND_BBM_DEMO_LBA, SPI_NAND_BBM_DEMO_PBA);
    printf("A1h updates block mapping only; back up/migrate data separately\r\n");

    if(ERROR == validate_candidate(SPI_NAND_BBM_DEMO_LBA, SPI_NAND_BBM_DEMO_PBA, links, links_full)) {
        printf("Candidate invalid; no A1h command sent\r\n");
        return;
    }

#if SPI_NAND_BBM_DEMO_EXECUTE
    printf("A1h result: %s\r\n",
           (SUCCESS == spi_nand_bbm_replace(SPI_NAND_BBM_DEMO_LBA, SPI_NAND_BBM_DEMO_PBA)) ? "PASS" : "FAIL");
    printf("Status after A1h attempt: 0x%02X\r\n", spi_nand_get_feature(SPI_NAND_REG_STATUS));
    if(ERROR == spi_nand_bbm_read_links(links, &links_full)) {
        printf("BBM: link-table read after A1h attempt failed\r\n");
        return;
    }
    printf("BBM: A5h readback after A1h attempt\r\n");
    print_link_table(links, links_full);
#else
    printf("Dry-run only; set SPI_NAND_BBM_DEMO_EXECUTE=1 to issue A1h\r\n");
#endif
}