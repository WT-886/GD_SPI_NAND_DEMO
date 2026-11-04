#include "gd5f_spi_nand_full_test.h"
#include "gd5f_spi_nand_cr.h"
#include <stdio.h>
#include <string.h>

#define PROGRESS_BLOCK_INTERVAL  64U
#define FULL_TEST_MAX_BLOCKS    4096U
#define FULL_TEST_CR_BUFFER_SIZE (64U * 1024U)
#define FULL_TEST_RANDOM_SEED    0xA5C31E27U

typedef enum {
    BLOCK_AVAILABLE = 0U,
    BLOCK_BAD_OR_UNREADABLE,
    BLOCK_ERASE_FAILED,
    BLOCK_PROGRAM_FAILED
} full_test_block_state_t;

static full_test_block_state_t block_state[FULL_TEST_MAX_BLOCKS];
static uint8_t tx_page[SPI_NAND_MAX_PAGE_SIZE];
static uint8_t rx_page[SPI_NAND_MAX_PAGE_SIZE];
static uint8_t cr_buffer[FULL_TEST_CR_BUFFER_SIZE];

static uint32_t random_next(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void fill_random_page(uint8_t *buf, uint16_t size, uint32_t block, uint32_t page)
{
    uint32_t state = FULL_TEST_RANDOM_SEED ^ (block * 0x9E3779B9U) ^ (page * 0x85EBCA6BU);

    if(0U == state) {
        state = 1U;
    }
    for(uint32_t i = 0U; i < size; i++) {
        buf[i] = (uint8_t)random_next(&state);
    }
}

static uint32_t verify_normal_mode(const spi_nand_info_t *nand, spi_nand_io_mode_t mode,
                                   const char *mode_name)
{
    uint32_t failed_blocks = 0U;

    printf("\r\n%s normal read/verify\r\n", mode_name);
    for(uint32_t block = 0U; block < nand->block_count; block++) {
        uint32_t first_bad_page = 0U;
        uint32_t block_errors = 0U;

        if(BLOCK_AVAILABLE != block_state[block]) {
            continue;
        }
        for(uint32_t page = 0U; page < nand->pages_per_block; page++) {
            const uint32_t address = (block * nand->pages_per_block + page) * nand->page_size;

            fill_random_page(tx_page, nand->page_size, block, page);
            if((ERROR == spi_nand_read_data(address, rx_page, nand->page_size, mode)) ||
               (0 != memcmp(tx_page, rx_page, nand->page_size))) {
                if(0U == block_errors) {
                    first_bad_page = page;
                }
                block_errors++;
            }
        }
        if(block_errors) {
            failed_blocks++;
            printf("Block %lu: %lu error(s), first page %lu\r\n",
                   (unsigned long)block, (unsigned long)block_errors, (unsigned long)first_bad_page);
        }
        if((0U == ((block + 1U) % PROGRESS_BLOCK_INTERVAL)) || (block + 1U == nand->block_count)) {
            printf("%s progress %lu/%lu\r\n", mode_name,
                   (unsigned long)(block + 1U), (unsigned long)nand->block_count);
        }
    }
    printf("%s result: %s, failed blocks %lu\r\n", mode_name,
           failed_blocks ? "FAIL" : "PASS", (unsigned long)failed_blocks);
    return failed_blocks;
}

static uint32_t verify_continuous_mode(const spi_nand_info_t *nand,
                                      ErrStatus (*read_stream)(uint32_t, uint8_t *, uint32_t),
                                      const char *mode_name)
{
    uint32_t chunk_pages = FULL_TEST_CR_BUFFER_SIZE / nand->page_size;
    uint32_t failed_blocks = 0U;

    if(chunk_pages > nand->pages_per_block) {
        chunk_pages = nand->pages_per_block;
    }

    printf("\r\n%s Continuous Read/verify\r\n", mode_name);
    for(uint32_t block = 0U; block < nand->block_count; block++) {
        uint8_t block_failed = 0U;

        if(BLOCK_AVAILABLE != block_state[block]) {
            continue;
        }
        for(uint32_t first = 0U; first < nand->pages_per_block; first += chunk_pages) {
            uint32_t pages = nand->pages_per_block - first;
            const uint32_t first_page = block * nand->pages_per_block + first;

            if(pages > chunk_pages) {
                pages = chunk_pages;
            }
            if(ERROR == read_stream(first_page, cr_buffer, pages * nand->page_size)) {
                printf("Block %lu: %s command FAIL at page %lu\r\n",
                       (unsigned long)block, mode_name, (unsigned long)first);
                block_failed = 1U;
                break;
            }
            for(uint32_t i = 0U; i < pages; i++) {
                fill_random_page(tx_page, nand->page_size, block, first + i);
                if(0 != memcmp(tx_page, &cr_buffer[i * nand->page_size], nand->page_size)) {
                    printf("Block %lu: %s mismatch at page %lu\r\n",
                           (unsigned long)block, mode_name, (unsigned long)(first + i));
                    block_failed = 1U;
                    break;
                }
            }
            if(block_failed) {
                break;
            }
        }
        failed_blocks += block_failed;
    }
    printf("%s result: %s, failed blocks %lu\r\n", mode_name,
           failed_blocks ? "FAIL" : "PASS", (unsigned long)failed_blocks);
    return failed_blocks;
}

ErrStatus spi_nand_full_random_test(const spi_nand_info_t *nand, spi_nand_io_mode_t mode)
{
    uint32_t marker_skipped = 0U;
    uint32_t erase_failed = 0U;
    uint32_t program_failed = 0U;
    uint32_t programmed_blocks = 0U;
    uint32_t read_failed = 0U;

     if((NULL == nand) || (0U == nand->block_count) ||
         (0U == nand->pages_per_block) || (nand->page_size > sizeof(tx_page)) ||
         (nand->block_count > FULL_TEST_MAX_BLOCKS) ||
       ((SPI_NAND_IO_SINGLE != mode) && (SPI_NAND_IO_QUAD != mode)) ||
       (0 == strcmp(nand->name, "unknown"))) {
        printf("Full-chip test: unsupported geometry or I/O mode\r\n");
        return ERROR;
    }

    printf("\r\nWARNING: destructive full-chip test; all usable blocks will be erased and rewritten\r\n");
    printf("Factory bad markers are checked first; no BBM/A1 mapping is used\r\n");
    printf("Full-chip test: %lu blocks, %u pages/block, %u-byte pages, write mode %s, seed 0x%08lX\r\n",
            (unsigned long)nand->block_count, nand->pages_per_block, nand->page_size,
            (SPI_NAND_IO_QUAD == mode) ? "quad" : "single", (unsigned long)FULL_TEST_RANDOM_SEED);
    for(uint32_t block = 0U; block < nand->block_count; block++) {
        if(ERROR == spi_nand_block_check_bad(block)) {
            block_state[block] = BLOCK_BAD_OR_UNREADABLE;
            marker_skipped++;
            printf("Block %lu: bad marker/read failure, skipped\r\n", (unsigned long)block);
        } else {
            block_state[block] = BLOCK_AVAILABLE;
        }
    }

    for(uint32_t block = 0U; block < nand->block_count; block++) {
        if(BLOCK_AVAILABLE == block_state[block] && ERROR == spi_nand_block_erase(block)) {
            block_state[block] = BLOCK_ERASE_FAILED;
            erase_failed++;
            printf("Block %lu: erase FAIL\r\n", (unsigned long)block);
        }
        if((0U == ((block + 1U) % PROGRESS_BLOCK_INTERVAL)) || (block + 1U == nand->block_count)) {
            printf("Erase progress %lu/%lu\r\n",
                   (unsigned long)(block + 1U), (unsigned long)nand->block_count);
        }
    }

    for(uint32_t block = 0U; block < nand->block_count; block++) {
        if(BLOCK_AVAILABLE != block_state[block]) {
            continue;
        }
        for(uint32_t page = 0U; page < nand->pages_per_block; page++) {
            const uint32_t address = (block * nand->pages_per_block + page) * nand->page_size;

            fill_random_page(tx_page, nand->page_size, block, page);
            if(ERROR == spi_nand_program_data(address, tx_page, nand->page_size, mode)) {
                block_state[block] = BLOCK_PROGRAM_FAILED;
                program_failed++;
                printf("Block %lu: write FAIL at page %lu\r\n",
                       (unsigned long)block, (unsigned long)page);
                break;
            }
        }
        if(BLOCK_AVAILABLE == block_state[block]) {
            programmed_blocks++;
        }
        if((0U == ((block + 1U) % PROGRESS_BLOCK_INTERVAL)) || (block + 1U == nand->block_count)) {
            printf("Write progress %lu/%lu, blocks written %lu\r\n",
                   (unsigned long)(block + 1U), (unsigned long)nand->block_count,
                   (unsigned long)programmed_blocks);
        }
    }

    read_failed += verify_normal_mode(nand, SPI_NAND_IO_SINGLE, "1x normal");
    read_failed += verify_normal_mode(nand, SPI_NAND_IO_QUAD, "4x normal");

    if(nand->cont_read && (SUCCESS == spi_nand_set_continuous_read(1U))) {
        read_failed += verify_continuous_mode(nand, spi_nand_continuous_read, "1x");
        read_failed += verify_continuous_mode(nand, spi_nand_quad_continuous_read, "4x");
        (void)spi_nand_set_continuous_read(0U);
    } else {
        printf("Continuous Read: unsupported, skipped\r\n");
    }

    printf("Full-chip summary: marker skip %lu, erase fail %lu, write fail %lu, read fail %lu\r\n",
           (unsigned long)marker_skipped, (unsigned long)erase_failed,
           (unsigned long)program_failed, (unsigned long)read_failed);
    return (erase_failed || program_failed || read_failed || (0U == programmed_blocks)) ? ERROR : SUCCESS;
}
