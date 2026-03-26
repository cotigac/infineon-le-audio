/**
 * @file hci_isoc.c
 * @brief HCI Isochronous Channels Implementation
 *
 * Implements HCI commands and event handling for LE Audio isochronous
 * channels (CIG/CIS for unicast, BIG/BIS for broadcast).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hci_isoc.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Platform Includes
 ******************************************************************************/

/* Infineon BTSTACK headers */
#include "wiced_bt_isoc.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "task.h"

/*******************************************************************************
 * HCI Opcodes for Isochronous Channels (BT Core 5.4)
 ******************************************************************************/

/* CIG/CIS Commands */
#define HCI_LE_SET_CIG_PARAMS           0x2062
#define HCI_LE_SET_CIG_PARAMS_TEST      0x2063
#define HCI_LE_CREATE_CIS               0x2064
#define HCI_LE_REMOVE_CIG               0x2065
#define HCI_LE_ACCEPT_CIS_REQUEST       0x2066
#define HCI_LE_REJECT_CIS_REQUEST       0x2067

/* BIG Commands */
#define HCI_LE_CREATE_BIG               0x2068
#define HCI_LE_CREATE_BIG_TEST          0x2069
#define HCI_LE_TERMINATE_BIG            0x206A
#define HCI_LE_BIG_CREATE_SYNC          0x206B
#define HCI_LE_BIG_TERMINATE_SYNC       0x206C

/* ISO Data Path Commands */
#define HCI_LE_SETUP_ISO_DATA_PATH      0x206E
#define HCI_LE_REMOVE_ISO_DATA_PATH     0x206F

/* ISO Link Quality */
#define HCI_LE_READ_ISO_LINK_QUALITY    0x2075

/* Disconnect (standard HCI) */
#ifndef HCI_DISCONNECT
#define HCI_DISCONNECT                  0x0406
#endif

/*******************************************************************************
 * HCI Event Codes
 ******************************************************************************/

#define HCI_EVENT_COMMAND_COMPLETE      0x0E
#define HCI_EVENT_COMMAND_STATUS        0x0F
#define HCI_EVENT_DISCONNECTION_COMPLETE 0x05
#define HCI_EVENT_LE_META               0x3E

/* LE Meta Subevents */
#define HCI_LE_CIS_ESTABLISHED          0x19
#define HCI_LE_CIS_REQUEST              0x1A
#define HCI_LE_CREATE_BIG_COMPLETE      0x1B
#define HCI_LE_TERMINATE_BIG_COMPLETE   0x1C
#define HCI_LE_BIG_SYNC_ESTABLISHED     0x1D
#define HCI_LE_BIG_SYNC_LOST            0x1E
#define HCI_LE_ISO_DATA_PATH_COMPLETE   0x22

/*******************************************************************************
 * Constants
 ******************************************************************************/

#define HCI_CMD_TIMEOUT_MS              5000
#define MAX_ISO_TX_QUEUE_SIZE           32

/* Handle range for CIS (0x0000-0x0EFF typically) */
#define CIS_HANDLE_MIN                  0x0000
#define CIS_HANDLE_MAX                  0x0EFF

/* Handle range for BIS (0x0000-0x0EFF typically) */
#define BIS_HANDLE_MIN                  0x0000
#define BIS_HANDLE_MAX                  0x0EFF

/*******************************************************************************
 * Types
 ******************************************************************************/

/** CIG runtime state */
typedef struct {
    bool in_use;
    uint8_t cig_id;
    uint8_t num_cis;
    uint16_t cis_handles[HCI_ISOC_MAX_CIS_PER_CIG];
    cis_info_t cis_info[HCI_ISOC_MAX_CIS_PER_CIG];
} cig_state_t;

/** BIG runtime state */
typedef struct {
    bool in_use;
    big_info_t info;
    bool is_source;  /* true = source, false = sink */
} big_state_struct_t;

/** Callback registry entry */
typedef struct {
    hci_isoc_callback_t callback;
    void *user_data;
    bool in_use;
} hci_isoc_callback_entry_t;

/** Module context */
typedef struct {
    bool initialized;

    /* Callback registry (supports multiple callbacks) */
    hci_isoc_callback_entry_t callbacks[HCI_ISOC_MAX_CALLBACKS];

    /* CIG/CIS state */
    cig_state_t cig[HCI_ISOC_MAX_CIG];
    uint8_t num_active_cig;

    /* BIG/BIS state */
    big_state_struct_t big[HCI_ISOC_MAX_BIG];
    uint8_t num_active_big;

    /* Statistics */
    hci_isoc_stats_t stats;

    /* Synchronization */
    SemaphoreHandle_t cmd_semaphore;
    volatile bool cmd_pending;
    volatile int cmd_status;
    uint8_t cmd_response[256];
    uint16_t cmd_response_len;

    /* TX sequence numbers (per handle) */
    uint16_t tx_seq_num[256];

} hci_isoc_ctx_t;

/*******************************************************************************
 * Module Variables
 ******************************************************************************/

static hci_isoc_ctx_t isoc_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static int hci_send_command(uint16_t opcode, const uint8_t *params, uint16_t len);
static int hci_wait_command_complete(uint32_t timeout_ms);
static void dispatch_event(const hci_isoc_event_t *event);
static cig_state_t* find_cig(uint8_t cig_id);
static cig_state_t* find_cig_by_cis_handle(uint16_t cis_handle);
static cis_info_t* find_cis(uint16_t cis_handle);
static big_state_struct_t* find_big(uint8_t big_handle);
static void handle_cis_established(const uint8_t *data, uint16_t len);
static void handle_cis_request(const uint8_t *data, uint16_t len);
static void handle_create_big_complete(const uint8_t *data, uint16_t len);
static void handle_terminate_big_complete(const uint8_t *data, uint16_t len);
static void handle_big_sync_established(const uint8_t *data, uint16_t len);
static void handle_big_sync_lost(const uint8_t *data, uint16_t len);
static void handle_disconnection_complete(const uint8_t *data, uint16_t len);

/*******************************************************************************
 * HCI Command Interface (Platform Abstraction)
 ******************************************************************************/

/**
 * @brief HCI command complete callback
 */
