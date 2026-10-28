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
#include "systick.h"
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "gd32f450i_eval.h"
#include "gd5f_spi_nand.h"
#include "gd5f_spi_nand_cr.h"

/* block used by the demo; block 0 is avoided as it often stores boot data */
#define NAND_TEST_BLOCK       1U
/* RAM used by the Continuous Read demo; one CS-low burst fills it */
#define CR_BUFFER_SIZE        (64U * 1024U)

typedef ErrStatus (*nand_program_fn)(uint32_t page, uint16_t column, const uint8_t *buf, uint16_t len);
typedef ErrStatus (*nand_read_fn)(uint32_t page, uint16_t column, uint8_t *buf, uint16_t len);
typedef ErrStatus (*nand_stream_fn)(uint32_t start_page, uint8_t *buf, uint32_t len);

static uint8_t tx_buffer[SPI_NAND_MAX_PAGE_SIZE];
static uint8_t rx_buffer[SPI_NAND_MAX_PAGE_SIZE];
static uint8_t cr_buffer[CR_BUFFER_SIZE];

static void cycle_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t to_kbps(uint32_t bytes, uint32_t cycles)
{
    return (0U == cycles) ? 0U : (uint32_t)(((uint64_t)bytes * SystemCoreClock) / ((uint64_t)cycles * 1024U));
}

/* pseudo-random content derived from the page index, so any page can be verified without keeping a copy */
static void fill_page(uint8_t *buf, uint16_t size, uint32_t page)
{
    uint16_t lfsr = (uint16_t)(0xACE1U ^ (page * 0x9E37U));

    if(0U == lfsr) {
        lfsr = 1U;
    }
    for(uint32_t i = 0U; i < size; i++) {
        lfsr = (uint16_t)((lfsr >> 1) ^ ((lfsr & 1U) ? 0xB400U : 0U));
        buf[i] = (uint8_t)lfsr;
    }
}

/*!
    \brief    erase the test block and program every page of it
    \param[in]  nand: device info
    \param[in]  program: single or quad page program function
    \param[out] cycles: CPU cycles spent in program calls (erase excluded)
    \retval     ErrStatus
*/
static ErrStatus program_block(const spi_nand_info_t *nand, nand_program_fn program, uint32_t *cycles)
{
    const uint32_t first = NAND_TEST_BLOCK * nand->pages_per_block;

    *cycles = 0U;
    if(ERROR == spi_nand_block_erase(NAND_TEST_BLOCK)) {
        return ERROR;
    }
    for(uint32_t p = 0U; p < nand->pages_per_block; p++) {
        uint32_t t;
        ErrStatus st;

        fill_page(tx_buffer, nand->page_size, first + p);
        t = DWT->CYCCNT;
        st = program(first + p, 0U, tx_buffer, nand->page_size);
        *cycles += DWT->CYCCNT - t;
        if(ERROR == st) {
            return ERROR;
        }
    }
    return SUCCESS;
}

/*!
    \brief    read back every page of the test block and verify it
    \param[in]  nand: device info
    \param[in]  read: single, dual or quad page read function
    \param[out] cycles: CPU cycles spent in read calls
    \retval     ErrStatus
*/
static ErrStatus verify_block(const spi_nand_info_t *nand, nand_read_fn read, uint32_t *cycles)
{
    const uint32_t first = NAND_TEST_BLOCK * nand->pages_per_block;

    *cycles = 0U;
    for(uint32_t p = 0U; p < nand->pages_per_block; p++) {
        uint32_t t;
        ErrStatus st;

        t = DWT->CYCCNT;
        st = read(first + p, 0U, rx_buffer, nand->page_size);
        *cycles += DWT->CYCCNT - t;
        fill_page(tx_buffer, nand->page_size, first + p);
        if((ERROR == st) || (0 != memcmp(tx_buffer, rx_buffer, nand->page_size))) {
            return ERROR;
        }
    }
    return SUCCESS;
}

