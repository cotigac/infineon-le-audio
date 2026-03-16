/**
 * @file cyhal_sdio.c
 * @brief CYHAL SDIO Implementation for PSoC Edge E84
 *
 * Implements the cyhal_sdio interface required by WHD (Wi-Fi Host Driver).
 * Uses the PDL SDHC driver to communicate with the CYW55512 WLAN chip.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cyhal_sdio.h"
#include "cy_pdl.h"
#include "cy_syslib.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <string.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** SDIO block size for F2 (backplane) */
#define SDIO_BLOCK_SIZE             64

/** SDIO function numbers */
#define SDIO_FUNC_0                 0   /* CIA */
#define SDIO_FUNC_1                 1   /* Backplane */
#define SDIO_FUNC_2                 2   /* WLAN */

/** SDIO operation timeout in ms */
#define SDIO_TIMEOUT_MS             1000

/** SDHC instance for WLAN (SDHC0) */
#define WLAN_SDHC_HW                SDHC0
#define WLAN_SDHC_IRQ               sdhc_0_interrupt_general_IRQn

/*******************************************************************************
 * Types
 ******************************************************************************/

/** SDIO driver state */
typedef struct {
    bool                        initialized;
    SemaphoreHandle_t           transfer_sema;
    SemaphoreHandle_t           mutex;
    cyhal_sdio_irq_handler_t    irq_handler;
    void                        *irq_handler_arg;
    uint32_t                    irq_mask;
    cy_stc_sd_host_context_t    sd_host_context;
    uint32_t                    frequency_hz;
    uint16_t                    block_size;
} cyhal_sdio_state_t;

/*******************************************************************************
 * Static Data
 ******************************************************************************/

static cyhal_sdio_state_t sdio_state = {0};

/** SDHC configuration for SDIO mode */
static const cy_stc_sd_host_init_config_t sdhc_config = {
    .emmc = false,
    .dmaType = CY_SD_HOST_DMA_ADMA2,
    .enableLedControl = false,
};

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief SDHC interrupt handler
 */
