/**
 * @file pacs.c
 * @brief Published Audio Capabilities Service (PACS) Implementation
 *
 * Implements PACS server and client roles per Bluetooth SIG specification.
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pacs.h"
#include <string.h>
#include <stdlib.h>

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** LC3 Codec ID */
#define LC3_CODEC_ID_FORMAT         0x06    /**< LC3 codec format */
#define LC3_CODEC_ID_COMPANY        0x0000  /**< Bluetooth SIG */
#define LC3_CODEC_ID_VENDOR         0x0000  /**< No vendor specific */

/** Maximum connected devices for client role */
#define PACS_MAX_CONNECTIONS        4

/** LC3 frequency codes (for Codec_Specific_Configuration) */
#define LC3_FREQ_CODE_8000          0x01
#define LC3_FREQ_CODE_11025         0x02
#define LC3_FREQ_CODE_16000         0x03
#define LC3_FREQ_CODE_22050         0x04
#define LC3_FREQ_CODE_24000         0x05
#define LC3_FREQ_CODE_32000         0x06
#define LC3_FREQ_CODE_44100         0x07
#define LC3_FREQ_CODE_48000         0x08
#define LC3_FREQ_CODE_88200         0x09
#define LC3_FREQ_CODE_96000         0x0A
#define LC3_FREQ_CODE_176400        0x0B
#define LC3_FREQ_CODE_192000        0x0C
#define LC3_FREQ_CODE_384000        0x0D

/** GATT characteristic handles (placeholder - populated by BT configurator) */
typedef struct {
    uint16_t sink_pac_handle;
    uint16_t sink_pac_ccc_handle;
    uint16_t sink_locations_handle;
    uint16_t sink_locations_ccc_handle;
    uint16_t source_pac_handle;
    uint16_t source_pac_ccc_handle;
    uint16_t source_locations_handle;
    uint16_t source_locations_ccc_handle;
    uint16_t available_contexts_handle;
    uint16_t available_contexts_ccc_handle;
    uint16_t supported_contexts_handle;
} pacs_gatt_handles_t;

/** Remote device discovery state */
typedef enum {
    PACS_DISC_STATE_IDLE,
    PACS_DISC_STATE_DISCOVERING_SERVICE,
    PACS_DISC_STATE_DISCOVERING_CHARS,
    PACS_DISC_STATE_READING_SINK_PAC,
    PACS_DISC_STATE_READING_SOURCE_PAC,
    PACS_DISC_STATE_READING_LOCATIONS,
    PACS_DISC_STATE_READING_CONTEXTS,
    PACS_DISC_STATE_COMPLETE
} pacs_discovery_state_t;

/** Remote connection context */
typedef struct {
    bool in_use;
    uint16_t conn_handle;
    pacs_discovery_state_t disc_state;
    pacs_gatt_handles_t remote_handles;
    pacs_remote_info_t info;
} pacs_connection_t;

/** PACS module context */
typedef struct {
    bool initialized;

    /* Server role */
    pacs_config_t config;
    pacs_gatt_handles_t local_handles;
    uint8_t sink_pac_data[256];
    uint16_t sink_pac_data_len;
    uint8_t source_pac_data[256];
    uint16_t source_pac_data_len;

    /* Client role */
    pacs_connection_t connections[PACS_MAX_CONNECTIONS];

    /* Callback */
    pacs_callback_t callback;
    void *callback_user_data;
} pacs_context_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static pacs_context_t pacs_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static pacs_connection_t* find_connection(uint16_t conn_handle);
static pacs_connection_t* alloc_connection(uint16_t conn_handle);
static void free_connection(uint16_t conn_handle);
static int build_pac_characteristic_data(const pacs_pac_record_t *records,
                                          uint8_t num_records,
                                          uint8_t *data, uint16_t *len);
static int parse_lc3_capabilities(const uint8_t *data, uint8_t len,
                                   pacs_lc3_capabilities_t *cap);
static void notify_event(pacs_event_type_t type, uint16_t conn_handle,
                          void *data);

/* GATT server callbacks (placeholders for BTSTACK integration) */
static void on_sink_pac_read(uint16_t conn_handle, uint8_t *data, uint16_t *len);
static void on_source_pac_read(uint16_t conn_handle, uint8_t *data, uint16_t *len);
static void on_sink_locations_read(uint16_t conn_handle, uint8_t *data, uint16_t *len);
static void on_source_locations_read(uint16_t conn_handle, uint8_t *data, uint16_t *len);
static void on_available_contexts_read(uint16_t conn_handle, uint8_t *data, uint16_t *len);
static void on_supported_contexts_read(uint16_t conn_handle, uint8_t *data, uint16_t *len);

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

