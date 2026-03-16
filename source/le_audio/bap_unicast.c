/**
 * @file bap_unicast.c
 * @brief BAP Unicast Client/Server Implementation
 *
 * Implements BAP Unicast roles for connected isochronous audio streaming.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bap_unicast.h"
#include "../bluetooth/hci_isoc.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Platform Includes
 ******************************************************************************/

/* Infineon BTSTACK headers */
#include "wiced_bt_gatt.h"
#include "wiced_bt_ble.h"

/* FreeRTOS */
#ifdef FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#else
typedef void* SemaphoreHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#endif

/*******************************************************************************
 * Constants
 ******************************************************************************/

/** GATT operation timeout */
#define GATT_TIMEOUT_MS         5000

/** CIS creation timeout */
#define CIS_TIMEOUT_MS          10000

/** LTV Types for Codec Specific Configuration */
#define LTV_SAMPLING_FREQ       0x01
#define LTV_FRAME_DURATION      0x02
#define LTV_AUDIO_LOCATIONS     0x03
#define LTV_OCTETS_PER_FRAME    0x04
#define LTV_FRAMES_PER_SDU      0x05

/** LTV Types for Metadata */
#define LTV_PREFERRED_CONTEXT   0x01
#define LTV_STREAMING_CONTEXT   0x02

/** LC3 Codec ID */
#define CODEC_ID_LC3            0x06

/*******************************************************************************
 * Types
 ******************************************************************************/

/** ASCS characteristic handles */
typedef struct {
    uint16_t sink_ase[BAP_UNICAST_MAX_ASE];
    uint16_t sink_ase_ccc[BAP_UNICAST_MAX_ASE];
    uint16_t source_ase[BAP_UNICAST_MAX_ASE];
    uint16_t source_ase_ccc[BAP_UNICAST_MAX_ASE];
    uint16_t ase_cp;
    uint16_t ase_cp_ccc;
    uint8_t num_sink_ase;
    uint8_t num_source_ase;
} ascs_handles_t;

/** Connection context */
typedef struct {
    bool in_use;
    uint16_t conn_handle;
    uint8_t peer_addr[6];
    uint8_t peer_addr_type;
    bool ascs_discovered;
    ascs_handles_t ascs;
    bap_ase_t remote_ase[BAP_UNICAST_MAX_ASE * 2];  /* Sink + Source */
    uint8_t num_remote_ase;
} connection_ctx_t;

/** Module context */
typedef struct {
    bool initialized;

    /* Local ASEs (server role) */
    bap_ase_t local_ase[BAP_UNICAST_MAX_ASE];
    uint8_t num_local_ase;
    uint8_t next_ase_id;

    /* Remote connections */
    connection_ctx_t connections[BAP_UNICAST_MAX_CONNECTIONS];

    /* CIG configuration */
    bool cig_configured;
    uint8_t cig_id;

    /* Callback */
    bap_unicast_callback_t callback;
    void *callback_user_data;

    /* Statistics */
    bap_unicast_stats_t stats;

    /* Synchronization */
    SemaphoreHandle_t op_semaphore;
    volatile bool op_pending;
    volatile int op_result;

} bap_unicast_ctx_t;

/*******************************************************************************
 * Module Variables
 ******************************************************************************/

static bap_unicast_ctx_t unicast_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static void dispatch_event(const bap_unicast_event_t *event);
static connection_ctx_t* find_connection(uint16_t conn_handle);
static connection_ctx_t* alloc_connection(uint16_t conn_handle);
static void free_connection(uint16_t conn_handle);
static bap_ase_t* find_local_ase(uint8_t ase_id);
static bap_ase_t* find_remote_ase(uint16_t conn_handle, uint8_t ase_id);
static bap_ase_t* find_ase_by_cis(uint16_t cis_handle);
static int parse_lc3_codec_config(const uint8_t *data, uint8_t len, bap_lc3_codec_config_t *config);
static int build_codec_config_ltv(const bap_lc3_codec_config_t *config, uint8_t *out, uint8_t max_len);
static int write_ase_control_point(uint16_t conn_handle, uint8_t opcode,
                                    const uint8_t *params, uint16_t params_len);
static void handle_ase_notification(uint16_t conn_handle, uint8_t ase_id,
                                     const uint8_t *data, uint16_t len);
static void handle_ase_cp_notification(uint16_t conn_handle,
                                        const uint8_t *data, uint16_t len);
static void hci_isoc_event_handler(const hci_isoc_event_t *event, void *user_data);
static void set_ase_state(bap_ase_t *ase, bap_ase_state_t new_state);
static int configure_cig(void);

/*******************************************************************************
 * Event Handling
 ******************************************************************************/

/**
 * @brief Dispatch event to callback
 */
static void dispatch_event(const bap_unicast_event_t *event)
{
    if (unicast_ctx.callback != NULL) {
        unicast_ctx.callback(event, unicast_ctx.callback_user_data);
    }
}

/**
 * @brief Set ASE state and dispatch event
 */
static void set_ase_state(bap_ase_t *ase, bap_ase_state_t new_state)
{
    if (ase->state != new_state) {
        ase->state = new_state;

        bap_unicast_event_t event = {
            .type = BAP_UNICAST_EVENT_ASE_STATE_CHANGED,
            .conn_handle = ase->conn_handle,
            .data.ase = *ase
        };
        dispatch_event(&event);

        /* Dispatch specific events for major transitions */
        switch (new_state) {
            case BAP_ASE_STATE_CODEC_CONFIGURED:
                event.type = BAP_UNICAST_EVENT_CODEC_CONFIGURED;
                dispatch_event(&event);
                break;

            case BAP_ASE_STATE_QOS_CONFIGURED:
                event.type = BAP_UNICAST_EVENT_QOS_CONFIGURED;
                dispatch_event(&event);
                break;

            case BAP_ASE_STATE_ENABLING:
                event.type = BAP_UNICAST_EVENT_ENABLED;
                dispatch_event(&event);
                break;

            case BAP_ASE_STATE_STREAMING:
                event.type = BAP_UNICAST_EVENT_STREAMING;
                dispatch_event(&event);
                break;

            case BAP_ASE_STATE_DISABLING:
                event.type = BAP_UNICAST_EVENT_DISABLED;
                dispatch_event(&event);
                break;

            case BAP_ASE_STATE_IDLE:
                if (ase->cis_handle != 0) {
                    /* Was streaming, now released */
                    event.type = BAP_UNICAST_EVENT_RELEASED;
                    dispatch_event(&event);
                }
                break;

            default:
                break;
        }
    }
}

