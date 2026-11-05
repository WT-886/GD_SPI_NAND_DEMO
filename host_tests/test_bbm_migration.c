#include "gd5f_spi_nand_bbm.h"
#include "gd5f_spi_nand_cr.h"
#include "gd5f_spi_nand_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_BLOCKS 8U        /* Simulated device block count; keeps address checks realistic. */
#define TEST_PAGES 3U         /* Pages per simulated block; small to keep scenarios fast. */
#define TEST_PAGE_SIZE 16U    /* Main-area bytes per simulated page. */
#define SOURCE_BLOCK 1U       /* Device-visible LBA whose contents are copied. */
#define TARGET_BLOCK 6U       /* Reserved PBA erased and populated by the migration. */

/* Faults are injected at the operation boundary; A1h faults occur after its command is issued. */
typedef enum {
    FAULT_NONE,                 /* Run without an injected failure. */
    FAULT_ERASE,                /* Fail erasing the target before copying. */
    FAULT_SOURCE_READ,          /* Fail while reading source data. */
    FAULT_PROGRAM,              /* Fail a target-page program during copying. */
    FAULT_TARGET_READ,          /* Fail while reading the copied target data. */
    FAULT_COMPARE,              /* Return corrupted target data to fail verification. */
    FAULT_TABLE_REREAD,         /* Fail the pre-commit A5h table reread. */
    FAULT_TABLE_CHANGE,         /* Occupy the target from another mapping before commit. */
    FAULT_SOURCE_MAPPING_CHANGE,/* Change the source mapping while copying. */
    FAULT_WRITE_ENABLE,         /* Fail the WREN command before A1h. */
    FAULT_WEL,                  /* Leave WEL clear after WREN. */
    FAULT_A1_TRANSFER,          /* Report a transfer error after issuing A1h. */
    FAULT_A1_BUSY,              /* Keep OIP set after A1h. */
    FAULT_A1_PFAIL,             /* Report program failure after A1h. */
    FAULT_A1_NO_MAPPING,         /* Complete A1h without adding the expected link. */
    FAULT_POST_TABLE_READ        /* Fail reading A5h after the A1h attempt. */
} test_fault_t;

static spi_nand_info_t device;
static spi_nand_bbm_link_t links[SPI_NAND_BBM_LINK_COUNT];
static uint8_t storage[TEST_BLOCKS][TEST_PAGES][TEST_PAGE_SIZE];
static uint8_t original[TEST_BLOCKS][TEST_PAGES][TEST_PAGE_SIZE];
static uint8_t expected[TEST_PAGES][TEST_PAGE_SIZE];
static uint8_t feature;
static uint8_t status;
static uint8_t target_bad;
static uint8_t no_device;
static test_fault_t fault;
static unsigned int erase_count;
static unsigned int program_count;
static unsigned int a1_count;
static unsigned int table_reads;
static unsigned int cases;

/* Resolve device-visible block addresses through the simulated valid-link table. */
static uint32_t physical_block(uint32_t block)
{
    for(uint32_t index = 0U; index < SPI_NAND_BBM_LINK_COUNT; index++) {
        if((SPI_NAND_BBM_LINK_VALID == links[index].status) && (block == links[index].lba)) {
            return links[index].pba;
        }
    }
    return block;
}

static void reset_device(void)
{
    /* Each scenario starts from independent geometry, feature state, table, and deterministic data. */
    memset(&device, 0, sizeof(device));
    memset(links, 0, sizeof(links));
    device.page_size = TEST_PAGE_SIZE;
    device.pages_per_block = TEST_PAGES;
    device.block_count = TEST_BLOCKS;
    device.bbm_supported = 1U;
    device.cont_read = 1U;
    feature = SPI_NAND_FEAT_ECC_EN | SPI_NAND_CR_FEAT_NR;
    status = 0U;
    target_bad = 0U;
    no_device = 0U;
    fault = FAULT_NONE;
    erase_count = 0U;
    program_count = 0U;
    a1_count = 0U;
    table_reads = 0U;
    for(uint32_t block = 0U; block < TEST_BLOCKS; block++) {
        for(uint32_t page = 0U; page < TEST_PAGES; page++) {
            for(uint32_t column = 0U; column < TEST_PAGE_SIZE; column++) {
                storage[block][page][column] = (uint8_t)(block * 29U + page * 7U + column);
            }
        }
    }
    memcpy(original, storage, sizeof(original));
}