static void hci_isoc_cmd_complete_callback(wiced_bt_dev_vendor_specific_command_complete_params_t *p_cmd_cplt_param)
{
    /* Store response data */
    if (p_cmd_cplt_param->p_param_buf != NULL && p_cmd_cplt_param->param_len > 0) {
        uint16_t copy_len = (p_cmd_cplt_param->param_len > sizeof(isoc_ctx.cmd_response)) ?
                            sizeof(isoc_ctx.cmd_response) : p_cmd_cplt_param->param_len;
        memcpy(isoc_ctx.cmd_response, p_cmd_cplt_param->p_param_buf, copy_len);
        isoc_ctx.cmd_response_len = copy_len;
    }

    /* Check status (first byte is typically HCI status) */
    isoc_ctx.cmd_status = (p_cmd_cplt_param->p_param_buf != NULL &&
                           p_cmd_cplt_param->param_len > 0 &&
                           p_cmd_cplt_param->p_param_buf[0] == 0) ?
                          HCI_ISOC_OK : HCI_ISOC_ERROR_COMMAND_FAILED;

    isoc_ctx.cmd_pending = false;

    /* Signal completion */
    if (isoc_ctx.cmd_semaphore != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(isoc_ctx.cmd_semaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief Send HCI command
 */
static int hci_send_command(uint16_t opcode, const uint8_t *params, uint16_t len)
{
    wiced_result_t result;

    if (isoc_ctx.cmd_semaphore == NULL) {
        /* Semaphore not initialized - can't wait for response */
        printf("ISOC: Command semaphore not initialized\n");
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    isoc_ctx.cmd_pending = true;
    isoc_ctx.cmd_status = HCI_ISOC_OK;
    isoc_ctx.cmd_response_len = 0;

    /* Send HCI command via BTSTACK vendor-specific command API
     * This handles both standard HCI LE commands and vendor commands
     * by using the opcode directly */
    result = wiced_bt_dev_vendor_specific_command(
        opcode,
        len,
        (uint8_t *)params,
        hci_isoc_cmd_complete_callback
    );

    if (result != WICED_BT_SUCCESS && result != WICED_BT_PENDING) {
        printf("ISOC: Failed to send HCI command 0x%04X: %d\n", opcode, result);
        isoc_ctx.cmd_pending = false;
        return HCI_ISOC_ERROR_COMMAND_FAILED;
    }

    return HCI_ISOC_OK;
}

/**
 * @brief Wait for command complete
 */
static int hci_wait_command_complete(uint32_t timeout_ms)
{
    if (isoc_ctx.cmd_semaphore == NULL) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    /* Wait on semaphore for command complete */
    if (xSemaphoreTake(isoc_ctx.cmd_semaphore, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        printf("ISOC: HCI command timeout\n");
        isoc_ctx.cmd_pending = false;
        return HCI_ISOC_ERROR_TIMEOUT;
    }

    return isoc_ctx.cmd_status;
}

/*******************************************************************************
 * Event Handling
 ******************************************************************************/

/**
 * @brief Dispatch event to callback
 */
static void dispatch_event(const hci_isoc_event_t *event)
{
    /* Dispatch to all registered callbacks */
    for (int i = 0; i < HCI_ISOC_MAX_CALLBACKS; i++) {
        if (isoc_ctx.callbacks[i].in_use && isoc_ctx.callbacks[i].callback != NULL) {
            isoc_ctx.callbacks[i].callback(event, isoc_ctx.callbacks[i].user_data);
        }
    }
}

/**
 * @brief Handle LE CIS Established event
 */
static void handle_cis_established(const uint8_t *data, uint16_t len)
{
    if (len < 28) {
        return;
    }

    uint8_t status = data[0];
    uint16_t cis_handle = data[1] | (data[2] << 8);

    cis_info_t *cis = find_cis(cis_handle);

    if (status == 0 && cis != NULL) {
        /* Parse CIS established parameters */
        cis->state = CIS_STATE_ESTABLISHED;
        /* CIG sync delay: bytes 3-5 (24-bit) */
        /* CIS sync delay: bytes 6-8 (24-bit) */
        cis->transport_latency_c_to_p = data[9] | (data[10] << 8) | (data[11] << 16);
        cis->transport_latency_p_to_c = data[12] | (data[13] << 8) | (data[14] << 16);
        cis->phy_c_to_p = data[15];
        cis->phy_p_to_c = data[16];
        cis->nse = data[17];
        cis->bn_c_to_p = data[18];
        cis->bn_p_to_c = data[19];
        cis->ft_c_to_p = data[20];
        cis->ft_p_to_c = data[21];
        cis->max_pdu_c_to_p = data[22] | (data[23] << 8);
        cis->max_pdu_p_to_c = data[24] | (data[25] << 8);
        cis->iso_interval = data[26] | (data[27] << 8);

        isoc_ctx.stats.cis_established++;

        /* Dispatch event */
        hci_isoc_event_t event = {
            .type = HCI_ISOC_EVENT_CIS_ESTABLISHED,
            .data.cis_info = *cis
        };
        dispatch_event(&event);
    } else {
        /* CIS establishment failed */
        hci_isoc_event_t event = {
            .type = HCI_ISOC_EVENT_ERROR,
            .data.error_code = status
        };
        dispatch_event(&event);
    }
}

/**
 * @brief Handle LE CIS Request event
 */
static void handle_cis_request(const uint8_t *data, uint16_t len)
{
    if (len < 7) {
        return;
    }

    cis_request_t request;
    request.acl_handle = data[0] | (data[1] << 8);
    request.cis_handle = data[2] | (data[3] << 8);
    request.cig_id = data[4];
    request.cis_id = data[5];

    /* Dispatch event - application decides to accept or reject */
    hci_isoc_event_t event = {
        .type = HCI_ISOC_EVENT_CIS_REQUEST,
        .data.cis_request = request
    };
    dispatch_event(&event);
}

/**
 * @brief Handle LE Create BIG Complete event
 */
static void handle_create_big_complete(const uint8_t *data, uint16_t len)
{
    if (len < 19) {
        return;
    }

    uint8_t status = data[0];
    uint8_t big_handle = data[1];

    big_state_struct_t *big = find_big(big_handle);

    if (status == 0) {
        if (big == NULL) {
            /* Find empty slot */
            for (int i = 0; i < HCI_ISOC_MAX_BIG; i++) {
                if (!isoc_ctx.big[i].in_use) {
                    big = &isoc_ctx.big[i];
                    big->in_use = true;
                    big->is_source = true;
                    isoc_ctx.num_active_big++;
                    break;
                }
            }
        }

        if (big != NULL) {
            big->info.big_handle = big_handle;
            big->info.state = BIG_STATE_ACTIVE;

            /* Parse BIG parameters */
            big->info.big_sync_delay = data[2] | (data[3] << 8) | (data[4] << 16);
            big->info.transport_latency = data[5] | (data[6] << 8) | (data[7] << 16);
            big->info.phy = data[8];
            big->info.nse = data[9];
            big->info.bn = data[10];
            big->info.pto = data[11];
            big->info.irc = data[12];
            big->info.max_pdu = data[13] | (data[14] << 8);
            big->info.iso_interval = data[15] | (data[16] << 8);

            uint8_t num_bis = data[17];
            big->info.num_bis = num_bis;

            /* Parse BIS handles */
            for (int i = 0; i < num_bis && i < HCI_ISOC_MAX_BIS_PER_BIG; i++) {
                big->info.bis_handles[i] = data[18 + i*2] | (data[19 + i*2] << 8);
            }

            isoc_ctx.stats.big_created++;

            /* Dispatch event */
            hci_isoc_event_t event = {
                .type = HCI_ISOC_EVENT_BIG_CREATED,
                .data.big_info = big->info
            };
            dispatch_event(&event);
        }
    } else {
        /* BIG creation failed */
        hci_isoc_event_t event = {
            .type = HCI_ISOC_EVENT_ERROR,
            .data.error_code = status
        };
        dispatch_event(&event);
    }
}

/**
 * @brief Handle LE Terminate BIG Complete event
 */
static void handle_terminate_big_complete(const uint8_t *data, uint16_t len)
{
    if (len < 2) {
        return;
    }

    uint8_t big_handle = data[0];
    uint8_t reason = data[1];

    big_state_struct_t *big = find_big(big_handle);

    if (big != NULL) {
        big->info.state = BIG_STATE_IDLE;
        big->in_use = false;
        isoc_ctx.num_active_big--;
        isoc_ctx.stats.big_terminated++;

        hci_isoc_event_t event = {
            .type = HCI_ISOC_EVENT_BIG_TERMINATED,
            .data.big_info = big->info
        };
        dispatch_event(&event);
    }

    (void)reason;
}

/**
 * @brief Handle LE BIG Sync Established event
 */
static void handle_big_sync_established(const uint8_t *data, uint16_t len)
{
    if (len < 16) {
        return;
    }

    uint8_t status = data[0];
    uint8_t big_handle = data[1];

    if (status == 0) {
        big_state_struct_t *big = find_big(big_handle);

        if (big == NULL) {
            /* Find empty slot */
            for (int i = 0; i < HCI_ISOC_MAX_BIG; i++) {
                if (!isoc_ctx.big[i].in_use) {
                    big = &isoc_ctx.big[i];
                    big->in_use = true;
                    big->is_source = false;  /* Sink */
                    isoc_ctx.num_active_big++;
                    break;
                }
            }
        }

        if (big != NULL) {
            big->info.big_handle = big_handle;
            big->info.state = BIG_STATE_ACTIVE;

            /* Parse sync parameters */
            big->info.transport_latency = data[2] | (data[3] << 8) | (data[4] << 16);
            big->info.nse = data[5];
            big->info.bn = data[6];
            big->info.pto = data[7];
            big->info.irc = data[8];
            big->info.max_pdu = data[9] | (data[10] << 8);
            big->info.iso_interval = data[11] | (data[12] << 8);

            uint8_t num_bis = data[13];
            big->info.num_bis = num_bis;

            for (int i = 0; i < num_bis && i < HCI_ISOC_MAX_BIS_PER_BIG; i++) {
                big->info.bis_handles[i] = data[14 + i*2] | (data[15 + i*2] << 8);
            }

            hci_isoc_event_t event = {
                .type = HCI_ISOC_EVENT_BIG_SYNC_ESTABLISHED,
                .data.big_info = big->info
            };
            dispatch_event(&event);
        }
    } else {
        hci_isoc_event_t event = {
            .type = HCI_ISOC_EVENT_ERROR,
            .data.error_code = status
        };
        dispatch_event(&event);
    }
}

/**
 * @brief Handle LE BIG Sync Lost event
 */
static void handle_big_sync_lost(const uint8_t *data, uint16_t len)
{
    if (len < 2) {
        return;
    }

    uint8_t big_handle = data[0];
    uint8_t reason = data[1];

    big_state_struct_t *big = find_big(big_handle);

    if (big != NULL && !big->is_source) {
        big->info.state = BIG_STATE_IDLE;
        big->in_use = false;
        isoc_ctx.num_active_big--;

        hci_isoc_event_t event = {
            .type = HCI_ISOC_EVENT_BIG_SYNC_LOST,
            .data.big_info = big->info
        };
        dispatch_event(&event);
    }

    (void)reason;
}

/**
 * @brief Handle Disconnection Complete event (for CIS)
 */
static void handle_disconnection_complete(const uint8_t *data, uint16_t len)
{
    if (len < 4) {
        return;
    }

    uint8_t status = data[0];
    uint16_t handle = data[1] | (data[2] << 8);
    uint8_t reason = data[3];

    (void)status;
    (void)reason;

    /* Check if this is a CIS handle */
    cis_info_t *cis = find_cis(handle);
    if (cis != NULL) {
        cis->state = CIS_STATE_IDLE;
        cis->cis_handle = HCI_ISOC_INVALID_HANDLE;
        isoc_ctx.stats.cis_disconnected++;

        hci_isoc_event_t event = {
            .type = HCI_ISOC_EVENT_CIS_DISCONNECTED,
            .data.handle = handle
        };
        dispatch_event(&event);
    }
}

/**
 * @brief Process LE Meta Event (called from bt_init HCI handler)
 *
 * This function should be called from the main BT event handler
 * when an LE Meta event related to ISOC is received.
 */
void hci_isoc_process_le_meta_event(uint8_t subevent, const uint8_t *data, uint16_t len)
{
    switch (subevent) {
        case HCI_LE_CIS_ESTABLISHED:
            handle_cis_established(data, len);
            break;

        case HCI_LE_CIS_REQUEST:
            handle_cis_request(data, len);
            break;

        case HCI_LE_CREATE_BIG_COMPLETE:
            handle_create_big_complete(data, len);
            break;

        case HCI_LE_TERMINATE_BIG_COMPLETE:
            handle_terminate_big_complete(data, len);
            break;

        case HCI_LE_BIG_SYNC_ESTABLISHED:
            handle_big_sync_established(data, len);
            break;

        case HCI_LE_BIG_SYNC_LOST:
            handle_big_sync_lost(data, len);
            break;

        default:
            /* Unknown subevent */
            break;
    }
}

/**
 * @brief Process ISO Data received (called from HCI layer)
 */
void hci_isoc_process_rx_data(uint16_t handle, uint8_t pb_flag, uint8_t ts_flag,
                               uint32_t timestamp, uint16_t seq_num,
                               const uint8_t *data, uint16_t len)
{
    isoc_ctx.stats.iso_rx_packets++;
    isoc_ctx.stats.iso_rx_bytes += len;

    /* Create packet structure */
    iso_data_packet_t packet = {
        .handle = handle,
        .pb_flag = pb_flag,
        .ts_flag = ts_flag,
        .timestamp = timestamp,
        .packet_seq_num = seq_num,
        .sdu_length = len,
        .data = (uint8_t *)data
    };

    /* Dispatch to callback */
    hci_isoc_event_t event = {
        .type = HCI_ISOC_EVENT_RX_DATA,
        .data.rx_data = packet
    };
    dispatch_event(&event);
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

/**
 * @brief Find CIG by ID
 */
static cig_state_t* find_cig(uint8_t cig_id)
{
    for (int i = 0; i < HCI_ISOC_MAX_CIG; i++) {
        if (isoc_ctx.cig[i].in_use && isoc_ctx.cig[i].cig_id == cig_id) {
            return &isoc_ctx.cig[i];
        }
    }
    return NULL;
}

/**
 * @brief Find CIG by CIS handle
 */
static cig_state_t* find_cig_by_cis_handle(uint16_t cis_handle)
{
    for (int i = 0; i < HCI_ISOC_MAX_CIG; i++) {
        if (!isoc_ctx.cig[i].in_use) continue;

        for (int j = 0; j < isoc_ctx.cig[i].num_cis; j++) {
            if (isoc_ctx.cig[i].cis_handles[j] == cis_handle) {
                return &isoc_ctx.cig[i];
            }
        }
    }
    return NULL;
}

/**
 * @brief Find CIS by handle
 */
static cis_info_t* find_cis(uint16_t cis_handle)
{
    for (int i = 0; i < HCI_ISOC_MAX_CIG; i++) {
        if (!isoc_ctx.cig[i].in_use) continue;

        for (int j = 0; j < isoc_ctx.cig[i].num_cis; j++) {
            if (isoc_ctx.cig[i].cis_info[j].cis_handle == cis_handle) {
                return &isoc_ctx.cig[i].cis_info[j];
            }
        }
    }
    return NULL;
}

/**
 * @brief Find BIG by handle
 */
static big_state_struct_t* find_big(uint8_t big_handle)
{
    for (int i = 0; i < HCI_ISOC_MAX_BIG; i++) {
        if (isoc_ctx.big[i].in_use &&
            isoc_ctx.big[i].info.big_handle == big_handle) {
            return &isoc_ctx.big[i];
        }
    }
    return NULL;
}

/*******************************************************************************
 * Public API - Initialization
 ******************************************************************************/

/*******************************************************************************
 * WICED ISOC Callback Handler
 ******************************************************************************/

/**
 * @brief Handle WICED ISOC events and translate to internal events
 */
static void wiced_isoc_callback(wiced_ble_isoc_event_t event_id, wiced_ble_isoc_event_data_t *p_event_data)
{
    hci_isoc_event_t event;
    memset(&event, 0, sizeof(event));

    printf("ISOC: WICED event %d\n", event_id);

    switch (event_id) {
        case WICED_BLE_ISOC_CIS_REQUEST_EVT:
            event.type = HCI_ISOC_EVENT_CIS_REQUEST;
            event.data.cis_request.acl_handle = p_event_data->cis_request.acl_conn_handle;
            event.data.cis_request.cig_id = p_event_data->cis_request.cig_id;
            event.data.cis_request.cis_id = p_event_data->cis_request.cis_id;
            event.data.cis_request.cis_handle = p_event_data->cis_request.cis_conn_handle;
            dispatch_event(&event);
            break;

        case WICED_BLE_ISOC_CIS_ESTABLISHED_EVT:
            handle_cis_established((const uint8_t*)p_event_data, sizeof(*p_event_data));
            break;

        case WICED_BLE_ISOC_CIS_DISCONNECTED_EVT:
            {
                cis_info_t *cis = find_cis(p_event_data->cis_disconnect.cis.cis_conn_handle);
                if (cis != NULL) {
                    cis->state = CIS_STATE_IDLE;
                    event.type = HCI_ISOC_EVENT_CIS_DISCONNECTED;
                    event.data.cis_info = *cis;
                    dispatch_event(&event);
                    isoc_ctx.stats.cis_disconnected++;
                }
            }
            break;

        case WICED_BLE_ISOC_BIG_CREATED_EVT:
            handle_create_big_complete((const uint8_t*)p_event_data, sizeof(*p_event_data));
            break;

        case WICED_BLE_ISOC_BIG_TERMINATED_EVT:
            handle_terminate_big_complete((const uint8_t*)p_event_data, sizeof(*p_event_data));
            break;

        case WICED_BLE_ISOC_BIG_SYNC_ESTABLISHED_EVT:
            {
                /* BIG Sync established (broadcast sink) */
                wiced_ble_isoc_big_sync_established_evt_t *p = &p_event_data->big_sync_established;
                
                big_state_struct_t *big = find_big(p->big_handle);
                if (big == NULL) {
                    /* Allocate new BIG for sink */
                    for (int i = 0; i < HCI_ISOC_MAX_BIG; i++) {
                        if (!isoc_ctx.big[i].in_use) {
                            big = &isoc_ctx.big[i];
                            big->in_use = true;
                            big->is_source = false;  /* Sink */
                            isoc_ctx.num_active_big++;
                            break;
                        }
                    }
                }
                
                if (big != NULL) {
                    big->info.big_handle = p->big_handle;
                    big->info.state = BIG_STATE_ACTIVE;
                    big->info.num_bis = p->num_bis;
                    big->info.transport_latency = p->trans_latency;
                    /* nse, bn, pto, irc not available in BTSTACK 4.x BIG sync event */
                    big->info.nse = 0;
                    big->info.bn = 0;
                    big->info.pto = 0;
                    big->info.irc = 0;
                    big->info.max_pdu = p->max_pdu;
                    big->info.iso_interval = p->iso_interval;

                    /* Copy BIS handles */
                    for (int i = 0; i < p->num_bis && i < HCI_ISOC_MAX_BIS_PER_BIG; i++) {
                        big->info.bis_handles[i] = p->bis_conn_hdl_list[i];
                    }
                    
                    event.type = HCI_ISOC_EVENT_BIG_SYNC_ESTABLISHED;
                    event.data.big_info = big->info;
                    dispatch_event(&event);
                }
            }
            break;

        case WICED_BLE_ISOC_BIG_SYNC_LOST_EVT:
            {
                big_state_struct_t *big = find_big(p_event_data->big_sync_lost.big_handle);
                if (big != NULL) {
                    event.type = HCI_ISOC_EVENT_BIG_SYNC_LOST;
                    event.data.big_info = big->info;
                    dispatch_event(&event);

                    /* Mark as inactive */
                    big->in_use = false;
                    big->info.state = BIG_STATE_IDLE;
                    isoc_ctx.num_active_big--;
                }
            }
            break;

        case WICED_BLE_ISOC_DATA_PATH_SETUP_EVT:
            event.type = HCI_ISOC_EVENT_DATA_PATH_SETUP;
            event.data.handle = 0;  /* Handle not provided in this event */
            dispatch_event(&event);
            break;

        default:
            printf("ISOC: Unhandled WICED event %d\n", event_id);
            break;
    }
}

/**
 * @brief Handle WICED ISOC RX data
 */
static void wiced_isoc_rx_callback(uint8_t *p_data, uint32_t length)
{
    /* Parse ISO data header and dispatch */
    if (p_data != NULL && length >= 4) {
        uint16_t handle = p_data[0] | ((p_data[1] & 0x0F) << 8);
        uint8_t pb_flag = (p_data[1] >> 4) & 0x03;
        uint8_t ts_flag = (p_data[1] >> 6) & 0x01;
        
        uint16_t offset = 4;
        uint32_t timestamp = 0;
        uint16_t seq_num = 0;
        
        if (ts_flag) {
            timestamp = p_data[4] | (p_data[5] << 8) | (p_data[6] << 16) | (p_data[7] << 24);
            offset += 4;
        }
        
        seq_num = p_data[offset] | (p_data[offset+1] << 8);
        offset += 2;
        
        uint16_t sdu_len = p_data[offset] | ((p_data[offset+1] & 0x0F) << 8);
        offset += 2;
        
        hci_isoc_process_rx_data(handle, pb_flag, ts_flag, timestamp, seq_num,
                                  &p_data[offset], sdu_len);
    }
}

/**
 * @brief Handle WICED ISOC TX complete
 * @param p_buf Pointer to transmitted buffer
 * @return Number of completed packets (always 1)
 */
static unsigned int wiced_isoc_num_complete_callback(uint8_t *p_buf)
{
    (void)p_buf;
    isoc_ctx.stats.iso_tx_packets++;
    return 1;
}

/*******************************************************************************
 * API Implementation
 ******************************************************************************/

int hci_isoc_init(void)
{
    if (isoc_ctx.initialized) {
        return HCI_ISOC_OK;
    }

    memset(&isoc_ctx, 0, sizeof(isoc_ctx));

    /* Create FreeRTOS semaphore for HCI command synchronization */
    isoc_ctx.cmd_semaphore = xSemaphoreCreateBinary();
    if (isoc_ctx.cmd_semaphore == NULL) {
        printf("ISOC: Failed to create command semaphore\n");
        return HCI_ISOC_ERROR_NO_RESOURCES;
    }

    /* Initialize ISOC with BTSTACK 4.x API
     * Register our callback to receive ISOC events from the stack.
     */
    static wiced_ble_isoc_cfg_t isoc_cfg = {
        .max_bis = HCI_ISOC_MAX_BIS_PER_BIG * HCI_ISOC_MAX_BIG
    };
    wiced_ble_isoc_init(&isoc_cfg, wiced_isoc_callback);
    
    /* Register ISO data callbacks for RX and TX complete */
    wiced_ble_isoc_register_data_cb(wiced_isoc_rx_callback, wiced_isoc_num_complete_callback);

    printf("ISOC: Initialized\n");
    isoc_ctx.initialized = true;

    return HCI_ISOC_OK;
}

void hci_isoc_deinit(void)
{
    if (!isoc_ctx.initialized) {
        return;
    }

    /* Terminate all active BIG */
    for (int i = 0; i < HCI_ISOC_MAX_BIG; i++) {
        if (isoc_ctx.big[i].in_use) {
            hci_isoc_terminate_big(isoc_ctx.big[i].info.big_handle, 0x13);
        }
    }

    /* Disconnect all CIS and remove CIG */
    for (int i = 0; i < HCI_ISOC_MAX_CIG; i++) {
        if (isoc_ctx.cig[i].in_use) {
            for (int j = 0; j < isoc_ctx.cig[i].num_cis; j++) {
                if (isoc_ctx.cig[i].cis_info[j].state == CIS_STATE_ESTABLISHED) {
                    hci_isoc_disconnect_cis(isoc_ctx.cig[i].cis_handles[j], 0x13);
                }
            }
            hci_isoc_remove_cig(isoc_ctx.cig[i].cig_id);
        }
    }

    /* Delete FreeRTOS semaphore */
    if (isoc_ctx.cmd_semaphore != NULL) {
        vSemaphoreDelete(isoc_ctx.cmd_semaphore);
        isoc_ctx.cmd_semaphore = NULL;
    }

    printf("ISOC: Deinitialized\n");
    isoc_ctx.initialized = false;
}

void hci_isoc_register_callback(hci_isoc_callback_t callback, void *user_data)
{
    /* Backward compatibility: register in first slot */
    isoc_ctx.callbacks[0].callback = callback;
    isoc_ctx.callbacks[0].user_data = user_data;
    isoc_ctx.callbacks[0].in_use = (callback != NULL);
}

int hci_isoc_register_callback_ex(hci_isoc_callback_t callback, void *user_data)
{
    if (callback == NULL) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    /* Find a free slot */
    for (int i = 0; i < HCI_ISOC_MAX_CALLBACKS; i++) {
        if (!isoc_ctx.callbacks[i].in_use) {
            isoc_ctx.callbacks[i].callback = callback;
            isoc_ctx.callbacks[i].user_data = user_data;
            isoc_ctx.callbacks[i].in_use = true;
            return HCI_ISOC_OK;
        }
    }

    return HCI_ISOC_ERROR_NO_RESOURCES;
}

int hci_isoc_unregister_callback(hci_isoc_callback_t callback)
{
    if (callback == NULL) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    /* Find and remove the callback */
    for (int i = 0; i < HCI_ISOC_MAX_CALLBACKS; i++) {
        if (isoc_ctx.callbacks[i].in_use && isoc_ctx.callbacks[i].callback == callback) {
            isoc_ctx.callbacks[i].callback = NULL;
            isoc_ctx.callbacks[i].user_data = NULL;
            isoc_ctx.callbacks[i].in_use = false;
            return HCI_ISOC_OK;
        }
    }

    return HCI_ISOC_ERROR_CALLBACK_NOT_FOUND;
}

/*******************************************************************************
 * Public API - CIG Management
 ******************************************************************************/

int hci_isoc_set_cig_params(const cig_config_t *config)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL || config->num_cis == 0 ||
        config->num_cis > HCI_ISOC_MAX_CIS_PER_CIG) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    /* Check if CIG already exists */
    if (find_cig(config->cig_id) != NULL) {
        return HCI_ISOC_ERROR_CIG_EXISTS;
    }

    /* Find empty slot */
    cig_state_t *cig = NULL;
    for (int i = 0; i < HCI_ISOC_MAX_CIG; i++) {
        if (!isoc_ctx.cig[i].in_use) {
            cig = &isoc_ctx.cig[i];
            break;
        }
    }

    if (cig == NULL) {
        return HCI_ISOC_ERROR_NO_RESOURCES;
    }

    /*
     * Build HCI_LE_Set_CIG_Parameters command
     *
     * Parameters (from BT Core Spec 5.4):
     * - CIG_ID: 1 byte
     * - SDU_Interval_C_To_P: 3 bytes
     * - SDU_Interval_P_To_C: 3 bytes
     * - Worst_Case_SCA: 1 byte
     * - Packing: 1 byte
     * - Framing: 1 byte
     * - Max_Transport_Latency_C_To_P: 2 bytes
     * - Max_Transport_Latency_P_To_C: 2 bytes
     * - CIS_Count: 1 byte
     * - CIS[i] parameters...
     */

    uint8_t params[256];
    uint16_t pos = 0;

    params[pos++] = config->cig_id;

    /* SDU Interval C to P (3 bytes, little endian) */
    params[pos++] = config->sdu_interval_c_to_p & 0xFF;
    params[pos++] = (config->sdu_interval_c_to_p >> 8) & 0xFF;
    params[pos++] = (config->sdu_interval_c_to_p >> 16) & 0xFF;

    /* SDU Interval P to C (3 bytes) */
    params[pos++] = config->sdu_interval_p_to_c & 0xFF;
    params[pos++] = (config->sdu_interval_p_to_c >> 8) & 0xFF;
    params[pos++] = (config->sdu_interval_p_to_c >> 16) & 0xFF;

    params[pos++] = config->sca;
    params[pos++] = config->packing;
    params[pos++] = config->framing;

    params[pos++] = config->max_transport_latency_c_to_p & 0xFF;
    params[pos++] = (config->max_transport_latency_c_to_p >> 8) & 0xFF;

    params[pos++] = config->max_transport_latency_p_to_c & 0xFF;
    params[pos++] = (config->max_transport_latency_p_to_c >> 8) & 0xFF;

    params[pos++] = config->num_cis;

    /* CIS parameters */
    for (int i = 0; i < config->num_cis; i++) {
        const cis_config_t *cis_cfg = &config->cis[i];

        params[pos++] = cis_cfg->cis_id;
        params[pos++] = cis_cfg->max_sdu_c_to_p & 0xFF;
        params[pos++] = (cis_cfg->max_sdu_c_to_p >> 8) & 0xFF;
        params[pos++] = cis_cfg->max_sdu_p_to_c & 0xFF;
        params[pos++] = (cis_cfg->max_sdu_p_to_c >> 8) & 0xFF;
        params[pos++] = cis_cfg->phy_c_to_p;
        params[pos++] = cis_cfg->phy_p_to_c;
        params[pos++] = cis_cfg->rtn_c_to_p;
        params[pos++] = cis_cfg->rtn_p_to_c;
    }

    int result = hci_send_command(HCI_LE_SET_CIG_PARAMS, params, pos);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    result = hci_wait_command_complete(HCI_CMD_TIMEOUT_MS);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    /* Store CIG state */
    cig->in_use = true;
    cig->cig_id = config->cig_id;
    cig->num_cis = config->num_cis;

    /* Parse returned CIS handles from command complete */
    /* Response: Status (1) + CIG_ID (1) + CIS_Count (1) + CIS_Handles (2 * num) */
    for (int i = 0; i < config->num_cis; i++) {
        /* In real implementation, parse from isoc_ctx.cmd_response */
        cig->cis_handles[i] = 0x0100 + i;  /* Placeholder handles */

        cig->cis_info[i].cig_id = config->cig_id;
        cig->cis_info[i].cis_id = config->cis[i].cis_id;
        cig->cis_info[i].cis_handle = cig->cis_handles[i];
        cig->cis_info[i].state = CIS_STATE_CONFIGURED;
    }

    isoc_ctx.num_active_cig++;

    /* Dispatch event */
    hci_isoc_event_t event = {
        .type = HCI_ISOC_EVENT_CIG_CREATED
    };
    dispatch_event(&event);

    return HCI_ISOC_OK;
}

int hci_isoc_create_cis(uint8_t cig_id, uint8_t num_cis,
                        const uint16_t *cis_handles,
                        const uint16_t *acl_handles)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    if (cis_handles == NULL || acl_handles == NULL || num_cis == 0) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    cig_state_t *cig = find_cig(cig_id);
    if (cig == NULL) {
        return HCI_ISOC_ERROR_CIG_NOT_FOUND;
    }

    /*
     * Build HCI_LE_Create_CIS command
     *
     * Parameters:
     * - CIS_Count: 1 byte
     * - CIS_Connection_Handle[i]: 2 bytes
     * - ACL_Connection_Handle[i]: 2 bytes
     */

    uint8_t params[1 + num_cis * 4];
    uint16_t pos = 0;

    params[pos++] = num_cis;

    for (int i = 0; i < num_cis; i++) {
        params[pos++] = cis_handles[i] & 0xFF;
        params[pos++] = (cis_handles[i] >> 8) & 0xFF;
        params[pos++] = acl_handles[i] & 0xFF;
        params[pos++] = (acl_handles[i] >> 8) & 0xFF;

        /* Update CIS state */
        for (int j = 0; j < cig->num_cis; j++) {
            if (cig->cis_handles[j] == cis_handles[i]) {
                cig->cis_info[j].acl_handle = acl_handles[i];
                cig->cis_info[j].state = CIS_STATE_CONNECTING;
                break;
            }
        }
    }

    int result = hci_send_command(HCI_LE_CREATE_CIS, params, pos);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    /* Note: Don't wait for command complete here.
     * CIS establishment is async - we'll get CIS_ESTABLISHED events */

    return HCI_ISOC_OK;
}

int hci_isoc_accept_cis(uint16_t cis_handle)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[2];
    params[0] = cis_handle & 0xFF;
    params[1] = (cis_handle >> 8) & 0xFF;

    int result = hci_send_command(HCI_LE_ACCEPT_CIS_REQUEST, params, 2);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    return HCI_ISOC_OK;
}

int hci_isoc_reject_cis(uint16_t cis_handle, uint8_t reason)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[3];
    params[0] = cis_handle & 0xFF;
    params[1] = (cis_handle >> 8) & 0xFF;
    params[2] = reason;

    int result = hci_send_command(HCI_LE_REJECT_CIS_REQUEST, params, 3);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    return hci_wait_command_complete(HCI_CMD_TIMEOUT_MS);
}

int hci_isoc_disconnect_cis(uint16_t cis_handle, uint8_t reason)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    cis_info_t *cis = find_cis(cis_handle);
    if (cis == NULL) {
        return HCI_ISOC_ERROR_CIS_NOT_FOUND;
    }

    cis->state = CIS_STATE_DISCONNECTING;

    uint8_t params[3];
    params[0] = cis_handle & 0xFF;
    params[1] = (cis_handle >> 8) & 0xFF;
    params[2] = reason;

    int result = hci_send_command(HCI_DISCONNECT, params, 3);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    return HCI_ISOC_OK;
}

