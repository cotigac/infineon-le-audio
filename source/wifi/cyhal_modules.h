/**
 * @file cyhal_modules.h
 * @brief HAL module definitions for result codes
 *
 * Local copy - does not include cy_result.h (must be included before this)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/* cy_result.h must be included before this header */
#ifndef CY_RSLT_MODULE_ABSTRACTION_HAL_BASE
#error "cy_result.h must be included before cyhal_modules.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Enum to indicate which module an error occurred in. */
enum cyhal_rslt_module_chip
{
    CYHAL_RSLT_MODULE_CHIP_HWMGR = CY_RSLT_MODULE_ABSTRACTION_HAL_BASE,
    CYHAL_RSLT_MODULE_ADC,
    CYHAL_RSLT_MODULE_COMP,
    CYHAL_RSLT_MODULE_CRC,
    CYHAL_RSLT_MODULE_DAC,
    CYHAL_RSLT_MODULE_DMA,
    CYHAL_RSLT_MODULE_FLASH,
    CYHAL_RSLT_MODULE_GPIO,
    CYHAL_RSLT_MODULE_I2C,
    CYHAL_RSLT_MODULE_I2S,
    CYHAL_RSLT_MODULE_INTERCONNECT,
    CYHAL_RSLT_MODULE_OPAMP,
    CYHAL_RSLT_MODULE_PDMPCM,
    CYHAL_RSLT_MODULE_PWM,
    CYHAL_RSLT_MODULE_QSPI,
    CYHAL_RSLT_MODULE_RTC,
    CYHAL_RSLT_MODULE_SDHC,
    CYHAL_RSLT_MODULE_SDIO,
    CYHAL_RSLT_MODULE_SPI,
    CYHAL_RSLT_MODULE_SYSTEM,
    CYHAL_RSLT_MODULE_TIMER,
    CYHAL_RSLT_MODULE_TRNG,
    CYHAL_RSLT_MODULE_UART,
    CYHAL_RSLT_MODULE_USB,
    CYHAL_RSLT_MODULE_WDT,
};

#ifdef __cplusplus
}
#endif
