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
#include "task.h"

/* PSoC Edge SDIO HAL headers */
#include "cyhal.h"
#include "cyhal_sdio.h"
#include "cybsp.h"

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

    /* PSoC Edge SDIO HAL handle */
    cyhal_sdio_t sdio_obj;

    /* Current configured frequency */
    uint32_t current_freq_hz;

    /* FreeRTOS synchronization for thread-safe bus access */
    SemaphoreHandle_t bus_mutex;

    /* Async transfer state */
    volatile bool async_pending;
    wifi_sdio_callback_t async_callback;
    void *async_user_data;
} wifi_sdio_state_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static wifi_sdio_state_t sdio_state = {0};

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief SDIO interrupt callback handler
 */
static void sdio_irq_handler(void *callback_arg, cyhal_sdio_event_t event)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)callback_arg;

    if (event & CYHAL_SDIO_CARD_INTERRUPT) {
        /* Card interrupt - signal Wi-Fi driver */
        /* This is handled by WHD via its registered callback */
    }

    if (event & CYHAL_SDIO_XFER_COMPLETE) {
        /* Async transfer complete - release mutex from ISR context */
        if (sdio_state.async_pending) {
            wifi_sdio_callback_t callback = sdio_state.async_callback;
            void *user_data = sdio_state.async_user_data;

            sdio_state.async_pending = false;
            sdio_state.async_callback = NULL;
            sdio_state.async_user_data = NULL;

            /* Release bus mutex from ISR */
            xSemaphoreGiveFromISR(sdio_state.bus_mutex, &higher_priority_task_woken);

            /* Call user callback */
            if (callback != NULL) {
                callback(true, user_data);
            }
        }
    }

    if (event & CYHAL_SDIO_ERR_INTERRUPT) {
        /* Error during async transfer */
        sdio_state.stats.errors++;
        if (sdio_state.async_pending) {
            wifi_sdio_callback_t callback = sdio_state.async_callback;
            void *user_data = sdio_state.async_user_data;

            sdio_state.async_pending = false;
            sdio_state.async_callback = NULL;
            sdio_state.async_user_data = NULL;

            /* Release bus mutex from ISR */
            xSemaphoreGiveFromISR(sdio_state.bus_mutex, &higher_priority_task_woken);

            /* Call user callback with error */
            if (callback != NULL) {
                callback(false, user_data);
            }
        }
    }

    /* Yield if a higher priority task was woken */
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief Wait for SDIO ready by polling I/O Ready register
 */
