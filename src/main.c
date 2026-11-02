/*!
    \file    main.c
    \brief   led spark with systick

    \version 2026-02-05, V3.3.3, firmware for GD32F4xx
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

/* Test pages in one reserved block. Erasing this block destroys all its data. */
#define NAND_TEST_BLOCK          1U
#define NAND_TEST_PAGE           0U
#define NAND_TEST_PAGE_COUNT     2U

#define CR_BUFFER_SIZE           (64U * 1024U)

static uint8_t tx_page[SPI_NAND_MAX_PAGE_SIZE];
static uint8_t rx_page[SPI_NAND_MAX_PAGE_SIZE];
static uint8_t cr_buffer[CR_BUFFER_SIZE];

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
    const uint32_t bytes = page_count * nand->page_size;

    if((bytes > sizeof(cr_buffer)) ||
       (ERROR == read_stream(first_page, cr_buffer, bytes))) {
        return ERROR;
    }
    for(uint32_t i = 0U; i < page_count; i++) {
        fill_test_page(tx_page, nand->page_size, first_page + i);
        if(0 != memcmp(tx_page, &cr_buffer[i * nand->page_size], nand->page_size)) {
            return ERROR;
        }
    }
    return SUCCESS;
}

static void print_result(const char *name, ErrStatus result)
{
    printf("%-28s %s\r\n", name, (SUCCESS == result) ? "PASS" : "FAIL");
}

static void nand_demo(const spi_nand_info_t *nand)
{
    const uint32_t first_page = NAND_TEST_BLOCK * nand->pages_per_block + NAND_TEST_PAGE;
    const uint32_t page_count = NAND_TEST_PAGE_COUNT;
    ErrStatus result;

    if((NAND_TEST_BLOCK >= nand->block_count) ||
       (NAND_TEST_PAGE >= nand->pages_per_block) ||
       (0U == page_count) ||
       (page_count > nand->pages_per_block - NAND_TEST_PAGE) ||
       ((page_count * nand->page_size) > sizeof(cr_buffer))) {
        printf("Invalid test block/page range\r\n");
        return;
    }

    printf("\r\nTest: block %u, page %u, %lu page(s)\r\n",
           NAND_TEST_BLOCK, NAND_TEST_PAGE, (unsigned long)page_count);

    result = spi_nand_block_check_bad(NAND_TEST_BLOCK);
    print_result("block bad-block check", result);
    if(ERROR == result) {
        printf("Skipping read/write test for this block\r\n");
        return;
    }

    result = spi_nand_block_erase(NAND_TEST_BLOCK);
    print_result("block erase", result);
    if(ERROR == result) {
        return;
    }

    result = write_pages(nand, first_page, page_count, SPI_NAND_IO_SINGLE);
    print_result("single write", result);
    if(ERROR == result) {
        return;
    }
    print_result("single read / verify", verify_pages(nand, first_page, page_count, SPI_NAND_IO_SINGLE));
    print_result("quad read / verify", verify_pages(nand, first_page, page_count, SPI_NAND_IO_QUAD));

    result = spi_nand_block_erase(NAND_TEST_BLOCK);
    print_result("block erase before quad write", result);
    if(ERROR == result) {
        return;
    }
    result = write_pages(nand, first_page, page_count, SPI_NAND_IO_QUAD);
    print_result("quad write", result);
    if(ERROR == result) {
        return;
    }
    print_result("quad-write / single-read verify", verify_pages(nand, first_page, page_count, SPI_NAND_IO_SINGLE));
    print_result("quad-write / quad-read verify", verify_pages(nand, first_page, page_count, SPI_NAND_IO_QUAD));

    if(ERROR == spi_nand_set_continuous_read(1U)) {
        printf("Continuous Read: unsupported, skipped\r\n");
        return;
    }
    print_result("single Continuous Read", verify_continuous(nand, first_page, page_count,
                 spi_nand_continuous_read));
    print_result("quad Continuous Read", verify_continuous(nand, first_page, page_count,
                 spi_nand_quad_continuous_read));
    (void)spi_nand_set_continuous_read(0U);
}

int main(void)
{
    const spi_nand_info_t *nand;

    gd_eval_com_init(EVAL_COM0);
    nand = spi_nand_init();
    (void)spi_nand_set_continuous_read(0U);

    printf("\r\nSPI NAND: %s, ID 0x%04X, page %u+%u B\r\n",
           nand->name, spi_nand_read_id(), nand->page_size, nand->spare_size);
    nand_demo(nand);

    while(1) {
    }
}