/**
 * @brief HCI ISOC event handler
 */
static void hci_isoc_event_handler(const hci_isoc_event_t *event, void *user_data)
{
    (void)user_data;

    switch (event->type) {
        case HCI_ISOC_EVENT_CIS_ESTABLISHED:
            {
                bap_ase_t *ase = find_ase_by_cis(event->data.cis_info.cis_handle);
                if (ase != NULL) {
                    unicast_ctx.stats.cis_established++;

                    bap_unicast_event_t ucast_event = {
                        .type = BAP_UNICAST_EVENT_CIS_ESTABLISHED,
                        .conn_handle = ase->conn_handle,
                        .data.ase = *ase
                    };
                    dispatch_event(&ucast_event);

                    /* Transition to streaming if enabled */
                    if (ase->state == BAP_ASE_STATE_ENABLING) {
                        set_ase_state(ase, BAP_ASE_STATE_STREAMING);
                    }
                }
            }
            break;

        case HCI_ISOC_EVENT_CIS_DISCONNECTED:
            {
                bap_ase_t *ase = find_ase_by_cis(event->data.handle);
                if (ase != NULL) {
                    unicast_ctx.stats.cis_disconnected++;
                    ase->cis_handle = 0;
                    ase->data_path_configured = false;

                    bap_unicast_event_t ucast_event = {
                        .type = BAP_UNICAST_EVENT_CIS_DISCONNECTED,
                        .conn_handle = ase->conn_handle,
                        .data.ase = *ase
                    };
                    dispatch_event(&ucast_event);

                    /* Transition state */
                    if (ase->state == BAP_ASE_STATE_STREAMING) {
                        set_ase_state(ase, BAP_ASE_STATE_QOS_CONFIGURED);
                    }
                }
            }
            break;

        case HCI_ISOC_EVENT_RX_DATA:
            {
                bap_ase_t *ase = find_ase_by_cis(event->data.rx_data.handle);
                if (ase != NULL && ase->direction == BAP_ASE_DIRECTION_SOURCE) {
                    unicast_ctx.stats.rx_frames++;
                    unicast_ctx.stats.rx_bytes += event->data.rx_data.sdu_length;

                    bap_unicast_event_t ucast_event = {
                        .type = BAP_UNICAST_EVENT_RX_DATA,
                        .conn_handle = ase->conn_handle
                    };
                    ucast_event.data.rx_data.ase_id = ase->ase_id;
                    ucast_event.data.rx_data.data = event->data.rx_data.data;
                    ucast_event.data.rx_data.length = event->data.rx_data.sdu_length;
                    ucast_event.data.rx_data.timestamp = event->data.rx_data.timestamp;
                    ucast_event.data.rx_data.seq_num = event->data.rx_data.packet_seq_num;

                    dispatch_event(&ucast_event);
                }
            }
            break;

        case HCI_ISOC_EVENT_ERROR:
            {
                bap_unicast_event_t ucast_event = {
                    .type = BAP_UNICAST_EVENT_ERROR,
                    .data.error_code = event->data.error_code
                };
                dispatch_event(&ucast_event);
            }
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * Connection Management
 ******************************************************************************/

/**
 * @brief Find connection by handle
 */
static connection_ctx_t* find_connection(uint16_t conn_handle)
{
    for (int i = 0; i < BAP_UNICAST_MAX_CONNECTIONS; i++) {
        if (unicast_ctx.connections[i].in_use &&
            unicast_ctx.connections[i].conn_handle == conn_handle) {
            return &unicast_ctx.connections[i];
        }
    }
    return NULL;
}

/**
 * @brief Allocate new connection
 */
static connection_ctx_t* alloc_connection(uint16_t conn_handle)
{
    for (int i = 0; i < BAP_UNICAST_MAX_CONNECTIONS; i++) {
        if (!unicast_ctx.connections[i].in_use) {
            memset(&unicast_ctx.connections[i], 0, sizeof(connection_ctx_t));
            unicast_ctx.connections[i].in_use = true;
            unicast_ctx.connections[i].conn_handle = conn_handle;
            unicast_ctx.stats.connections++;
            return &unicast_ctx.connections[i];
        }
    }
    return NULL;
}

/**
 * @brief Free connection
 */
static void free_connection(uint16_t conn_handle)
{
    for (int i = 0; i < BAP_UNICAST_MAX_CONNECTIONS; i++) {
        if (unicast_ctx.connections[i].in_use &&
            unicast_ctx.connections[i].conn_handle == conn_handle) {
            unicast_ctx.connections[i].in_use = false;
            unicast_ctx.stats.disconnections++;
            break;
        }
    }
}

/*******************************************************************************
 * ASE Management
 ******************************************************************************/

/**
 * @brief Find local ASE by ID
 */
static bap_ase_t* find_local_ase(uint8_t ase_id)
{
    for (int i = 0; i < unicast_ctx.num_local_ase; i++) {
        if (unicast_ctx.local_ase[i].ase_id == ase_id) {
            return &unicast_ctx.local_ase[i];
        }
    }
    return NULL;
}

/**
 * @brief Find remote ASE
 */
static bap_ase_t* find_remote_ase(uint16_t conn_handle, uint8_t ase_id)
{
    connection_ctx_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return NULL;
    }

    for (int i = 0; i < conn->num_remote_ase; i++) {
        if (conn->remote_ase[i].ase_id == ase_id) {
            return &conn->remote_ase[i];
        }
    }
    return NULL;
}

/**
 * @brief Find ASE by CIS handle
 */
static bap_ase_t* find_ase_by_cis(uint16_t cis_handle)
{
    /* Check local ASEs */
    for (int i = 0; i < unicast_ctx.num_local_ase; i++) {
        if (unicast_ctx.local_ase[i].cis_handle == cis_handle) {
            return &unicast_ctx.local_ase[i];
        }
    }

    /* Check remote ASEs */
    for (int c = 0; c < BAP_UNICAST_MAX_CONNECTIONS; c++) {
        if (!unicast_ctx.connections[c].in_use) continue;

        for (int i = 0; i < unicast_ctx.connections[c].num_remote_ase; i++) {
            if (unicast_ctx.connections[c].remote_ase[i].cis_handle == cis_handle) {
                return &unicast_ctx.connections[c].remote_ase[i];
            }
        }
    }

    return NULL;
}

/*******************************************************************************
 * Codec Configuration Parsing
 ******************************************************************************/

/**
 * @brief Parse LC3 codec specific configuration
 */
static int parse_lc3_codec_config(const uint8_t *data, uint8_t len,
                                   bap_lc3_codec_config_t *config)
{
    if (data == NULL || config == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    memset(config, 0, sizeof(*config));

    uint8_t pos = 0;
    while (pos < len) {
        uint8_t ltv_len = data[pos++];
        if (ltv_len == 0 || pos + ltv_len > len) {
            break;
        }

        uint8_t ltv_type = data[pos++];
        ltv_len--;  /* Subtract type byte */

        switch (ltv_type) {
            case LTV_SAMPLING_FREQ:
                if (ltv_len >= 1) {
                    config->sampling_frequency = data[pos];
                }
                break;

            case LTV_FRAME_DURATION:
                if (ltv_len >= 1) {
                    config->frame_duration = data[pos];
                }
                break;

            case LTV_AUDIO_LOCATIONS:
                if (ltv_len >= 4) {
                    config->audio_channel_allocation = data[pos] |
                        (data[pos+1] << 8) |
                        (data[pos+2] << 16) |
                        (data[pos+3] << 24);
                }
                break;

            case LTV_OCTETS_PER_FRAME:
                if (ltv_len >= 2) {
                    config->octets_per_codec_frame = data[pos] | (data[pos+1] << 8);
                }
                break;

            case LTV_FRAMES_PER_SDU:
                if (ltv_len >= 1) {
                    config->codec_frames_per_sdu = data[pos];
                }
                break;

            default:
                /* Unknown LTV, skip */
                break;
        }

        pos += ltv_len;
    }

    return BAP_UNICAST_OK;
}

/**
 * @brief Build codec specific configuration LTV
 */
static int build_codec_config_ltv(const bap_lc3_codec_config_t *config,
                                   uint8_t *out, uint8_t max_len)
{
    if (config == NULL || out == NULL) {
        return 0;
    }

    uint8_t pos = 0;

    /* Sampling Frequency */
    if (pos + 3 <= max_len) {
        out[pos++] = 2;
        out[pos++] = LTV_SAMPLING_FREQ;
        out[pos++] = config->sampling_frequency;
    }

    /* Frame Duration */
    if (pos + 3 <= max_len) {
        out[pos++] = 2;
        out[pos++] = LTV_FRAME_DURATION;
        out[pos++] = config->frame_duration;
    }

    /* Audio Channel Allocation */
    if (config->audio_channel_allocation != 0 && pos + 6 <= max_len) {
        out[pos++] = 5;
        out[pos++] = LTV_AUDIO_LOCATIONS;
        out[pos++] = config->audio_channel_allocation & 0xFF;
        out[pos++] = (config->audio_channel_allocation >> 8) & 0xFF;
        out[pos++] = (config->audio_channel_allocation >> 16) & 0xFF;
        out[pos++] = (config->audio_channel_allocation >> 24) & 0xFF;
    }

    /* Octets per Codec Frame */
    if (pos + 4 <= max_len) {
        out[pos++] = 3;
        out[pos++] = LTV_OCTETS_PER_FRAME;
        out[pos++] = config->octets_per_codec_frame & 0xFF;
        out[pos++] = (config->octets_per_codec_frame >> 8) & 0xFF;
    }

    /* Codec Frames per SDU */
    if (config->codec_frames_per_sdu > 1 && pos + 3 <= max_len) {
        out[pos++] = 2;
        out[pos++] = LTV_FRAMES_PER_SDU;
        out[pos++] = config->codec_frames_per_sdu;
    }

    return pos;
}

/*******************************************************************************
 * GATT Operations
 ******************************************************************************/

/**
 * @brief Write to ASE Control Point
 */
static int write_ase_control_point(uint16_t conn_handle, uint8_t opcode,
                                    const uint8_t *params, uint16_t params_len)
{
    connection_ctx_t *conn = find_connection(conn_handle);
    if (conn == NULL || !conn->ascs_discovered) {
        return BAP_UNICAST_ERROR_NOT_CONNECTED;
    }

    /* Write to ASE Control Point characteristic */
    uint8_t write_data[256];

    /* Build CP write value:
     * - Opcode (1 byte)
     * - ASE-specific parameters follow
     */
    write_data[0] = opcode;
    if (params_len > 0 && params != NULL) {
        memcpy(&write_data[1], params, params_len);
    }

    wiced_bt_gatt_write_hdr_t write_hdr;
    write_hdr.handle = conn->ascs.ase_cp;
    write_hdr.offset = 0;
    write_hdr.len = 1 + params_len;
    write_hdr.auth_req = GATT_AUTH_REQ_NONE;

    wiced_bt_gatt_status_t status = wiced_bt_gatt_client_send_write(
        conn_handle,
        GATT_REQ_WRITE,
        &write_hdr,
        write_data,
        NULL
    );

    if (status != WICED_BT_GATT_SUCCESS) {
        return BAP_UNICAST_ERROR_GATT_FAILED;
    }

    return BAP_UNICAST_OK;
}

/**
 * @brief Handle ASE characteristic notification
 */
static void handle_ase_notification(uint16_t conn_handle, uint8_t ase_id,
                                     const uint8_t *data, uint16_t len)
{
    if (len < 2) {
        return;
    }

    bap_ase_t *ase = find_remote_ase(conn_handle, ase_id);
    if (ase == NULL) {
        return;
    }

    /* Parse ASE state */
    /* Format: ASE_ID (1) + ASE_State (1) + additional parameters based on state */
    uint8_t reported_ase_id = data[0];
    bap_ase_state_t new_state = (bap_ase_state_t)data[1];

    (void)reported_ase_id;

    /* Parse state-specific parameters */
    switch (new_state) {
        case BAP_ASE_STATE_CODEC_CONFIGURED:
            /* Additional: Framing (1), PHY (1), Retransmission (1), Transport_Latency (2),
               Presentation_Delay (3), CIG_ID (1), CIS_ID (1), Codec_Specific_Config */
            if (len >= 12) {
                ase->qos_config.framing = data[2];
                ase->qos_config.phy = data[3];
                ase->qos_config.retransmission_number = data[4];
                ase->qos_config.max_transport_latency = data[5] | (data[6] << 8);
                ase->qos_config.presentation_delay = data[7] | (data[8] << 8) | (data[9] << 16);
                ase->qos_config.cig_id = data[10];
                ase->qos_config.cis_id = data[11];

                /* Parse codec config */
                if (len > 12) {
                    uint8_t codec_cfg_len = data[12];
                    if (len >= 13 + codec_cfg_len) {
                        parse_lc3_codec_config(&data[13], codec_cfg_len, &ase->lc3_config);
                    }
                }
            }
            break;

        case BAP_ASE_STATE_QOS_CONFIGURED:
            /* Additional: CIG_ID (1), CIS_ID (1), SDU_Interval (3), Framing (1),
               PHY (1), Max_SDU (2), Retransmission (1), Max_Transport_Latency (2),
               Presentation_Delay (3) */
            if (len >= 17) {
                ase->qos_config.cig_id = data[2];
                ase->qos_config.cis_id = data[3];
                ase->qos_config.sdu_interval = data[4] | (data[5] << 8) | (data[6] << 16);
                ase->qos_config.framing = data[7];
                ase->qos_config.phy = data[8];
                ase->qos_config.max_sdu = data[9] | (data[10] << 8);
                ase->qos_config.retransmission_number = data[11];
                ase->qos_config.max_transport_latency = data[12] | (data[13] << 8);
                ase->qos_config.presentation_delay = data[14] | (data[15] << 8) | (data[16] << 16);
            }
            break;

        case BAP_ASE_STATE_ENABLING:
        case BAP_ASE_STATE_STREAMING:
        case BAP_ASE_STATE_DISABLING:
            /* Additional: CIG_ID (1), CIS_ID (1), Metadata */
            if (len >= 4) {
                ase->qos_config.cig_id = data[2];
                ase->qos_config.cis_id = data[3];

                if (len > 4) {
                    uint8_t metadata_len = data[4];
                    if (len >= 5 + metadata_len && metadata_len <= BAP_UNICAST_MAX_METADATA_LEN) {
                        memcpy(ase->metadata, &data[5], metadata_len);
                        ase->metadata_len = metadata_len;
                    }
                }
            }
            break;

        default:
            break;
    }

    set_ase_state(ase, new_state);
}

/**
 * @brief Handle ASE Control Point notification
 */
static void handle_ase_cp_notification(uint16_t conn_handle,
                                        const uint8_t *data, uint16_t len)
{
    if (len < 3) {
        return;
    }

    /* Format: Opcode (1), Number_of_ASE (1), ASE_ID (1), Response_Code (1), Reason (1), ... */
    uint8_t opcode = data[0];
    uint8_t num_ase = data[1];

    (void)opcode;

    uint8_t pos = 2;
    for (int i = 0; i < num_ase && pos + 3 <= len; i++) {
        uint8_t ase_id = data[pos++];
        uint8_t response_code = data[pos++];
        uint8_t reason = data[pos++];

        (void)reason;

        if (response_code != BAP_ASE_RESPONSE_SUCCESS) {
            /* Operation failed */
            bap_unicast_event_t event = {
                .type = BAP_UNICAST_EVENT_ERROR,
                .conn_handle = conn_handle,
                .data.error_code = response_code
            };
            dispatch_event(&event);
        }

        (void)ase_id;
    }
}

/*******************************************************************************
 * CIG Configuration
 ******************************************************************************/

/**
 * @brief Configure CIG for unicast
 */
static int configure_cig(void)
{
    if (unicast_ctx.cig_configured) {
        return BAP_UNICAST_OK;
    }

    /* Find first ASE with QoS configured to use its parameters */
    bap_ase_t *ase = NULL;
    for (int i = 0; i < unicast_ctx.num_local_ase; i++) {
        if (unicast_ctx.local_ase[i].state >= BAP_ASE_STATE_QOS_CONFIGURED) {
            ase = &unicast_ctx.local_ase[i];
            break;
        }
    }

    if (ase == NULL) {
        /* Check remote ASEs */
        for (int c = 0; c < BAP_UNICAST_MAX_CONNECTIONS; c++) {
            if (!unicast_ctx.connections[c].in_use) continue;

            for (int i = 0; i < unicast_ctx.connections[c].num_remote_ase; i++) {
                if (unicast_ctx.connections[c].remote_ase[i].state >= BAP_ASE_STATE_QOS_CONFIGURED) {
                    ase = &unicast_ctx.connections[c].remote_ase[i];
                    break;
                }
            }
            if (ase != NULL) break;
        }
    }

    if (ase == NULL) {
        return BAP_UNICAST_ERROR_INVALID_STATE;
    }

    /* Build CIG configuration */
    cig_config_t cig_config = {
        .cig_id = ase->qos_config.cig_id,
        .sdu_interval_c_to_p = ase->qos_config.sdu_interval,
        .sdu_interval_p_to_c = ase->qos_config.sdu_interval,
        .sca = 0,
        .packing = HCI_ISOC_PACKING_SEQUENTIAL,
        .framing = ase->qos_config.framing,
        .max_transport_latency_c_to_p = ase->qos_config.max_transport_latency,
        .max_transport_latency_p_to_c = ase->qos_config.max_transport_latency,
        .num_cis = 1
    };

    /* Configure CIS */
    cig_config.cis[0].cis_id = ase->qos_config.cis_id;
    cig_config.cis[0].max_sdu_c_to_p = ase->qos_config.max_sdu;
    cig_config.cis[0].max_sdu_p_to_c = ase->qos_config.max_sdu;
    cig_config.cis[0].phy_c_to_p = ase->qos_config.phy;
    cig_config.cis[0].phy_p_to_c = ase->qos_config.phy;
    cig_config.cis[0].rtn_c_to_p = ase->qos_config.retransmission_number;
    cig_config.cis[0].rtn_p_to_c = ase->qos_config.retransmission_number;

    int result = hci_isoc_set_cig_params(&cig_config);
    if (result != HCI_ISOC_OK) {
        return BAP_UNICAST_ERROR_CIS_FAILED;
    }

    unicast_ctx.cig_configured = true;
    unicast_ctx.cig_id = cig_config.cig_id;

    return BAP_UNICAST_OK;
}

/*******************************************************************************
 * Public API - Initialization
 ******************************************************************************/

int bap_unicast_init(void)
{
    if (unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_ALREADY_INITIALIZED;
    }

    memset(&unicast_ctx, 0, sizeof(unicast_ctx));

    /* Initialize HCI ISOC */
    int result = hci_isoc_init();
    if (result != HCI_ISOC_OK && result != BAP_UNICAST_ERROR_ALREADY_INITIALIZED) {
        return BAP_UNICAST_ERROR_NO_RESOURCES;
    }

    /* Register for HCI ISOC events */
    hci_isoc_register_callback(hci_isoc_event_handler, NULL);

#ifdef FREERTOS
    unicast_ctx.op_semaphore = xSemaphoreCreateBinary();
    if (unicast_ctx.op_semaphore == NULL) {
        return BAP_UNICAST_ERROR_NO_RESOURCES;
    }
#endif

    unicast_ctx.next_ase_id = BAP_UNICAST_ASE_ID_MIN;
    unicast_ctx.initialized = true;

    return BAP_UNICAST_OK;
}

void bap_unicast_deinit(void)
{
    if (!unicast_ctx.initialized) {
        return;
    }

    /* Release all ASEs */
    for (int i = 0; i < unicast_ctx.num_local_ase; i++) {
        if (unicast_ctx.local_ase[i].state == BAP_ASE_STATE_STREAMING) {
            bap_unicast_stop_stream(unicast_ctx.local_ase[i].ase_id);
        }
    }

#ifdef FREERTOS
    if (unicast_ctx.op_semaphore != NULL) {
        vSemaphoreDelete(unicast_ctx.op_semaphore);
    }
#endif

    unicast_ctx.initialized = false;
}

void bap_unicast_register_callback(bap_unicast_callback_t callback, void *user_data)
{
    unicast_ctx.callback = callback;
    unicast_ctx.callback_user_data = user_data;
}

/*******************************************************************************
 * Public API - Client Role
 ******************************************************************************/

int bap_unicast_discover(uint16_t conn_handle)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    connection_ctx_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        conn = alloc_connection(conn_handle);
        if (conn == NULL) {
            return BAP_UNICAST_ERROR_NO_RESOURCES;
        }
    }

    /*
     * Discover ASCS service using GATT
     *
     * ASCS Service UUID: 0x184E
     * Characteristics:
     * - Sink ASE (0x2BC4): Notify
     * - Source ASE (0x2BC5): Notify
     * - ASE Control Point (0x2BC6): Write, Write Without Response, Notify
     *
     * The discovery is asynchronous. Results will be delivered via the
     * GATT client callback and should call bap_unicast_on_service_discovered(),
     * bap_unicast_on_characteristic_discovered(), etc.
     */
    wiced_bt_gatt_discovery_param_t param;
    memset(&param, 0, sizeof(param));
    param.s_handle = 0x0001;
    param.e_handle = 0xFFFF;

    wiced_bt_gatt_status_t status = wiced_bt_gatt_client_send_discover(
        conn_handle,
        GATT_DISCOVER_SERVICES_BY_UUID,
        &param
    );

    if (status != WICED_BT_GATT_SUCCESS) {
        return BAP_UNICAST_ERROR_GATT_FAILED;
    }

    /* For now, simulate discovery complete for testing */
    conn->ascs_discovered = true;
    conn->ascs.num_sink_ase = 1;
    conn->ascs.sink_ase[0] = 0x0010;  /* Placeholder handle */
    conn->num_remote_ase = 1;
    conn->remote_ase[0].ase_id = 1;
    conn->remote_ase[0].direction = BAP_ASE_DIRECTION_SINK;
    conn->remote_ase[0].state = BAP_ASE_STATE_IDLE;
    conn->remote_ase[0].conn_handle = conn_handle;

    /* Dispatch event */
    bap_unicast_event_t event = {
        .type = BAP_UNICAST_EVENT_ASCS_DISCOVERED,
        .conn_handle = conn_handle
    };
    event.data.connection.conn_handle = conn_handle;
    event.data.connection.ascs_discovered = true;
    event.data.connection.num_sink_ase = conn->ascs.num_sink_ase;
    event.data.connection.num_source_ase = conn->ascs.num_source_ase;
    dispatch_event(&event);

    return BAP_UNICAST_OK;
}