static int sdio_wait_ready(uint32_t timeout_ms)
{
    uint8_t io_ready = 0;
    uint32_t start_tick = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    cy_rslt_t result;
    uint32_t response;

    while ((xTaskGetTickCount() - start_tick) < timeout_ticks) {
        /* Read I/O Ready register (CCCR offset 0x03) */
        result = cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                                     CYHAL_SDIO_CMD_IO_RW_DIRECT,
                                     (0 << 28) |                    /* Function 0 */
                                     (SDIO_CCCR_IO_READY << 9),     /* Address */
                                     &response);
        if (result == CY_RSLT_SUCCESS) {
            io_ready = (uint8_t)(response & 0xFF);
            /* Check if Function 1 (WLAN) is ready */
            if (io_ready & 0x02) {
                return 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    sdio_state.stats.timeout_errors++;
    return -1;
}

/**
 * @brief Get frequency for speed mode
 */
static uint32_t get_freq_for_speed(wifi_sdio_speed_t speed)
{
    switch (speed) {
        case WIFI_SDIO_SPEED_DEFAULT: return 25000000;   /* 25 MHz */
        case WIFI_SDIO_SPEED_HIGH:    return 50000000;   /* 50 MHz */
        case WIFI_SDIO_SPEED_SDR50:   return 100000000;  /* 100 MHz */
        case WIFI_SDIO_SPEED_DDR50:   return 50000000;   /* 50 MHz DDR */
        case WIFI_SDIO_SPEED_SDR104:  return 208000000;  /* 208 MHz */
        default:                       return 25000000;
    }
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int wifi_sdio_init(const wifi_sdio_config_t *config)
{
    cy_rslt_t result;
    cyhal_sdio_cfg_t sdio_cfg;

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

    /* Reset statistics and async state */
    memset(&sdio_state.stats, 0, sizeof(wifi_sdio_stats_t));
    sdio_state.async_pending = false;
    sdio_state.async_callback = NULL;
    sdio_state.async_user_data = NULL;

    /* Create FreeRTOS mutex for thread-safe bus access */
    sdio_state.bus_mutex = xSemaphoreCreateMutex();
    if (sdio_state.bus_mutex == NULL) {
        return -2;  /* FreeRTOS resource allocation failed */
    }

    /* Initialize SDIO HAL with Wi-Fi chip pins */
    result = cyhal_sdio_init(&sdio_state.sdio_obj,
                             CYBSP_WIFI_SDIO_CMD,
                             CYBSP_WIFI_SDIO_CLK,
                             CYBSP_WIFI_SDIO_DATA0,
                             CYBSP_WIFI_SDIO_DATA1,
                             CYBSP_WIFI_SDIO_DATA2,
                             CYBSP_WIFI_SDIO_DATA3);
    if (result != CY_RSLT_SUCCESS) {
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
        return -3;
    }

    /* Configure SDIO bus parameters */
    sdio_state.current_freq_hz = get_freq_for_speed(sdio_state.config.speed);
    sdio_cfg.frequencyhal_hz = sdio_state.current_freq_hz;
    sdio_cfg.block_size = sdio_state.config.block_size;

    result = cyhal_sdio_configure(&sdio_state.sdio_obj, &sdio_cfg);
    if (result != CY_RSLT_SUCCESS) {
        cyhal_sdio_free(&sdio_state.sdio_obj);
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
        return -4;
    }

    /* Register interrupt callback for async transfers and card interrupts */
    cyhal_sdio_register_callback(&sdio_state.sdio_obj, sdio_irq_handler, NULL);
    cyhal_sdio_enable_event(&sdio_state.sdio_obj,
                            CYHAL_SDIO_CARD_INTERRUPT |
                            CYHAL_SDIO_XFER_COMPLETE |
                            CYHAL_SDIO_ERR_INTERRUPT,
                            CYHAL_ISR_PRIORITY_DEFAULT, true);

    /* Enable I/O Function 1 (WLAN) */
    uint32_t response;
    result = cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                                 CYHAL_SDIO_CMD_IO_RW_DIRECT,
                                 (1 << 31) |                    /* Write */
                                 (0 << 28) |                    /* Function 0 */
                                 (SDIO_CCCR_IO_ENABLE << 9) |   /* Address */
                                 0x02,                          /* Enable Function 1 */
                                 &response);
    if (result != CY_RSLT_SUCCESS) {
        cyhal_sdio_free(&sdio_state.sdio_obj);
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
        return -5;
    }

    /* Wait for Function 1 to become ready */
    if (sdio_wait_ready(SDIO_CMD_TIMEOUT_MS) != 0) {
        cyhal_sdio_free(&sdio_state.sdio_obj);
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
        return -6;
    }

    /* Set 4-bit bus width if configured */
    if (sdio_state.config.bus_width == WIFI_SDIO_BUS_WIDTH_4BIT) {
        result = cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                                     CYHAL_SDIO_CMD_IO_RW_DIRECT,
                                     (1 << 31) |                    /* Write */
                                     (0 << 28) |                    /* Function 0 */
                                     (SDIO_CCCR_BUS_CONTROL << 9) | /* Address */
                                     0x02,                          /* 4-bit mode */
                                     &response);
        if (result != CY_RSLT_SUCCESS) {
            /* Non-fatal: continue with 1-bit mode */
            sdio_state.config.bus_width = WIFI_SDIO_BUS_WIDTH_1BIT;
        }
    }

    /* Set block size for Function 1 */
    /* FBR1 block size low byte at 0x110, high byte at 0x111 */
    uint32_t block_size = sdio_state.config.block_size;
    cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                        CYHAL_SDIO_CMD_IO_RW_DIRECT,
                        (1 << 31) | (0 << 28) | (0x110 << 9) | (block_size & 0xFF),
                        &response);
    cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                        CYHAL_SDIO_CMD_IO_RW_DIRECT,
                        (1 << 31) | (0 << 28) | (0x111 << 9) | ((block_size >> 8) & 0xFF),
                        &response);

    sdio_state.initialized = true;
    return 0;
}

