/**
 * @file cyhal_sdio.h
 * @brief CYHAL SDIO interface for WHD
 *
 * Local copy that ensures core-lib cy_result.h is used instead of WHD's
 * simplified version. This prevents macro redefinition conflicts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Include cy_result.h from core-lib via cy_pdl.h */
#include "cy_pdl.h"

/* Now include local HAL types (these don't include cy_result.h) */
#include "cyhal_hw_types.h"
#include "cyhal_modules.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Macros
 ******************************************************************************/

#define CYHAL_SDIO_RET_NO_ERRORS           (0x00)
#define CYHAL_SDIO_RET_NO_SP_ERRORS        (0x01)
#define CYHAL_SDIO_RET_CMD_CRC_ERROR       (0x02)
#define CYHAL_SDIO_RET_CMD_IDX_ERROR       (0x04)
#define CYHAL_SDIO_RET_CMD_EB_ERROR        (0x08)
#define CYHAL_SDIO_RET_DAT_CRC_ERROR       (0x10)
#define CYHAL_SDIO_RET_CMD_TIMEOUT         (0x20)
#define CYHAL_SDIO_RET_DAT_TIMEOUT         (0x40)
#define CYHAL_SDIO_RET_RESP_FLAG_ERROR     (0x80)

#define CYHAL_SDIO_CLOCK_ERROR          (0x100)
#define CYHAL_SDIO_BAD_ARGUMENT         (0x200)
#define CYHAL_SDIO_SEMA_NOT_INITED      (0x400)
#define CYHAL_SDIO_FUNC_NOT_SUPPORTED   (0x800)

/** Incorrect parameter value define */
#define CYHAL_SDIO_RSLT_ERR_BAD_PARAM          CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, \
                                                              CYHAL_RSLT_MODULE_SDIO, \
                                                              CYHAL_SDIO_BAD_ARGUMENT)

/** Clock initialization error define */
#define CYHAL_SDIO_RSLT_ERR_CLOCK             CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, \
                                                             CYHAL_RSLT_MODULE_SDIO, \
                                                             CYHAL_SDIO_CLOCK_ERROR)

/** Semaphore not initiated error define */
#define CYHAL_SDIO_RSLT_ERR_SEMA_NOT_INITED   CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, \
                                                             CYHAL_RSLT_MODULE_SDIO, \
                                                             CYHAL_SDIO_SEMA_NOT_INITED)

/** Error define based on SDIO lower function return value */
#define CYHAL_SDIO_RSLT_ERR_FUNC_RET(retVal)  CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, \
                                                             CYHAL_RSLT_MODULE_SDIO, (retVal) )

/*******************************************************************************
 * Enums
 ******************************************************************************/

/** Commands that can be issued */
typedef enum
{
    CYHAL_SDIO_CMD_GO_IDLE_STATE  =  0,
    CYHAL_SDIO_CMD_SEND_RELATIVE_ADDR  =  3,
    CYHAL_SDIO_CMD_IO_SEND_OP_COND  =  5,
    CYHAL_SDIO_CMD_SELECT_CARD  =  7,
    CYHAL_SDIO_CMD_GO_INACTIVE_STATE = 15,
    CYHAL_SDIO_CMD_IO_RW_DIRECT = 52,
    CYHAL_SDIO_CMD_IO_RW_EXTENDED = 53,
} cyhal_sdio_command_t;

/** Types of transfer that can be performed */
typedef enum
{
    CYHAL_READ,
    CYHAL_WRITE
} cyhal_transfer_t;

/** Events that can cause an SDIO interrupt */
typedef enum
{
    CYHAL_SDIO_CMD_COMPLETE   = 0x0001,
    CYHAL_SDIO_XFER_COMPLETE  = 0x0002,
    CYHAL_SDIO_BGAP_EVENT     = 0x0004,
    CYHAL_SDIO_DMA_INTERRUPT  = 0x0008,
    CYHAL_SDIO_BUF_WR_READY   = 0x0010,
    CYHAL_SDIO_BUF_RD_READY   = 0x0020,
    CYHAL_SDIO_CARD_INSERTION = 0x0040,
    CYHAL_SDIO_CARD_REMOVAL   = 0x0080,
    CYHAL_SDIO_CARD_INTERRUPT = 0x0100,
    CYHAL_SDIO_INT_A          = 0x0200,
    CYHAL_SDIO_INT_B          = 0x0400,
    CYHAL_SDIO_INT_C          = 0x0800,
    CYHAL_SDIO_RE_TUNE_EVENT  = 0x1000,
    CYHAL_SDIO_FX_EVENT       = 0x2000,
    CYHAL_SDIO_CQE_EVENT      = 0x4000,
    CYHAL_SDIO_ERR_INTERRUPT  = 0x8000,
    CYHAL_SDIO_ALL_INTERRUPTS = 0xE1FF,
} cyhal_sdio_irq_event_t;

/*******************************************************************************
 * Types
 ******************************************************************************/

/** SDIO controller initial configuration */
typedef struct
{
    uint32_t frequencyhal_hz;
    uint16_t block_size;
} cyhal_sdio_cfg_t;

/** Handler for SDIO interrupts */
typedef void (*cyhal_sdio_irq_handler_t)(void *handler_arg, cyhal_sdio_irq_event_t event);

/*******************************************************************************
 * Functions
 ******************************************************************************/

cy_rslt_t cyhal_sdio_init(cyhal_sdio_t *obj, cyhal_gpio_t cmd, cyhal_gpio_t clk,
                          cyhal_gpio_t data0, cyhal_gpio_t data1,
                          cyhal_gpio_t data2, cyhal_gpio_t data3);

void cyhal_sdio_free(cyhal_sdio_t *obj);

cy_rslt_t cyhal_sdio_configure(cyhal_sdio_t *obj, const cyhal_sdio_cfg_t *config);

cy_rslt_t cyhal_sdio_send_cmd(const cyhal_sdio_t *obj, cyhal_transfer_t direction,
                              cyhal_sdio_command_t command, uint32_t argument,
                              uint32_t *response);

cy_rslt_t cyhal_sdio_bulk_transfer(cyhal_sdio_t *obj, cyhal_transfer_t direction,
                                   uint32_t argument, const uint32_t *data,
                                   uint16_t length, uint32_t *response);

cy_rslt_t cyhal_sdio_transfer_async(cyhal_sdio_t *obj, cyhal_transfer_t direction,
                                    uint32_t argument, const uint32_t *data,
                                    uint16_t length);

bool cyhal_sdio_is_busy(const cyhal_sdio_t *obj);

cy_rslt_t cyhal_sdio_abort_async(const cyhal_sdio_t *obj);

void cyhal_sdio_register_irq(cyhal_sdio_t *obj, cyhal_sdio_irq_handler_t handler,
                             void *handler_arg);

void cyhal_sdio_irq_enable(cyhal_sdio_t *obj, cyhal_sdio_irq_event_t event, bool enable);

#ifdef __cplusplus
}
#endif
