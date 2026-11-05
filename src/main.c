/*!
    \file    main.c
    \brief   SPI NAND validation and BBM demo on GD32450Z-EVAL (GD32F450ZKT6)

    \version 2026-11-04, v1.0.0, project based on GD32F4xx firmware V3.3.3
*/

/*
    Copyright (c) 2026, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include "gd32f4xx.h"
#include <stdio.h>
#include <string.h>
#include "gd32f450i_eval.h"
#include "gd5f_spi_nand.h"
#include "gd5f_spi_nand_cr.h"
#include "gd5f_spi_nand_full_test.h"
#include "gd5f_spi_nand_bbm.h"

/* Reserve this block range for the timing demo; it will be erased and rewritten. */
#define NAND_TEST_BLOCK          1U
#define NAND_TEST_BLOCK_COUNT    5U

#define CR_BUFFER_SIZE           (64U * 1024U)
#define NAND_BBM_ONLY_TEST       0U
#define NAND_FULL_TEST_ENABLE    0U
#define NAND_FULL_TEST_IO_MODE   SPI_NAND_IO_QUAD

static uint8_t tx_page[SPI_NAND_MAX_PAGE_SIZE];
static uint8_t rx_page[SPI_NAND_MAX_PAGE_SIZE];
static uint8_t cr_buffer[CR_BUFFER_SIZE];

static void timing_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t timing_start(void)
{
    return DWT->CYCCNT;
}

static void timing_report(const char *name, uint32_t start)
{
    const uint32_t cycles = DWT->CYCCNT - start;
    const uint64_t usec = ((uint64_t)cycles * 1000000U) / SystemCoreClock;

    printf("  %-30s %lu.%03lu ms\r\n", name,
           (unsigned long)(usec / 1000U), (unsigned long)(usec % 1000U));
}

static uint32_t spi_nand_sck_divisor(void)
{
    const uint32_t prescaler = (SPI_CTL0(SPI_NAND_SPI) & SPI_CTL0_PSC) >> 3U;

    return 2U << prescaler;
}

static void fill_test_page(uint8_t *buf, uint16_t size, uint32_t page)
{
    for(uint32_t i = 0U; i < size; i++) {
        buf[i] = (uint8_t)(i + page);
    }
}

static ErrStatus write_pages(const spi_nand_info_t *nand, uint32_t first_page,
                             uint32_t page_count, spi_nand_io_mode_t mode)
{
    for(uint32_t i = 0U; i < page_count; i++) {
        const uint32_t page = first_page + i;

        fill_test_page(tx_page, nand->page_size, page);
        if(ERROR == spi_nand_program_data(page * nand->page_size, tx_page, nand->page_size, mode)) {
            return ERROR;
        }
    }
    return SUCCESS;
}

static ErrStatus verify_pages(const spi_nand_info_t *nand, uint32_t first_page,
                              uint32_t page_count, spi_nand_io_mode_t mode)
{
    for(uint32_t i = 0U; i < page_count; i++) {
        const uint32_t page = first_page + i;

        if(ERROR == spi_nand_read_data(page * nand->page_size, rx_page, nand->page_size, mode)) {
            return ERROR;
        }
        fill_test_page(tx_page, nand->page_size, page);
        if(0 != memcmp(tx_page, rx_page, nand->page_size)) {
            return ERROR;
        }
    }
    return SUCCESS;
}