int pacs_init(void)
{
    if (pacs_ctx.initialized) {
        return PACS_ERROR_ALREADY_INITIALIZED;
    }

    memset(&pacs_ctx, 0, sizeof(pacs_ctx));

    /*
     * TODO: Register PACS service with BTSTACK GATT server
     *
     * The GATT database should include:
     * - Sink PAC characteristic (Read, Notify)
     * - Sink Audio Locations characteristic (Read, Notify)
     * - Source PAC characteristic (Read, Notify)
     * - Source Audio Locations characteristic (Read, Notify)
     * - Available Audio Contexts characteristic (Read, Notify)
     * - Supported Audio Contexts characteristic (Read)
     *
     * Example using BT Configurator generated handles:
     * pacs_ctx.local_handles.sink_pac_handle = GATT_DB_SINK_PAC_VALUE;
     * pacs_ctx.local_handles.sink_pac_ccc_handle = GATT_DB_SINK_PAC_CCC;
     * ...
     */

    pacs_ctx.initialized = true;

    return PACS_OK;
}

void pacs_deinit(void)
{
    if (!pacs_ctx.initialized) {
        return;
    }

    /* Clear all connections */
    for (int i = 0; i < PACS_MAX_CONNECTIONS; i++) {
        pacs_ctx.connections[i].in_use = false;
    }

    pacs_ctx.initialized = false;
}

void pacs_register_callback(pacs_callback_t callback, void *user_data)
{
    pacs_ctx.callback = callback;
    pacs_ctx.callback_user_data = user_data;
}

/*******************************************************************************
 * API Functions - Server Role (Local Capabilities)
 ******************************************************************************/

int pacs_configure(const pacs_config_t *config)
{
    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    /* Copy configuration */
    memcpy(&pacs_ctx.config, config, sizeof(pacs_config_t));

    /* Build Sink PAC characteristic data */
    build_pac_characteristic_data(config->sink_pac, config->num_sink_pac,
                                   pacs_ctx.sink_pac_data,
                                   &pacs_ctx.sink_pac_data_len);

    /* Build Source PAC characteristic data */
    build_pac_characteristic_data(config->source_pac, config->num_source_pac,
                                   pacs_ctx.source_pac_data,
                                   &pacs_ctx.source_pac_data_len);

    return PACS_OK;
}