static void print_row(const char *mode, ErrStatus ok, uint32_t bytes, uint32_t prog_cycles, uint32_t read_cycles)
{
    char prog[12] = "-";

    if(0U != prog_cycles) {
        snprintf(prog, sizeof(prog), "%lu", (unsigned long)to_kbps(bytes, prog_cycles));
    }
    printf("%-18s %10s %10lu   %s\r\n", mode, prog, (unsigned long)to_kbps(bytes, read_cycles),
           (SUCCESS == ok) ? "PASS" : "FAIL");
}

/* compare cr_buffer against the expected content of consecutive pages */
static ErrStatus check_cr_buffer(const spi_nand_info_t *nand, uint32_t first, uint32_t pages)
{
    for(uint32_t p = 0U; p < pages; p++) {
        fill_page(tx_buffer, nand->page_size, first + p);
        if(0 != memcmp(tx_buffer, &cr_buffer[p * nand->page_size], nand->page_size)) {
            return ERROR;
        }
    }
    return SUCCESS;
}

static void print_cr_row(const char *lines, const char *mode, uint32_t cycles, uint32_t bytes, ErrStatus ok)
{
    printf("%-7s %-11s %9lu %9lu   %s\r\n", lines, mode,
           (unsigned long)(cycles / (SystemCoreClock / 1000000U)), (unsigned long)to_kbps(bytes, cycles),
           (SUCCESS == ok) ? "PASS" : "FAIL");
}

/*!
    \brief    Continuous Read demo (e.g. GD5F1GM9): read the same pages with Normal Read (13h + read
              from cache per page) and with Continuous Read (one 13h, one CS-low stream), in single
              and quad mode; verify the data and compare the time
    \param[in]  nand: device info
    \param[out] none
    \retval     none
*/
static void continuous_read_demo(const spi_nand_info_t *nand)
{
    static const struct {
        const char *lines;
        nand_read_fn page_read;
        nand_stream_fn stream;
    } modes[] = {
        { "single", spi_nand_page_read,      spi_nand_continuous_read },
        { "quad",   spi_nand_quad_page_read, spi_nand_quad_continuous_read },
    };
    const uint32_t first = NAND_TEST_BLOCK * nand->pages_per_block;
    uint32_t pages = CR_BUFFER_SIZE / nand->page_size;
    uint32_t bytes, cycles, t;
    ErrStatus st;

    printf("\r\n===== Continuous Read demo =====\r\n");
    if(ERROR == spi_nand_set_continuous_read(1U)) {
        printf("%s does not support Continuous Read, skipped\r\n", nand->name);
        return;
    }
    /* program in Normal Read mode */
    (void)spi_nand_set_continuous_read(0U);

    /* Continuous Read cannot skip bad blocks, so keep the region inside the test block */
    if(pages > nand->pages_per_block) {
        pages = nand->pages_per_block;
    }
    bytes = pages * nand->page_size;
    if(ERROR == program_block(nand, spi_nand_quad_page_program, &cycles)) {
        printf("program test block FAIL\r\n");
        return;
    }

    printf("Region: block %u, %lu pages, %lu KB\r\n\r\n", NAND_TEST_BLOCK,
           (unsigned long)pages, (unsigned long)(bytes / 1024U));
    printf("%-7s %-11s %9s %9s   %s\r\n", "lines", "mode", "time us", "KB/s", "verify");

    for(uint32_t m = 0U; m < sizeof(modes) / sizeof(modes[0]); m++) {
        uint32_t normal_cycles;

        /* Normal Read: one 13h + read from cache per page */
        memset(cr_buffer, 0, bytes);
        st = SUCCESS;
        t = DWT->CYCCNT;
        for(uint32_t p = 0U; (p < pages) && (SUCCESS == st); p++) {
            st = modes[m].page_read(first + p, 0U, &cr_buffer[p * nand->page_size], nand->page_size);
        }
        normal_cycles = DWT->CYCCNT - t;
        print_cr_row(modes[m].lines, "normal", normal_cycles, bytes,
                     (SUCCESS == st) ? check_cr_buffer(nand, first, pages) : ERROR);

        /* Continuous Read: one 13h, then a single stream across all pages */
        memset(cr_buffer, 0, bytes);
        cycles = 0U;
        st = spi_nand_set_continuous_read(1U);
        if(SUCCESS == st) {
            t = DWT->CYCCNT;
            st = modes[m].stream(first, cr_buffer, bytes);
            cycles = DWT->CYCCNT - t;
        }
        (void)spi_nand_set_continuous_read(0U);
        print_cr_row(modes[m].lines, "continuous", cycles, bytes,
                     (SUCCESS == st) ? check_cr_buffer(nand, first, pages) : ERROR);

        if(0U != cycles) {
            const uint32_t x100 = (uint32_t)((uint64_t)normal_cycles * 100U / cycles);
            printf("%-7s speed-up %lu.%02lux\r\n\r\n", "", (unsigned long)(x100 / 100U), (unsigned long)(x100 % 100U));
        }
    }
}

