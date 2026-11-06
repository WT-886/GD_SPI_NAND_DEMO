#include "gd5f_spi_nand_bbm.h"
#include "gd5f_spi_nand_internal.h"
#include "gd5f_spi_nand_cr.h"
#include "gd5f_spi_nand_test_config.h"
#include <stdio.h>
#include <string.h>

#define CMD_WRITE_ENABLE             0x06U
#define CMD_WRITE_BBM                0xA1U
#define CMD_READ_BBM_LINK_TABLE      0xA5U
#define BBM_LBA_VALUE_MASK           0x3FFFU

static ErrStatus validate_candidate(uint16_t lba, uint16_t pba,
                                    const spi_nand_bbm_link_t *links, uint8_t links_full)
{
    const spi_nand_info_t *nand = spi_nand_get_info();
    uint8_t available = 0U;
     uint8_t source_links = 0U;

    /* Reject address/table conflicts before the destination is erased; marker-read errors also reject it. */
    if((NULL == nand) || !nand->bbm_supported || (lba >= nand->block_count) ||
         (pba >= nand->block_count) || (lba == pba) || links_full) {
        return ERROR;
    }

    for(uint32_t i = 0U; i < SPI_NAND_BBM_LINK_COUNT; i++) {
        if(SPI_NAND_BBM_LINK_AVAILABLE == links[i].status) {
            available++;
        } else if((SPI_NAND_BBM_LINK_VALID != links[i].status) &&
                  (SPI_NAND_BBM_LINK_INVALID != links[i].status)) {
            return ERROR;
        } else if((SPI_NAND_BBM_LINK_VALID == links[i].status) &&
                  ((pba == links[i].pba) || (pba == links[i].lba) ||
                   (lba == links[i].pba))) {
            return ERROR;
        }
        if((SPI_NAND_BBM_LINK_VALID == links[i].status) && (lba == links[i].lba)) {
            source_links++;
            if(source_links > 1U) {
                return ERROR;
            }
        }
    }
    return (available && (SUCCESS == spi_nand_block_check_bad(pba))) ? SUCCESS : ERROR;
}

static ErrStatus migrate_block(uint16_t lba, uint16_t pba, const spi_nand_info_t *nand)
{
    static uint8_t source_data[SPI_NAND_MAX_PAGE_SIZE];
    static uint8_t verify_data[SPI_NAND_MAX_PAGE_SIZE];
    const uint32_t source_page = (uint32_t)lba * nand->pages_per_block;
    const uint32_t target_page = (uint32_t)pba * nand->pages_per_block;

    /* The caller must reserve this block: migration deliberately destroys its previous contents. */
    if(ERROR == spi_nand_block_erase(pba)) {
        return ERROR;
    }

    /* Copy the currently visible LBA data first; a later page error prevents A1h from being issued. */
    for(uint32_t page = 0U; page < nand->pages_per_block; page++) {
        if((ERROR == spi_nand_page_read(source_page + page, 0U, source_data, nand->page_size)) ||
           (ERROR == spi_nand_page_program(target_page + page, 0U, source_data, nand->page_size))) {
            return ERROR;
        }
    }

    /* Verify every main-area page before changing the persistent mapping. */
    for(uint32_t page = 0U; page < nand->pages_per_block; page++) {
        if((ERROR == spi_nand_page_read(source_page + page, 0U, source_data, nand->page_size)) ||
           (ERROR == spi_nand_page_read(target_page + page, 0U, verify_data, nand->page_size)) ||
           (0 != memcmp(source_data, verify_data, nand->page_size))) {
            return ERROR;
        }
    }
    return SUCCESS;
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

    /* A5h returns fixed four-byte entries; the top two LBA bits carry each entry's state. */
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
    const spi_nand_info_t *nand = spi_nand_get_info();
    spi_nand_bbm_link_t links[SPI_NAND_BBM_LINK_COUNT];
    spi_nand_bbm_link_t checked_links[SPI_NAND_BBM_LINK_COUNT];
    uint8_t links_full;
    uint8_t wel;
    uint8_t command_data[4];
    uint8_t status;
    uint8_t feature;
    uint8_t matching_links = 0U;

    /* Require a supported, idle device in the normal data path before reading source pages. */
    if((NULL == nand) || !nand->bbm_supported || (0U == nand->page_size) ||
       (nand->page_size > SPI_NAND_MAX_PAGE_SIZE) || (0U == nand->pages_per_block) ||
       (spi_nand_wait_ready_internal() & SPI_NAND_STAT_OIP)) {
        return ERROR;
    }
    feature = spi_nand_get_feature(SPI_NAND_REG_FEATURE);
    if((0U == (feature & SPI_NAND_FEAT_ECC_EN)) || (feature & SPI_NAND_FEAT_OTP_EN) ||
       (nand->cont_read && (0U == (feature & SPI_NAND_CR_FEAT_NR)))) {
        return ERROR;
    }

    /* Snapshot the table and validate the target before any destructive operation. */
    if(ERROR == spi_nand_bbm_read_links(links, &links_full) ||
       ERROR == validate_candidate(lba, pba, links, links_full)) {
        return ERROR;
    }

    /* A1h is the commit point: do not reach it unless copy and readback verification succeed. */
    if(ERROR == migrate_block(lba, pba, nand)) {
        return ERROR;
    }
    /* Abort if another client changed the mapping table during the copy. */
    if(ERROR == spi_nand_bbm_read_links(checked_links, &links_full) ||
       ERROR == validate_candidate(lba, pba, checked_links, links_full)) {
        return ERROR;
    }
    for(uint32_t index = 0U; index < SPI_NAND_BBM_LINK_COUNT; index++) {
        if((links[index].status != checked_links[index].status) ||
           (links[index].lba != checked_links[index].lba) ||
           (links[index].pba != checked_links[index].pba)) {
            return ERROR;
        }
    }

    /* Arm the one-shot A1h write only after migration and the table recheck. */
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
    if(status & (SPI_NAND_STAT_OIP | SPI_NAND_STAT_P_FAIL)) {
        return ERROR;
    }
    if(ERROR == spi_nand_bbm_read_links(links, &links_full)) {
        return ERROR;
    }

    /* A successful command must leave exactly one valid entry for this LBA and target. */
    for(uint32_t i = 0U; i < SPI_NAND_BBM_LINK_COUNT; i++) {
        if((SPI_NAND_BBM_LINK_VALID == links[i].status) && (lba == links[i].lba)) {
            if(pba != links[i].pba) {
                return ERROR;
            }
            matching_links++;
        }
    }
    return (1U == matching_links) ? SUCCESS : ERROR;
}

