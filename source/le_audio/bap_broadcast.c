/**
 * @file bap_broadcast.c
 * @brief BAP Broadcast Source Implementation (Auracast)
 *
 * Implements the BAP Broadcast Source role for Auracast transmission.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bap_broadcast.h"
#include "../bluetooth/hci_isoc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*******************************************************************************
 * Platform Includes
 ******************************************************************************/
/* Infineon BTSTACK headers */
#include "wiced_bt_ble.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_isoc.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

/*******************************************************************************
 * Constants
 ******************************************************************************/

/** Advertising handles */
#define EXT_ADV_HANDLE          0x00
#define PERIODIC_ADV_HANDLE     0x00

/** AD Type values */
#define AD_TYPE_FLAGS           0x01
#define AD_TYPE_SERVICE_UUID_16 0x03
#define AD_TYPE_COMPLETE_NAME   0x09
#define AD_TYPE_SERVICE_DATA_16 0x16
#define AD_TYPE_BROADCAST_NAME  0x30

/** UUID for Broadcast Audio Announcement Service */
#define UUID_BROADCAST_AUDIO_ANNOUNCEMENT   0x1852

/** BASE Level structure types */
#define BASE_LEVEL_1            0x01
#define BASE_LEVEL_2            0x02
#define BASE_LEVEL_3            0x03

/** LTV (Length-Type-Value) types for Codec Specific Configuration */
#define LTV_TYPE_SAMPLING_FREQ      0x01
#define LTV_TYPE_FRAME_DURATION     0x02
#define LTV_TYPE_AUDIO_LOCATIONS    0x03
#define LTV_TYPE_OCTETS_PER_FRAME   0x04
#define LTV_TYPE_FRAMES_PER_SDU     0x05

/** LTV types for Metadata */
#define LTV_TYPE_PREF_AUDIO_CONTEXT 0x01
#define LTV_TYPE_STREAM_AUDIO_CONTEXT 0x02
#define LTV_TYPE_PROGRAM_INFO       0x03
#define LTV_TYPE_LANGUAGE           0x04
#define LTV_TYPE_CCID_LIST          0x05
#define LTV_TYPE_PARENTAL_RATING    0x06

/** BIG parameters */
#define BIG_SDU_INTERVAL_US         10000   /* 10ms */
#define BIG_MAX_TRANSPORT_LATENCY   40      /* ms */
#define BIG_RTN                     2       /* retransmissions */
#define BIG_PHY_2M                  0x02

/*******************************************************************************
 * Types
 ******************************************************************************/

/** Module context */
typedef struct {
    bool initialized;
    bap_broadcast_state_t state;

    /* Configuration */
    bap_broadcast_config_t config;

    /* Runtime info */
    bap_broadcast_info_t info;

    /* BASE structure */
    uint8_t base_data[BAP_BROADCAST_MAX_BASE_SIZE];
    uint16_t base_len;

    /* Advertising data */
    uint8_t adv_data[31];
    uint8_t adv_data_len;
    uint8_t periodic_adv_data[254];
    uint8_t periodic_adv_len;

    /* Callback */
    bap_broadcast_callback_t callback;
    void *callback_user_data;

    /* Statistics */
    bap_broadcast_stats_t stats;
    uint32_t start_time_ms;

    /* TX tracking */
    uint16_t tx_seq_num;
    uint8_t next_bis_index;

} bap_broadcast_ctx_t;

/*******************************************************************************
 * Module Variables
 ******************************************************************************/

static bap_broadcast_ctx_t bcast_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static void dispatch_event(const bap_broadcast_event_t *event);
static void set_state(bap_broadcast_state_t new_state);
static int build_adv_data(void);
static int build_base_structure(void);
static int start_extended_advertising(void);
static int stop_extended_advertising(void);
static int start_periodic_advertising(void);
static int stop_periodic_advertising(void);
static int create_big(void);
static int terminate_big(void);
static void hci_isoc_event_handler(const hci_isoc_event_t *event, void *user_data);
static uint8_t write_ltv(uint8_t *buf, uint8_t type, const uint8_t *value, uint8_t len);
static uint32_t get_time_ms(void);

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * @brief Get current time in milliseconds
 */
static uint32_t get_time_ms(void)
{
/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
}

/**
 * @brief Write LTV (Length-Type-Value) structure
 */
static uint8_t write_ltv(uint8_t *buf, uint8_t type, const uint8_t *value, uint8_t len)
{
    buf[0] = len + 1;  /* Length includes type byte */
    buf[1] = type;
    if (value != NULL && len > 0) {
        memcpy(&buf[2], value, len);
    }
    return len + 2;
}

/*******************************************************************************
 * Event Handling
 ******************************************************************************/

/**
 * @brief Dispatch event to callback
 */
static void dispatch_event(const bap_broadcast_event_t *event)
{
    if (bcast_ctx.callback != NULL) {
        bcast_ctx.callback(event, bcast_ctx.callback_user_data);
    }
}

