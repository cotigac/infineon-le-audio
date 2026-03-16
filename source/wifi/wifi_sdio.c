/**
 * @file wifi_sdio.c
 * @brief SDIO Driver Implementation for CYW55512 Wi-Fi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_sdio.h"
#include <string.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "semphr.h"

/* TODO: Include PSoC Edge SDIO HAL headers */
/* #include "cy_sd_host.h" */
/* #include "cyhal_sdio.h" */

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** SDIO Function 0 (CIA) registers */
#define SDIO_CCCR_REVISION      0x00
#define SDIO_CCCR_SD_SPEC       0x01
#define SDIO_CCCR_IO_ENABLE     0x02
#define SDIO_CCCR_IO_READY      0x03
#define SDIO_CCCR_INT_ENABLE    0x04
#define SDIO_CCCR_INT_PENDING   0x05
#define SDIO_CCCR_ABORT         0x06
#define SDIO_CCCR_BUS_CONTROL   0x07
#define SDIO_CCCR_CARD_CAPS     0x08
#define SDIO_CCCR_SPEED         0x13

/** SDIO command timeout (ms) */
#define SDIO_CMD_TIMEOUT_MS     1000

/** Maximum block count per transfer */
#define SDIO_MAX_BLOCK_COUNT    511

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/** SDIO driver state */
typedef struct {
    bool initialized;
    wifi_sdio_config_t config;
    wifi_sdio_stats_t stats;
    /* TODO: Add HAL handle */
    /* cyhal_sdio_t sdio_obj; */

    /* FreeRTOS synchronization for thread-safe bus access */
    SemaphoreHandle_t bus_mutex;
} wifi_sdio_state_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static wifi_sdio_state_t sdio_state = {0};

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief Wait for SDIO ready
 */