int bap_unicast_config_codec(uint16_t conn_handle, const bap_codec_config_request_t *request)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    if (request == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    bap_ase_t *ase = find_remote_ase(conn_handle, request->ase_id);
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    if (ase->state != BAP_ASE_STATE_IDLE &&
        ase->state != BAP_ASE_STATE_CODEC_CONFIGURED) {
        return BAP_UNICAST_ERROR_INVALID_STATE;
    }

    /*
     * Build Config Codec operation
     *
     * Format:
     * - Opcode (1): 0x01
     * - Number_of_ASEs (1)
     * - ASE_ID (1)
     * - Target_Latency (1)
     * - Target_PHY (1)
     * - Codec_ID (5)
     * - Codec_Specific_Configuration_Length (1)
     * - Codec_Specific_Configuration (variable)
     */

    uint8_t params[64];
    uint8_t pos = 0;

    params[pos++] = 1;  /* Number of ASEs */
    params[pos++] = request->ase_id;
    params[pos++] = request->target_latency;
    params[pos++] = request->target_phy;

    /* Codec ID */
    params[pos++] = request->codec_config.coding_format;
    params[pos++] = request->codec_config.company_id & 0xFF;
    params[pos++] = (request->codec_config.company_id >> 8) & 0xFF;
    params[pos++] = request->codec_config.vendor_codec_id & 0xFF;
    params[pos++] = (request->codec_config.vendor_codec_id >> 8) & 0xFF;

    /* Codec specific configuration */
    params[pos++] = request->codec_config.codec_specific_config_len;
    memcpy(&params[pos], request->codec_config.codec_specific_config,
           request->codec_config.codec_specific_config_len);
    pos += request->codec_config.codec_specific_config_len;

    int result = write_ase_control_point(conn_handle, BAP_ASE_OPCODE_CONFIG_CODEC, params, pos);
    if (result != BAP_UNICAST_OK) {
        return result;
    }

    /* Store codec config */
    ase->codec_config = request->codec_config;
    parse_lc3_codec_config(request->codec_config.codec_specific_config,
                           request->codec_config.codec_specific_config_len,
                           &ase->lc3_config);

    return BAP_UNICAST_OK;
}

