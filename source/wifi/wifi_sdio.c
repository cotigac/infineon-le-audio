/**
 * @file wifi_sdio.c
 * @brief SDIO Driver Implementation for CYW55512 Wi-Fi
 *
 * Uses PSoC Edge PDL (cy_sd_host.h) with Device Configurator settings.
 * The BSP provides CYBSP_WIFI_SDIO_* configuration structures.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_sdio.h"
#include <string.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* PSoC Edge PDL headers */
#include "cy_pdl.h"
#include "cy_sd_host.h"
#include "cybsp.h"
#include "cycfg_peripherals.h"

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

/** SDIO command codes */
#define SDIO_CMD52              52  /* IO_RW_DIRECT */
#define SDIO_CMD53              53  /* IO_RW_EXTENDED */

/** Base clock frequency for SD Host (from BSP) */
#define SDIO_BASE_CLK_HZ        100000000UL  /* 100 MHz typical */

/** Polling timeout for command/transfer complete (iterations) */
#define SDIO_POLL_TIMEOUT       100000

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/** SDIO driver state */
typedef struct {
    bool initialized;
    wifi_sdio_config_t config;
    wifi_sdio_stats_t stats;

    /* Current configured frequency */
    uint32_t current_freq_hz;

    /* FreeRTOS synchronization for thread-safe bus access */
    SemaphoreHandle_t bus_mutex;

    /* SD Host context (PDL) */
    cy_stc_sd_host_context_t sd_host_context;

} wifi_sdio_state_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static wifi_sdio_state_t sdio_state = {0};

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief Wait for SDIO ready by polling I/O Ready register
 */