int hci_isoc_remove_cig(uint8_t cig_id)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    cig_state_t *cig = find_cig(cig_id);
    if (cig == NULL) {
        return HCI_ISOC_ERROR_CIG_NOT_FOUND;
    }

    /* Verify all CIS are disconnected */
    for (int i = 0; i < cig->num_cis; i++) {
        if (cig->cis_info[i].state == CIS_STATE_ESTABLISHED) {
            return HCI_ISOC_ERROR_INVALID_STATE;
        }
    }

    uint8_t params[1] = { cig_id };

    int result = hci_send_command(HCI_LE_REMOVE_CIG, params, 1);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    result = hci_wait_command_complete(HCI_CMD_TIMEOUT_MS);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    cig->in_use = false;
    isoc_ctx.num_active_cig--;

    hci_isoc_event_t event = {
        .type = HCI_ISOC_EVENT_CIG_REMOVED
    };
    dispatch_event(&event);

    return HCI_ISOC_OK;
}

int hci_isoc_get_cis_info(uint16_t cis_handle, cis_info_t *info)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    if (info == NULL) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    cis_info_t *cis = find_cis(cis_handle);
    if (cis == NULL) {
        return HCI_ISOC_ERROR_CIS_NOT_FOUND;
    }

    *info = *cis;
    return HCI_ISOC_OK;
}

