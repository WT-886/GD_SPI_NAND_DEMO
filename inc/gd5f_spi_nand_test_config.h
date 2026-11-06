#ifndef GD5F_SPI_NAND_TEST_CONFIG_H
#define GD5F_SPI_NAND_TEST_CONFIG_H

#include "gd5f_spi_nand.h"

/* Firmware test settings only. Block numbers are disposable test choices, not device geometry.
   Change settings here before building; no setting below changes existing chip BBM mappings. */

/* Each test is enabled independently. Disable all destructive tests for marker/table inspection. */
#define NAND_BAD_BLOCK_CHECK_ENABLE 1U  /* Read block markers; does not erase/program data. */
#define NAND_TIMING_TEST_ENABLE     1U  /* Erase/write the selected range and verify normal/CR reads. */
#define NAND_FULL_TEST_ENABLE       0U  /* Destructive random test of all marker-good blocks. */
#define NAND_BBM_DEMO_ENABLE        1U  /* Read A5h and inspect the candidate; execution is gated below. */

/* Marker scan: count=0 means from first block to the end; count=1 checks a single block. */
#define NAND_BAD_BLOCK_FIRST_BLOCK  0U
#define NAND_BAD_BLOCK_COUNT        0U

/* Timing test: reserve this entire range, because it will be erased and rewritten. */
#define NAND_TEST_BLOCK             10U
#define NAND_TEST_BLOCK_COUNT       20U
#define CR_BUFFER_SIZE              (64U * 1024U)

/* Full-chip test parameters; a fixed seed makes write/verify data reproducible. */
#define NAND_FULL_TEST_IO_MODE      SPI_NAND_IO_QUAD
#define FULL_TEST_CR_BUFFER_SIZE    CR_BUFFER_SIZE
#define FULL_TEST_RANDOM_SEED       0xA5C31E27U
#define PROGRESS_BLOCK_INTERVAL     64U

/* A1h consumes persistent links and erases the migration target. Choose both addresses explicitly.
   These example values are not proof that the target is good, free, or safe to erase. */
#ifndef SPI_NAND_BBM_DEMO_EXECUTE
#define SPI_NAND_BBM_DEMO_EXECUTE    0U
#endif
#ifndef SPI_NAND_BBM_DEMO_LBA
#define SPI_NAND_BBM_DEMO_LBA        511U
#endif
#ifndef SPI_NAND_BBM_DEMO_PBA
#define SPI_NAND_BBM_DEMO_PBA        1012U
#endif

#endif /* GD5F_SPI_NAND_TEST_CONFIG_H */