static void sdhc_irq_handler(void)
{
    uint32_t status = Cy_SD_Host_GetNormalInterruptStatus(WLAN_SDHC_HW);

    /* Clear interrupts */
    Cy_SD_Host_ClearNormalInterruptStatus(WLAN_SDHC_HW, status);

    /* Handle transfer complete */
    if (status & (CY_SD_HOST_CMD_COMPLETE | CY_SD_HOST_XFER_COMPLETE)) {
        if (sdio_state.transfer_sema != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(sdio_state.transfer_sema, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

    /* Handle card interrupt (SDIO interrupt from device) */
    if (status & CY_SD_HOST_CARD_INTERRUPT) {
        if (sdio_state.irq_handler != NULL &&
            (sdio_state.irq_mask & CYHAL_SDIO_CARD_INTERRUPT)) {
            sdio_state.irq_handler(sdio_state.irq_handler_arg,
                                   CYHAL_SDIO_CARD_INTERRUPT);
        }
    }
}

/**
 * @brief Wait for transfer complete with timeout
 */
static cy_rslt_t wait_transfer_complete(uint32_t timeout_ms)
{
    if (sdio_state.transfer_sema == NULL) {
        return CYHAL_SDIO_RSLT_ERR_SEMA_NOT_INITED;
    }

    if (xSemaphoreTake(sdio_state.transfer_sema,
                       pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_CMD_TIMEOUT);
    }

    return CY_RSLT_SUCCESS;
}

/**
 * @brief Send CMD52 (IO_RW_DIRECT)
 */
static cy_rslt_t send_cmd52(bool write, uint32_t func, uint32_t addr,
                            uint8_t data, uint8_t *response)
{
    cy_stc_sd_host_cmd_config_t cmd = {0};
    uint32_t argument;
    cy_en_sd_host_status_t status;

    /* Build CMD52 argument */
    argument = (write ? (1UL << 31) : 0) |  /* R/W flag */
               ((func & 0x7) << 28) |        /* Function number */
               ((addr & 0x1FFFF) << 9) |     /* Register address */
               (data & 0xFF);                 /* Write data */

    cmd.commandIndex = 52;
    cmd.commandArgument = argument;
    cmd.enableCrcCheck = true;
    cmd.enableIdxCheck = true;
    cmd.respType = CY_SD_HOST_RESPONSE_LEN_48;
    cmd.enableAutoResponseErrorCheck = false;
    cmd.dataPresent = false;

    status = Cy_SD_Host_SendCommand(WLAN_SDHC_HW, &cmd);
    if (status != CY_SD_HOST_SUCCESS) {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
    }

    /* Wait for command complete */
    cy_rslt_t result = wait_transfer_complete(SDIO_TIMEOUT_MS);
    if (result != CY_RSLT_SUCCESS) {
        return result;
    }

    /* Get response */
    if (response != NULL) {
        uint32_t resp = Cy_SD_Host_GetResponse(WLAN_SDHC_HW,
                                                CY_SD_HOST_RESPONSE_TYPE_R5,
                                                false);
        *response = (uint8_t)(resp & 0xFF);
    }

    return CY_RSLT_SUCCESS;
}

/**
 * @brief Send CMD53 (IO_RW_EXTENDED)
 */
static cy_rslt_t send_cmd53(bool write, uint32_t func, uint32_t addr,
                            const uint32_t *data, uint16_t length, bool block_mode)
{
    cy_stc_sd_host_cmd_config_t cmd = {0};
    cy_stc_sd_host_data_config_t data_cfg = {0};
    uint32_t argument;
    uint16_t block_count;
    cy_en_sd_host_status_t status;

    if (block_mode) {
        block_count = (length + sdio_state.block_size - 1) / sdio_state.block_size;
    } else {
        block_count = 1;
    }

    /* Build CMD53 argument */
    argument = (write ? (1UL << 31) : 0) |   /* R/W flag */
               ((func & 0x7) << 28) |         /* Function number */
               (block_mode ? (1UL << 27) : 0) | /* Block mode */
               (1UL << 26) |                  /* OP code (increment address) */
               ((addr & 0x1FFFF) << 9) |      /* Register address */
               (block_mode ? (block_count & 0x1FF) : (length & 0x1FF));

    /* Configure data transfer */
    data_cfg.data = (uint32_t *)data;
    data_cfg.blockSize = block_mode ? sdio_state.block_size : length;
    data_cfg.numberOfBlock = block_count;
    data_cfg.enableDma = true;
    data_cfg.autoCommand = CY_SD_HOST_AUTO_CMD_NONE;
    data_cfg.read = !write;
    data_cfg.dataTimeout = 0x0E;  /* ~2.7 seconds */
    data_cfg.enableIntAtBlockGap = false;
    data_cfg.enReliableWrite = false;

    status = Cy_SD_Host_InitDataTransfer(WLAN_SDHC_HW, &data_cfg);
    if (status != CY_SD_HOST_SUCCESS) {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
    }

    cmd.commandIndex = 53;
    cmd.commandArgument = argument;
    cmd.enableCrcCheck = true;
    cmd.enableIdxCheck = true;
    cmd.respType = CY_SD_HOST_RESPONSE_LEN_48;
    cmd.enableAutoResponseErrorCheck = false;
    cmd.dataPresent = true;

    status = Cy_SD_Host_SendCommand(WLAN_SDHC_HW, &cmd);
    if (status != CY_SD_HOST_SUCCESS) {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
    }

    /* Wait for transfer complete */
    return wait_transfer_complete(SDIO_TIMEOUT_MS);
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

cy_rslt_t cyhal_sdio_init(cyhal_sdio_t *obj, cyhal_gpio_t cmd, cyhal_gpio_t clk,
                          cyhal_gpio_t data0, cyhal_gpio_t data1,
                          cyhal_gpio_t data2, cyhal_gpio_t data3)
{
    cy_en_sd_host_status_t status;

    (void)obj;   /* Object not used - we have single instance */
    (void)cmd;   /* Pins configured by BSP */
    (void)clk;
    (void)data0;
    (void)data1;
    (void)data2;
    (void)data3;

    if (sdio_state.initialized) {
        return CY_RSLT_SUCCESS;
    }

    /* Create synchronization primitives */
    sdio_state.transfer_sema = xSemaphoreCreateBinary();
    if (sdio_state.transfer_sema == NULL) {
        return CYHAL_SDIO_RSLT_ERR_SEMA_NOT_INITED;
    }

    sdio_state.mutex = xSemaphoreCreateMutex();
    if (sdio_state.mutex == NULL) {
        vSemaphoreDelete(sdio_state.transfer_sema);
        return CYHAL_SDIO_RSLT_ERR_SEMA_NOT_INITED;
    }

    /* Initialize SDHC */
    status = Cy_SD_Host_Init(WLAN_SDHC_HW, &sdhc_config, &sdio_state.sd_host_context);
    if (status != CY_SD_HOST_SUCCESS) {
        vSemaphoreDelete(sdio_state.transfer_sema);
        vSemaphoreDelete(sdio_state.mutex);
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
    }

    /* Enable SDHC */
    Cy_SD_Host_Enable(WLAN_SDHC_HW);

    /* Set up interrupt handler */
    Cy_SysInt_SetVector(WLAN_SDHC_IRQ, sdhc_irq_handler);
    NVIC_EnableIRQ(WLAN_SDHC_IRQ);

    /* Enable interrupts */
    Cy_SD_Host_SetNormalInterruptMask(WLAN_SDHC_HW,
        CY_SD_HOST_CMD_COMPLETE | CY_SD_HOST_XFER_COMPLETE | CY_SD_HOST_CARD_INTERRUPT);

    /* Set default configuration */
    sdio_state.frequency_hz = 400000;  /* Start at 400 kHz for init */
    sdio_state.block_size = SDIO_BLOCK_SIZE;
    sdio_state.initialized = true;

    return CY_RSLT_SUCCESS;
}

void cyhal_sdio_free(cyhal_sdio_t *obj)
{
    (void)obj;

    if (!sdio_state.initialized) {
        return;
    }

    NVIC_DisableIRQ(WLAN_SDHC_IRQ);
    Cy_SD_Host_Disable(WLAN_SDHC_HW);
    Cy_SD_Host_DeInit(WLAN_SDHC_HW);

    if (sdio_state.transfer_sema != NULL) {
        vSemaphoreDelete(sdio_state.transfer_sema);
        sdio_state.transfer_sema = NULL;
    }

    if (sdio_state.mutex != NULL) {
        vSemaphoreDelete(sdio_state.mutex);
        sdio_state.mutex = NULL;
    }

    sdio_state.initialized = false;
}

cy_rslt_t cyhal_sdio_configure(cyhal_sdio_t *obj, const cyhal_sdio_cfg_t *config)
{
    (void)obj;

    if (config == NULL) {
        return CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
    }

    if (!sdio_state.initialized) {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
    }

    /* Update block size */
    if (config->block_size > 0) {
        sdio_state.block_size = config->block_size;
    }

    /* Update clock frequency */
    if (config->frequencyhal_hz > 0) {
        sdio_state.frequency_hz = config->frequencyhal_hz;
        Cy_SD_Host_SetSdClkFrequency(WLAN_SDHC_HW, config->frequencyhal_hz,
                                      &sdio_state.sd_host_context);
    }

    return CY_RSLT_SUCCESS;
}

cy_rslt_t cyhal_sdio_send_cmd(const cyhal_sdio_t *obj, cyhal_transfer_t direction,
                              cyhal_sdio_command_t command, uint32_t argument,
                              uint32_t *response)
{
    cy_rslt_t result;

    (void)obj;

    if (!sdio_state.initialized) {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
    }

    xSemaphoreTake(sdio_state.mutex, portMAX_DELAY);

    switch (command) {
        case CYHAL_SDIO_CMD_IO_RW_DIRECT: {
            /* CMD52 */
            uint8_t resp_byte;
            uint8_t func = (argument >> 28) & 0x7;
            uint32_t addr = (argument >> 9) & 0x1FFFF;
            uint8_t data = argument & 0xFF;
            bool write = (direction == CYHAL_WRITE);

            result = send_cmd52(write, func, addr, data, &resp_byte);
            if (response != NULL && result == CY_RSLT_SUCCESS) {
                *response = resp_byte;
            }
            break;
        }

        case CYHAL_SDIO_CMD_GO_IDLE_STATE:
        case CYHAL_SDIO_CMD_SEND_RELATIVE_ADDR:
        case CYHAL_SDIO_CMD_IO_SEND_OP_COND:
        case CYHAL_SDIO_CMD_SELECT_CARD:
        case CYHAL_SDIO_CMD_GO_INACTIVE_STATE: {
            /* Standard SD commands - send via PDL */
            cy_stc_sd_host_cmd_config_t cmd = {0};
            cy_en_sd_host_status_t status;

            cmd.commandIndex = (uint32_t)command;
            cmd.commandArgument = argument;
            cmd.enableCrcCheck = true;
            cmd.enableIdxCheck = true;
            cmd.respType = (command == CYHAL_SDIO_CMD_GO_IDLE_STATE) ?
                           CY_SD_HOST_RESPONSE_LEN_NONE : CY_SD_HOST_RESPONSE_LEN_48;
            cmd.dataPresent = false;

            status = Cy_SD_Host_SendCommand(WLAN_SDHC_HW, &cmd);
            if (status != CY_SD_HOST_SUCCESS) {
                result = CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
            } else {
                result = wait_transfer_complete(SDIO_TIMEOUT_MS);
                if (response != NULL && result == CY_RSLT_SUCCESS) {
                    *response = Cy_SD_Host_GetResponse(WLAN_SDHC_HW,
                                                       CY_SD_HOST_RESPONSE_TYPE_R1,
                                                       false);
                }
            }
            break;
        }

        default:
            result = CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
            break;
    }

    xSemaphoreGive(sdio_state.mutex);
    return result;
}

cy_rslt_t cyhal_sdio_bulk_transfer(cyhal_sdio_t *obj, cyhal_transfer_t direction,
                                   uint32_t argument, const uint32_t *data,
                                   uint16_t length, uint32_t *response)
{
    cy_rslt_t result;

    (void)obj;
    (void)response;

    if (data == NULL || length == 0) {
        return CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
    }

    if (!sdio_state.initialized) {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
    }

    uint32_t func = (argument >> 28) & 0x7;
    uint32_t addr = (argument >> 9) & 0x1FFFF;
    bool block_mode = (argument >> 27) & 0x1;
    bool write = (direction == CYHAL_WRITE);

    xSemaphoreTake(sdio_state.mutex, portMAX_DELAY);
    result = send_cmd53(write, func, addr, data, length, block_mode);
    xSemaphoreGive(sdio_state.mutex);

    return result;
}

cy_rslt_t cyhal_sdio_transfer_async(cyhal_sdio_t *obj, cyhal_transfer_t direction,
                                    uint32_t argument, const uint32_t *data,
                                    uint16_t length)
{
    /* For now, implement as synchronous - async can be added later */
    return cyhal_sdio_bulk_transfer(obj, direction, argument, data, length, NULL);
}

bool cyhal_sdio_is_busy(const cyhal_sdio_t *obj)
{
    (void)obj;

    if (!sdio_state.initialized) {
        return false;
    }

    /* Check if transfer is in progress */
    uint32_t state = Cy_SD_Host_GetPresentState(WLAN_SDHC_HW);
    return (state & (CY_SD_HOST_CMD_INHIBIT | CY_SD_HOST_DAT_INHIBIT)) != 0;
}

cy_rslt_t cyhal_sdio_abort_async(const cyhal_sdio_t *obj)
{
    (void)obj;

    if (!sdio_state.initialized) {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_NO_SP_ERRORS);
    }

    Cy_SD_Host_AbortTransfer(WLAN_SDHC_HW, &sdio_state.sd_host_context);
    return CY_RSLT_SUCCESS;
}

void cyhal_sdio_register_irq(cyhal_sdio_t *obj, cyhal_sdio_irq_handler_t handler,
                             void *handler_arg)
{
    (void)obj;

    sdio_state.irq_handler = handler;
    sdio_state.irq_handler_arg = handler_arg;
}

void cyhal_sdio_irq_enable(cyhal_sdio_t *obj, cyhal_sdio_irq_event_t event, bool enable)
{
    (void)obj;

    if (enable) {
        sdio_state.irq_mask |= event;
    } else {
        sdio_state.irq_mask &= ~event;
    }

    /* Update hardware interrupt mask for card interrupt */
    if (event & CYHAL_SDIO_CARD_INTERRUPT) {
        uint32_t mask = Cy_SD_Host_GetNormalInterruptMask(WLAN_SDHC_HW);
        if (enable) {
            mask |= CY_SD_HOST_CARD_INTERRUPT;
        } else {
            mask &= ~CY_SD_HOST_CARD_INTERRUPT;
        }
        Cy_SD_Host_SetNormalInterruptMask(WLAN_SDHC_HW, mask);
    }
}