static ErrStatus verify_continuous(const spi_nand_info_t *nand, uint32_t first_page, uint32_t page_count,
                                   ErrStatus (*read_stream)(uint32_t, uint8_t *, uint32_t))
{
    const uint32_t max_chunk_pages = sizeof(cr_buffer) / nand->page_size;
    uint32_t offset_pages = 0U;

    if(0U == max_chunk_pages) {
        return ERROR;
    }
    while(offset_pages < page_count) {
        const uint32_t page = first_page + offset_pages;
        const uint32_t pages_to_block_end = nand->pages_per_block - (page % nand->pages_per_block);
        uint32_t chunk_pages = page_count - offset_pages;

        if(chunk_pages > max_chunk_pages) {
            chunk_pages = max_chunk_pages;
        }
        if(chunk_pages > pages_to_block_end) {
            chunk_pages = pages_to_block_end;
        }
        if(ERROR == read_stream(page, cr_buffer, chunk_pages * nand->page_size)) {
            printf("Continuous Read command failed at page %lu\r\n", (unsigned long)page);
            return ERROR;
        }
        for(uint32_t i = 0U; i < chunk_pages; i++) {
            fill_test_page(tx_page, nand->page_size, page + i);
            if(0 != memcmp(tx_page, &cr_buffer[i * nand->page_size], nand->page_size)) {
                for(uint32_t byte = 0U; byte < nand->page_size; byte++) {
                    if(tx_page[byte] != cr_buffer[i * nand->page_size + byte]) {
                        printf("Continuous Read mismatch: page %lu byte %lu, expected 0x%02X got 0x%02X\r\n",
                               (unsigned long)(page + i), (unsigned long)byte,
                               tx_page[byte], cr_buffer[i * nand->page_size + byte]);
                        break;
                    }
                }
                return ERROR;
            }
        }
        offset_pages += chunk_pages;
    }
    return SUCCESS;
}

static ErrStatus set_crdc(uint8_t enabled)
{
    uint8_t reg = spi_nand_get_feature(SPI_NAND_CR_REG_BLOCK_LOCK) & SPI_NAND_CR_LOCK_RW_MASK;

    if(enabled) {
        reg |= SPI_NAND_CR_LOCK_CRDC;
    } else {
        reg &= (uint8_t)~SPI_NAND_CR_LOCK_CRDC;
    }
    spi_nand_set_feature(SPI_NAND_CR_REG_BLOCK_LOCK, reg);
    return ((spi_nand_get_feature(SPI_NAND_CR_REG_BLOCK_LOCK) & SPI_NAND_CR_LOCK_RW_MASK) == reg) ? SUCCESS : ERROR;
}

static void print_result(const char *name, ErrStatus result)
{
    printf("%-28s %s\r\n", name, (SUCCESS == result) ? "PASS" : "FAIL");
}

