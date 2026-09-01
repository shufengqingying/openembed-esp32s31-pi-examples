/**
 * @file sdmmc_example.h
 * @brief SDMMC card example component
 *
 * This module demonstrates SDMMC card initialization and basic information reading.
 * Supports SD, SDHC, and SDXC cards via SDMMC host interface.
 */

#ifndef SDMMC_EXAMPLE_H
#define SDMMC_EXAMPLE_H

#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_SDMMC_EXAMPLE_ENABLE

/**
 * @brief Initialize the SDMMC example component
 *
 * Initializes SDMMC host, detects and mounts the SD card,
 * and prints basic card information such as capacity, sector size, etc.
 */
void sdmmc_example_init(void);

#else

/**
 * @brief Empty stub when component is disabled
 */
#define sdmmc_example_init() do {} while(0)

#endif /* CONFIG_SDMMC_EXAMPLE_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* SDMMC_EXAMPLE_H */