static void print_link_table(const spi_nand_bbm_link_t *links, uint8_t links_full)
{
    uint8_t available = 0U;

    /* Keep available and consumed-invalid entries visible; both affect future capacity decisions. */
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

    /* Report actual device state first; the configured candidate is not assumed to be free. */
    if(ERROR == spi_nand_bbm_read_links(links, &links_full)) {
        printf("BBM: unsupported or link-table read failed\r\n");
        return;
    }

    print_link_table(links, links_full);
    printf("Candidate: LBA %u -> PBA %u\r\n", SPI_NAND_BBM_DEMO_LBA, SPI_NAND_BBM_DEMO_PBA);
    printf("BBM: check target, erase/copy main data, verify, then commit A1h\r\n");
    printf("Target data will be erased; OOB is not copied; exclusive NAND access required\r\n");

    if(ERROR == validate_candidate(SPI_NAND_BBM_DEMO_LBA, SPI_NAND_BBM_DEMO_PBA, links, links_full)) {
        printf("Candidate invalid; no A1h command sent\r\n");
        return;
    }

#if SPI_NAND_BBM_DEMO_EXECUTE
    const ErrStatus result = spi_nand_bbm_replace(SPI_NAND_BBM_DEMO_LBA, SPI_NAND_BBM_DEMO_PBA);

    printf("BBM migration/map result: %s\r\n",
           (SUCCESS == result) ? "PASS" : "FAIL");
    if(ERROR == result) {
        printf("Failure may precede A1h or leave its outcome unknown; inspect A5h\r\n");
    }
    printf("Status after BBM attempt: 0x%02X\r\n", spi_nand_get_feature(SPI_NAND_REG_STATUS));
    if(ERROR == spi_nand_bbm_read_links(links, &links_full)) {
        printf("BBM: link-table read after BBM attempt failed\r\n");
        return;
    }
    printf("BBM: A5h readback after migration/map attempt\r\n");
    print_link_table(links, links_full);
#else
    printf("Dry-run only; set SPI_NAND_BBM_DEMO_EXECUTE=1 to issue A1h\r\n");
#endif
}
