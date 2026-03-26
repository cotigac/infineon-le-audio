/**
 * @file bap_broadcast_sink.c
 * @brief BAP Broadcast Sink Implementation (Auracast RX)
 *
 * Implements the BAP Broadcast Sink role for receiving Auracast broadcasts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bap_broadcast_sink.h"
#include "../bluetooth/hci_isoc.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Platform Includes
 ******************************************************************************/

/* Infineon BTSTACK headers */
#include "wiced_bt_ble.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_isoc.h"
#include "wiced_bt_adv_scan_extended.h"
#include "wiced_bt_adv_scan_periodic.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*******************************************************************************
 * Constants
 ******************************************************************************/

/** AD Type values */
#define AD_TYPE_FLAGS           0x01
#define AD_TYPE_SERVICE_UUID_16 0x03
#define AD_TYPE_COMPLETE_NAME   0x09
#define AD_TYPE_SERVICE_DATA_16 0x16
#define AD_TYPE_BROADCAST_NAME  0x30

/** UUID for Broadcast Audio Announcement Service */
#define UUID_BROADCAST_AUDIO_ANNOUNCEMENT   0x1852

/** UUID for Basic Audio Announcement Service (contains BASE) */
#define UUID_BASIC_AUDIO_ANNOUNCEMENT       0x1851

/** LTV (Length-Type-Value) types for Codec Specific Configuration */
#define LTV_TYPE_SAMPLING_FREQ      0x01
#define LTV_TYPE_FRAME_DURATION     0x02
#define LTV_TYPE_AUDIO_LOCATIONS    0x03
#define LTV_TYPE_OCTETS_PER_FRAME   0x04
#define LTV_TYPE_FRAMES_PER_SDU     0x05

/** LTV types for Metadata */
#define LTV_TYPE_PREF_AUDIO_CONTEXT     0x01
#define LTV_TYPE_STREAM_AUDIO_CONTEXT   0x02
#define LTV_TYPE_PROGRAM_INFO           0x03
#define LTV_TYPE_LANGUAGE               0x04

/** Scan parameters */
#define SCAN_INTERVAL_MS        100
#define SCAN_WINDOW_MS          50
#define PA_SYNC_TIMEOUT_10MS    1000    /* 10 seconds */
#define BIG_SYNC_TIMEOUT_10MS   300     /* 3 seconds */

/*******************************************************************************
 * Types
 ******************************************************************************/

/** Module context */
typedef struct {
    bool initialized;
    bap_broadcast_sink_state_t state;

    /* Callback */
    bap_broadcast_sink_callback_t callback;
    void *callback_user_data;

    /* Scan state */
    bool scanning;

    /* PA Sync state */
    uint16_t pa_sync_handle;
    bap_broadcast_source_t current_source;

    /* BASE and BIGInfo */
    bap_base_info_t base_info;
    bool base_received;
    bap_biginfo_t biginfo;
    bool biginfo_received;

    /* BIG Sync state */
    uint8_t big_handle;
    uint16_t bis_handles[BAP_BASE_MAX_BIS];
    uint8_t num_bis;
    uint8_t broadcast_code[16];
    bool encrypted;

    /* Statistics */
    bap_broadcast_sink_stats_t stats;
    uint32_t start_time_ms;

    /* Demo mode */
    bool demo_auto_sync;
    const uint8_t *demo_broadcast_code;

    /* Pending sync - auto-trigger BIG sync when BIGInfo received */
    bool pending_big_sync;
    uint8_t pending_broadcast_code[16];
    bool pending_has_code;

} bap_broadcast_sink_ctx_t;

/*******************************************************************************
 * Module Variables
 ******************************************************************************/

static bap_broadcast_sink_ctx_t sink_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static void dispatch_event(const bap_broadcast_sink_event_t *event);
static void set_state(bap_broadcast_sink_state_t new_state);
static uint32_t get_time_ms(void);
static void ext_scan_callback(wiced_bt_ble_scan_results_t *p_scan_result,
                               uint8_t *p_adv_data);
static bool parse_broadcast_announcement(const uint8_t *adv_data, uint8_t len,
                                          bap_broadcast_source_t *source);
static void padv_event_callback(wiced_ble_ext_adv_event_t event,
                                 wiced_ble_ext_adv_event_data_t *p_data);
static void hci_isoc_event_handler(const hci_isoc_event_t *event, void *user_data);
static int parse_codec_config_ltv(const uint8_t *data, uint8_t len,
                                   bap_sink_codec_config_t *config);
static int parse_metadata_ltv(const uint8_t *data, uint8_t len,
                               bap_metadata_t *metadata);

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

static uint32_t get_time_ms(void)
{
    TickType_t ticks = xTaskGetTickCount();
    return (uint32_t)(ticks * (1000UL / configTICK_RATE_HZ));
}

static void dispatch_event(const bap_broadcast_sink_event_t *event)
{
    if (sink_ctx.callback != NULL) {
        sink_ctx.callback(event, sink_ctx.callback_user_data);
    }
}