static int sdio_wait_ready(uint32_t timeout_ms)
{
    uint32_t start_tick = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    cy_en_sd_host_status_t result;
    uint32_t response = 0;

    while ((xTaskGetTickCount() - start_tick) < timeout_ticks) {
        /* Read I/O Ready register (CCCR offset 0x03) using CMD52 */
        cy_stc_sd_host_cmd_config_t cmd = {
            .commandIndex = SDIO_CMD52,
            .commandArgument = (0 << 28) |                    /* Function 0 */
                              (SDIO_CCCR_IO_READY << 9),     /* Address */
            .enableCrcCheck = true,
            .enableIdxCheck = true,
            .respType = CY_SD_HOST_RESPONSE_LEN_48,
            .enableAutoResponseErrorCheck = false,
            .cmdType = CY_SD_HOST_CMD_NORMAL,
            .dataPresent = false,
        };

        result = Cy_SD_Host_SendCommand(CYBSP_WIFI_SDIO_HW, &cmd);
        if (result == CY_SD_HOST_SUCCESS) {
            result = Cy_SD_Host_GetResponse(CYBSP_WIFI_SDIO_HW, &response, false);
            if (result == CY_SD_HOST_SUCCESS) {
                /* Check if Function 1 (WLAN) is ready */
                if ((response & 0xFF) & 0x02) {
                    return 0;
                }
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

/**
 * @brief Set SD clock frequency using clock divider
 *
 * PDL doesn't have Cy_SD_Host_SetSdClkFrequency(), so we calculate the divider
 * and use Cy_SD_Host_SetSdClkDiv() instead.
 */
static cy_en_sd_host_status_t set_sd_clk_frequency(SDHC_Type *base, uint32_t freq_hz)
{
    /* Calculate divider: div = base_clk / (2 * target_freq) */
    uint16_t clk_div;
    if (freq_hz >= SDIO_BASE_CLK_HZ) {
        clk_div = 0;  /* No division */
    } else {
        clk_div = (uint16_t)((SDIO_BASE_CLK_HZ + (2 * freq_hz) - 1) / (2 * freq_hz));
        if (clk_div > 0) {
            clk_div--;  /* Divider is (clk_div + 1) * 2 */
        }
    }
    return Cy_SD_Host_SetSdClkDiv(base, clk_div);
}

/**
 * @brief Poll for command completion
 *
 * PDL doesn't have Cy_SD_Host_PollCmdComplete(), so we poll the interrupt status.
 */
static cy_en_sd_host_status_t poll_cmd_complete(SDHC_Type *base)
{
    uint32_t status;
    uint32_t timeout = SDIO_POLL_TIMEOUT;

    while (timeout > 0) {
        status = Cy_SD_Host_GetNormalInterruptStatus(base);
        if (status & CY_SD_HOST_CMD_COMPLETE) {
            Cy_SD_Host_ClearNormalInterruptStatus(base, CY_SD_HOST_CMD_COMPLETE);
            return CY_SD_HOST_SUCCESS;
        }
        /* Check for errors */
        if (Cy_SD_Host_GetErrorInterruptStatus(base) != 0) {
            Cy_SD_Host_ClearErrorInterruptStatus(base, 0xFFFFFFFFUL);
            return CY_SD_HOST_ERROR;
        }
        timeout--;
    }
    return CY_SD_HOST_ERROR_TIMEOUT;
}

/**
 * @brief Poll for transfer completion
 *
 * PDL doesn't have Cy_SD_Host_PollTransferComplete(), so we poll the interrupt status.
 */
static cy_en_sd_host_status_t poll_transfer_complete(SDHC_Type *base)
{
    uint32_t status;
    uint32_t timeout = SDIO_POLL_TIMEOUT;

    while (timeout > 0) {
        status = Cy_SD_Host_GetNormalInterruptStatus(base);
        if (status & CY_SD_HOST_XFER_COMPLETE) {
            Cy_SD_Host_ClearNormalInterruptStatus(base, CY_SD_HOST_XFER_COMPLETE);
            return CY_SD_HOST_SUCCESS;
        }
        /* Check for errors */
        if (Cy_SD_Host_GetErrorInterruptStatus(base) != 0) {
            Cy_SD_Host_ClearErrorInterruptStatus(base, 0xFFFFFFFFUL);
            return CY_SD_HOST_ERROR;
        }
        timeout--;
    }
    return CY_SD_HOST_ERROR_TIMEOUT;
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int wifi_sdio_init(const wifi_sdio_config_t *config)
{
    cy_en_sd_host_status_t result;

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

    /* Initialize SD Host controller using BSP configuration */
    result = Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW,
                             &CYBSP_WIFI_SDIO_config,
                             &sdio_state.sd_host_context);
    if (result != CY_SD_HOST_SUCCESS) {
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
        return -3;
    }

    /* Enable SD Host */
    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);

    /* Set initial clock frequency */
    sdio_state.current_freq_hz = get_freq_for_speed(sdio_state.config.speed);
    set_sd_clk_frequency(CYBSP_WIFI_SDIO_HW, sdio_state.current_freq_hz);

    /* Set 4-bit bus width if configured */
    if (sdio_state.config.bus_width == WIFI_SDIO_BUS_WIDTH_4BIT) {
        Cy_SD_Host_SetBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT, &sdio_state.sd_host_context);
    } else {
        Cy_SD_Host_SetBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_1_BIT, &sdio_state.sd_host_context);
    }

    /* Enable I/O Function 1 (WLAN) using CMD52 */
    uint8_t response;
    int cmd_result = wifi_sdio_cmd52(true, 0, SDIO_CCCR_IO_ENABLE, 0x02, &response);
    if (cmd_result != 0) {
        Cy_SD_Host_DeInit(CYBSP_WIFI_SDIO_HW);
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
        return -5;
    }

    /* Wait for Function 1 to become ready */
    if (sdio_wait_ready(SDIO_CMD_TIMEOUT_MS) != 0) {
        Cy_SD_Host_DeInit(CYBSP_WIFI_SDIO_HW);
        vSemaphoreDelete(sdio_state.bus_mutex);
        sdio_state.bus_mutex = NULL;
        return -6;
    }

    /* Set block size for Function 1 */
    uint32_t block_size = sdio_state.config.block_size;
    wifi_sdio_cmd52(true, 0, 0x110, block_size & 0xFF, &response);
    wifi_sdio_cmd52(true, 0, 0x111, (block_size >> 8) & 0xFF, &response);

    sdio_state.initialized = true;
    return 0;
}

void wifi_sdio_deinit(void)
{
    if (!sdio_state.initialized) {
        return;
    }

    /* Disable I/O Function 1 */
    uint8_t response;
    wifi_sdio_cmd52(true, 0, SDIO_CCCR_IO_ENABLE, 0x00, &response);

    /* Disable and deinitialize SD Host */
    Cy_SD_Host_Disable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_DeInit(CYBSP_WIFI_SDIO_HW);

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
    cy_en_sd_host_status_t result;
    uint32_t cmd_response = 0;

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
    uint32_t argument = (write ? (1UL << 31) : 0) |
                        ((uint32_t)(function & 0x7) << 28) |
                        ((address & 0x1FFFF) << 9) |
                        (data & 0xFF);

    /* Configure CMD52 */
    cy_stc_sd_host_cmd_config_t cmd = {
        .commandIndex = SDIO_CMD52,
        .commandArgument = argument,
        .enableCrcCheck = true,
        .enableIdxCheck = true,
        .respType = CY_SD_HOST_RESPONSE_LEN_48,
        .enableAutoResponseErrorCheck = false,
        .cmdType = CY_SD_HOST_CMD_NORMAL,
        .dataPresent = false,
    };

    /* Send command */
    result = Cy_SD_Host_SendCommand(CYBSP_WIFI_SDIO_HW, &cmd);
    if (result != CY_SD_HOST_SUCCESS) {
        xSemaphoreGive(sdio_state.bus_mutex);
        sdio_state.stats.errors++;
        return -3;
    }

    /* Wait for command complete */
    result = poll_cmd_complete(CYBSP_WIFI_SDIO_HW);
    if (result != CY_SD_HOST_SUCCESS) {
        xSemaphoreGive(sdio_state.bus_mutex);
        sdio_state.stats.errors++;
        return -4;
    }

    /* Get response */
    result = Cy_SD_Host_GetResponse(CYBSP_WIFI_SDIO_HW, &cmd_response, false);

    xSemaphoreGive(sdio_state.bus_mutex);

    if (result != CY_SD_HOST_SUCCESS) {
        sdio_state.stats.errors++;
        return -5;
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
        return -6;
    }

    return 0;
}

int wifi_sdio_cmd53(bool write, uint8_t function, uint32_t address,
                    bool increment, uint8_t *buffer, uint32_t length)
{
    cy_en_sd_host_status_t result;

    if (!sdio_state.initialized || buffer == NULL || length == 0) {
        return -1;
    }

    /* Acquire bus mutex for thread safety */
    if (xSemaphoreTake(sdio_state.bus_mutex, pdMS_TO_TICKS(SDIO_CMD_TIMEOUT_MS)) != pdTRUE) {
        sdio_state.stats.timeout_errors++;
        return -2;
    }

    /* Build CMD53 argument:
     * [31]    R/W flag (1=write, 0=read)
     * [30:28] Function number
     * [27]    Block mode (0=byte, 1=block)
     * [26]    OP Code (0=fixed addr, 1=incrementing)
     * [25:9]  Register address
     * [8:0]   Byte/Block count
     */
    uint32_t block_count = (length + sdio_state.config.block_size - 1) / sdio_state.config.block_size;
    bool use_block_mode = (length > 512);
    uint32_t count = use_block_mode ? block_count : length;

    uint32_t argument = (write ? (1UL << 31) : 0) |
                        ((uint32_t)(function & 0x7) << 28) |
                        (use_block_mode ? (1UL << 27) : 0) |
                        (increment ? (1UL << 26) : 0) |
                        ((address & 0x1FFFF) << 9) |
                        (count & 0x1FF);

    /* Configure data transfer */
    cy_stc_sd_host_data_config_t data_config = {
        .blockSize = use_block_mode ? sdio_state.config.block_size : (uint16_t)length,
        .numberOfBlock = use_block_mode ? block_count : 1,
        .enableDma = true,
        .autoCommand = CY_SD_HOST_AUTO_CMD_NONE,
        .read = !write,
        .data = (uint32_t *)buffer,
        .dataTimeout = 0xC,  /* ~1 second timeout */
        .enableIntAtBlockGap = false,
        .enReliableWrite = false,
    };

    /* Configure CMD53 */
    cy_stc_sd_host_cmd_config_t cmd = {
        .commandIndex = SDIO_CMD53,
        .commandArgument = argument,
        .enableCrcCheck = true,
        .enableIdxCheck = true,
        .respType = CY_SD_HOST_RESPONSE_LEN_48,
        .enableAutoResponseErrorCheck = false,
        .cmdType = CY_SD_HOST_CMD_NORMAL,
        .dataPresent = true,
    };

    /* Initiate data transfer */
    result = Cy_SD_Host_InitDataTransfer(CYBSP_WIFI_SDIO_HW, &data_config);
    if (result != CY_SD_HOST_SUCCESS) {
        xSemaphoreGive(sdio_state.bus_mutex);
        sdio_state.stats.errors++;
        return -3;
    }

    /* Send command */
    result = Cy_SD_Host_SendCommand(CYBSP_WIFI_SDIO_HW, &cmd);
    if (result != CY_SD_HOST_SUCCESS) {
        xSemaphoreGive(sdio_state.bus_mutex);
        sdio_state.stats.errors++;
        return -4;
    }

    /* Wait for command complete */
    result = poll_cmd_complete(CYBSP_WIFI_SDIO_HW);
    if (result != CY_SD_HOST_SUCCESS) {
        xSemaphoreGive(sdio_state.bus_mutex);
        sdio_state.stats.errors++;
        return -5;
    }

    /* Wait for transfer complete */
    result = poll_transfer_complete(CYBSP_WIFI_SDIO_HW);

    xSemaphoreGive(sdio_state.bus_mutex);

    if (result != CY_SD_HOST_SUCCESS) {
        sdio_state.stats.errors++;
        return -6;
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
    /* For now, fall back to synchronous transfer */
    /* TODO: Implement async using DMA interrupts */
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
    uint32_t freq_hz;
    uint8_t speed_reg = 0;
    uint8_t response;
    int result;

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
    result = wifi_sdio_cmd52(false, 0, SDIO_CCCR_SPEED, 0, &speed_reg);
    if (result != 0) {
        xSemaphoreGive(sdio_state.bus_mutex);
        return -3;
    }

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
    result = wifi_sdio_cmd52(true, 0, SDIO_CCCR_SPEED, speed_reg, &response);
    if (result != 0) {
        xSemaphoreGive(sdio_state.bus_mutex);
        return -5;
    }

    /* Reconfigure clock frequency */
    set_sd_clk_frequency(CYBSP_WIFI_SDIO_HW, freq_hz);

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

    /* Update PDL bus width setting */
    if (xSemaphoreTake(sdio_state.bus_mutex, pdMS_TO_TICKS(SDIO_CMD_TIMEOUT_MS)) != pdTRUE) {
        return -2;
    }

    if (width == WIFI_SDIO_BUS_WIDTH_4BIT) {
        Cy_SD_Host_SetBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT, &sdio_state.sd_host_context);
    } else {
        Cy_SD_Host_SetBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_1_BIT, &sdio_state.sd_host_context);
    }

    xSemaphoreGive(sdio_state.bus_mutex);

    sdio_state.config.bus_width = width;
    return 0;
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