void wifi_sdio_deinit(void)
{
    if (!sdio_state.initialized) {
        return;
    }

    /* Wait for any pending async transfer */
    uint32_t timeout = 100;
    while (sdio_state.async_pending && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    /* Disable interrupts */
    cyhal_sdio_enable_event(&sdio_state.sdio_obj,
                            CYHAL_SDIO_CARD_INTERRUPT |
                            CYHAL_SDIO_XFER_COMPLETE |
                            CYHAL_SDIO_ERR_INTERRUPT,
                            CYHAL_ISR_PRIORITY_DEFAULT, false);

    /* Disable I/O Function 1 */
    uint32_t response;
    cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                        CYHAL_SDIO_CMD_IO_RW_DIRECT,
                        (1 << 31) |                    /* Write */
                        (0 << 28) |                    /* Function 0 */
                        (SDIO_CCCR_IO_ENABLE << 9) |   /* Address */
                        0x00,                          /* Disable all functions */
                        &response);

    /* Free SDIO HAL resources */
    cyhal_sdio_free(&sdio_state.sdio_obj);

    /* Delete FreeRTOS mutex */
    if (sdio_state.bus_mutex != NULL) {
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
    }

    /* Reset async state */
    sdio_state.async_pending = false;
    sdio_state.async_callback = NULL;
    sdio_state.async_user_data = NULL;

    sdio_state.initialized = false;
}

int wifi_sdio_cmd52(bool write, uint8_t function, uint32_t address,
                    uint8_t data, uint8_t *response)
{
    cy_rslt_t result;
    uint32_t cmd_response = 0;
    uint32_t argument;

    if (!sdio_state.initialized) {
        return -1;
    }

    /* Acquire bus mutex for thread safety */
    if (xSemaphoreTake(sdio_state.bus_mutex, pdMS_TO_TICKS(SDIO_CMD_TIMEOUT_MS)) != pdTRUE) {
        sdio_state.stats.timeout_errors++;
        return -2;
    }

    /* Build CMD52 argument:
     * [31]    R/W flag (1=write, 0=read)
     * [30:28] Function number
     * [27]    RAW flag (read after write)
     * [25:9]  Register address
     * [7:0]   Write data
     */
    argument = (write ? (1UL << 31) : 0) |
               ((uint32_t)(function & 0x7) << 28) |
               ((address & 0x1FFFF) << 9) |
               (data & 0xFF);

    /* Send CMD52 (IO_RW_DIRECT) */
    result = cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                                 CYHAL_SDIO_CMD_IO_RW_DIRECT,
                                 argument,
                                 &cmd_response);

    xSemaphoreGive(sdio_state.bus_mutex);

    if (result != CY_RSLT_SUCCESS) {
        sdio_state.stats.errors++;
        return -3;
    }

    sdio_state.stats.commands_sent++;

    if (write) {
        sdio_state.stats.bytes_sent++;
    } else {
        sdio_state.stats.bytes_received++;
    }

    /* Extract response byte from R5 response */
    if (response != NULL) {
        *response = (uint8_t)(cmd_response & 0xFF);
    }

    /* Check for errors in R5 response flags [15:8] */
    uint8_t flags = (uint8_t)((cmd_response >> 8) & 0xFF);
    if (flags & 0xCB) {  /* COM_CRC_ERROR, ILLEGAL_COMMAND, ERROR, FUNCTION_NUMBER */
        sdio_state.stats.errors++;
        return -4;
    }

    return 0;
}