static void nand_demo(const spi_nand_info_t *nand)
{
    const uint32_t first_page = NAND_TEST_BLOCK * nand->pages_per_block;
    const uint32_t page_count = NAND_TEST_BLOCK_COUNT * nand->pages_per_block;
    uint32_t read_start;
    uint8_t saved_lock;
    ErrStatus result;

    if((NAND_TEST_BLOCK >= nand->block_count) ||
       (0U == NAND_TEST_BLOCK_COUNT) ||
       (NAND_TEST_BLOCK_COUNT > (uint32_t)nand->block_count - NAND_TEST_BLOCK)) {
        printf("Invalid test block/page range\r\n");
        return;
    }

    printf("\r\nTest: blocks %u..%lu, %lu pages/block, %lu total pages\r\n",
           NAND_TEST_BLOCK, (unsigned long)(NAND_TEST_BLOCK + NAND_TEST_BLOCK_COUNT - 1U),
           (unsigned long)nand->pages_per_block, (unsigned long)page_count);

    result = SUCCESS;
    for(uint32_t block = NAND_TEST_BLOCK; block < NAND_TEST_BLOCK + NAND_TEST_BLOCK_COUNT; block++) {
        if(ERROR == spi_nand_block_check_bad(block)) {
            printf("Block %lu is bad or marker check failed\r\n", (unsigned long)block);
            result = ERROR;
        }
    }
    print_result("block bad-block checks", result);
    if(ERROR == result) {
        printf("Skipping read/write test for this range\r\n");
        return;
    }

    result = spi_nand_erase_blocks(NAND_TEST_BLOCK, NAND_TEST_BLOCK_COUNT);
    print_result("erase test blocks", result);
    if(ERROR == result) {
        return;
    }

    result = write_pages(nand, first_page, page_count, SPI_NAND_IO_SINGLE);
    print_result("single write", result);
    if(ERROR == result) {
        return;
    }
    read_start = timing_start();
    print_result("single read / verify", verify_pages(nand, first_page, page_count, SPI_NAND_IO_SINGLE));
    timing_report("1x normal read + verify", read_start);
    read_start = timing_start();
    print_result("quad read / verify", verify_pages(nand, first_page, page_count, SPI_NAND_IO_QUAD));
    timing_report("4x normal read + verify", read_start);

    result = spi_nand_erase_blocks(NAND_TEST_BLOCK, NAND_TEST_BLOCK_COUNT);
    print_result("erase before quad write", result);
    if(ERROR == result) {
        return;
    }
    result = write_pages(nand, first_page, page_count, SPI_NAND_IO_QUAD);
    print_result("quad write", result);
    if(ERROR == result) {
        return;
    }
    read_start = timing_start();
    print_result("quad-write / single-read verify", verify_pages(nand, first_page, page_count, SPI_NAND_IO_SINGLE));
    timing_report("1x normal read + verify", read_start);
    read_start = timing_start();
    print_result("quad-write / quad-read verify", verify_pages(nand, first_page, page_count, SPI_NAND_IO_QUAD));
    timing_report("4x normal read + verify", read_start);

    if(ERROR == spi_nand_set_continuous_read(1U)) {
        printf("Continuous Read: unsupported, skipped\r\n");
        return;
    }
    read_start = timing_start();
    print_result("single Continuous Read", verify_continuous(nand, first_page, page_count,
                 spi_nand_continuous_read));
    timing_report("1x Continuous Read + verify", read_start);
    saved_lock = spi_nand_get_feature(SPI_NAND_CR_REG_BLOCK_LOCK) & SPI_NAND_CR_LOCK_RW_MASK;
    for(uint8_t crdc = 0U; crdc <= 1U; crdc++) {
        char label[32];

        if(ERROR == set_crdc(crdc)) {
            printf("CRDC=%u configuration FAIL\r\n", crdc);
            continue;
        }
        snprintf(label, sizeof(label), "quad Continuous Read CRDC=%u", crdc);
        read_start = timing_start();
        print_result(label, verify_continuous(nand, first_page, page_count,
                     spi_nand_quad_continuous_read));
        timing_report("4x Continuous Read + verify", read_start);
    }
    spi_nand_set_feature(SPI_NAND_CR_REG_BLOCK_LOCK, saved_lock);
    (void)spi_nand_set_continuous_read(0U);
}

int main(void)
{
    const spi_nand_info_t *nand;

    gd_eval_com_init(EVAL_COM0);
    timing_init();
    nand = spi_nand_init();
    (void)spi_nand_set_continuous_read(0U);

    printf("\r\nSPI NAND: %s, ID 0x%04X, page %u+%u B\r\n",
           nand->name, spi_nand_read_id(), nand->page_size, nand->spare_size);
    printf("Clock: core %lu MHz, APB2 %lu MHz, SPI SCK %lu MHz (PCLK2/%lu)\r\n",
           (unsigned long)(SystemCoreClock / 1000000U),
           (unsigned long)(rcu_clock_freq_get(CK_APB2) / 1000000U),
           (unsigned long)(rcu_clock_freq_get(CK_APB2) / spi_nand_sck_divisor() / 1000000U),
           (unsigned long)spi_nand_sck_divisor());
#if NAND_BBM_ONLY_TEST
    printf("BBM-only test mode: page and full-chip tests skipped\r\n");
#else
    nand_demo(nand);
#if NAND_FULL_TEST_ENABLE
    (void)spi_nand_full_random_test(nand, NAND_FULL_TEST_IO_MODE);
#endif
#endif
    spi_nand_bbm_demo();

    while(1) {
    }
}