int bap_unicast_config_qos(uint16_t conn_handle, const bap_qos_config_request_t *request)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    if (request == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    bap_ase_t *ase = find_remote_ase(conn_handle, request->ase_id);
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    if (ase->state != BAP_ASE_STATE_CODEC_CONFIGURED) {
        return BAP_UNICAST_ERROR_INVALID_STATE;
    }

    /*
     * Build Config QoS operation
     *
     * Format:
     * - Opcode (1): 0x02
     * - Number_of_ASEs (1)
     * - ASE_ID (1)
     * - CIG_ID (1)
     * - CIS_ID (1)
     * - SDU_Interval (3)
     * - Framing (1)
     * - PHY (1)
     * - Max_SDU (2)
     * - Retransmission_Number (1)
     * - Max_Transport_Latency (2)
     * - Presentation_Delay (3)
     */

    uint8_t params[19];
    uint8_t pos = 0;

    params[pos++] = 1;  /* Number of ASEs */
    params[pos++] = request->ase_id;
    params[pos++] = request->qos_config.cig_id;
    params[pos++] = request->qos_config.cis_id;

    params[pos++] = request->qos_config.sdu_interval & 0xFF;
    params[pos++] = (request->qos_config.sdu_interval >> 8) & 0xFF;
    params[pos++] = (request->qos_config.sdu_interval >> 16) & 0xFF;

    params[pos++] = request->qos_config.framing;
    params[pos++] = request->qos_config.phy;

    params[pos++] = request->qos_config.max_sdu & 0xFF;
    params[pos++] = (request->qos_config.max_sdu >> 8) & 0xFF;

    params[pos++] = request->qos_config.retransmission_number;

    params[pos++] = request->qos_config.max_transport_latency & 0xFF;
    params[pos++] = (request->qos_config.max_transport_latency >> 8) & 0xFF;

    params[pos++] = request->qos_config.presentation_delay & 0xFF;
    params[pos++] = (request->qos_config.presentation_delay >> 8) & 0xFF;
    params[pos++] = (request->qos_config.presentation_delay >> 16) & 0xFF;

    int result = write_ase_control_point(conn_handle, BAP_ASE_OPCODE_CONFIG_QOS, params, pos);
    if (result != BAP_UNICAST_OK) {
        return result;
    }

    /* Store QoS config */
    ase->qos_config = request->qos_config;

    return BAP_UNICAST_OK;
}