const spi_nand_info_t *spi_nand_get_info(void)
{
    /* Stand-in for the driver's initialized device descriptor. */
    return no_device ? NULL : &device;
}

uint8_t spi_nand_get_feature(uint8_t reg)
{
    return (SPI_NAND_REG_FEATURE == reg) ? feature : status;
}

uint8_t spi_nand_wait_ready_internal(void)
{
    /* Model delayed completion and program-fail status for the A1h commit path. */
    if(a1_count && (FAULT_A1_BUSY == fault)) {
        return SPI_NAND_STAT_OIP;
    }
    if(a1_count && (FAULT_A1_PFAIL == fault)) {
        return SPI_NAND_STAT_P_FAIL;
    }
    return status;
}

ErrStatus spi_nand_block_check_bad(uint32_t block)
{
    /* The test target is fixed; this also proves marker validation precedes erase and mapping. */
    assert(TARGET_BLOCK == block);
    assert(0U == a1_count);
    return target_bad ? ERROR : SUCCESS;
}

ErrStatus spi_nand_block_erase(uint32_t block)
{
    /* A successful erase destroys the target's previous simulated contents. */
    assert(TARGET_BLOCK == block);
    assert(0U == a1_count);
    erase_count++;
    if(FAULT_ERASE == fault) {
        return ERROR;
    }
    memset(storage[block], 0xFF, sizeof(storage[block]));
    return SUCCESS;
}

ErrStatus spi_nand_page_read(uint32_t page, uint16_t column, uint8_t *data, uint16_t length)
{
    const uint32_t block = physical_block(page / TEST_PAGES);
    assert(0U == column);
    assert(TEST_PAGE_SIZE == length);
    assert(block < TEST_BLOCKS);
    /* Inject source/target read errors or a silent target corruption detected by readback. */
    if(((SOURCE_BLOCK == page / TEST_PAGES) && (FAULT_SOURCE_READ == fault)) ||
       ((TARGET_BLOCK == block) && (FAULT_TARGET_READ == fault))) {
        return ERROR;
    }
    memcpy(data, storage[block][page % TEST_PAGES], length);
    if((TARGET_BLOCK == block) && (FAULT_COMPARE == fault)) {
        data[0] ^= 1U;
    }
    return SUCCESS;
}

ErrStatus spi_nand_page_program(uint32_t page, uint16_t column, const uint8_t *data, uint16_t length)
{
    assert(TARGET_BLOCK == page / TEST_PAGES);
    assert(0U == column);
    assert(TEST_PAGE_SIZE == length);
    assert(1U == erase_count);
    assert(0U == a1_count);
    /* Programs are accepted only after target erase and before the A1h mapping commit. */
    program_count++;
    if((FAULT_PROGRAM == fault) && (2U == program_count)) {
        return ERROR;
    }
    memcpy(storage[TARGET_BLOCK][page % TEST_PAGES], data, length);
    return SUCCESS;
}

