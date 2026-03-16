/**
 * @file cyhal_hw_types.h
 * @brief HAL hardware types for WHD SDIO interface
 *
 * Local copy - does not include cy_result.h (must be included before this)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** GPIO object */
typedef uint32_t cyhal_gpio_t;

/** Clock divider object */
typedef struct {
    void *div_type;
} cyhal_clock_divider_t;

/** SDIO object */
typedef struct {
    void *empty;
} cyhal_sdio_t;

/** SPI object */
typedef struct {
    void *empty;
} cyhal_spi_t;

/** M2M/DMA object */
typedef struct {
    void *empty;
} cyhal_m2m_t;

#ifdef __cplusplus
}
#endif