int wifi_sdio_cmd53(bool write, uint8_t function, uint32_t address,
                    bool increment, uint8_t *buffer, uint32_t length)
{
    cy_rslt_t result;
    cyhal_sdio_transfer_type_t xfer_type;

    if (!sdio_state.initialized || buffer == NULL || length == 0) {
        return -1;
    }

    /* Acquire bus mutex for thread safety */
    if (xSemaphoreTake(sdio_state.bus_mutex, pdMS_TO_TICKS(SDIO_CMD_TIMEOUT_MS)) != pdTRUE) {
        sdio_state.stats.timeout_errors++;
        return -2;
    }

    /* Determine transfer type */
    xfer_type = write ? CYHAL_SDIO_XFER_TYPE_WRITE : CYHAL_SDIO_XFER_TYPE_READ;

    /* Perform bulk transfer using CMD53 (IO_RW_EXTENDED) */
    result = cyhal_sdio_bulk_transfer(&sdio_state.sdio_obj,
                                      xfer_type,
                                      function,
                                      address,
                                      buffer,
                                      length,
                                      NULL);  /* No response needed for CMD53 */

    xSemaphoreGive(sdio_state.bus_mutex);

    if (result != CY_RSLT_SUCCESS) {
        sdio_state.stats.errors++;

        /* Check for specific error types */
        if (CY_RSLT_GET_CODE(result) == CYHAL_SDIO_RSLT_ERR_CRC) {
            sdio_state.stats.crc_errors++;
        }

        return -3;
    }

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
    cy_rslt_t result;
    cyhal_sdio_transfer_type_t xfer_type;

    if (!sdio_state.initialized || buffer == NULL || length == 0) {
        return -1;
    }

    /* Check if another async transfer is in progress */
    if (sdio_state.async_pending) {
        return -2;
    }

    /* Acquire bus mutex for thread safety */
    if (xSemaphoreTake(sdio_state.bus_mutex, pdMS_TO_TICKS(SDIO_CMD_TIMEOUT_MS)) != pdTRUE) {
        sdio_state.stats.timeout_errors++;
        return -3;
    }

    /* Store callback info for IRQ handler */
    sdio_state.async_callback = callback;
    sdio_state.async_user_data = user_data;
    sdio_state.async_pending = true;

    /* Determine transfer type */
    xfer_type = write ? CYHAL_SDIO_XFER_TYPE_WRITE : CYHAL_SDIO_XFER_TYPE_READ;

    /* Start async bulk transfer using CMD53 with DMA */
    result = cyhal_sdio_bulk_transfer_async(&sdio_state.sdio_obj,
                                            xfer_type,
                                            function,
                                            address,
                                            buffer,
                                            length);

    if (result != CY_RSLT_SUCCESS) {
        /* Async start failed, clean up and fall back to sync */
        sdio_state.async_pending = false;
        sdio_state.async_callback = NULL;
        sdio_state.async_user_data = NULL;
        xSemaphoreGive(sdio_state.bus_mutex);

        /* Fall back to synchronous transfer */
        int sync_result = wifi_sdio_cmd53(write, function, address, true, buffer, length);
        if (callback != NULL) {
            callback(sync_result == 0, user_data);
        }
        return sync_result;
    }

    /* Note: bus_mutex will be released when async callback fires */
    /* The callback handles releasing the mutex in the IRQ handler */

    sdio_state.stats.commands_sent++;

    if (write) {
        sdio_state.stats.bytes_sent += length;
    } else {
        sdio_state.stats.bytes_received += length;
    }

    return 0;
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
    cy_rslt_t result;
    uint32_t freq_hz;
    uint8_t speed_reg = 0;
    uint8_t response;

    if (!sdio_state.initialized) {
        return -1;
    }

    /* Get target frequency */
    freq_hz = get_freq_for_speed(speed);

    /* Acquire bus mutex */
    if (xSemaphoreTake(sdio_state.bus_mutex, pdMS_TO_TICKS(SDIO_CMD_TIMEOUT_MS)) != pdTRUE) {
        return -2;
    }

    /* Read current CCCR Speed register */
    uint32_t cmd_response;
    result = cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                                 CYHAL_SDIO_CMD_IO_RW_DIRECT,
                                 (0 << 28) | (SDIO_CCCR_SPEED << 9),
                                 &cmd_response);
    if (result != CY_RSLT_SUCCESS) {
        xSemaphoreGive(sdio_state.bus_mutex);
        return -3;
    }

    speed_reg = (uint8_t)(cmd_response & 0xFF);

    /* Set Bus Speed Select (BSS) bits [3:1] based on speed mode */
    speed_reg &= ~0x0E;  /* Clear BSS bits */
    switch (speed) {
        case WIFI_SDIO_SPEED_DEFAULT:
            speed_reg |= (0 << 1);  /* SDR12 */
            break;
        case WIFI_SDIO_SPEED_HIGH:
            speed_reg |= (1 << 1);  /* SDR25 / High Speed */
            break;
        case WIFI_SDIO_SPEED_SDR50:
            speed_reg |= (2 << 1);  /* SDR50 */
            break;
        case WIFI_SDIO_SPEED_DDR50:
            speed_reg |= (3 << 1);  /* DDR50 */
            break;
        case WIFI_SDIO_SPEED_SDR104:
            speed_reg |= (4 << 1);  /* SDR104 */
            break;
        default:
            xSemaphoreGive(sdio_state.bus_mutex);
            return -4;
    }

    /* Write speed register */
    result = cyhal_sdio_send_cmd(&sdio_state.sdio_obj,
                                 CYHAL_SDIO_CMD_IO_RW_DIRECT,
                                 (1 << 31) |                   /* Write */
                                 (0 << 28) |                   /* Function 0 */
                                 (SDIO_CCCR_SPEED << 9) |      /* Address */
                                 speed_reg,
                                 &cmd_response);
    if (result != CY_RSLT_SUCCESS) {
        xSemaphoreGive(sdio_state.bus_mutex);
        return -5;
    }

    /* Reconfigure HAL frequency */
    result = cyhal_sdio_set_frequency(&sdio_state.sdio_obj, freq_hz, false);
    if (result != CY_RSLT_SUCCESS) {
        xSemaphoreGive(sdio_state.bus_mutex);
        return -6;
    }

    sdio_state.config.speed = speed;
    sdio_state.current_freq_hz = freq_hz;

    xSemaphoreGive(sdio_state.bus_mutex);
    return 0;
}

int wifi_sdio_set_bus_width(wifi_sdio_bus_width_t width)
{
    uint8_t reg_val = 0;
    uint8_t response;
    int result;
    cy_rslt_t hal_result;

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
    if (result != 0) {
        return result;
    }

    /* Update HAL bus width setting */
    if (xSemaphoreTake(sdio_state.bus_mutex, pdMS_TO_TICKS(SDIO_CMD_TIMEOUT_MS)) != pdTRUE) {
        return -2;
    }

    cyhal_sdio_cfg_t sdio_cfg;
    sdio_cfg.frequencyhal_hz = sdio_state.current_freq_hz;
    sdio_cfg.block_size = sdio_state.config.block_size;

    hal_result = cyhal_sdio_configure(&sdio_state.sdio_obj, &sdio_cfg);

    xSemaphoreGive(sdio_state.bus_mutex);

    if (hal_result == CY_RSLT_SUCCESS) {
        sdio_state.config.bus_width = width;
        return 0;
    }

    return -3;
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