ErrStatus spi_nand_command_transfer(uint8_t command, const uint8_t *tx, uint32_t tx_len,
                                   uint8_t *rx, uint32_t rx_len)
{
    if(0xA5U == command) {
        /* A5h serializes the 20-entry table and can model a concurrent table change between reads. */
        assert(1U == tx_len);
        assert(SPI_NAND_BBM_LINK_COUNT * 4U == rx_len);
        table_reads++;
        if(((2U == table_reads) && (FAULT_TABLE_REREAD == fault)) ||
           ((3U == table_reads) && (FAULT_POST_TABLE_READ == fault))) {
            return ERROR;
        }
        if((2U == table_reads) && (FAULT_TABLE_CHANGE == fault)) {
            links[0].status = SPI_NAND_BBM_LINK_VALID;
            links[0].lba = 2U;
            links[0].pba = TARGET_BLOCK;
        }
        if((2U == table_reads) && (FAULT_SOURCE_MAPPING_CHANGE == fault)) {
            links[0].status = SPI_NAND_BBM_LINK_VALID;
            links[0].lba = SOURCE_BLOCK;
            links[0].pba = 5U;
        }
        for(uint32_t index = 0U; index < SPI_NAND_BBM_LINK_COUNT; index++) {
            const uint16_t raw = (uint16_t)(((uint16_t)links[index].status << 14) | links[index].lba);
            rx[index * 4U] = (uint8_t)(raw >> 8);
            rx[index * 4U + 1U] = (uint8_t)raw;
            rx[index * 4U + 2U] = (uint8_t)(links[index].pba >> 8);
            rx[index * 4U + 3U] = (uint8_t)links[index].pba;
        }
        return SUCCESS;
    }
    if(0x06U == command) {
        /* WREN sets WEL unless the selected pre-A1h failure is injected. */
        if(FAULT_WRITE_ENABLE == fault) {
            return ERROR;
        }
        status = (FAULT_WEL == fault) ? 0U : SPI_NAND_STAT_WEL;
        return SUCCESS;
    }
    assert(0xA1U == command);
    /* Reaching A1h proves every source page was copied and the destination readback matched. */
    assert(4U == tx_len);
    assert(SOURCE_BLOCK == (((uint16_t)tx[0] << 8) | tx[1]));
    assert(TARGET_BLOCK == (((uint16_t)tx[2] << 8) | tx[3]));
    assert(TEST_PAGES == program_count);
    assert(0 == memcmp(expected, storage[TARGET_BLOCK], sizeof(expected)));
    a1_count++;
    if(FAULT_A1_TRANSFER == fault) {
        return ERROR;
    }
    if(FAULT_A1_NO_MAPPING == fault) {
        return SUCCESS;
    }
    for(uint32_t index = 0U; index < SPI_NAND_BBM_LINK_COUNT; index++) {
        if((SPI_NAND_BBM_LINK_VALID == links[index].status) && (SOURCE_BLOCK == links[index].lba)) {
            links[index].status = SPI_NAND_BBM_LINK_INVALID;
        }
    }
    for(uint32_t index = 0U; index < SPI_NAND_BBM_LINK_COUNT; index++) {
        if(SPI_NAND_BBM_LINK_AVAILABLE == links[index].status) {
            links[index].status = SPI_NAND_BBM_LINK_VALID;
            links[index].lba = SOURCE_BLOCK;
            links[index].pba = TARGET_BLOCK;
            break;
        }
    }
    return SUCCESS;
}

static void check_case(const char *name, ErrStatus result, unsigned int commits)
{
    /* Verify the expected result/commit count and that only a successful mapping redirects the source. */
    memcpy(expected, storage[physical_block(SOURCE_BLOCK)], sizeof(expected));
    assert(result == spi_nand_bbm_replace(SOURCE_BLOCK, TARGET_BLOCK));
    assert(commits == a1_count);
    for(uint32_t block = 0U; block < TEST_BLOCKS; block++) {
        if(TARGET_BLOCK != block) {
            assert(0 == memcmp(original[block], storage[block], sizeof(storage[block])));
        }
    }
    if(SUCCESS == result) {
        assert(TARGET_BLOCK == physical_block(SOURCE_BLOCK));
        assert(0 == memcmp(expected, storage[TARGET_BLOCK], sizeof(expected)));
    }
    cases++;
    printf("PASS: %s (fault=%u)\n", name, (unsigned int)fault);
}