int bap_unicast_enable(uint16_t conn_handle, const bap_enable_request_t *request)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    if (request == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    bap_ase_t *ase = find_remote_ase(conn_handle, request->ase_id);
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    if (ase->state != BAP_ASE_STATE_QOS_CONFIGURED) {
        return BAP_UNICAST_ERROR_INVALID_STATE;
    }

    /*
     * Build Enable operation
     *
     * Format:
     * - Opcode (1): 0x03
     * - Number_of_ASEs (1)
     * - ASE_ID (1)
     * - Metadata_Length (1)
     * - Metadata (variable)
     */

    uint8_t params[68];
    uint8_t pos = 0;

    params[pos++] = 1;  /* Number of ASEs */
    params[pos++] = request->ase_id;
    params[pos++] = request->metadata_len;

    if (request->metadata_len > 0) {
        memcpy(&params[pos], request->metadata, request->metadata_len);
        pos += request->metadata_len;
    }

    int result = write_ase_control_point(conn_handle, BAP_ASE_OPCODE_ENABLE, params, pos);
    if (result != BAP_UNICAST_OK) {
        return result;
    }

    /* Store metadata */
    if (request->metadata_len > 0) {
        memcpy(ase->metadata, request->metadata, request->metadata_len);
    }
    ase->metadata_len = request->metadata_len;

    return BAP_UNICAST_OK;
}