static void set_state(bap_broadcast_sink_state_t new_state)
{
    if (sink_ctx.state != new_state) {
        printf("Broadcast Sink: State %d -> %d\n", sink_ctx.state, new_state);
        sink_ctx.state = new_state;

        bap_broadcast_sink_event_t event = {
            .type = BAP_BROADCAST_SINK_EVENT_STATE_CHANGED,
            .data.new_state = new_state
        };
        dispatch_event(&event);
    }
}

/*******************************************************************************
 * BASE Parser Implementation
 ******************************************************************************/

int bap_parse_base(const uint8_t *data, uint16_t len, bap_base_info_t *base_info)
{
    if (data == NULL || base_info == NULL || len < 4) {
        return BAP_BROADCAST_SINK_ERROR_INVALID_PARAM;
    }

    memset(base_info, 0, sizeof(bap_base_info_t));
    uint16_t pos = 0;

    /* Presentation Delay (3 bytes, little endian) */
    base_info->presentation_delay_us = data[pos] |
                                       ((uint32_t)data[pos+1] << 8) |
                                       ((uint32_t)data[pos+2] << 16);
    pos += 3;

    /* Number of Subgroups */
    if (pos >= len) return BAP_BROADCAST_SINK_ERROR_INVALID_PARAM;
    base_info->num_subgroups = data[pos++];
    if (base_info->num_subgroups > BAP_BASE_MAX_SUBGROUPS) {
        base_info->num_subgroups = BAP_BASE_MAX_SUBGROUPS;
    }

    /* Parse each subgroup */
    for (int sg = 0; sg < base_info->num_subgroups && pos < len; sg++) {
        bap_subgroup_info_t *subgroup = &base_info->subgroups[sg];

        /* Num_BIS */
        if (pos >= len) break;
        subgroup->num_bis = data[pos++];
        if (subgroup->num_bis > BAP_BASE_MAX_BIS) {
            subgroup->num_bis = BAP_BASE_MAX_BIS;
        }

        /* Codec_ID (5 bytes) */
        if (pos + 5 > len) break;
        subgroup->codec_id = data[pos];  /* Coding Format */
        pos += 5;  /* Skip full Codec_ID */

        /* Codec_Specific_Configuration_Length and data */
        if (pos >= len) break;
        uint8_t codec_cfg_len = data[pos++];
        if (codec_cfg_len > 0 && pos + codec_cfg_len <= len) {
            parse_codec_config_ltv(&data[pos], codec_cfg_len, &subgroup->codec_config);
            pos += codec_cfg_len;
        }

        /* Metadata_Length and data */
        if (pos >= len) break;
        uint8_t metadata_len = data[pos++];
        if (metadata_len > 0 && pos + metadata_len <= len) {
            parse_metadata_ltv(&data[pos], metadata_len, &subgroup->metadata);
            pos += metadata_len;
        }

        /* Parse each BIS in subgroup */
        for (int bis = 0; bis < subgroup->num_bis && pos < len; bis++) {
            /* BIS_Index */
            if (pos >= len) break;
            subgroup->bis[bis].bis_index = data[pos++];

            /* BIS-level Codec_Specific_Configuration */
            if (pos >= len) break;
            uint8_t bis_cfg_len = data[pos++];

            /* Start with subgroup config, override with BIS-specific */
            subgroup->bis[bis].codec_config = subgroup->codec_config;

            if (bis_cfg_len > 0 && pos + bis_cfg_len <= len) {
                parse_codec_config_ltv(&data[pos], bis_cfg_len,
                                       &subgroup->bis[bis].codec_config);
                pos += bis_cfg_len;
            }
        }
    }

    printf("BASE parsed: delay=%lu us, %d subgroups\n",
           (unsigned long)base_info->presentation_delay_us,
           base_info->num_subgroups);

    return BAP_BROADCAST_SINK_OK;
}

static int parse_codec_config_ltv(const uint8_t *data, uint8_t len,
                                   bap_sink_codec_config_t *config)
{
    uint8_t pos = 0;

    /* Default values */
    config->frames_per_sdu = 1;

    while (pos < len) {
        uint8_t ltv_len = data[pos++];
        if (ltv_len == 0 || pos + ltv_len > len) break;

        uint8_t type = data[pos++];
        uint8_t value_len = ltv_len - 1;

        switch (type) {
            case LTV_TYPE_SAMPLING_FREQ:
                if (value_len >= 1) {
                    config->sampling_freq = data[pos];
                }
                break;

            case LTV_TYPE_FRAME_DURATION:
                if (value_len >= 1) {
                    config->frame_duration = data[pos];
                }
                break;

            case LTV_TYPE_AUDIO_LOCATIONS:
                if (value_len >= 4) {
                    config->audio_location = data[pos] |
                                             ((uint32_t)data[pos+1] << 8) |
                                             ((uint32_t)data[pos+2] << 16) |
                                             ((uint32_t)data[pos+3] << 24);
                }
                break;

            case LTV_TYPE_OCTETS_PER_FRAME:
                if (value_len >= 2) {
                    config->octets_per_frame = data[pos] |
                                               ((uint16_t)data[pos+1] << 8);
                }
                break;

            case LTV_TYPE_FRAMES_PER_SDU:
                if (value_len >= 1) {
                    config->frames_per_sdu = data[pos];
                }
                break;

            default:
                break;
        }

        pos += value_len;
    }

    return BAP_BROADCAST_SINK_OK;
}