/*******************************************************************************
 * Public API - BIG Management
 ******************************************************************************/

int hci_isoc_create_big(const big_config_t *config)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL || config->num_bis == 0 ||
        config->num_bis > HCI_ISOC_MAX_BIS_PER_BIG) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    /* Check if BIG already exists */
    if (find_big(config->big_handle) != NULL) {
        return HCI_ISOC_ERROR_BIG_EXISTS;
    }

    /* Find empty slot */
    big_state_struct_t *big = NULL;
    for (int i = 0; i < HCI_ISOC_MAX_BIG; i++) {
        if (!isoc_ctx.big[i].in_use) {
            big = &isoc_ctx.big[i];
            break;
        }
    }

    if (big == NULL) {
        return HCI_ISOC_ERROR_NO_RESOURCES;
    }

    /*
     * Build HCI_LE_Create_BIG command
     *
     * Parameters:
     * - BIG_Handle: 1 byte
     * - Advertising_Handle: 1 byte
     * - Num_BIS: 1 byte
     * - SDU_Interval: 3 bytes
     * - Max_SDU: 2 bytes
     * - Max_Transport_Latency: 2 bytes
     * - RTN: 1 byte
     * - PHY: 1 byte
     * - Packing: 1 byte
     * - Framing: 1 byte
     * - Encryption: 1 byte
     * - Broadcast_Code: 16 bytes
     */

    uint8_t params[31];
    uint16_t pos = 0;

    params[pos++] = config->big_handle;
    params[pos++] = config->adv_handle;
    params[pos++] = config->num_bis;

    params[pos++] = config->sdu_interval & 0xFF;
    params[pos++] = (config->sdu_interval >> 8) & 0xFF;
    params[pos++] = (config->sdu_interval >> 16) & 0xFF;

    params[pos++] = config->max_sdu & 0xFF;
    params[pos++] = (config->max_sdu >> 8) & 0xFF;

    params[pos++] = config->max_transport_latency & 0xFF;
    params[pos++] = (config->max_transport_latency >> 8) & 0xFF;

    params[pos++] = config->rtn;
    params[pos++] = config->phy;
    params[pos++] = config->packing;
    params[pos++] = config->framing;
    params[pos++] = config->encryption;

    memcpy(&params[pos], config->broadcast_code, 16);
    pos += 16;

    /* Mark as creating */
    big->in_use = true;
    big->is_source = true;
    big->info.big_handle = config->big_handle;
    big->info.state = BIG_STATE_CREATING;
    big->info.num_bis = config->num_bis;

    int result = hci_send_command(HCI_LE_CREATE_BIG, params, pos);
    if (result != HCI_ISOC_OK) {
        big->in_use = false;
        return result;
    }

    /* BIG creation is async - we'll get CREATE_BIG_COMPLETE event */

    return HCI_ISOC_OK;
}