/**
 * @brief Set new state and dispatch event
 */
static void set_state(bap_broadcast_state_t new_state)
{
    if (bcast_ctx.state != new_state) {
        bcast_ctx.state = new_state;
        bcast_ctx.info.state = new_state;

        bap_broadcast_event_t event = {
            .type = BAP_BROADCAST_EVENT_STATE_CHANGED,
            .data.new_state = new_state
        };
        dispatch_event(&event);
    }
}

/**
 * @brief HCI ISOC event handler
 */
static void hci_isoc_event_handler(const hci_isoc_event_t *event, void *user_data)
{
    (void)user_data;

    switch (event->type) {
        case HCI_ISOC_EVENT_BIG_CREATED:
            /* BIG created successfully */
            bcast_ctx.info.big_handle = event->data.big_info.big_handle;
            bcast_ctx.info.num_bis = event->data.big_info.num_bis;
            bcast_ctx.info.big_sync_delay = event->data.big_info.big_sync_delay;
            bcast_ctx.info.transport_latency = event->data.big_info.transport_latency;
            bcast_ctx.info.iso_interval = event->data.big_info.iso_interval;

            /* Copy BIS handles */
            for (int i = 0; i < event->data.big_info.num_bis; i++) {
                bcast_ctx.info.bis_handles[i] = event->data.big_info.bis_handles[i];
            }

            /* Calculate SDU interval from ISO interval */
            bcast_ctx.info.sdu_interval = (uint32_t)event->data.big_info.iso_interval * 1250;
            bcast_ctx.info.max_sdu = event->data.big_info.max_pdu;

            set_state(BAP_BROADCAST_STATE_STREAMING);
            bcast_ctx.start_time_ms = get_time_ms();

            /* Dispatch BIG created event */
            {
                bap_broadcast_event_t bcast_event = {
                    .type = BAP_BROADCAST_EVENT_BIG_CREATED,
                    .data.info = bcast_ctx.info
                };
                dispatch_event(&bcast_event);
            }

            /* Dispatch started event */
            {
                bap_broadcast_event_t bcast_event = {
                    .type = BAP_BROADCAST_EVENT_STARTED,
                    .data.info = bcast_ctx.info
                };
                dispatch_event(&bcast_event);
            }
            break;

        case HCI_ISOC_EVENT_BIG_TERMINATED:
            set_state(BAP_BROADCAST_STATE_CONFIGURED);

            /* Dispatch BIG terminated event */
            {
                bap_broadcast_event_t bcast_event = {
                    .type = BAP_BROADCAST_EVENT_BIG_TERMINATED
                };
                dispatch_event(&bcast_event);
            }

            /* Dispatch stopped event */
            {
                bap_broadcast_event_t bcast_event = {
                    .type = BAP_BROADCAST_EVENT_STOPPED
                };
                dispatch_event(&bcast_event);
            }
            break;

        case HCI_ISOC_EVENT_TX_COMPLETE:
            bcast_ctx.stats.big_events++;
            break;

        case HCI_ISOC_EVENT_ERROR:
            {
                bap_broadcast_event_t bcast_event = {
                    .type = BAP_BROADCAST_EVENT_ERROR,
                    .data.error_code = event->data.error_code
                };
                dispatch_event(&bcast_event);
            }
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * Advertising
 ******************************************************************************/

/**
 * @brief Build extended advertising data
 *
 * Extended advertising contains:
 * - Flags
 * - Broadcast Audio Announcement UUID
 * - Broadcast_ID
 * - Broadcast Name (if fits)
 */
static int build_adv_data(void)
{
    uint8_t *p = bcast_ctx.adv_data;
    uint8_t pos = 0;

    /* Flags (LE General Discoverable, BR/EDR Not Supported) */
    p[pos++] = 2;
    p[pos++] = AD_TYPE_FLAGS;
    p[pos++] = 0x06;

    /* Service UUID: Broadcast Audio Announcement Service (0x1852) */
    p[pos++] = 3;
    p[pos++] = AD_TYPE_SERVICE_UUID_16;
    p[pos++] = UUID_BROADCAST_AUDIO_ANNOUNCEMENT & 0xFF;
    p[pos++] = (UUID_BROADCAST_AUDIO_ANNOUNCEMENT >> 8) & 0xFF;

    /* Service Data: Broadcast Audio Announcement */
    /* Format: UUID (2) + Broadcast_ID (3) */
    p[pos++] = 6;
    p[pos++] = AD_TYPE_SERVICE_DATA_16;
    p[pos++] = UUID_BROADCAST_AUDIO_ANNOUNCEMENT & 0xFF;
    p[pos++] = (UUID_BROADCAST_AUDIO_ANNOUNCEMENT >> 8) & 0xFF;
    p[pos++] = bcast_ctx.config.broadcast_id[0];
    p[pos++] = bcast_ctx.config.broadcast_id[1];
    p[pos++] = bcast_ctx.config.broadcast_id[2];

    /* Broadcast Name (if room) */
    uint8_t name_len = (uint8_t)strlen(bcast_ctx.config.broadcast_name);
    if (pos + 2 + name_len <= 31) {
        p[pos++] = name_len + 1;
        p[pos++] = AD_TYPE_BROADCAST_NAME;
        memcpy(&p[pos], bcast_ctx.config.broadcast_name, name_len);
        pos += name_len;
    }

    bcast_ctx.adv_data_len = pos;

    return BAP_BROADCAST_OK;
}

/**
 * @brief Build BASE structure for periodic advertising
 *
 * BASE (Broadcast Audio Source Endpoint) structure:
 * - Presentation Delay (3 bytes)
 * - Num_Subgroups (1 byte)
 * - For each subgroup:
 *   - Num_BIS (1 byte)
 *   - Codec_ID (5 bytes)
 *   - Codec_Specific_Configuration (variable)
 *   - Metadata (variable)
 *   - For each BIS:
 *     - BIS_Index (1 byte)
 *     - Codec_Specific_Configuration (BIS-level)
 */
static int build_base_structure(void)
{
    uint8_t *p = bcast_ctx.base_data;
    uint16_t pos = 0;

    /* Presentation Delay (3 bytes, little endian) */
    uint32_t pres_delay = bcast_ctx.config.presentation_delay_us;
    p[pos++] = pres_delay & 0xFF;
    p[pos++] = (pres_delay >> 8) & 0xFF;
    p[pos++] = (pres_delay >> 16) & 0xFF;

    /* Number of Subgroups */
    p[pos++] = bcast_ctx.config.num_subgroups;

    /* For each subgroup */
    for (int sg = 0; sg < bcast_ctx.config.num_subgroups; sg++) {
        const bap_subgroup_config_t *subgroup = &bcast_ctx.config.subgroups[sg];

        /* Num_BIS */
        p[pos++] = subgroup->num_bis;

        /* Codec_ID (5 bytes) */
        /* Format: Coding_Format (1) + Company_ID (2) + Vendor_Codec_ID (2) */
        /* LC3: 0x06, 0x0000, 0x0000 */
        p[pos++] = BAP_CODEC_ID_LC3;
        p[pos++] = 0x00;  /* Company ID LSB */
        p[pos++] = 0x00;  /* Company ID MSB */
        p[pos++] = 0x00;  /* Vendor Codec ID LSB */
        p[pos++] = 0x00;  /* Vendor Codec ID MSB */

        /* Codec_Specific_Configuration_Length and data */
        uint8_t codec_cfg_start = pos;
        pos++;  /* Reserve byte for length */

        /* Sampling Frequency */
        uint8_t freq_val = subgroup->codec_config.sampling_freq;
        pos += write_ltv(&p[pos], LTV_TYPE_SAMPLING_FREQ, &freq_val, 1);

        /* Frame Duration */
        uint8_t duration_val = subgroup->codec_config.frame_duration;
        pos += write_ltv(&p[pos], LTV_TYPE_FRAME_DURATION, &duration_val, 1);

        /* Octets per Codec Frame */
        uint8_t octets[2];
        octets[0] = subgroup->codec_config.octets_per_frame & 0xFF;
        octets[1] = (subgroup->codec_config.octets_per_frame >> 8) & 0xFF;
        pos += write_ltv(&p[pos], LTV_TYPE_OCTETS_PER_FRAME, octets, 2);

        /* Frames per SDU (if not 1) */
        if (subgroup->codec_config.frames_per_sdu > 1) {
            uint8_t frames = subgroup->codec_config.frames_per_sdu;
            pos += write_ltv(&p[pos], LTV_TYPE_FRAMES_PER_SDU, &frames, 1);
        }

        /* Write codec config length */
        p[codec_cfg_start] = pos - codec_cfg_start - 1;

        /* Metadata_Length and data */
        uint8_t metadata_start = pos;
        pos++;  /* Reserve byte for length */

        /* Streaming Audio Contexts */
        uint8_t context[2];
        context[0] = subgroup->audio_context & 0xFF;
        context[1] = (subgroup->audio_context >> 8) & 0xFF;
        pos += write_ltv(&p[pos], LTV_TYPE_STREAM_AUDIO_CONTEXT, context, 2);

        /* Language (if set) */
        if (subgroup->language[0] != '\0') {
            pos += write_ltv(&p[pos], LTV_TYPE_LANGUAGE,
                            (const uint8_t *)subgroup->language, 3);
        }

        /* Additional metadata */
        if (subgroup->metadata_len > 0) {
            memcpy(&p[pos], subgroup->metadata, subgroup->metadata_len);
            pos += subgroup->metadata_len;
        }

        /* Write metadata length */
        p[metadata_start] = pos - metadata_start - 1;

        /* For each BIS in subgroup */
        for (int bis = 0; bis < subgroup->num_bis; bis++) {
            const bap_bis_config_t *bis_cfg = &subgroup->bis[bis];

            /* BIS_Index */
            p[pos++] = bis_cfg->bis_index;

            /* BIS-level Codec_Specific_Configuration_Length and data */
            uint8_t bis_cfg_start = pos;
            pos++;  /* Reserve byte for length */

            /* Audio Channel Allocation (if not mono) */
            if (bis_cfg->audio_location != BAP_AUDIO_LOCATION_MONO) {
                uint8_t loc[4];
                loc[0] = bis_cfg->audio_location & 0xFF;
                loc[1] = (bis_cfg->audio_location >> 8) & 0xFF;
                loc[2] = (bis_cfg->audio_location >> 16) & 0xFF;
                loc[3] = (bis_cfg->audio_location >> 24) & 0xFF;
                pos += write_ltv(&p[pos], LTV_TYPE_AUDIO_LOCATIONS, loc, 4);
            }

            /* Additional BIS-specific codec config */
            if (bis_cfg->codec_cfg_len > 0) {
                memcpy(&p[pos], bis_cfg->codec_cfg, bis_cfg->codec_cfg_len);
                pos += bis_cfg->codec_cfg_len;
            }

            /* Write BIS codec config length */
            p[bis_cfg_start] = pos - bis_cfg_start - 1;
        }
    }

    bcast_ctx.base_len = pos;

    /* Build periodic advertising data with BASE */
    /* Service Data: Basic Audio Announcement Service (UUID 0x1851) + BASE */
    uint8_t *pa = bcast_ctx.periodic_adv_data;
    uint8_t pa_pos = 0;

    pa[pa_pos++] = bcast_ctx.base_len + 3;  /* Length */
    pa[pa_pos++] = AD_TYPE_SERVICE_DATA_16;
    pa[pa_pos++] = 0x51;  /* UUID 0x1851 LSB (Basic Audio Announcement) */
    pa[pa_pos++] = 0x18;  /* UUID 0x1851 MSB */
    memcpy(&pa[pa_pos], bcast_ctx.base_data, bcast_ctx.base_len);
    pa_pos += bcast_ctx.base_len;

    bcast_ctx.periodic_adv_len = pa_pos;

    return BAP_BROADCAST_OK;
}

/**
 * @brief Start extended advertising
 */
static int start_extended_advertising(void)
{
    /*
     * TODO: Configure and start extended advertising using BTSTACK
     *
     * Steps:
     * 1. Set Extended Advertising Parameters
     *    - Advertising type: ADV_EXT_IND (non-connectable, scannable or not)
     *    - Properties: Include TX Power
     *    - PHY: 1M or Coded
     *
     * 2. Set Extended Advertising Data
     *
     * 3. Enable Extended Advertising
     *
     * Example with Infineon BTSTACK:
     *
     * wiced_bt_ble_ext_adv_set_params_t params = {
     *     .event_properties = WICED_BT_BLE_EXT_ADV_EVENT_LEGACY_ADV,
     *     .primary_adv_int_min = bcast_ctx.config.adv_interval_min,
     *     .primary_adv_int_max = bcast_ctx.config.adv_interval_max,
     *     .primary_adv_channel_map = 0x07,
     *     .own_addr_type = BLE_ADDR_RANDOM,
     *     .peer_addr_type = BLE_ADDR_PUBLIC,
     *     .adv_filter_policy = BTM_BLE_ADV_POLICY_ACCEPT_CONN_AND_SCAN,
     *     .adv_tx_power = bcast_ctx.config.tx_power,
     *     .primary_adv_phy = WICED_BT_BLE_EXT_ADV_PHY_1M,
     *     .secondary_adv_phy = WICED_BT_BLE_EXT_ADV_PHY_2M
     * };
     *
     * wiced_bt_ble_set_ext_adv_parameters(EXT_ADV_HANDLE, &params);
     * wiced_bt_ble_set_ext_adv_data(EXT_ADV_HANDLE, bcast_ctx.adv_data_len, bcast_ctx.adv_data);
     * wiced_bt_ble_start_ext_adv(1, EXT_ADV_HANDLE, 0, 0);
     */

    bcast_ctx.info.adv_handle = EXT_ADV_HANDLE;

    return BAP_BROADCAST_OK;
}

/**
 * @brief Stop extended advertising
 */
static int stop_extended_advertising(void)
{
    /*
     * TODO: Stop extended advertising
     *
     * wiced_bt_ble_stop_ext_adv(EXT_ADV_HANDLE);
     */

    return BAP_BROADCAST_OK;
}

/**
 * @brief Start periodic advertising
 */
static int start_periodic_advertising(void)
{
    /*
     * TODO: Configure and start periodic advertising using BTSTACK
     *
     * Steps:
     * 1. Set Periodic Advertising Parameters
     *    - Interval: typically 100ms-200ms for audio
     *
     * 2. Set Periodic Advertising Data (BASE structure)
     *
     * 3. Enable Periodic Advertising
     *
     * Example:
     *
     * wiced_bt_ble_periodic_adv_params_t pa_params = {
     *     .adv_int_min = 80,   // 100ms (in 1.25ms units)
     *     .adv_int_max = 160   // 200ms
     * };
     *
     * wiced_bt_ble_set_periodic_adv_parameters(EXT_ADV_HANDLE, &pa_params);
     * wiced_bt_ble_set_periodic_adv_data(EXT_ADV_HANDLE, bcast_ctx.periodic_adv_len, bcast_ctx.periodic_adv_data);
     * wiced_bt_ble_start_periodic_adv(EXT_ADV_HANDLE, TRUE);
     */

    return BAP_BROADCAST_OK;
}

/**
 * @brief Stop periodic advertising
 */
static int stop_periodic_advertising(void)
{
    /*
     * TODO: Stop periodic advertising
     *
     * wiced_bt_ble_start_periodic_adv(EXT_ADV_HANDLE, FALSE);
     */

    return BAP_BROADCAST_OK;
}

/*******************************************************************************
 * BIG Management
 ******************************************************************************/

/**
 * @brief Create BIG for broadcast
 */
static int create_big(void)
{
    /* Count total BIS */
    uint8_t total_bis = 0;
    for (int sg = 0; sg < bcast_ctx.config.num_subgroups; sg++) {
        total_bis += bcast_ctx.config.subgroups[sg].num_bis;
    }

    /* Get SDU interval from first subgroup's codec config */
    uint32_t sdu_interval;
    if (bcast_ctx.config.subgroups[0].codec_config.frame_duration == BAP_LC3_DURATION_7_5MS) {
        sdu_interval = 7500;
    } else {
        sdu_interval = 10000;
    }

    /* Get max SDU size from first subgroup */
    uint16_t max_sdu = bcast_ctx.config.subgroups[0].codec_config.octets_per_frame;
    if (bcast_ctx.config.subgroups[0].codec_config.frames_per_sdu > 1) {
        max_sdu *= bcast_ctx.config.subgroups[0].codec_config.frames_per_sdu;
    }

    /* Configure BIG */
    big_config_t big_config = {
        .big_handle = 0,  /* Let controller assign */
        .adv_handle = EXT_ADV_HANDLE,
        .num_bis = total_bis,
        .sdu_interval = sdu_interval,
        .max_sdu = max_sdu,
        .max_transport_latency = bcast_ctx.config.max_transport_latency_ms,
        .rtn = bcast_ctx.config.rtn,
        .phy = bcast_ctx.config.phy,
        .packing = HCI_ISOC_PACKING_SEQUENTIAL,
        .framing = HCI_ISOC_FRAMING_UNFRAMED,
        .encryption = bcast_ctx.config.encrypted ? HCI_ISOC_ENCRYPT_ENABLED : HCI_ISOC_ENCRYPT_DISABLED
    };

    if (bcast_ctx.config.encrypted) {
        memcpy(big_config.broadcast_code, bcast_ctx.config.broadcast_code, 16);
    }

    int result = hci_isoc_create_big(&big_config);
    if (result != HCI_ISOC_OK) {
        return BAP_BROADCAST_ERROR_BIG_FAILED;
    }

    return BAP_BROADCAST_OK;
}

/**
 * @brief Terminate BIG
 */
static int terminate_big(void)
{
    int result = hci_isoc_terminate_big(bcast_ctx.info.big_handle, 0x13);
    if (result != HCI_ISOC_OK) {
        return BAP_BROADCAST_ERROR_BIG_FAILED;
    }

    return BAP_BROADCAST_OK;
}

/*******************************************************************************
 * Public API - Initialization
 ******************************************************************************/

int bap_broadcast_init(void)
{
    if (bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_ALREADY_INITIALIZED;
    }

    memset(&bcast_ctx, 0, sizeof(bcast_ctx));

    /* Initialize HCI ISOC if not already */
    int result = hci_isoc_init();
    if (result != HCI_ISOC_OK && result != BAP_BROADCAST_ERROR_ALREADY_INITIALIZED) {
        return BAP_BROADCAST_ERROR_NO_RESOURCES;
    }

    /* Register for HCI ISOC events */
    hci_isoc_register_callback(hci_isoc_event_handler, NULL);

    bcast_ctx.initialized = true;
    bcast_ctx.state = BAP_BROADCAST_STATE_IDLE;
    bcast_ctx.info.state = BAP_BROADCAST_STATE_IDLE;

    return BAP_BROADCAST_OK;
}

void bap_broadcast_deinit(void)
{
    if (!bcast_ctx.initialized) {
        return;
    }

    /* Stop if streaming */
    if (bcast_ctx.state == BAP_BROADCAST_STATE_STREAMING) {
        bap_broadcast_stop();
    }

    /* Stop advertising if active */
    if (bcast_ctx.state == BAP_BROADCAST_STATE_ADVERTISING) {
        stop_periodic_advertising();
        stop_extended_advertising();
    }

    bcast_ctx.initialized = false;
    bcast_ctx.state = BAP_BROADCAST_STATE_IDLE;
}

void bap_broadcast_register_callback(bap_broadcast_callback_t callback, void *user_data)
{
    bcast_ctx.callback = callback;
    bcast_ctx.callback_user_data = user_data;
}

/*******************************************************************************
 * Public API - Broadcast Control
 ******************************************************************************/

int bap_broadcast_configure(const bap_broadcast_config_t *config)
{
    if (!bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    if (bcast_ctx.state != BAP_BROADCAST_STATE_IDLE &&
        bcast_ctx.state != BAP_BROADCAST_STATE_CONFIGURED) {
        return BAP_BROADCAST_ERROR_INVALID_STATE;
    }

    /* Validate configuration */
    if (config->num_subgroups == 0 ||
        config->num_subgroups > BAP_BROADCAST_MAX_SUBGROUPS) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    for (int sg = 0; sg < config->num_subgroups; sg++) {
        if (config->subgroups[sg].num_bis == 0 ||
            config->subgroups[sg].num_bis > BAP_BROADCAST_MAX_BIS) {
            return BAP_BROADCAST_ERROR_INVALID_PARAM;
        }
    }

    /* Store configuration */
    bcast_ctx.config = *config;

    /* Copy Broadcast_ID to info */
    memcpy(bcast_ctx.info.broadcast_id, config->broadcast_id, 3);

    /* Build advertising data */
    int result = build_adv_data();
    if (result != BAP_BROADCAST_OK) {
        return result;
    }

    /* Build BASE structure */
    result = build_base_structure();
    if (result != BAP_BROADCAST_OK) {
        return result;
    }

    set_state(BAP_BROADCAST_STATE_CONFIGURED);

    return BAP_BROADCAST_OK;
}

int bap_broadcast_start(void)
{
    if (!bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_NOT_INITIALIZED;
    }

    if (bcast_ctx.state != BAP_BROADCAST_STATE_CONFIGURED) {
        return BAP_BROADCAST_ERROR_INVALID_STATE;
    }

    int result;

    /* Start extended advertising */
    result = start_extended_advertising();
    if (result != BAP_BROADCAST_OK) {
        return result;
    }

    /* Start periodic advertising with BASE */
    result = start_periodic_advertising();
    if (result != BAP_BROADCAST_OK) {
        stop_extended_advertising();
        return result;
    }

    set_state(BAP_BROADCAST_STATE_ADVERTISING);

    /* Create BIG */
    result = create_big();
    if (result != BAP_BROADCAST_OK) {
        stop_periodic_advertising();
        stop_extended_advertising();
        set_state(BAP_BROADCAST_STATE_CONFIGURED);
        return result;
    }

    /* State will transition to STREAMING when BIG_CREATED event is received */

    return BAP_BROADCAST_OK;
}

int bap_broadcast_stop(void)
{
    if (!bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_NOT_INITIALIZED;
    }

    if (bcast_ctx.state != BAP_BROADCAST_STATE_STREAMING &&
        bcast_ctx.state != BAP_BROADCAST_STATE_ADVERTISING) {
        return BAP_BROADCAST_ERROR_INVALID_STATE;
    }

    set_state(BAP_BROADCAST_STATE_STOPPING);

    /* Terminate BIG if streaming */
    if (bcast_ctx.state == BAP_BROADCAST_STATE_STREAMING ||
        bcast_ctx.info.big_handle != 0) {
        terminate_big();
    }

    /* Stop periodic advertising */
    stop_periodic_advertising();

    /* Stop extended advertising */
    stop_extended_advertising();

    /* Update statistics */
    if (bcast_ctx.start_time_ms > 0) {
        bcast_ctx.stats.uptime_ms += get_time_ms() - bcast_ctx.start_time_ms;
    }

    /* Reset TX state */
    bcast_ctx.tx_seq_num = 0;
    bcast_ctx.next_bis_index = 0;
    bcast_ctx.info.big_handle = 0;

    set_state(BAP_BROADCAST_STATE_CONFIGURED);

    return BAP_BROADCAST_OK;
}

bap_broadcast_state_t bap_broadcast_get_state(void)
{
    return bcast_ctx.state;
}

int bap_broadcast_get_info(bap_broadcast_info_t *info)
{
    if (info == NULL) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    *info = bcast_ctx.info;
    return BAP_BROADCAST_OK;
}

/*******************************************************************************
 * Public API - Audio Transmission
 ******************************************************************************/

int bap_broadcast_send_frame(const bap_broadcast_frame_t *frame)
{
    if (!bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_NOT_INITIALIZED;
    }

    if (bcast_ctx.state != BAP_BROADCAST_STATE_STREAMING) {
        return BAP_BROADCAST_ERROR_INVALID_STATE;
    }

    if (frame == NULL || frame->data == NULL || frame->length == 0) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    /* Calculate BIS handle from subgroup and BIS index */
    uint8_t bis_offset = 0;
    for (int sg = 0; sg < frame->subgroup; sg++) {
        bis_offset += bcast_ctx.config.subgroups[sg].num_bis;
    }
    bis_offset += frame->bis_index;

    if (bis_offset >= bcast_ctx.info.num_bis) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    uint16_t bis_handle = bcast_ctx.info.bis_handles[bis_offset];

    /* Send via HCI ISOC */
    int result = hci_isoc_send_data_ts(bis_handle, frame->data, frame->length,
                                        frame->timestamp, frame->seq_num);

    if (result == HCI_ISOC_OK) {
        bcast_ctx.stats.frames_sent++;
        bcast_ctx.stats.bytes_sent += frame->length;
    } else {
        bcast_ctx.stats.frames_dropped++;
        return BAP_BROADCAST_ERROR_NO_RESOURCES;
    }

    return BAP_BROADCAST_OK;
}

int bap_broadcast_send_all(const uint8_t *data, uint16_t length, uint32_t timestamp)
{
    if (!bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_NOT_INITIALIZED;
    }

    if (bcast_ctx.state != BAP_BROADCAST_STATE_STREAMING) {
        return BAP_BROADCAST_ERROR_INVALID_STATE;
    }

    if (data == NULL || length == 0) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    int result = BAP_BROADCAST_OK;

    /* Send to all BIS */
    for (int i = 0; i < bcast_ctx.info.num_bis; i++) {
        uint16_t bis_handle = bcast_ctx.info.bis_handles[i];

        int send_result = hci_isoc_send_data_ts(bis_handle, data, length,
                                                 timestamp, bcast_ctx.tx_seq_num);

        if (send_result == HCI_ISOC_OK) {
            bcast_ctx.stats.frames_sent++;
            bcast_ctx.stats.bytes_sent += length;
        } else {
            bcast_ctx.stats.frames_dropped++;
            result = BAP_BROADCAST_ERROR_NO_RESOURCES;
        }
    }

    bcast_ctx.tx_seq_num++;

    return result;
}

uint16_t bap_broadcast_get_next_bis_handle(void)
{
    if (bcast_ctx.state != BAP_BROADCAST_STATE_STREAMING ||
        bcast_ctx.info.num_bis == 0) {
        return 0xFFFF;
    }

    uint16_t handle = bcast_ctx.info.bis_handles[bcast_ctx.next_bis_index];
    bcast_ctx.next_bis_index = (bcast_ctx.next_bis_index + 1) % bcast_ctx.info.num_bis;

    return handle;
}

/*******************************************************************************
 * Public API - Metadata Updates
 ******************************************************************************/

int bap_broadcast_update_name(const char *name)
{
    if (!bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_NOT_INITIALIZED;
    }

    if (name == NULL) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    strncpy(bcast_ctx.config.broadcast_name, name, BAP_BROADCAST_MAX_NAME_LEN - 1);
    bcast_ctx.config.broadcast_name[BAP_BROADCAST_MAX_NAME_LEN - 1] = '\0';

    /* Rebuild advertising data */
    int result = build_adv_data();
    if (result != BAP_BROADCAST_OK) {
        return result;
    }

    /* Update advertising if active */
    if (bcast_ctx.state == BAP_BROADCAST_STATE_ADVERTISING ||
        bcast_ctx.state == BAP_BROADCAST_STATE_STREAMING) {
        /*
         * TODO: Update extended advertising data
         *
         * wiced_bt_ble_set_ext_adv_data(EXT_ADV_HANDLE, bcast_ctx.adv_data_len, bcast_ctx.adv_data);
         */
    }

    return BAP_BROADCAST_OK;
}

int bap_broadcast_update_context(uint8_t subgroup, uint16_t context)
{
    if (!bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_NOT_INITIALIZED;
    }

    if (subgroup >= bcast_ctx.config.num_subgroups) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    bcast_ctx.config.subgroups[subgroup].audio_context = context;

    /* Rebuild BASE structure */
    int result = build_base_structure();
    if (result != BAP_BROADCAST_OK) {
        return result;
    }

    /* Update periodic advertising if active */
    if (bcast_ctx.state == BAP_BROADCAST_STATE_ADVERTISING ||
        bcast_ctx.state == BAP_BROADCAST_STATE_STREAMING) {
        /*
         * TODO: Update periodic advertising data
         *
         * wiced_bt_ble_set_periodic_adv_data(EXT_ADV_HANDLE, bcast_ctx.periodic_adv_len, bcast_ctx.periodic_adv_data);
         */
    }

    return BAP_BROADCAST_OK;
}

int bap_broadcast_update_metadata(uint8_t subgroup, const uint8_t *metadata, uint8_t length)
{
    if (!bcast_ctx.initialized) {
        return BAP_BROADCAST_ERROR_NOT_INITIALIZED;
    }

    if (subgroup >= bcast_ctx.config.num_subgroups ||
        length > BAP_BROADCAST_MAX_METADATA_LEN) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    if (metadata != NULL && length > 0) {
        memcpy(bcast_ctx.config.subgroups[subgroup].metadata, metadata, length);
    }
    bcast_ctx.config.subgroups[subgroup].metadata_len = length;

    /* Rebuild BASE structure */
    return build_base_structure();
}

/*******************************************************************************
 * Public API - BASE Generation
 ******************************************************************************/

int bap_broadcast_generate_base(const bap_broadcast_config_t *config,
                                 uint8_t *base_out, uint16_t base_size,
                                 uint16_t *base_len_out)
{
    if (config == NULL || base_out == NULL || base_len_out == NULL) {
        return BAP_BROADCAST_ERROR_INVALID_PARAM;
    }

    /* Temporarily store config and generate BASE */
    bap_broadcast_config_t saved_config = bcast_ctx.config;
    bcast_ctx.config = *config;

    int result = build_base_structure();

    if (result == BAP_BROADCAST_OK) {
        if (bcast_ctx.base_len <= base_size) {
            memcpy(base_out, bcast_ctx.base_data, bcast_ctx.base_len);
            *base_len_out = bcast_ctx.base_len;
        } else {
            result = BAP_BROADCAST_ERROR_NO_RESOURCES;
        }
    }

    /* Restore config */
    bcast_ctx.config = saved_config;

    return result;
}

/*******************************************************************************
 * Public API - Statistics
 ******************************************************************************/

void bap_broadcast_get_stats(bap_broadcast_stats_t *stats)
{
    if (stats != NULL) {
        *stats = bcast_ctx.stats;

        /* Add current streaming time */
        if (bcast_ctx.state == BAP_BROADCAST_STATE_STREAMING &&
            bcast_ctx.start_time_ms > 0) {
            stats->uptime_ms += get_time_ms() - bcast_ctx.start_time_ms;
        }
    }
}

void bap_broadcast_reset_stats(void)
{
    memset(&bcast_ctx.stats, 0, sizeof(bcast_ctx.stats));
    bcast_ctx.start_time_ms = get_time_ms();
}

/*******************************************************************************
 * Public API - Utilities
 ******************************************************************************/

uint8_t bap_broadcast_sample_rate_to_lc3(uint32_t sample_rate)
{
    switch (sample_rate) {
        case 8000:   return BAP_LC3_FREQ_8000;
        case 11025:  return BAP_LC3_FREQ_11025;
        case 16000:  return BAP_LC3_FREQ_16000;
        case 22050:  return BAP_LC3_FREQ_22050;
        case 24000:  return BAP_LC3_FREQ_24000;
        case 32000:  return BAP_LC3_FREQ_32000;
        case 44100:  return BAP_LC3_FREQ_44100;
        case 48000:  return BAP_LC3_FREQ_48000;
        case 88200:  return BAP_LC3_FREQ_88200;
        case 96000:  return BAP_LC3_FREQ_96000;
        case 176400: return BAP_LC3_FREQ_176400;
        case 192000: return BAP_LC3_FREQ_192000;
        case 384000: return BAP_LC3_FREQ_384000;
        default:     return 0;
    }
}

uint32_t bap_broadcast_lc3_to_sample_rate(uint8_t lc3_freq)
{
    switch (lc3_freq) {
        case BAP_LC3_FREQ_8000:   return 8000;
        case BAP_LC3_FREQ_11025:  return 11025;
        case BAP_LC3_FREQ_16000:  return 16000;
        case BAP_LC3_FREQ_22050:  return 22050;
        case BAP_LC3_FREQ_24000:  return 24000;
        case BAP_LC3_FREQ_32000:  return 32000;
        case BAP_LC3_FREQ_44100:  return 44100;
        case BAP_LC3_FREQ_48000:  return 48000;
        case BAP_LC3_FREQ_88200:  return 88200;
        case BAP_LC3_FREQ_96000:  return 96000;
        case BAP_LC3_FREQ_176400: return 176400;
        case BAP_LC3_FREQ_192000: return 192000;
        case BAP_LC3_FREQ_384000: return 384000;
        default:                  return 0;
    }
}

void bap_broadcast_generate_id(uint8_t broadcast_id[3])
{
    /*
     * TODO: Generate random Broadcast_ID
     *
     * Use hardware RNG if available:
     * cyhal_trng_generate(&trng_obj, &random_value);
     *
     * For now, use simple pseudo-random
     */
    static uint32_t seed = 0x12345678;

    seed = seed * 1103515245 + 12345;
    broadcast_id[0] = (seed >> 16) & 0xFF;

    seed = seed * 1103515245 + 12345;
    broadcast_id[1] = (seed >> 16) & 0xFF;

    seed = seed * 1103515245 + 12345;
    broadcast_id[2] = (seed >> 16) & 0xFF;
}