static int parse_metadata_ltv(const uint8_t *data, uint8_t len,
                               bap_metadata_t *metadata)
{
    uint8_t pos = 0;

    /* Store raw metadata */
    if (len <= BAP_BASE_MAX_METADATA_LEN) {
        memcpy(metadata->raw_metadata, data, len);
        metadata->raw_metadata_len = len;
    }

    while (pos < len) {
        uint8_t ltv_len = data[pos++];
        if (ltv_len == 0 || pos + ltv_len > len) break;

        uint8_t type = data[pos++];
        uint8_t value_len = ltv_len - 1;

        switch (type) {
            case LTV_TYPE_STREAM_AUDIO_CONTEXT:
                if (value_len >= 2) {
                    metadata->audio_context = data[pos] |
                                              ((uint16_t)data[pos+1] << 8);
                }
                break;

            case LTV_TYPE_LANGUAGE:
                if (value_len >= 3) {
                    memcpy(metadata->language, &data[pos], 3);
                    metadata->language[3] = '\0';
                }
                break;

            default:
                break;
        }

        pos += value_len;
    }

    return BAP_BROADCAST_SINK_OK;
}

uint32_t bap_lc3_freq_to_hz(uint8_t lc3_freq)
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

uint32_t bap_lc3_duration_to_us(uint8_t lc3_duration)
{
    switch (lc3_duration) {
        case BAP_LC3_DURATION_7_5MS: return 7500;
        case BAP_LC3_DURATION_10MS:  return 10000;
        default:                     return 10000;
    }
}

/*******************************************************************************
 * Scan and Advertisement Handling
 ******************************************************************************/

static void ext_scan_callback(wiced_bt_ble_scan_results_t *p_scan_result,
                               uint8_t *p_adv_data)
{
    if (p_scan_result == NULL || p_adv_data == NULL) {
        return;
    }

    /* Only process if scanning */
    if (!sink_ctx.scanning) {
        return;
    }

    /* Try to parse as broadcast announcement */
    bap_broadcast_source_t source;
    memset(&source, 0, sizeof(source));

    if (parse_broadcast_announcement(p_adv_data, 31, &source)) {
        /* Copy address info */
        memcpy(source.addr, p_scan_result->remote_bd_addr, 6);
        source.addr_type = p_scan_result->ble_addr_type;
        source.rssi = p_scan_result->rssi;

        /* Extract adv_sid - for standard scan results, use default of 0
         * Note: Full extended scan implementation would get this from
         * wiced_bt_ble_ext_scan_results_t.adv_sid */
        source.adv_sid = 0;  /* Default SID for basic periodic advertising */

        printf("Broadcast found: %02X%02X%02X, name=%s, RSSI=%d\n",
               source.broadcast_id[0], source.broadcast_id[1], source.broadcast_id[2],
               source.broadcast_name, source.rssi);

        /* Dispatch event */
        bap_broadcast_sink_event_t event = {
            .type = BAP_BROADCAST_SINK_EVENT_SOURCE_FOUND,
            .data.source = source
        };
        dispatch_event(&event);

        /* Auto-sync in demo mode */
        if (sink_ctx.demo_auto_sync && sink_ctx.state == BAP_BROADCAST_SINK_STATE_SCANNING) {
            printf("Demo: Auto-syncing to first broadcast\n");
            sink_ctx.demo_auto_sync = false;
            bap_broadcast_sink_sync_to_pa(&source);
        }
    }
}

static bool parse_broadcast_announcement(const uint8_t *adv_data, uint8_t len,
                                          bap_broadcast_source_t *source)
{
    uint8_t pos = 0;
    bool found_uuid = false;
    bool found_broadcast_id = false;

    while (pos < len) {
        uint8_t ad_len = adv_data[pos++];
        if (ad_len == 0 || pos + ad_len > len) break;

        uint8_t ad_type = adv_data[pos++];
        uint8_t data_len = ad_len - 1;

        switch (ad_type) {
            case AD_TYPE_SERVICE_UUID_16:
                /* Check for Broadcast Audio Announcement UUID */
                for (int i = 0; i + 1 < data_len; i += 2) {
                    uint16_t uuid = adv_data[pos + i] | ((uint16_t)adv_data[pos + i + 1] << 8);
                    if (uuid == UUID_BROADCAST_AUDIO_ANNOUNCEMENT) {
                        found_uuid = true;
                        break;
                    }
                }
                break;

            case AD_TYPE_SERVICE_DATA_16:
                /* Check for Broadcast Audio Announcement service data */
                if (data_len >= 5) {
                    uint16_t uuid = adv_data[pos] | ((uint16_t)adv_data[pos + 1] << 8);
                    if (uuid == UUID_BROADCAST_AUDIO_ANNOUNCEMENT) {
                        /* Extract Broadcast_ID (3 bytes) */
                        source->broadcast_id[0] = adv_data[pos + 2];
                        source->broadcast_id[1] = adv_data[pos + 3];
                        source->broadcast_id[2] = adv_data[pos + 4];
                        found_broadcast_id = true;
                    }
                }
                break;

            case AD_TYPE_COMPLETE_NAME:
            case AD_TYPE_BROADCAST_NAME:
                /* Extract broadcast name */
                if (data_len > 0) {
                    uint8_t copy_len = data_len;
                    if (copy_len >= BAP_BROADCAST_SINK_MAX_NAME) {
                        copy_len = BAP_BROADCAST_SINK_MAX_NAME - 1;
                    }
                    memcpy(source->broadcast_name, &adv_data[pos], copy_len);
                    source->broadcast_name[copy_len] = '\0';
                }
                break;

            default:
                break;
        }

        pos += data_len;
    }

    return found_uuid && found_broadcast_id;
}