/*!
    \brief    QSPI demo: program one block in single / quad mode, read it back in single / dual / quad
              mode, verify and report throughput
    \param[in]  nand: device info
    \param[out] none
    \retval     none
*/
static void qspi_flash_demo(const spi_nand_info_t *nand)
{
    uint32_t bytes, sck_khz, prog_cycles, read_cycles;
    ErrStatus ok, prog_ok;

    bytes = (uint32_t)nand->page_size * nand->pages_per_block;
    sck_khz = rcu_clock_freq_get(CK_APB2) / (2U << ((SPI_CTL0(SPI_NAND_SPI) & SPI_CTL0_PSC) >> 3)) / 1000U;

    printf("\r\n===== Nand QSPI demo =====\r\n");
    printf("Test area: block %u, %lu KB, SCK %lu kHz\r\n\r\n",
           NAND_TEST_BLOCK, (unsigned long)(bytes / 1024U), (unsigned long)sck_khz);
    printf("%-18s %10s %10s   %s\r\n", "mode", "prog KB/s", "read KB/s", "verify");

    /* single-line program + single-line read */
    read_cycles = 0U;
    ok = program_block(nand, spi_nand_page_program, &prog_cycles);
    if(SUCCESS == ok) {
        ok = verify_block(nand, spi_nand_page_read, &read_cycles);
    }
    print_row("single (1-1-1)", ok, bytes, prog_cycles, read_cycles);

    /* quad program, then read the same data back with dual and quad output */
    read_cycles = 0U;
    prog_ok = program_block(nand, spi_nand_quad_page_program, &prog_cycles);
    ok = (SUCCESS == prog_ok) ? verify_block(nand, spi_nand_dual_page_read, &read_cycles) : ERROR;
    print_row("dual read (1-1-2)", ok, bytes, 0U, read_cycles);
    read_cycles = 0U;
    ok = (SUCCESS == prog_ok) ? verify_block(nand, spi_nand_quad_page_read, &read_cycles) : ERROR;
    print_row("quad (1-1-4)", ok, bytes, prog_cycles, read_cycles);
}

/*!
    \brief    main function
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
    const spi_nand_info_t *nand;

    gd_eval_com_init(EVAL_COM0);
    systick_config();

    cycle_counter_init();
    nand = spi_nand_init();
    printf("\r\nSPI NAND: %s (ID 0x%04X), page %u+%u B, %u pages/block, %u blocks\r\n",
           nand->name, spi_nand_read_id(), nand->page_size, nand->spare_size,
           nand->pages_per_block, nand->block_count);
    qspi_flash_demo(nand);
    continuous_read_demo(nand);

    while(1) {
    }
}