int hci_isoc_terminate_big(uint8_t big_handle, uint8_t reason)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    big_state_struct_t *big = find_big(big_handle);
    if (big == NULL) {
        return HCI_ISOC_ERROR_BIG_NOT_FOUND;
    }

    big->info.state = BIG_STATE_TERMINATING;

    uint8_t params[2];
    params[0] = big_handle;
    params[1] = reason;

    int result = hci_send_command(HCI_LE_TERMINATE_BIG, params, 2);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    /* Async - wait for TERMINATE_BIG_COMPLETE event */

    return HCI_ISOC_OK;
}

int hci_isoc_big_create_sync(const big_sync_config_t *config)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL || config->num_bis == 0 ||
        config->num_bis > HCI_ISOC_MAX_BIS_PER_BIG) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    /*
     * Build HCI_LE_BIG_Create_Sync command
     *
     * Parameters:
     * - BIG_Handle: 1 byte
     * - Sync_Handle: 2 bytes
     * - Encryption: 1 byte
     * - Broadcast_Code: 16 bytes
     * - MSE: 1 byte
     * - BIG_Sync_Timeout: 2 bytes
     * - Num_BIS: 1 byte
     * - BIS[i]: 1 byte each
     */

    uint8_t params[24 + HCI_ISOC_MAX_BIS_PER_BIG];
    uint16_t pos = 0;

    params[pos++] = config->big_handle;
    params[pos++] = config->sync_handle & 0xFF;
    params[pos++] = (config->sync_handle >> 8) & 0xFF;
    params[pos++] = config->encryption;

    memcpy(&params[pos], config->broadcast_code, 16);
    pos += 16;

    params[pos++] = config->mse;
    params[pos++] = config->big_sync_timeout & 0xFF;
    params[pos++] = (config->big_sync_timeout >> 8) & 0xFF;
    params[pos++] = config->num_bis;

    for (int i = 0; i < config->num_bis; i++) {
        params[pos++] = config->bis_indices[i];
    }

    int result = hci_send_command(HCI_LE_BIG_CREATE_SYNC, params, pos);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    /* Async - wait for BIG_SYNC_ESTABLISHED event */

    return HCI_ISOC_OK;
}