static int sdio_wait_ready(uint32_t timeout_ms)
{
    /* TODO: Implement with actual HAL */
    (void)timeout_ms;
    return 0;
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int wifi_sdio_init(const wifi_sdio_config_t *config)
{
    if (sdio_state.initialized) {
        return -1; /* Already initialized */
    }

    /* Use default config if not provided */
    if (config != NULL) {
        memcpy(&sdio_state.config, config, sizeof(wifi_sdio_config_t));
    } else {
        wifi_sdio_config_t default_config = WIFI_SDIO_CONFIG_DEFAULT;
        memcpy(&sdio_state.config, &default_config, sizeof(wifi_sdio_config_t));
    }

    /* Reset statistics */
    memset(&sdio_state.stats, 0, sizeof(wifi_sdio_stats_t));

    /* Create FreeRTOS mutex for thread-safe bus access */
    sdio_state.bus_mutex = xSemaphoreCreateMutex();
    if (sdio_state.bus_mutex == NULL) {
        return -2;  /* FreeRTOS resource allocation failed */
    }

    /* TODO: Initialize SDIO HAL
     *
     * cyhal_sdio_cfg_t sdio_cfg = {
     *     .frequencyhal_hz = 100000000,  // 100 MHz for SDR50
     *     .block_size = sdio_state.config.block_size
     * };
     *
     * cy_rslt_t result = cyhal_sdio_init(&sdio_state.sdio_obj,
     *                                     CYBSP_WIFI_SDIO_CMD,
     *                                     CYBSP_WIFI_SDIO_CLK,
     *                                     CYBSP_WIFI_SDIO_DATA0,
     *                                     CYBSP_WIFI_SDIO_DATA1,
     *                                     CYBSP_WIFI_SDIO_DATA2,
     *                                     CYBSP_WIFI_SDIO_DATA3);
     * if (result != CY_RSLT_SUCCESS) {
     *     return -1;
     * }
     *
     * result = cyhal_sdio_configure(&sdio_state.sdio_obj, &sdio_cfg);
     * if (result != CY_RSLT_SUCCESS) {
     *     cyhal_sdio_free(&sdio_state.sdio_obj);
     *     return -1;
     * }
     */

    sdio_state.initialized = true;
    return 0;
}

void wifi_sdio_deinit(void)
{
    if (!sdio_state.initialized) {
        return;
    }

    /* TODO: Deinitialize SDIO HAL
     * cyhal_sdio_free(&sdio_state.sdio_obj);
     */

    /* Delete FreeRTOS mutex */
    if (sdio_state.bus_mutex != NULL) {
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
    }

    sdio_state.initialized = false;
}

int wifi_sdio_cmd52(bool write, uint8_t function, uint32_t address,
                    uint8_t data, uint8_t *response)
{
    if (!sdio_state.initialized) {
        return -1;
    }

    /* TODO: Implement CMD52 using HAL
     *
     * cyhal_sdio_transfer_type_t type = write ?
     *     CYHAL_SDIO_CMD_WRITE : CYHAL_SDIO_CMD_READ;
     *
     * uint32_t argument = (write ? 0x80000000 : 0) |
     *                     ((function & 0x7) << 28) |
     *                     ((address & 0x1FFFF) << 9) |
     *                     (data & 0xFF);
     *
     * cy_rslt_t result = cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
     *                                        type, 52, argument, response);
     */

    sdio_state.stats.commands_sent++;

    if (write) {
        sdio_state.stats.bytes_sent++;
    } else {
        sdio_state.stats.bytes_received++;
    }

    /* Placeholder - simulate success */
    if (response != NULL) {
        *response = 0;
    }

    return 0;
}

int wifi_sdio_cmd53(bool write, uint8_t function, uint32_t address,
                    bool increment, uint8_t *buffer, uint32_t length)
{
    if (!sdio_state.initialized || buffer == NULL || length == 0) {
        return -1;
    }

    /* TODO: Implement CMD53 using HAL
     *
     * cyhal_sdio_transfer_type_t type = write ?
     *     CYHAL_SDIO_XFER_TYPE_WRITE : CYHAL_SDIO_XFER_TYPE_READ;
     *
     * cy_rslt_t result = cyhal_sdio_bulk_transfer(&sdio_state.sdio_obj,
     *                                              type, function,
     *                                              address, buffer, length,
     *                                              NULL);
     */

    sdio_state.stats.commands_sent++;

    if (write) {
        sdio_state.stats.bytes_sent += length;
    } else {
        sdio_state.stats.bytes_received += length;
    }

    return 0;
}

int wifi_sdio_cmd53_block(bool write, uint8_t function, uint32_t address,
                          bool increment, uint8_t *buffer, uint32_t block_count)
{
    if (block_count > SDIO_MAX_BLOCK_COUNT) {
        return -1;
    }

    uint32_t length = block_count * sdio_state.config.block_size;
    return wifi_sdio_cmd53(write, function, address, increment, buffer, length);
}

int wifi_sdio_cmd53_async(bool write, uint8_t function, uint32_t address,
                          uint8_t *buffer, uint32_t length,
                          wifi_sdio_callback_t callback, void *user_data)
{
    if (!sdio_state.initialized || buffer == NULL || length == 0) {
        return -1;
    }

    /* TODO: Implement async transfer using DMA
     *
     * For now, fall back to synchronous transfer
     */
    int result = wifi_sdio_cmd53(write, function, address, true, buffer, length);

    if (callback != NULL) {
        callback(result == 0, user_data);
    }

    return result;
}

int wifi_sdio_enable_interrupt(uint8_t function, bool enable)
{
    uint8_t reg_val = 0;
    uint8_t response;
    int result;

    /* Read current interrupt enable register */
    result = wifi_sdio_cmd52(false, 0, SDIO_CCCR_INT_ENABLE, 0, &reg_val);
    if (result != 0) {
        return result;
    }

    /* Modify bit for this function */
    if (enable) {
        reg_val |= (1 << function) | 0x01; /* Enable function + master enable */
    } else {
        reg_val &= ~(1 << function);
    }

    /* Write back */
    result = wifi_sdio_cmd52(true, 0, SDIO_CCCR_INT_ENABLE, reg_val, &response);
    return result;
}

bool wifi_sdio_interrupt_pending(uint8_t function)
{
    uint8_t reg_val = 0;

    if (wifi_sdio_cmd52(false, 0, SDIO_CCCR_INT_PENDING, 0, &reg_val) != 0) {
        return false;
    }

    return (reg_val & (1 << function)) != 0;
}

int wifi_sdio_set_speed(wifi_sdio_speed_t speed)
{
    if (!sdio_state.initialized) {
        return -1;
    }

    sdio_state.config.speed = speed;

    /* TODO: Reconfigure SDIO clock based on speed mode
     *
     * uint32_t freq_hz;
     * switch (speed) {
     *     case WIFI_SDIO_SPEED_DEFAULT: freq_hz = 25000000; break;
     *     case WIFI_SDIO_SPEED_HIGH:    freq_hz = 50000000; break;
     *     case WIFI_SDIO_SPEED_SDR50:   freq_hz = 100000000; break;
     *     case WIFI_SDIO_SPEED_DDR50:   freq_hz = 50000000; break;
     *     case WIFI_SDIO_SPEED_SDR104:  freq_hz = 208000000; break;
     *     default: return -1;
     * }
     *
     * cyhal_sdio_set_frequency(&sdio_state.sdio_obj, freq_hz);
     */

    return 0;
}

int wifi_sdio_set_bus_width(wifi_sdio_bus_width_t width)
{
    uint8_t reg_val = 0;
    uint8_t response;
    int result;

    if (!sdio_state.initialized) {
        return -1;
    }

    /* Read bus control register */
    result = wifi_sdio_cmd52(false, 0, SDIO_CCCR_BUS_CONTROL, 0, &reg_val);
    if (result != 0) {
        return result;
    }

    /* Set bus width bits */
    reg_val &= ~0x03; /* Clear bus width bits */
    if (width == WIFI_SDIO_BUS_WIDTH_4BIT) {
        reg_val |= 0x02; /* 4-bit mode */
    }

    /* Write back */
    result = wifi_sdio_cmd52(true, 0, SDIO_CCCR_BUS_CONTROL, reg_val, &response);
    if (result == 0) {
        sdio_state.config.bus_width = width;
    }

    return result;
}

void wifi_sdio_get_stats(wifi_sdio_stats_t *stats)
{
    if (stats != NULL) {
        memcpy(stats, &sdio_state.stats, sizeof(wifi_sdio_stats_t));
    }
}

void wifi_sdio_reset_stats(void)
{
    memset(&sdio_state.stats, 0, sizeof(wifi_sdio_stats_t));
}

bool wifi_sdio_is_ready(void)
{
    return sdio_state.initialized;
}