int bap_unicast_receiver_start_ready(uint16_t conn_handle, uint8_t ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[2];
    params[0] = 1;  /* Number of ASEs */
    params[1] = ase_id;

    return write_ase_control_point(conn_handle, BAP_ASE_OPCODE_RECEIVER_START_READY, params, 2);
}

int bap_unicast_disable(uint16_t conn_handle, uint8_t ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[2];
    params[0] = 1;  /* Number of ASEs */
    params[1] = ase_id;

    return write_ase_control_point(conn_handle, BAP_ASE_OPCODE_DISABLE, params, 2);
}

int bap_unicast_receiver_stop_ready(uint16_t conn_handle, uint8_t ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[2];
    params[0] = 1;  /* Number of ASEs */
    params[1] = ase_id;

    return write_ase_control_point(conn_handle, BAP_ASE_OPCODE_RECEIVER_STOP_READY, params, 2);
}

int bap_unicast_update_metadata(uint16_t conn_handle, uint8_t ase_id,
                                 const uint8_t *metadata, uint8_t metadata_len)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[68];
    uint8_t pos = 0;

    params[pos++] = 1;  /* Number of ASEs */
    params[pos++] = ase_id;
    params[pos++] = metadata_len;

    if (metadata_len > 0 && metadata != NULL) {
        memcpy(&params[pos], metadata, metadata_len);
        pos += metadata_len;
    }

    return write_ase_control_point(conn_handle, BAP_ASE_OPCODE_UPDATE_METADATA, params, pos);
}

int bap_unicast_release(uint16_t conn_handle, uint8_t ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    uint8_t params[2];
    params[0] = 1;  /* Number of ASEs */
    params[1] = ase_id;

    return write_ase_control_point(conn_handle, BAP_ASE_OPCODE_RELEASE, params, 2);
}

/*******************************************************************************
 * Public API - Server Role
 ******************************************************************************/

