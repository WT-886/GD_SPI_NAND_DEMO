#include "gd5f_spi_nand_bad_block_test.h"
#include <stdio.h>

ErrStatus spi_nand_bad_block_scan(const spi_nand_info_t *nand, uint32_t first_block, uint32_t count)
{
    uint32_t unavailable = 0U;

    if((NULL == nand) || (first_block >= nand->block_count) ||
    (count > (uint32_t)nand->block_count - first_block)) {
     printf("Bad-block scan: invalid block range\r\n");
        return ERROR;
    }
    if(0U == count) {
        count = (uint32_t)nand->block_count - first_block;
    }

    printf("\r\nBad-block check: blocks %lu..%lu\r\n",
           (unsigned long)first_block, (unsigned long)(first_block + count - 1U));
    for(uint32_t block = first_block; block < first_block + count; block++) {
        if(ERROR == spi_nand_block_check_bad(block)) {
            unavailable++;
            printf("Block %lu: bad marker or marker check failed\r\n", (unsigned long)block);
        } else if(1U == count) {
            printf("Block %lu: good marker\r\n", (unsigned long)block);
        }
    }
    printf("Bad-block check summary: checked %lu, bad/unreadable %lu\r\n",
           (unsigned long)count, (unsigned long)unavailable);
    return unavailable ? ERROR : SUCCESS;
}