int pacs_add_sink_pac(const pacs_pac_record_t *record)
{
    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    if (record == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    if (pacs_ctx.config.num_sink_pac >= PACS_MAX_PAC_RECORDS) {
        return PACS_ERROR_NO_RESOURCES;
    }

    /* Add record */
    memcpy(&pacs_ctx.config.sink_pac[pacs_ctx.config.num_sink_pac],
           record, sizeof(pacs_pac_record_t));
    pacs_ctx.config.num_sink_pac++;

    /* Rebuild characteristic data */
    build_pac_characteristic_data(pacs_ctx.config.sink_pac,
                                   pacs_ctx.config.num_sink_pac,
                                   pacs_ctx.sink_pac_data,
                                   &pacs_ctx.sink_pac_data_len);

    return PACS_OK;
}

int pacs_add_source_pac(const pacs_pac_record_t *record)
{
    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    if (record == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    if (pacs_ctx.config.num_source_pac >= PACS_MAX_PAC_RECORDS) {
        return PACS_ERROR_NO_RESOURCES;
    }

    /* Add record */
    memcpy(&pacs_ctx.config.source_pac[pacs_ctx.config.num_source_pac],
           record, sizeof(pacs_pac_record_t));
    pacs_ctx.config.num_source_pac++;

    /* Rebuild characteristic data */
    build_pac_characteristic_data(pacs_ctx.config.source_pac,
                                   pacs_ctx.config.num_source_pac,
                                   pacs_ctx.source_pac_data,
                                   &pacs_ctx.source_pac_data_len);

    return PACS_OK;
}

int pacs_add_sink_lc3(const pacs_lc3_capabilities_t *capabilities)
{
    pacs_pac_record_t record;
    int result;

    if (capabilities == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    result = pacs_build_lc3_pac_record(capabilities, &record);
    if (result != PACS_OK) {
        return result;
    }

    return pacs_add_sink_pac(&record);
}

int pacs_add_source_lc3(const pacs_lc3_capabilities_t *capabilities)
{
    pacs_pac_record_t record;
    int result;

    if (capabilities == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    result = pacs_build_lc3_pac_record(capabilities, &record);
    if (result != PACS_OK) {
        return result;
    }

    return pacs_add_source_pac(&record);
}

int pacs_set_sink_locations(uint32_t locations)
{
    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    pacs_ctx.config.sink_audio_locations = locations;

    /*
     * TODO: Notify connected clients if CCC is enabled
     * btstack_gatt_server_send_notification(
     *     conn_handle,
     *     pacs_ctx.local_handles.sink_locations_handle,
     *     (uint8_t*)&locations, 4
     * );
     */

    return PACS_OK;
}

int pacs_set_source_locations(uint32_t locations)
{
    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    pacs_ctx.config.source_audio_locations = locations;

    /*
     * TODO: Notify connected clients if CCC is enabled
     */

    return PACS_OK;
}

int pacs_set_available_contexts(uint16_t sink_contexts, uint16_t source_contexts)
{
    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    pacs_ctx.config.available_sink_contexts = sink_contexts;
    pacs_ctx.config.available_source_contexts = source_contexts;

    return PACS_OK;
}

int pacs_set_supported_contexts(uint16_t sink_contexts, uint16_t source_contexts)
{
    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    pacs_ctx.config.supported_sink_contexts = sink_contexts;
    pacs_ctx.config.supported_source_contexts = source_contexts;

    return PACS_OK;
}

int pacs_notify_contexts_changed(void)
{
    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    /*
     * TODO: Send notification to all connected clients with CCC enabled
     *
     * uint8_t contexts_data[4];
     * contexts_data[0] = pacs_ctx.config.available_sink_contexts & 0xFF;
     * contexts_data[1] = (pacs_ctx.config.available_sink_contexts >> 8) & 0xFF;
     * contexts_data[2] = pacs_ctx.config.available_source_contexts & 0xFF;
     * contexts_data[3] = (pacs_ctx.config.available_source_contexts >> 8) & 0xFF;
     *
     * for each connected client with CCC enabled:
     *     btstack_gatt_server_send_notification(
     *         conn_handle,
     *         pacs_ctx.local_handles.available_contexts_handle,
     *         contexts_data, 4
     *     );
     */

    return PACS_OK;
}

/*******************************************************************************
 * API Functions - Client Role (Remote Discovery)
 ******************************************************************************/

int pacs_discover(uint16_t conn_handle)
{
    pacs_connection_t *conn;

    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    /* Find or allocate connection context */
    conn = find_connection(conn_handle);
    if (conn == NULL) {
        conn = alloc_connection(conn_handle);
        if (conn == NULL) {
            return PACS_ERROR_NO_RESOURCES;
        }
    }

    /* Reset discovery state */
    conn->disc_state = PACS_DISC_STATE_DISCOVERING_SERVICE;
    memset(&conn->info, 0, sizeof(pacs_remote_info_t));
    conn->info.conn_handle = conn_handle;

    /*
     * TODO: Start GATT service discovery for PACS (UUID 0x1850)
     *
     * btstack_gatt_client_discover_primary_services_by_uuid16(
     *     conn_handle,
     *     UUID_PACS_SERVICE,
     *     pacs_service_discovered_callback
     * );
     */

    return PACS_OK;
}

int pacs_read_sink_pac(uint16_t conn_handle)
{
    pacs_connection_t *conn;

    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    conn = find_connection(conn_handle);
    if (conn == NULL) {
        return PACS_ERROR_NOT_FOUND;
    }

    if (conn->remote_handles.sink_pac_handle == 0) {
        return PACS_ERROR_NOT_FOUND;
    }

    /*
     * TODO: Read Sink PAC characteristic
     *
     * btstack_gatt_client_read_value_of_characteristic_using_value_handle(
     *     conn_handle,
     *     conn->remote_handles.sink_pac_handle,
     *     pacs_sink_pac_read_callback
     * );
     */

    return PACS_OK;
}

int pacs_read_source_pac(uint16_t conn_handle)
{
    pacs_connection_t *conn;

    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    conn = find_connection(conn_handle);
    if (conn == NULL) {
        return PACS_ERROR_NOT_FOUND;
    }

    if (conn->remote_handles.source_pac_handle == 0) {
        return PACS_ERROR_NOT_FOUND;
    }

    /*
     * TODO: Read Source PAC characteristic
     */

    return PACS_OK;
}

int pacs_read_audio_locations(uint16_t conn_handle)
{
    pacs_connection_t *conn;

    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    conn = find_connection(conn_handle);
    if (conn == NULL) {
        return PACS_ERROR_NOT_FOUND;
    }

    /*
     * TODO: Read Sink and Source Audio Locations characteristics
     */

    return PACS_OK;
}

int pacs_read_audio_contexts(uint16_t conn_handle)
{
    pacs_connection_t *conn;

    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    conn = find_connection(conn_handle);
    if (conn == NULL) {
        return PACS_ERROR_NOT_FOUND;
    }

    /*
     * TODO: Read Available and Supported Audio Contexts characteristics
     */

    return PACS_OK;
}

int pacs_get_remote_info(uint16_t conn_handle, pacs_remote_info_t *info)
{
    pacs_connection_t *conn;

    if (!pacs_ctx.initialized) {
        return PACS_ERROR_NOT_INITIALIZED;
    }

    if (info == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    conn = find_connection(conn_handle);
    if (conn == NULL) {
        return PACS_ERROR_NOT_FOUND;
    }

    memcpy(info, &conn->info, sizeof(pacs_remote_info_t));

    return PACS_OK;
}

/*******************************************************************************
 * API Functions - Utilities
 ******************************************************************************/

int pacs_build_lc3_pac_record(const pacs_lc3_capabilities_t *capabilities,
                               pacs_pac_record_t *record)
{
    uint8_t *p;

    if (capabilities == NULL || record == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    memset(record, 0, sizeof(pacs_pac_record_t));

    /* Set LC3 Codec ID */
    record->codec_id[0] = LC3_CODEC_ID_FORMAT;
    record->codec_id[1] = LC3_CODEC_ID_COMPANY & 0xFF;
    record->codec_id[2] = (LC3_CODEC_ID_COMPANY >> 8) & 0xFF;
    record->codec_id[3] = LC3_CODEC_ID_VENDOR & 0xFF;
    record->codec_id[4] = (LC3_CODEC_ID_VENDOR >> 8) & 0xFF;

    /* Build codec specific capabilities in LTV format */
    p = record->codec_specific_cap;

    /* Supported Sampling Frequencies (LTV: len=3, type=0x01, value=2 bytes) */
    *p++ = 3;  /* Length (type + value) */
    *p++ = PACS_LTV_SUPPORTED_FREQ;
    *p++ = capabilities->supported_frequencies & 0xFF;
    *p++ = (capabilities->supported_frequencies >> 8) & 0xFF;

    /* Supported Frame Durations (LTV: len=2, type=0x02, value=1 byte) */
    *p++ = 2;
    *p++ = PACS_LTV_SUPPORTED_DURATION;
    *p++ = capabilities->supported_durations;

    /* Supported Audio Channel Counts (LTV: len=2, type=0x03, value=1 byte) */
    *p++ = 2;
    *p++ = PACS_LTV_SUPPORTED_CHANNELS;
    *p++ = capabilities->supported_channels;

    /* Supported Octets per Codec Frame (LTV: len=5, type=0x04, value=4 bytes) */
    *p++ = 5;
    *p++ = PACS_LTV_SUPPORTED_OCTETS_MIN;
    *p++ = capabilities->min_octets_per_frame & 0xFF;
    *p++ = (capabilities->min_octets_per_frame >> 8) & 0xFF;
    *p++ = capabilities->max_octets_per_frame & 0xFF;
    *p++ = (capabilities->max_octets_per_frame >> 8) & 0xFF;

    /* Max Supported Codec Frames per SDU (LTV: len=2, type=0x05, value=1 byte) */
    *p++ = 2;
    *p++ = PACS_LTV_SUPPORTED_FRAMES_MAX;
    *p++ = capabilities->max_frames_per_sdu;

    record->codec_specific_cap_len = (uint8_t)(p - record->codec_specific_cap);

    /* Copy parsed capabilities */
    record->is_lc3 = true;
    memcpy(&record->lc3_cap, capabilities, sizeof(pacs_lc3_capabilities_t));

    /* No metadata for now */
    record->metadata_len = 0;

    return PACS_OK;
}

int pacs_parse_pac_data(const uint8_t *data, uint16_t len,
                         pacs_pac_record_t *records, uint8_t max_records,
                         uint8_t *num_records)
{
    const uint8_t *p = data;
    const uint8_t *end = data + len;
    uint8_t count = 0;

    if (data == NULL || records == NULL || num_records == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    /* First byte is number of PAC records */
    if (p >= end) {
        *num_records = 0;
        return PACS_OK;
    }

    uint8_t record_count = *p++;

    /* Parse each PAC record */
    while (p < end && count < record_count && count < max_records) {
        pacs_pac_record_t *rec = &records[count];
        memset(rec, 0, sizeof(pacs_pac_record_t));

        /* Codec ID (5 bytes) */
        if (p + 5 > end) break;
        memcpy(rec->codec_id, p, 5);
        p += 5;

        /* Check if LC3 codec */
        rec->is_lc3 = (rec->codec_id[0] == LC3_CODEC_ID_FORMAT);

        /* Codec Specific Capabilities Length */
        if (p >= end) break;
        rec->codec_specific_cap_len = *p++;

        /* Codec Specific Capabilities */
        if (p + rec->codec_specific_cap_len > end) break;
        if (rec->codec_specific_cap_len > PACS_MAX_CODEC_CAP_LEN) {
            rec->codec_specific_cap_len = PACS_MAX_CODEC_CAP_LEN;
        }
        memcpy(rec->codec_specific_cap, p, rec->codec_specific_cap_len);
        p += rec->codec_specific_cap_len;

        /* Parse LC3 capabilities if applicable */
        if (rec->is_lc3) {
            parse_lc3_capabilities(rec->codec_specific_cap,
                                   rec->codec_specific_cap_len,
                                   &rec->lc3_cap);
        }

        /* Metadata Length */
        if (p >= end) break;
        rec->metadata_len = *p++;

        /* Metadata */
        if (p + rec->metadata_len > end) break;
        if (rec->metadata_len > PACS_MAX_METADATA_LEN) {
            rec->metadata_len = PACS_MAX_METADATA_LEN;
        }
        memcpy(rec->metadata, p, rec->metadata_len);
        p += rec->metadata_len;

        count++;
    }

    *num_records = count;
    return PACS_OK;
}

bool pacs_is_config_supported(const pacs_pac_record_t *pac,
                               uint8_t freq, uint8_t duration,
                               uint8_t channels, uint16_t octets)
{
    if (pac == NULL || !pac->is_lc3) {
        return false;
    }

    const pacs_lc3_capabilities_t *cap = &pac->lc3_cap;

    /* Convert frequency code to bitmask and check */
    uint16_t freq_bitmask = pacs_freq_code_to_bitmask(freq);
    if (!(cap->supported_frequencies & freq_bitmask)) {
        return false;
    }

    /* Check frame duration */
    uint8_t dur_bit = (duration == 0x00) ? PACS_DURATION_7_5MS : PACS_DURATION_10MS;
    if (!(cap->supported_durations & dur_bit)) {
        return false;
    }

    /* Check channel count */
    if (channels > 0 && channels <= 8) {
        uint8_t chan_bit = 1 << (channels - 1);
        if (!(cap->supported_channels & chan_bit)) {
            return false;
        }
    }

    /* Check octets per frame */
    if (octets < cap->min_octets_per_frame || octets > cap->max_octets_per_frame) {
        return false;
    }

    return true;
}

int pacs_get_preferred_config(const pacs_pac_record_t *pac,
                               uint8_t *freq, uint8_t *duration,
                               uint8_t *channels, uint16_t *octets)
{
    if (pac == NULL || !pac->is_lc3) {
        return PACS_ERROR_INVALID_PARAM;
    }

    const pacs_lc3_capabilities_t *cap = &pac->lc3_cap;

    /* Select highest supported frequency (prefer 48kHz) */
    if (cap->supported_frequencies & PACS_FREQ_48000) {
        *freq = LC3_FREQ_CODE_48000;
    } else if (cap->supported_frequencies & PACS_FREQ_24000) {
        *freq = LC3_FREQ_CODE_24000;
    } else if (cap->supported_frequencies & PACS_FREQ_16000) {
        *freq = LC3_FREQ_CODE_16000;
    } else if (cap->supported_frequencies & PACS_FREQ_32000) {
        *freq = LC3_FREQ_CODE_32000;
    } else {
        /* Use first available */
        *freq = pacs_freq_bitmask_to_code(cap->supported_frequencies & 0xFF);
    }

    /* Select preferred frame duration (prefer 10ms) */
    if (cap->supported_durations & PACS_DURATION_10MS_PREF) {
        *duration = 0x01;  /* 10ms */
    } else if (cap->supported_durations & PACS_DURATION_7_5MS_PREF) {
        *duration = 0x00;  /* 7.5ms */
    } else if (cap->supported_durations & PACS_DURATION_10MS) {
        *duration = 0x01;
    } else {
        *duration = 0x00;
    }

    /* Select channel count (prefer stereo if supported) */
    if (cap->supported_channels & PACS_CHANNELS_2) {
        *channels = 2;
    } else if (cap->supported_channels & PACS_CHANNELS_1) {
        *channels = 1;
    } else {
        *channels = 1;  /* Default to mono */
    }

    /* Select octets per frame (use mid-range for good quality) */
    *octets = (cap->min_octets_per_frame + cap->max_octets_per_frame) / 2;

    /* Adjust for common quality targets */
    uint32_t hz = pacs_freq_bitmask_to_hz(pacs_freq_code_to_bitmask(*freq));
    if (hz >= 48000) {
        /* High quality: ~100 octets for 48kHz */
        if (*octets < 80) *octets = 80;
        if (*octets > 120) *octets = 120;
    } else if (hz >= 24000) {
        /* Medium quality: ~60 octets for 24kHz */
        if (*octets < 40) *octets = 40;
        if (*octets > 80) *octets = 80;
    } else {
        /* Voice quality: ~40 octets for 16kHz */
        if (*octets < 30) *octets = 30;
        if (*octets > 60) *octets = 60;
    }

    /* Clamp to actual range */
    if (*octets < cap->min_octets_per_frame) *octets = cap->min_octets_per_frame;
    if (*octets > cap->max_octets_per_frame) *octets = cap->max_octets_per_frame;

    return PACS_OK;
}

uint8_t pacs_freq_bitmask_to_code(uint16_t freq_bitmask)
{
    switch (freq_bitmask) {
        case PACS_FREQ_8000:   return LC3_FREQ_CODE_8000;
        case PACS_FREQ_11025:  return LC3_FREQ_CODE_11025;
        case PACS_FREQ_16000:  return LC3_FREQ_CODE_16000;
        case PACS_FREQ_22050:  return LC3_FREQ_CODE_22050;
        case PACS_FREQ_24000:  return LC3_FREQ_CODE_24000;
        case PACS_FREQ_32000:  return LC3_FREQ_CODE_32000;
        case PACS_FREQ_44100:  return LC3_FREQ_CODE_44100;
        case PACS_FREQ_48000:  return LC3_FREQ_CODE_48000;
        case PACS_FREQ_88200:  return LC3_FREQ_CODE_88200;
        case PACS_FREQ_96000:  return LC3_FREQ_CODE_96000;
        case PACS_FREQ_176400: return LC3_FREQ_CODE_176400;
        case PACS_FREQ_192000: return LC3_FREQ_CODE_192000;
        case PACS_FREQ_384000: return LC3_FREQ_CODE_384000;
        default:               return 0;
    }
}

uint16_t pacs_freq_code_to_bitmask(uint8_t freq_code)
{
    switch (freq_code) {
        case LC3_FREQ_CODE_8000:   return PACS_FREQ_8000;
        case LC3_FREQ_CODE_11025:  return PACS_FREQ_11025;
        case LC3_FREQ_CODE_16000:  return PACS_FREQ_16000;
        case LC3_FREQ_CODE_22050:  return PACS_FREQ_22050;
        case LC3_FREQ_CODE_24000:  return PACS_FREQ_24000;
        case LC3_FREQ_CODE_32000:  return PACS_FREQ_32000;
        case LC3_FREQ_CODE_44100:  return PACS_FREQ_44100;
        case LC3_FREQ_CODE_48000:  return PACS_FREQ_48000;
        case LC3_FREQ_CODE_88200:  return PACS_FREQ_88200;
        case LC3_FREQ_CODE_96000:  return PACS_FREQ_96000;
        case LC3_FREQ_CODE_176400: return PACS_FREQ_176400;
        case LC3_FREQ_CODE_192000: return PACS_FREQ_192000;
        case LC3_FREQ_CODE_384000: return PACS_FREQ_384000;
        default:                   return 0;
    }
}

uint32_t pacs_freq_bitmask_to_hz(uint16_t freq_bitmask)
{
    switch (freq_bitmask) {
        case PACS_FREQ_8000:   return 8000;
        case PACS_FREQ_11025:  return 11025;
        case PACS_FREQ_16000:  return 16000;
        case PACS_FREQ_22050:  return 22050;
        case PACS_FREQ_24000:  return 24000;
        case PACS_FREQ_32000:  return 32000;
        case PACS_FREQ_44100:  return 44100;
        case PACS_FREQ_48000:  return 48000;
        case PACS_FREQ_88200:  return 88200;
        case PACS_FREQ_96000:  return 96000;
        case PACS_FREQ_176400: return 176400;
        case PACS_FREQ_192000: return 192000;
        case PACS_FREQ_384000: return 384000;
        default:               return 0;
    }
}

/*******************************************************************************
 * Private Functions - Connection Management
 ******************************************************************************/

static pacs_connection_t* find_connection(uint16_t conn_handle)
{
    for (int i = 0; i < PACS_MAX_CONNECTIONS; i++) {
        if (pacs_ctx.connections[i].in_use &&
            pacs_ctx.connections[i].conn_handle == conn_handle) {
            return &pacs_ctx.connections[i];
        }
    }
    return NULL;
}

static pacs_connection_t* alloc_connection(uint16_t conn_handle)
{
    for (int i = 0; i < PACS_MAX_CONNECTIONS; i++) {
        if (!pacs_ctx.connections[i].in_use) {
            memset(&pacs_ctx.connections[i], 0, sizeof(pacs_connection_t));
            pacs_ctx.connections[i].in_use = true;
            pacs_ctx.connections[i].conn_handle = conn_handle;
            return &pacs_ctx.connections[i];
        }
    }
    return NULL;
}

static void free_connection(uint16_t conn_handle)
{
    for (int i = 0; i < PACS_MAX_CONNECTIONS; i++) {
        if (pacs_ctx.connections[i].in_use &&
            pacs_ctx.connections[i].conn_handle == conn_handle) {
            pacs_ctx.connections[i].in_use = false;
            return;
        }
    }
}

/*******************************************************************************
 * Private Functions - PAC Data Building
 ******************************************************************************/

static int build_pac_characteristic_data(const pacs_pac_record_t *records,
                                          uint8_t num_records,
                                          uint8_t *data, uint16_t *len)
{
    uint8_t *p = data;

    if (records == NULL || data == NULL || len == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    /* Number of PAC records */
    *p++ = num_records;

    /* Encode each PAC record */
    for (int i = 0; i < num_records; i++) {
        const pacs_pac_record_t *rec = &records[i];

        /* Codec ID (5 bytes) */
        memcpy(p, rec->codec_id, 5);
        p += 5;

        /* Codec Specific Capabilities Length */
        *p++ = rec->codec_specific_cap_len;

        /* Codec Specific Capabilities */
        memcpy(p, rec->codec_specific_cap, rec->codec_specific_cap_len);
        p += rec->codec_specific_cap_len;

        /* Metadata Length */
        *p++ = rec->metadata_len;

        /* Metadata */
        if (rec->metadata_len > 0) {
            memcpy(p, rec->metadata, rec->metadata_len);
            p += rec->metadata_len;
        }
    }

    *len = (uint16_t)(p - data);
    return PACS_OK;
}

/*******************************************************************************
 * Private Functions - LC3 Capabilities Parsing
 ******************************************************************************/

static int parse_lc3_capabilities(const uint8_t *data, uint8_t len,
                                   pacs_lc3_capabilities_t *cap)
{
    const uint8_t *p = data;
    const uint8_t *end = data + len;

    if (data == NULL || cap == NULL) {
        return PACS_ERROR_INVALID_PARAM;
    }

    /* Initialize with defaults */
    memset(cap, 0, sizeof(pacs_lc3_capabilities_t));
    cap->max_frames_per_sdu = 1;  /* Default */

    /* Parse LTV structures */
    while (p < end) {
        uint8_t ltv_len = *p++;
        if (p + ltv_len > end || ltv_len == 0) break;

        uint8_t ltv_type = *p++;
        uint8_t value_len = ltv_len - 1;

        switch (ltv_type) {
            case PACS_LTV_SUPPORTED_FREQ:
                if (value_len >= 2) {
                    cap->supported_frequencies = p[0] | (p[1] << 8);
                }
                break;

            case PACS_LTV_SUPPORTED_DURATION:
                if (value_len >= 1) {
                    cap->supported_durations = p[0];
                }
                break;

            case PACS_LTV_SUPPORTED_CHANNELS:
                if (value_len >= 1) {
                    cap->supported_channels = p[0];
                }
                break;

            case PACS_LTV_SUPPORTED_OCTETS_MIN:
                if (value_len >= 4) {
                    cap->min_octets_per_frame = p[0] | (p[1] << 8);
                    cap->max_octets_per_frame = p[2] | (p[3] << 8);
                }
                break;

            case PACS_LTV_SUPPORTED_FRAMES_MAX:
                if (value_len >= 1) {
                    cap->max_frames_per_sdu = p[0];
                }
                break;

            default:
                /* Unknown LTV type - skip */
                break;
        }

        p += value_len;
    }

    return PACS_OK;
}

/*******************************************************************************
 * Private Functions - Event Notification
 ******************************************************************************/

static void notify_event(pacs_event_type_t type, uint16_t conn_handle,
                          void *data)
{
    if (pacs_ctx.callback == NULL) {
        return;
    }

    pacs_event_t event;
    event.type = type;
    event.conn_handle = conn_handle;

    switch (type) {
        case PACS_EVENT_DISCOVERY_COMPLETE:
        case PACS_EVENT_CONTEXTS_CHANGED:
        case PACS_EVENT_LOCATIONS_CHANGED:
            if (data != NULL) {
                memcpy(&event.data.remote_info, data, sizeof(pacs_remote_info_t));
            }
            break;

        case PACS_EVENT_ERROR:
            if (data != NULL) {
                event.data.error_code = *(int*)data;
            }
            break;
    }

    pacs_ctx.callback(&event, pacs_ctx.callback_user_data);
}

/*******************************************************************************
 * Private Functions - GATT Server Callbacks (Placeholders)
 ******************************************************************************/

static void on_sink_pac_read(uint16_t conn_handle, uint8_t *data, uint16_t *len)
{
    (void)conn_handle;

    memcpy(data, pacs_ctx.sink_pac_data, pacs_ctx.sink_pac_data_len);
    *len = pacs_ctx.sink_pac_data_len;
}

static void on_source_pac_read(uint16_t conn_handle, uint8_t *data, uint16_t *len)
{
    (void)conn_handle;

    memcpy(data, pacs_ctx.source_pac_data, pacs_ctx.source_pac_data_len);
    *len = pacs_ctx.source_pac_data_len;
}

static void on_sink_locations_read(uint16_t conn_handle, uint8_t *data, uint16_t *len)
{
    (void)conn_handle;

    data[0] = pacs_ctx.config.sink_audio_locations & 0xFF;
    data[1] = (pacs_ctx.config.sink_audio_locations >> 8) & 0xFF;
    data[2] = (pacs_ctx.config.sink_audio_locations >> 16) & 0xFF;
    data[3] = (pacs_ctx.config.sink_audio_locations >> 24) & 0xFF;
    *len = 4;
}

static void on_source_locations_read(uint16_t conn_handle, uint8_t *data, uint16_t *len)
{
    (void)conn_handle;

    data[0] = pacs_ctx.config.source_audio_locations & 0xFF;
    data[1] = (pacs_ctx.config.source_audio_locations >> 8) & 0xFF;
    data[2] = (pacs_ctx.config.source_audio_locations >> 16) & 0xFF;
    data[3] = (pacs_ctx.config.source_audio_locations >> 24) & 0xFF;
    *len = 4;
}

static void on_available_contexts_read(uint16_t conn_handle, uint8_t *data, uint16_t *len)
{
    (void)conn_handle;

    /* Available Sink Contexts (2 bytes) */
    data[0] = pacs_ctx.config.available_sink_contexts & 0xFF;
    data[1] = (pacs_ctx.config.available_sink_contexts >> 8) & 0xFF;
    /* Available Source Contexts (2 bytes) */
    data[2] = pacs_ctx.config.available_source_contexts & 0xFF;
    data[3] = (pacs_ctx.config.available_source_contexts >> 8) & 0xFF;
    *len = 4;
}

static void on_supported_contexts_read(uint16_t conn_handle, uint8_t *data, uint16_t *len)
{
    (void)conn_handle;

    /* Supported Sink Contexts (2 bytes) */
    data[0] = pacs_ctx.config.supported_sink_contexts & 0xFF;
    data[1] = (pacs_ctx.config.supported_sink_contexts >> 8) & 0xFF;
    /* Supported Source Contexts (2 bytes) */
    data[2] = pacs_ctx.config.supported_source_contexts & 0xFF;
    data[3] = (pacs_ctx.config.supported_source_contexts >> 8) & 0xFF;
    *len = 4;
}

/*******************************************************************************
 * GATT Client Callback Handlers (for remote PACS discovery)
 *
 * TODO: Implement these callbacks and register with BTSTACK GATT client
 ******************************************************************************/

/**
 * @brief Handle PACS service discovery complete
 *
 * Called when GATT service discovery finds PACS service.
 * Should then start characteristic discovery.
 */
void pacs_on_service_discovered(uint16_t conn_handle, uint16_t start_handle,
                                 uint16_t end_handle)
{
    pacs_connection_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return;
    }

    (void)start_handle;
    (void)end_handle;

    /*
     * TODO: Start characteristic discovery within service handle range
     *
     * btstack_gatt_client_discover_characteristics_for_service(
     *     conn_handle,
     *     start_handle,
     *     end_handle,
     *     pacs_characteristic_discovered_callback
     * );
     */

    conn->disc_state = PACS_DISC_STATE_DISCOVERING_CHARS;
}

/**
 * @brief Handle PACS characteristic discovery
 *
 * Called for each characteristic found within PACS service.
 */
void pacs_on_characteristic_discovered(uint16_t conn_handle, uint16_t uuid16,
                                        uint16_t value_handle)
{
    pacs_connection_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return;
    }

    /* Store characteristic handles */
    switch (uuid16) {
        case UUID_PACS_SINK_PAC:
            conn->remote_handles.sink_pac_handle = value_handle;
            break;
        case UUID_PACS_SINK_AUDIO_LOCATIONS:
            conn->remote_handles.sink_locations_handle = value_handle;
            break;
        case UUID_PACS_SOURCE_PAC:
            conn->remote_handles.source_pac_handle = value_handle;
            break;
        case UUID_PACS_SOURCE_AUDIO_LOCATIONS:
            conn->remote_handles.source_locations_handle = value_handle;
            break;
        case UUID_PACS_AVAILABLE_CONTEXTS:
            conn->remote_handles.available_contexts_handle = value_handle;
            break;
        case UUID_PACS_SUPPORTED_CONTEXTS:
            conn->remote_handles.supported_contexts_handle = value_handle;
            break;
    }
}

/**
 * @brief Handle characteristic discovery complete
 *
 * Called when all PACS characteristics have been discovered.
 * Should then start reading characteristic values.
 */
void pacs_on_characteristic_discovery_complete(uint16_t conn_handle)
{
    pacs_connection_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return;
    }

    /* Start reading Sink PAC */
    conn->disc_state = PACS_DISC_STATE_READING_SINK_PAC;
    pacs_read_sink_pac(conn_handle);
}

/**
 * @brief Handle PAC read complete
 *
 * Called when a PAC characteristic read completes.
 */
void pacs_on_pac_read_complete(uint16_t conn_handle, bool is_sink,
                                const uint8_t *data, uint16_t len)
{
    pacs_connection_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return;
    }

    if (is_sink) {
        /* Parse Sink PAC */
        pacs_parse_pac_data(data, len, conn->info.sink_pac,
                            PACS_MAX_PAC_RECORDS, &conn->info.num_sink_pac);

        /* Read Source PAC next */
        conn->disc_state = PACS_DISC_STATE_READING_SOURCE_PAC;
        pacs_read_source_pac(conn_handle);
    } else {
        /* Parse Source PAC */
        pacs_parse_pac_data(data, len, conn->info.source_pac,
                            PACS_MAX_PAC_RECORDS, &conn->info.num_source_pac);

        /* Read audio locations next */
        conn->disc_state = PACS_DISC_STATE_READING_LOCATIONS;
        pacs_read_audio_locations(conn_handle);
    }
}

/**
 * @brief Handle audio locations read complete
 */
void pacs_on_locations_read_complete(uint16_t conn_handle,
                                      uint32_t sink_locations,
                                      uint32_t source_locations)
{
    pacs_connection_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return;
    }

    conn->info.sink_audio_locations = sink_locations;
    conn->info.source_audio_locations = source_locations;

    /* Read audio contexts next */
    conn->disc_state = PACS_DISC_STATE_READING_CONTEXTS;
    pacs_read_audio_contexts(conn_handle);
}

/**
 * @brief Handle audio contexts read complete
 */
void pacs_on_contexts_read_complete(uint16_t conn_handle,
                                     uint16_t avail_sink, uint16_t avail_source,
                                     uint16_t supp_sink, uint16_t supp_source)
{
    pacs_connection_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return;
    }

    conn->info.available_sink_contexts = avail_sink;
    conn->info.available_source_contexts = avail_source;
    conn->info.supported_sink_contexts = supp_sink;
    conn->info.supported_source_contexts = supp_source;

    /* Discovery complete */
    conn->disc_state = PACS_DISC_STATE_COMPLETE;
    conn->info.discovered = true;

    /* Notify application */
    notify_event(PACS_EVENT_DISCOVERY_COMPLETE, conn_handle, &conn->info);
}

/**
 * @brief Handle context notification from remote device
 */
void pacs_on_context_notification(uint16_t conn_handle,
                                   uint16_t avail_sink, uint16_t avail_source)
{
    pacs_connection_t *conn = find_connection(conn_handle);
    if (conn == NULL) {
        return;
    }

    conn->info.available_sink_contexts = avail_sink;
    conn->info.available_source_contexts = avail_source;

    /* Notify application */
    notify_event(PACS_EVENT_CONTEXTS_CHANGED, conn_handle, &conn->info);
}

/**
 * @brief Handle disconnection
 *
 * Clean up connection context when ACL disconnects.
 */
void pacs_on_disconnect(uint16_t conn_handle)
{
    free_connection(conn_handle);
}