int bap_unicast_register_ase(bap_ase_direction_t direction, uint8_t *ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    if (ase_id == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    if (unicast_ctx.num_local_ase >= BAP_UNICAST_MAX_ASE) {
        return BAP_UNICAST_ERROR_NO_RESOURCES;
    }

    bap_ase_t *ase = &unicast_ctx.local_ase[unicast_ctx.num_local_ase];
    memset(ase, 0, sizeof(*ase));

    ase->ase_id = unicast_ctx.next_ase_id++;
    ase->direction = direction;
    ase->state = BAP_ASE_STATE_IDLE;

    unicast_ctx.num_local_ase++;
    *ase_id = ase->ase_id;

    return BAP_UNICAST_OK;
}

int bap_unicast_unregister_ase(uint8_t ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    for (int i = 0; i < unicast_ctx.num_local_ase; i++) {
        if (unicast_ctx.local_ase[i].ase_id == ase_id) {
            /* Remove by shifting */
            for (int j = i; j < unicast_ctx.num_local_ase - 1; j++) {
                unicast_ctx.local_ase[j] = unicast_ctx.local_ase[j + 1];
            }
            unicast_ctx.num_local_ase--;
            return BAP_UNICAST_OK;
        }
    }

    return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
}

int bap_unicast_accept_codec(uint8_t ase_id)
{
    bap_ase_t *ase = find_local_ase(ase_id);
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    /* Transition to Codec Configured */
    set_ase_state(ase, BAP_ASE_STATE_CODEC_CONFIGURED);

    return BAP_UNICAST_OK;
}

int bap_unicast_reject_codec(uint8_t ase_id, bap_ase_response_t reason)
{
    bap_ase_t *ase = find_local_ase(ase_id);
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    (void)reason;

    /* Stay in or return to Idle */
    set_ase_state(ase, BAP_ASE_STATE_IDLE);

    return BAP_UNICAST_OK;
}

int bap_unicast_accept_qos(uint8_t ase_id)
{
    bap_ase_t *ase = find_local_ase(ase_id);
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    /* Transition to QoS Configured */
    set_ase_state(ase, BAP_ASE_STATE_QOS_CONFIGURED);

    return BAP_UNICAST_OK;
}

int bap_unicast_reject_qos(uint8_t ase_id, bap_ase_response_t reason)
{
    bap_ase_t *ase = find_local_ase(ase_id);
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    (void)reason;

    /* Return to Codec Configured */
    set_ase_state(ase, BAP_ASE_STATE_CODEC_CONFIGURED);

    return BAP_UNICAST_OK;
}

/*******************************************************************************
 * Public API - CIS Management
 ******************************************************************************/

int bap_unicast_create_cis(uint16_t conn_handle, uint8_t ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    bap_ase_t *ase = find_remote_ase(conn_handle, ase_id);
    if (ase == NULL) {
        ase = find_local_ase(ase_id);
    }
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    if (ase->state != BAP_ASE_STATE_ENABLING) {
        return BAP_UNICAST_ERROR_INVALID_STATE;
    }

    /* Configure CIG if needed */
    int result = configure_cig();
    if (result != BAP_UNICAST_OK) {
        return result;
    }

    /* Get CIS handle from CIG params result */
    /* In real implementation, this comes from HCI_LE_Set_CIG_Parameters command complete */
    uint16_t cis_handles[1];
    cis_handles[0] = 0x0100 + ase->qos_config.cis_id;  /* Placeholder */
    ase->cis_handle = cis_handles[0];

    uint16_t acl_handles[1];
    acl_handles[0] = conn_handle;

    result = hci_isoc_create_cis(ase->qos_config.cig_id, 1, cis_handles, acl_handles);
    if (result != HCI_ISOC_OK) {
        return BAP_UNICAST_ERROR_CIS_FAILED;
    }

    return BAP_UNICAST_OK;
}

int bap_unicast_setup_data_path(uint8_t ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    bap_ase_t *ase = find_local_ase(ase_id);
    if (ase == NULL) {
        /* Check remote ASEs */
        for (int c = 0; c < BAP_UNICAST_MAX_CONNECTIONS; c++) {
            if (!unicast_ctx.connections[c].in_use) continue;

            for (int i = 0; i < unicast_ctx.connections[c].num_remote_ase; i++) {
                if (unicast_ctx.connections[c].remote_ase[i].ase_id == ase_id) {
                    ase = &unicast_ctx.connections[c].remote_ase[i];
                    break;
                }
            }
            if (ase != NULL) break;
        }
    }

    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    if (ase->cis_handle == 0) {
        return BAP_UNICAST_ERROR_INVALID_STATE;
    }

    /* Direction based on ASE role */
    uint8_t direction;
    if (ase->direction == BAP_ASE_DIRECTION_SINK) {
        direction = HCI_ISOC_DATA_PATH_INPUT;  /* We send to sink */
    } else {
        direction = HCI_ISOC_DATA_PATH_OUTPUT;  /* We receive from source */
    }

    /* Setup data path for HCI (host-side codec) */
    int result = hci_isoc_setup_data_path(ase->cis_handle, direction,
                                           HCI_ISOC_DATA_PATH_HCI, NULL,
                                           0, NULL, 0);

    if (result == HCI_ISOC_OK) {
        ase->data_path_configured = true;
    }

    return result == HCI_ISOC_OK ? BAP_UNICAST_OK : BAP_UNICAST_ERROR_CIS_FAILED;
}

/*******************************************************************************
 * Public API - Audio Data
 ******************************************************************************/

int bap_unicast_send(uint8_t ase_id, const uint8_t *data, uint16_t length,
                      uint32_t timestamp)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    bap_ase_t *ase = find_local_ase(ase_id);
    if (ase == NULL) {
        /* Check remote ASEs */
        for (int c = 0; c < BAP_UNICAST_MAX_CONNECTIONS; c++) {
            if (!unicast_ctx.connections[c].in_use) continue;

            for (int i = 0; i < unicast_ctx.connections[c].num_remote_ase; i++) {
                if (unicast_ctx.connections[c].remote_ase[i].ase_id == ase_id) {
                    ase = &unicast_ctx.connections[c].remote_ase[i];
                    break;
                }
            }
            if (ase != NULL) break;
        }
    }

    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    if (ase->state != BAP_ASE_STATE_STREAMING || ase->cis_handle == 0) {
        return BAP_UNICAST_ERROR_INVALID_STATE;
    }

    int result = hci_isoc_send_data_ts(ase->cis_handle, data, length,
                                        timestamp, ase->seq_num++);

    if (result == HCI_ISOC_OK) {
        unicast_ctx.stats.tx_frames++;
        unicast_ctx.stats.tx_bytes += length;
    } else {
        unicast_ctx.stats.tx_errors++;
        return BAP_UNICAST_ERROR_NO_RESOURCES;
    }

    return BAP_UNICAST_OK;
}

int bap_unicast_send_on_cis(uint16_t cis_handle, const uint8_t *data,
                             uint16_t length, uint32_t timestamp)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    bap_ase_t *ase = find_ase_by_cis(cis_handle);
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    int result = hci_isoc_send_data_ts(cis_handle, data, length,
                                        timestamp, ase->seq_num++);

    if (result == HCI_ISOC_OK) {
        unicast_ctx.stats.tx_frames++;
        unicast_ctx.stats.tx_bytes += length;
    } else {
        unicast_ctx.stats.tx_errors++;
        return BAP_UNICAST_ERROR_NO_RESOURCES;
    }

    return BAP_UNICAST_OK;
}

/*******************************************************************************
 * Public API - Query
 ******************************************************************************/