int hci_isoc_big_terminate_sync(uint8_t big_handle)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    big_state_struct_t *big = find_big(big_handle);
    if (big == NULL || big->is_source) {
        return HCI_ISOC_ERROR_BIG_NOT_FOUND;
    }

    uint8_t params[1] = { big_handle };

    int result = hci_send_command(HCI_LE_BIG_TERMINATE_SYNC, params, 1);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    result = hci_wait_command_complete(HCI_CMD_TIMEOUT_MS);
    if (result == HCI_ISOC_OK) {
        big->in_use = false;
        big->info.state = BIG_STATE_IDLE;
        isoc_ctx.num_active_big--;
    }

    return result;
}

int hci_isoc_get_big_info(uint8_t big_handle, big_info_t *info)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    if (info == NULL) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    big_state_struct_t *big = find_big(big_handle);
    if (big == NULL) {
        return HCI_ISOC_ERROR_BIG_NOT_FOUND;
    }

    *info = big->info;
    return HCI_ISOC_OK;
}

/*******************************************************************************
 * Public API - ISO Data Path
 ******************************************************************************/

int hci_isoc_setup_data_path(uint16_t handle, uint8_t direction,
                              uint8_t data_path_id, const uint8_t *codec_id,
                              uint32_t controller_delay,
                              const uint8_t *codec_config, uint8_t codec_config_len)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    /*
     * Build HCI_LE_Setup_ISO_Data_Path command
     *
     * Parameters:
     * - Connection_Handle: 2 bytes
     * - Data_Path_Direction: 1 byte
     * - Data_Path_ID: 1 byte
     * - Codec_ID: 5 bytes
     * - Controller_Delay: 3 bytes
     * - Codec_Configuration_Length: 1 byte
     * - Codec_Configuration: variable
     */

    uint8_t params[13 + 255];
    uint16_t pos = 0;

    params[pos++] = handle & 0xFF;
    params[pos++] = (handle >> 8) & 0xFF;
    params[pos++] = direction;
    params[pos++] = data_path_id;

    /* Codec ID (5 bytes) */
    if (codec_id != NULL) {
        memcpy(&params[pos], codec_id, 5);
    } else {
        /* Transparent codec (pass-through) */
        memset(&params[pos], 0, 5);
        params[pos] = 0x03;  /* Transparent */
    }
    pos += 5;

    /* Controller delay (3 bytes) */
    params[pos++] = controller_delay & 0xFF;
    params[pos++] = (controller_delay >> 8) & 0xFF;
    params[pos++] = (controller_delay >> 16) & 0xFF;

    params[pos++] = codec_config_len;

    if (codec_config != NULL && codec_config_len > 0) {
        memcpy(&params[pos], codec_config, codec_config_len);
        pos += codec_config_len;
    }

    int result = hci_send_command(HCI_LE_SETUP_ISO_DATA_PATH, params, pos);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    result = hci_wait_command_complete(HCI_CMD_TIMEOUT_MS);
    if (result == HCI_ISOC_OK) {
        hci_isoc_event_t event = {
            .type = HCI_ISOC_EVENT_DATA_PATH_SETUP,
            .data.handle = handle
        };
        dispatch_event(&event);
    }

    return result;
}