int main(void)
{
    /* Success cases cover first mapping, remapping the current visible source, and historical PBA reuse. */
    reset_device();
    check_case("new mapping preserves main data", SUCCESS, 1U);
    reset_device();
    links[0] = (spi_nand_bbm_link_t){ SOURCE_BLOCK, 4U, SPI_NAND_BBM_LINK_VALID };
    check_case("remap copies current mapped source", SUCCESS, 1U);
    reset_device();
    links[0] = (spi_nand_bbm_link_t){ SOURCE_BLOCK, TARGET_BLOCK, SPI_NAND_BBM_LINK_INVALID };
    check_case("historical target reuse", SUCCESS, 1U);

    /* Every injected failure must abort at its boundary; pre-A1h faults must not consume a link. */
    for(test_fault_t current = FAULT_ERASE; current <= FAULT_POST_TABLE_READ; current++) {
        reset_device();
        fault = current;
        check_case("injected migration/commit failure", ERROR,
                   (current >= FAULT_A1_TRANSFER) ? 1U : 0U);
    }
    /* Invalid operating modes, geometry, mappings, or capacity must be rejected before erase. */
    reset_device(); target_bad = 1U;
    check_case("bad/unreadable target rejected", ERROR, 0U);
    assert(0U == erase_count);
    reset_device(); feature &= (uint8_t)~SPI_NAND_CR_FEAT_NR;
    check_case("continuous-read mode rejected", ERROR, 0U);
    reset_device(); feature &= (uint8_t)~SPI_NAND_FEAT_ECC_EN;
    check_case("disabled ECC rejected", ERROR, 0U);
    reset_device(); feature |= SPI_NAND_FEAT_OTP_EN;
    check_case("OTP mode rejected", ERROR, 0U);
    reset_device(); no_device = 1U;
    check_case("uninitialized device rejected", ERROR, 0U);
    reset_device(); device.bbm_supported = 0U;
    check_case("unsupported device rejected", ERROR, 0U);
    reset_device(); device.page_size = SPI_NAND_MAX_PAGE_SIZE + 1U;
    check_case("oversized geometry rejected", ERROR, 0U);
    reset_device(); status = SPI_NAND_STAT_OIP;
    check_case("busy device rejected", ERROR, 0U);
    reset_device(); status = SPI_NAND_CR_STAT_BBLS;
    check_case("full link table rejected", ERROR, 0U);
    reset_device(); links[0].status = 1U;
    check_case("reserved link status rejected", ERROR, 0U);
    reset_device();
    links[0] = (spi_nand_bbm_link_t){ 2U, TARGET_BLOCK, SPI_NAND_BBM_LINK_VALID };
    check_case("active target collision rejected", ERROR, 0U);
    assert(0U == erase_count);
    reset_device();
    links[0] = (spi_nand_bbm_link_t){ TARGET_BLOCK, 4U, SPI_NAND_BBM_LINK_VALID };
    check_case("target address already redirected rejected", ERROR, 0U);
    reset_device();
    links[0] = (spi_nand_bbm_link_t){ 2U, SOURCE_BLOCK, SPI_NAND_BBM_LINK_VALID };
    check_case("source is another mapping target rejected", ERROR, 0U);
    reset_device();
    links[0] = (spi_nand_bbm_link_t){ SOURCE_BLOCK, 4U, SPI_NAND_BBM_LINK_VALID };
    links[1] = (spi_nand_bbm_link_t){ SOURCE_BLOCK, 5U, SPI_NAND_BBM_LINK_VALID };
    check_case("ambiguous source mapping rejected", ERROR, 0U);
    reset_device();
    for(uint32_t index = 0U; index < SPI_NAND_BBM_LINK_COUNT; index++) {
        links[index].status = SPI_NAND_BBM_LINK_INVALID;
    }
    check_case("no available links rejected", ERROR, 0U);
    reset_device();
    assert(ERROR == spi_nand_bbm_replace(SOURCE_BLOCK, SOURCE_BLOCK));
    assert(ERROR == spi_nand_bbm_replace(TEST_BLOCKS, TARGET_BLOCK));
    assert(ERROR == spi_nand_bbm_replace(SOURCE_BLOCK, TEST_BLOCKS));
    assert(0U == erase_count);
    assert(0U == a1_count);
    printf("%u migration scenarios plus 3 address checks passed\n", cases);
    return 0;
}