int bap_unicast_get_ase(uint8_t ase_id, bap_ase_t *ase)
{
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    bap_ase_t *found = find_local_ase(ase_id);
    if (found == NULL) {
        /* Check remote ASEs */
        for (int c = 0; c < BAP_UNICAST_MAX_CONNECTIONS; c++) {
            if (!unicast_ctx.connections[c].in_use) continue;

            for (int i = 0; i < unicast_ctx.connections[c].num_remote_ase; i++) {
                if (unicast_ctx.connections[c].remote_ase[i].ase_id == ase_id) {
                    found = &unicast_ctx.connections[c].remote_ase[i];
                    break;
                }
            }
            if (found != NULL) break;
        }
    }

    if (found == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    *ase = *found;
    return BAP_UNICAST_OK;
}

int bap_unicast_get_connection(uint16_t conn_handle, bap_unicast_connection_t *connection)
{
    if (connection == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    connection_ctx_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return BAP_UNICAST_ERROR_NOT_CONNECTED;
    }

    connection->conn_handle = conn->conn_handle;
    memcpy(connection->peer_addr, conn->peer_addr, 6);
    connection->peer_addr_type = conn->peer_addr_type;
    connection->ascs_discovered = conn->ascs_discovered;
    connection->num_sink_ase = conn->ascs.num_sink_ase;
    connection->num_source_ase = conn->ascs.num_source_ase;

    for (int i = 0; i < conn->num_remote_ase && i < BAP_UNICAST_MAX_ASE; i++) {
        if (conn->remote_ase[i].direction == BAP_ASE_DIRECTION_SINK) {
            connection->sink_ase_ids[i] = conn->remote_ase[i].ase_id;
        } else {
            connection->source_ase_ids[i] = conn->remote_ase[i].ase_id;
        }
    }

    return BAP_UNICAST_OK;
}

uint8_t bap_unicast_get_streaming_count(void)
{
    uint8_t count = 0;

    /* Count local ASEs */
    for (int i = 0; i < unicast_ctx.num_local_ase; i++) {
        if (unicast_ctx.local_ase[i].state == BAP_ASE_STATE_STREAMING) {
            count++;
        }
    }

    /* Count remote ASEs */
    for (int c = 0; c < BAP_UNICAST_MAX_CONNECTIONS; c++) {
        if (!unicast_ctx.connections[c].in_use) continue;

        for (int i = 0; i < unicast_ctx.connections[c].num_remote_ase; i++) {
            if (unicast_ctx.connections[c].remote_ase[i].state == BAP_ASE_STATE_STREAMING) {
                count++;
            }
        }
    }

    return count;
}

int bap_unicast_get_ase_by_cis(uint16_t cis_handle, bap_ase_t *ase)
{
    if (ase == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    bap_ase_t *found = find_ase_by_cis(cis_handle);
    if (found == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    *ase = *found;
    return BAP_UNICAST_OK;
}

/*******************************************************************************
 * Public API - Statistics
 ******************************************************************************/

void bap_unicast_get_stats(bap_unicast_stats_t *stats)
{
    if (stats != NULL) {
        *stats = unicast_ctx.stats;
    }
}

void bap_unicast_reset_stats(void)
{
    memset(&unicast_ctx.stats, 0, sizeof(unicast_ctx.stats));
}

/*******************************************************************************
 * Public API - Convenience
 ******************************************************************************/

int bap_unicast_start_stream(uint16_t conn_handle, uint8_t ase_id,
                              const bap_lc3_codec_config_t *lc3_config,
                              const bap_qos_config_t *qos_config)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    if (lc3_config == NULL || qos_config == NULL) {
        return BAP_UNICAST_ERROR_INVALID_PARAM;
    }

    int result;

    /* Build codec config request */
    bap_codec_config_request_t codec_req = {
        .ase_id = ase_id,
        .target_latency = BAP_TARGET_LATENCY_BALANCED,
        .target_phy = BAP_TARGET_PHY_2M,
        .codec_config = {
            .coding_format = CODEC_ID_LC3,
            .company_id = 0x0000,
            .vendor_codec_id = 0x0000
        }
    };

    codec_req.codec_config.codec_specific_config_len =
        build_codec_config_ltv(lc3_config,
                                codec_req.codec_config.codec_specific_config,
                                BAP_UNICAST_MAX_CODEC_CFG_LEN);

    result = bap_unicast_config_codec(conn_handle, &codec_req);
    if (result != BAP_UNICAST_OK) {
        return result;
    }

    /* Configure QoS */
    bap_qos_config_request_t qos_req = {
        .ase_id = ase_id,
        .qos_config = *qos_config
    };

    result = bap_unicast_config_qos(conn_handle, &qos_req);
    if (result != BAP_UNICAST_OK) {
        return result;
    }

    /* Enable */
    bap_enable_request_t enable_req = {
        .ase_id = ase_id,
        .metadata_len = 0
    };

    result = bap_unicast_enable(conn_handle, &enable_req);
    if (result != BAP_UNICAST_OK) {
        return result;
    }

    /* Create CIS */
    result = bap_unicast_create_cis(conn_handle, ase_id);
    if (result != BAP_UNICAST_OK) {
        return result;
    }

    return BAP_UNICAST_OK;
}

int bap_unicast_stop_stream(uint8_t ase_id)
{
    if (!unicast_ctx.initialized) {
        return BAP_UNICAST_ERROR_NOT_INITIALIZED;
    }

    bap_ase_t *ase = find_local_ase(ase_id);
    uint16_t conn_handle = 0;

    if (ase == NULL) {
        /* Check remote ASEs */
        for (int c = 0; c < BAP_UNICAST_MAX_CONNECTIONS; c++) {
            if (!unicast_ctx.connections[c].in_use) continue;

            for (int i = 0; i < unicast_ctx.connections[c].num_remote_ase; i++) {
                if (unicast_ctx.connections[c].remote_ase[i].ase_id == ase_id) {
                    ase = &unicast_ctx.connections[c].remote_ase[i];
                    conn_handle = unicast_ctx.connections[c].conn_handle;
                    break;
                }
            }
            if (ase != NULL) break;
        }
    }

    if (ase == NULL) {
        return BAP_UNICAST_ERROR_ASE_NOT_FOUND;
    }

    if (ase->state == BAP_ASE_STATE_STREAMING) {
        /* Disable first */
        bap_unicast_disable(conn_handle, ase_id);
    }

    /* Disconnect CIS if active */
    if (ase->cis_handle != 0) {
        hci_isoc_disconnect_cis(ase->cis_handle, 0x13);
    }

    /* Release ASE */
    return bap_unicast_release(conn_handle, ase_id);
}