int hci_isoc_remove_data_path(uint16_t handle, uint8_t direction)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[3];
    params[0] = handle & 0xFF;
    params[1] = (handle >> 8) & 0xFF;
    params[2] = direction;

    int result = hci_send_command(HCI_LE_REMOVE_ISO_DATA_PATH, params, 3);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    return hci_wait_command_complete(HCI_CMD_TIMEOUT_MS);
}

/*******************************************************************************
 * Public API - ISO Data Transfer
 ******************************************************************************/

int hci_isoc_send_data(uint16_t handle, const uint8_t *data,
                       uint16_t length, uint32_t timestamp)
{
    return hci_isoc_send_data_ts(handle, data, length, timestamp,
                                  isoc_ctx.tx_seq_num[handle & 0xFF]++);
}

int hci_isoc_send_data_ts(uint16_t handle, const uint8_t *data,
                          uint16_t length, uint32_t timestamp,
                          uint16_t seq_num)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || length == 0 || length > HCI_ISOC_MAX_SDU_SIZE) {
        return HCI_ISOC_ERROR_INVALID_PARAM;
    }

    /*
     * Send ISO data via BTSTACK 4.x API
     *
     * wiced_ble_isoc_write_data_to_lower() sends a pre-formatted HCI ISO packet.
     * We need to build the HCI ISO data packet header ourselves.
     *
     * HCI ISO Data Packet format:
     * - Handle (12 bits) + PB Flag (2 bits) + TS Flag (1 bit) + reserved (1 bit) = 2 bytes
     * - Data length (14 bits) + reserved (2 bits) = 2 bytes
     * - Timestamp (4 bytes, if TS flag set)
     * - Sequence number (2 bytes)
     * - SDU length (12 bits) + reserved (4 bits) = 2 bytes (framed only)
     * - Data
     */
    uint8_t iso_pkt[4 + length];  /* Header + data */
    uint16_t pkt_len = 0;

    /* Build HCI ISO header */
    /* Handle + PB=00 (start) + TS=0 */
    iso_pkt[0] = handle & 0xFF;
    iso_pkt[1] = ((handle >> 8) & 0x0F);  /* PB=00, TS=0 */
    /* Data length */
    iso_pkt[2] = length & 0xFF;
    iso_pkt[3] = (length >> 8) & 0x3F;
    pkt_len = 4;

    /* Copy data */
    memcpy(&iso_pkt[pkt_len], data, length);
    pkt_len += length;

    wiced_bool_t success = wiced_ble_isoc_write_data_to_lower(iso_pkt, pkt_len);

    if (!success) {
        printf("ISOC: Failed to send ISO data on handle 0x%04X\n", handle);
        isoc_ctx.stats.iso_tx_failed++;
        return HCI_ISOC_ERROR_COMMAND_FAILED;
    }

    (void)timestamp;  /* Timestamp handled by controller */
    (void)seq_num;    /* Sequence number tracked internally */

    isoc_ctx.stats.iso_tx_packets++;
    isoc_ctx.stats.iso_tx_bytes += length;

    /* Dispatch TX complete event */
    hci_isoc_event_t event = {
        .type = HCI_ISOC_EVENT_TX_COMPLETE,
        .data.handle = handle
    };
    dispatch_event(&event);

    return HCI_ISOC_OK;
}