/*******************************************************************************
 * Periodic Advertising Callback
 ******************************************************************************/

static void padv_event_callback(wiced_ble_ext_adv_event_t event,
                                 wiced_ble_ext_adv_event_data_t *p_data)
{
    switch (event) {
        case WICED_BLE_PERIODIC_ADV_SYNC_ESTABLISHED_EVENT:
            if (p_data->sync_establish.status == 0) {
                sink_ctx.pa_sync_handle = p_data->sync_establish.sync_handle;
                printf("PA sync established: handle=%d\n", sink_ctx.pa_sync_handle);
                set_state(BAP_BROADCAST_SINK_STATE_PA_SYNCED);

                bap_broadcast_sink_event_t evt = {
                    .type = BAP_BROADCAST_SINK_EVENT_PA_SYNCED
                };
                dispatch_event(&evt);
            } else {
                printf("PA sync failed: status=%d\n", p_data->sync_establish.status);
                set_state(BAP_BROADCAST_SINK_STATE_ERROR);

                bap_broadcast_sink_event_t evt = {
                    .type = BAP_BROADCAST_SINK_EVENT_ERROR,
                    .data.error_code = BAP_BROADCAST_SINK_ERROR_PA_SYNC_FAILED
                };
                dispatch_event(&evt);
            }
            break;

        case WICED_BLE_PERIODIC_ADV_REPORT_EVENT:
            /* Parse BASE from periodic advertising data */
            if (p_data->periodic_adv_report.sync_handle == sink_ctx.pa_sync_handle) {
                /* Look for Service Data with UUID 0x1851 (Basic Audio Announcement) */
                const uint8_t *adv_data = p_data->periodic_adv_report.p_data;
                uint16_t adv_len = p_data->periodic_adv_report.data_length;
                uint8_t pos = 0;

                while (pos < adv_len) {
                    uint8_t ad_len = adv_data[pos++];
                    if (ad_len == 0 || pos + ad_len > adv_len) break;

                    uint8_t ad_type = adv_data[pos++];
                    uint8_t data_len = ad_len - 1;

                    if (ad_type == AD_TYPE_SERVICE_DATA_16 && data_len >= 2) {
                        uint16_t uuid = adv_data[pos] | ((uint16_t)adv_data[pos + 1] << 8);
                        if (uuid == UUID_BASIC_AUDIO_ANNOUNCEMENT && data_len > 2) {
                            /* Parse BASE (data after UUID) */
                            if (bap_parse_base(&adv_data[pos + 2], data_len - 2,
                                              &sink_ctx.base_info) == BAP_BROADCAST_SINK_OK) {
                                sink_ctx.base_received = true;

                                bap_broadcast_sink_event_t evt = {
                                    .type = BAP_BROADCAST_SINK_EVENT_BASE_RECEIVED,
                                    .data.base_info = &sink_ctx.base_info
                                };
                                dispatch_event(&evt);

                                /* Auto-sync to BIG if ready */
                                if (sink_ctx.state == BAP_BROADCAST_SINK_STATE_PA_SYNCED &&
                                    sink_ctx.biginfo_received) {
                                    if (sink_ctx.demo_auto_sync) {
                                        bap_broadcast_sink_sync_to_big(sink_ctx.demo_broadcast_code, NULL, 0);
                                    } else if (sink_ctx.pending_big_sync) {
                                        sink_ctx.pending_big_sync = false;
                                        bap_broadcast_sink_sync_to_big(
                                            sink_ctx.pending_has_code ? sink_ctx.pending_broadcast_code : NULL,
                                            NULL, 0);
                                    }
                                }
                            }
                        }
                    }

                    pos += data_len;
                }
            }
            break;

        case WICED_BLE_PERIODIC_ADV_SYNC_LOST_EVENT:
            printf("PA sync lost: handle=%d\n", p_data->sync_handle);
            if (p_data->sync_handle == sink_ctx.pa_sync_handle) {
                sink_ctx.pa_sync_handle = 0;
                sink_ctx.base_received = false;
                sink_ctx.biginfo_received = false;
                set_state(BAP_BROADCAST_SINK_STATE_IDLE);

                bap_broadcast_sink_event_t evt = {
                    .type = BAP_BROADCAST_SINK_EVENT_PA_SYNC_LOST
                };
                dispatch_event(&evt);
            }
            break;

        case WICED_BT_BLE_BIGINFO_ADV_REPORT_EVENT:
            /* BIGInfo received in periodic advertising */
            if (p_data->biginfo_adv_report.sync_handle == sink_ctx.pa_sync_handle) {
                wiced_ble_biginfo_adv_report_t *biginfo = &p_data->biginfo_adv_report;

                sink_ctx.biginfo.sync_handle = biginfo->sync_handle;
                sink_ctx.biginfo.num_bis = biginfo->num_bis;
                sink_ctx.biginfo.nse = biginfo->number_of_subevents;
                sink_ctx.biginfo.iso_interval = biginfo->iso_interval;
                sink_ctx.biginfo.bn = biginfo->burst_number;
                sink_ctx.biginfo.pto = biginfo->pretransmission_offset;
                sink_ctx.biginfo.irc = biginfo->immediate_repetition_count;
                sink_ctx.biginfo.max_pdu = biginfo->max_pdu;
                sink_ctx.biginfo.sdu_interval = biginfo->sdu_interval;
                sink_ctx.biginfo.max_sdu = biginfo->max_sdu;
                sink_ctx.biginfo.phy = biginfo->phy;
                sink_ctx.biginfo.framing = biginfo->framing;
                sink_ctx.biginfo.encrypted = biginfo->encryption;

                sink_ctx.biginfo_received = true;

                printf("BIGInfo: %d BIS, interval=%d, encrypted=%d\n",
                       sink_ctx.biginfo.num_bis,
                       sink_ctx.biginfo.iso_interval,
                       sink_ctx.biginfo.encrypted);

                bap_broadcast_sink_event_t evt = {
                    .type = BAP_BROADCAST_SINK_EVENT_BIGINFO_RECEIVED,
                    .data.biginfo = &sink_ctx.biginfo
                };
                dispatch_event(&evt);

                /* Auto-sync to BIG if ready */
                if (sink_ctx.state == BAP_BROADCAST_SINK_STATE_PA_SYNCED &&
                    sink_ctx.base_received) {
                    if (sink_ctx.demo_auto_sync) {
                        bap_broadcast_sink_sync_to_big(sink_ctx.demo_broadcast_code, NULL, 0);
                    } else if (sink_ctx.pending_big_sync) {
                        sink_ctx.pending_big_sync = false;
                        bap_broadcast_sink_sync_to_big(
                            sink_ctx.pending_has_code ? sink_ctx.pending_broadcast_code : NULL,
                            NULL, 0);
                    }
                }
            }
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * HCI ISOC Event Handler
 ******************************************************************************/

static void hci_isoc_event_handler(const hci_isoc_event_t *event, void *user_data)
{
    (void)user_data;

    switch (event->type) {
        case HCI_ISOC_EVENT_BIG_SYNC_ESTABLISHED:
            /* BIG sync established */
            sink_ctx.big_handle = event->data.big_info.big_handle;
            sink_ctx.num_bis = event->data.big_info.num_bis;
            for (int i = 0; i < event->data.big_info.num_bis && i < BAP_BASE_MAX_BIS; i++) {
                sink_ctx.bis_handles[i] = event->data.big_info.bis_handles[i];
            }

            printf("BIG sync established: handle=%d, %d BIS\n",
                   sink_ctx.big_handle, sink_ctx.num_bis);

            set_state(BAP_BROADCAST_SINK_STATE_STREAMING);
            sink_ctx.start_time_ms = get_time_ms();

            {
                bap_broadcast_sink_event_t evt = {
                    .type = BAP_BROADCAST_SINK_EVENT_STREAMING_STARTED
                };
                dispatch_event(&evt);
            }
            break;

        case HCI_ISOC_EVENT_BIG_SYNC_LOST:
            printf("BIG sync lost\n");
            sink_ctx.num_bis = 0;
            sink_ctx.big_handle = 0;

            if (sink_ctx.start_time_ms > 0) {
                sink_ctx.stats.uptime_ms += get_time_ms() - sink_ctx.start_time_ms;
            }
            sink_ctx.stats.sync_losses++;

            set_state(BAP_BROADCAST_SINK_STATE_PA_SYNCED);

            {
                bap_broadcast_sink_event_t evt = {
                    .type = BAP_BROADCAST_SINK_EVENT_STREAMING_STOPPED
                };
                dispatch_event(&evt);
            }
            break;

        case HCI_ISOC_EVENT_RX_DATA:
            /* Audio frame received */
            {
                const iso_data_packet_t *pkt = &event->data.rx_data;

                sink_ctx.stats.frames_received++;
                sink_ctx.stats.bytes_received += pkt->sdu_length;

                /* Find BIS index from handle */
                uint8_t bis_index = 0;
                for (int i = 0; i < sink_ctx.num_bis; i++) {
                    if (sink_ctx.bis_handles[i] == pkt->handle) {
                        bis_index = i + 1;  /* 1-based */
                        break;
                    }
                }

                bap_broadcast_sink_event_t evt = {
                    .type = BAP_BROADCAST_SINK_EVENT_AUDIO_FRAME,
                    .data.audio_frame = {
                        .bis_handle = pkt->handle,
                        .bis_index = bis_index,
                        .timestamp = pkt->timestamp,
                        .seq_num = pkt->packet_seq_num,
                        .data = pkt->data,
                        .length = pkt->sdu_length
                    }
                };
                dispatch_event(&evt);
            }
            break;

        case HCI_ISOC_EVENT_ERROR:
            printf("ISOC error: %d\n", event->data.error_code);
            {
                bap_broadcast_sink_event_t evt = {
                    .type = BAP_BROADCAST_SINK_EVENT_ERROR,
                    .data.error_code = event->data.error_code
                };
                dispatch_event(&evt);
            }
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * Public API - Initialization
 ******************************************************************************/

int bap_broadcast_sink_init(void)
{
    if (sink_ctx.initialized) {
        return BAP_BROADCAST_SINK_ERROR_ALREADY_INITIALIZED;
    }

    memset(&sink_ctx, 0, sizeof(sink_ctx));

    /* Register for HCI ISOC events */
    int result = hci_isoc_register_callback_ex(hci_isoc_event_handler, NULL);
    if (result != HCI_ISOC_OK) {
        printf("Broadcast Sink: Failed to register ISOC callback\n");
        return BAP_BROADCAST_SINK_ERROR_NO_RESOURCES;
    }

    /* Register periodic advertising event callback */
    wiced_ble_ext_adv_register_cback(padv_event_callback);

    sink_ctx.initialized = true;
    sink_ctx.state = BAP_BROADCAST_SINK_STATE_IDLE;

    printf("Broadcast Sink: Initialized\n");

    return BAP_BROADCAST_SINK_OK;
}

void bap_broadcast_sink_deinit(void)
{
    if (!sink_ctx.initialized) {
        return;
    }

    /* Stop if active */
    if (sink_ctx.state != BAP_BROADCAST_SINK_STATE_IDLE) {
        bap_broadcast_sink_stop();
    }

    /* Unregister callbacks */
    hci_isoc_unregister_callback(hci_isoc_event_handler);

    sink_ctx.initialized = false;
    sink_ctx.state = BAP_BROADCAST_SINK_STATE_IDLE;

    printf("Broadcast Sink: Deinitialized\n");
}

void bap_broadcast_sink_register_callback(bap_broadcast_sink_callback_t callback, void *user_data)
{
    sink_ctx.callback = callback;
    sink_ctx.callback_user_data = user_data;
}

/*******************************************************************************
 * Public API - Scanning
 ******************************************************************************/

int bap_broadcast_sink_start_scan(void)
{
    if (!sink_ctx.initialized) {
        return BAP_BROADCAST_SINK_ERROR_NOT_INITIALIZED;
    }

    if (sink_ctx.state != BAP_BROADCAST_SINK_STATE_IDLE) {
        return BAP_BROADCAST_SINK_ERROR_INVALID_STATE;
    }

    /* Register scan callback */
    wiced_bt_ble_observe(WICED_TRUE, 0, ext_scan_callback);

    sink_ctx.scanning = true;
    set_state(BAP_BROADCAST_SINK_STATE_SCANNING);

    printf("Broadcast Sink: Scanning started\n");

    return BAP_BROADCAST_SINK_OK;
}

int bap_broadcast_sink_stop_scan(void)
{
    if (!sink_ctx.initialized) {
        return BAP_BROADCAST_SINK_ERROR_NOT_INITIALIZED;
    }

    /* Stop scanning */
    wiced_bt_ble_observe(WICED_FALSE, 0, NULL);
    sink_ctx.scanning = false;

    if (sink_ctx.state == BAP_BROADCAST_SINK_STATE_SCANNING) {
        set_state(BAP_BROADCAST_SINK_STATE_IDLE);
    }

    printf("Broadcast Sink: Scanning stopped\n");

    return BAP_BROADCAST_SINK_OK;
}

/*******************************************************************************
 * Public API - Sync Control
 ******************************************************************************/

/**
 * @brief Set pending BIG sync with broadcast code
 *
 * When PA sync completes and BIGInfo is received, BIG sync will be
 * triggered automatically using the stored broadcast code.
 *
 * @param broadcast_code 16-byte encryption key (NULL if unencrypted)
 */
static void set_pending_big_sync(const uint8_t *broadcast_code)
{
    sink_ctx.pending_big_sync = true;
    if (broadcast_code != NULL) {
        memcpy(sink_ctx.pending_broadcast_code, broadcast_code, 16);
        sink_ctx.pending_has_code = true;
    } else {
        sink_ctx.pending_has_code = false;
    }
}

int bap_broadcast_sink_sync_to_pa(const bap_broadcast_source_t *source)
{
    if (!sink_ctx.initialized) {
        return BAP_BROADCAST_SINK_ERROR_NOT_INITIALIZED;
    }

    if (source == NULL) {
        return BAP_BROADCAST_SINK_ERROR_INVALID_PARAM;
    }

    if (sink_ctx.state != BAP_BROADCAST_SINK_STATE_SCANNING &&
        sink_ctx.state != BAP_BROADCAST_SINK_STATE_IDLE) {
        return BAP_BROADCAST_SINK_ERROR_INVALID_STATE;
    }

    /* Stop scanning if active */
    if (sink_ctx.scanning) {
        bap_broadcast_sink_stop_scan();
    }

    /* Save source info */
    sink_ctx.current_source = *source;
    sink_ctx.base_received = false;
    sink_ctx.biginfo_received = false;

    /* Create PA sync */
    wiced_ble_padv_create_sync_params_t sync_params;
    memset(&sync_params, 0, sizeof(sync_params));
    sync_params.options = 0;  /* Use parameters, don't filter */
    sync_params.adv_sid = source->adv_sid;
    sync_params.adv_addr_type = source->addr_type;
    memcpy(sync_params.adv_addr, source->addr, 6);
    sync_params.skip = 0;
    sync_params.sync_timeout = PA_SYNC_TIMEOUT_10MS;
    sync_params.sync_cte_type = 0;

    wiced_result_t result = wiced_ble_padv_create_sync(&sync_params);
    if (result != WICED_BT_SUCCESS) {
        printf("PA sync create failed: %d\n", result);
        return BAP_BROADCAST_SINK_ERROR_PA_SYNC_FAILED;
    }

    set_state(BAP_BROADCAST_SINK_STATE_PA_SYNCING);

    printf("Broadcast Sink: Creating PA sync to %02X%02X%02X\n",
           source->broadcast_id[0], source->broadcast_id[1], source->broadcast_id[2]);

    return BAP_BROADCAST_SINK_OK;
}

int bap_broadcast_sink_sync_to_pa_auto_big(const bap_broadcast_source_t *source,
                                            const uint8_t *broadcast_code)
{
    int result;

    /* Set up pending BIG sync before calling sync_to_pa */
    set_pending_big_sync(broadcast_code);

    result = bap_broadcast_sink_sync_to_pa(source);
    if (result != BAP_BROADCAST_SINK_OK) {
        /* Clear pending state on failure */
        sink_ctx.pending_big_sync = false;
        sink_ctx.pending_has_code = false;
    }

    return result;
}

int bap_broadcast_sink_sync_to_big(const uint8_t *broadcast_code,
                                    const uint8_t *bis_indices, uint8_t num_bis)
{
    if (!sink_ctx.initialized) {
        return BAP_BROADCAST_SINK_ERROR_NOT_INITIALIZED;
    }

    if (sink_ctx.state != BAP_BROADCAST_SINK_STATE_PA_SYNCED) {
        return BAP_BROADCAST_SINK_ERROR_INVALID_STATE;
    }

    if (!sink_ctx.base_received || !sink_ctx.biginfo_received) {
        return BAP_BROADCAST_SINK_ERROR_NO_BASE;
    }

    /* Check encryption */
    if (sink_ctx.biginfo.encrypted && broadcast_code == NULL) {
        printf("Broadcast is encrypted but no broadcast code provided\n");
        return BAP_BROADCAST_SINK_ERROR_DECRYPT_FAILED;
    }

    /* Store encryption info */
    sink_ctx.encrypted = sink_ctx.biginfo.encrypted;
    if (broadcast_code != NULL) {
        memcpy(sink_ctx.broadcast_code, broadcast_code, 16);
    }

    /* Configure BIG sync */
    big_sync_config_t big_sync_cfg;
    memset(&big_sync_cfg, 0, sizeof(big_sync_cfg));
    big_sync_cfg.big_handle = 0;  /* Let controller assign */
    big_sync_cfg.sync_handle = sink_ctx.pa_sync_handle;
    big_sync_cfg.encryption = sink_ctx.encrypted ? 1 : 0;
    if (sink_ctx.encrypted) {
        memcpy(big_sync_cfg.broadcast_code, sink_ctx.broadcast_code, 16);
    }
    big_sync_cfg.mse = 0;  /* Controller default */
    big_sync_cfg.big_sync_timeout = BIG_SYNC_TIMEOUT_10MS;

    /* Select BIS to sync */
    if (bis_indices != NULL && num_bis > 0) {
        big_sync_cfg.num_bis = num_bis;
        memcpy(big_sync_cfg.bis_indices, bis_indices, num_bis);
    } else {
        /* Sync to all BIS from first subgroup */
        big_sync_cfg.num_bis = sink_ctx.base_info.subgroups[0].num_bis;
        for (int i = 0; i < big_sync_cfg.num_bis; i++) {
            big_sync_cfg.bis_indices[i] = sink_ctx.base_info.subgroups[0].bis[i].bis_index;
        }
    }

    /* Create BIG sync */
    int result = hci_isoc_big_create_sync(&big_sync_cfg);
    if (result != HCI_ISOC_OK) {
        printf("BIG sync create failed: %d\n", result);
        return BAP_BROADCAST_SINK_ERROR_BIG_SYNC_FAILED;
    }

    set_state(BAP_BROADCAST_SINK_STATE_BIG_SYNCING);

    printf("Broadcast Sink: Creating BIG sync, %d BIS\n", big_sync_cfg.num_bis);

    return BAP_BROADCAST_SINK_OK;
}

int bap_broadcast_sink_stop(void)
{
    if (!sink_ctx.initialized) {
        return BAP_BROADCAST_SINK_ERROR_NOT_INITIALIZED;
    }

    /* Stop BIG sync if active */
    if (sink_ctx.state == BAP_BROADCAST_SINK_STATE_STREAMING ||
        sink_ctx.state == BAP_BROADCAST_SINK_STATE_BIG_SYNCING) {
        hci_isoc_big_terminate_sync(sink_ctx.big_handle);
        sink_ctx.big_handle = 0;
        sink_ctx.num_bis = 0;

        if (sink_ctx.start_time_ms > 0) {
            sink_ctx.stats.uptime_ms += get_time_ms() - sink_ctx.start_time_ms;
            sink_ctx.start_time_ms = 0;
        }
    }

    /* Terminate PA sync if active */
    if (sink_ctx.pa_sync_handle != 0) {
        wiced_ble_padv_terminate_sync(sink_ctx.pa_sync_handle);
        sink_ctx.pa_sync_handle = 0;
    }

    /* Stop scanning if active */
    if (sink_ctx.scanning) {
        bap_broadcast_sink_stop_scan();
    }

    sink_ctx.base_received = false;
    sink_ctx.biginfo_received = false;
    sink_ctx.demo_auto_sync = false;
    sink_ctx.pending_big_sync = false;
    sink_ctx.pending_has_code = false;

    set_state(BAP_BROADCAST_SINK_STATE_IDLE);

    printf("Broadcast Sink: Stopped\n");

    return BAP_BROADCAST_SINK_OK;
}

/*******************************************************************************
 * Public API - State and Info
 ******************************************************************************/

bap_broadcast_sink_state_t bap_broadcast_sink_get_state(void)
{
    return sink_ctx.state;
}

int bap_broadcast_sink_get_info(bap_broadcast_sink_info_t *info)
{
    if (info == NULL) {
        return BAP_BROADCAST_SINK_ERROR_INVALID_PARAM;
    }

    info->state = sink_ctx.state;
    info->source = sink_ctx.current_source;
    info->pa_sync_handle = sink_ctx.pa_sync_handle;
    info->big_handle = sink_ctx.big_handle;
    info->num_bis = sink_ctx.num_bis;
    memcpy(info->bis_handles, sink_ctx.bis_handles, sizeof(info->bis_handles));
    info->base_info = sink_ctx.base_info;
    info->biginfo = sink_ctx.biginfo;

    return BAP_BROADCAST_SINK_OK;
}

void bap_broadcast_sink_get_stats(bap_broadcast_sink_stats_t *stats)
{
    if (stats != NULL) {
        *stats = sink_ctx.stats;

        /* Add current streaming time */
        if (sink_ctx.state == BAP_BROADCAST_SINK_STATE_STREAMING &&
            sink_ctx.start_time_ms > 0) {
            stats->uptime_ms += get_time_ms() - sink_ctx.start_time_ms;
        }
    }
}

void bap_broadcast_sink_reset_stats(void)
{
    memset(&sink_ctx.stats, 0, sizeof(sink_ctx.stats));
    sink_ctx.start_time_ms = (sink_ctx.state == BAP_BROADCAST_SINK_STATE_STREAMING) ?
                             get_time_ms() : 0;
}

/*******************************************************************************
 * Public API - Demo/Test
 ******************************************************************************/

int bap_broadcast_sink_demo_auto_sync(const uint8_t *broadcast_code)
{
    if (!sink_ctx.initialized) {
        return BAP_BROADCAST_SINK_ERROR_NOT_INITIALIZED;
    }

    if (sink_ctx.state != BAP_BROADCAST_SINK_STATE_IDLE) {
        return BAP_BROADCAST_SINK_ERROR_INVALID_STATE;
    }

    /* Enable auto-sync mode */
    sink_ctx.demo_auto_sync = true;
    sink_ctx.demo_broadcast_code = broadcast_code;

    /* Start scanning */
    int result = bap_broadcast_sink_start_scan();
    if (result != BAP_BROADCAST_SINK_OK) {
        sink_ctx.demo_auto_sync = false;
        return result;
    }

    printf("Broadcast Sink: Demo auto-sync started\n");

    return BAP_BROADCAST_SINK_OK;
}