int hci_isoc_read_link_quality(uint16_t handle,
                                uint32_t *tx_unacked_packets,
                                uint32_t *tx_flushed_packets,
                                uint32_t *tx_last_subevent_packets,
                                uint32_t *retransmitted_packets,
                                uint32_t *crc_error_packets,
                                uint32_t *rx_unreceived_packets,
                                uint32_t *duplicate_packets)
{
    if (!isoc_ctx.initialized) {
        return HCI_ISOC_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[2];
    params[0] = handle & 0xFF;
    params[1] = (handle >> 8) & 0xFF;

    int result = hci_send_command(HCI_LE_READ_ISO_LINK_QUALITY, params, 2);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    result = hci_wait_command_complete(HCI_CMD_TIMEOUT_MS);
    if (result != HCI_ISOC_OK) {
        return result;
    }

    /*
     * Parse response:
     * - Status: 1 byte
     * - Connection_Handle: 2 bytes
     * - TX_UnACKed_Packets: 4 bytes
     * - TX_Flushed_Packets: 4 bytes
     * - TX_Last_Subevent_Packets: 4 bytes
     * - Retransmitted_Packets: 4 bytes
     * - CRC_Error_Packets: 4 bytes
     * - RX_Unreceived_Packets: 4 bytes
     * - Duplicate_Packets: 4 bytes
     */

    /* Placeholder values */
    if (tx_unacked_packets) *tx_unacked_packets = 0;
    if (tx_flushed_packets) *tx_flushed_packets = 0;
    if (tx_last_subevent_packets) *tx_last_subevent_packets = 0;
    if (retransmitted_packets) *retransmitted_packets = 0;
    if (crc_error_packets) *crc_error_packets = 0;
    if (rx_unreceived_packets) *rx_unreceived_packets = 0;
    if (duplicate_packets) *duplicate_packets = 0;

    return HCI_ISOC_OK;
}

/*******************************************************************************
 * Public API - Utilities
 ******************************************************************************/

bool hci_isoc_is_cis_handle(uint16_t handle)
{
    return find_cis(handle) != NULL;
}

bool hci_isoc_is_bis_handle(uint16_t handle)
{
    for (int i = 0; i < HCI_ISOC_MAX_BIG; i++) {
        if (!isoc_ctx.big[i].in_use) continue;

        for (int j = 0; j < isoc_ctx.big[i].info.num_bis; j++) {
            if (isoc_ctx.big[i].info.bis_handles[j] == handle) {
                return true;
            }
        }
    }
    return false;
}

uint8_t hci_isoc_get_active_cis_count(void)
{
    uint8_t count = 0;

    for (int i = 0; i < HCI_ISOC_MAX_CIG; i++) {
        if (!isoc_ctx.cig[i].in_use) continue;

        for (int j = 0; j < isoc_ctx.cig[i].num_cis; j++) {
            if (isoc_ctx.cig[i].cis_info[j].state == CIS_STATE_ESTABLISHED) {
                count++;
            }
        }
    }

    return count;
}

uint8_t hci_isoc_get_active_big_count(void)
{
    return isoc_ctx.num_active_big;
}

void hci_isoc_get_stats(hci_isoc_stats_t *stats)
{
    if (stats != NULL) {
        *stats = isoc_ctx.stats;
    }
}

void hci_isoc_reset_stats(void)
{
    memset(&isoc_ctx.stats, 0, sizeof(isoc_ctx.stats));
}
