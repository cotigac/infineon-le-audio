/**
 * @file bap_broadcast_sink.h
 * @brief BAP Broadcast Sink Implementation (Auracast RX)
 *
 * Implements the BAP Broadcast Sink role for receiving Auracast broadcasts.
 * Includes BASE parser, PA sync, BIG sync, and RX data path management.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BAP_BROADCAST_SINK_H
#define BAP_BROADCAST_SINK_H

#include <stdint.h>
#include <stdbool.h>
#include "bap_broadcast.h"  /* For shared LC3 types */

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum subgroups in BASE */
#define BAP_BASE_MAX_SUBGROUPS      4

/** Maximum BIS per subgroup */
#define BAP_BASE_MAX_BIS            8

/** Maximum metadata length */
#define BAP_BASE_MAX_METADATA_LEN   64

/** Maximum broadcast name length */
#define BAP_BROADCAST_SINK_MAX_NAME 32

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    BAP_BROADCAST_SINK_OK = 0,
    BAP_BROADCAST_SINK_ERROR_INVALID_PARAM = -1,
    BAP_BROADCAST_SINK_ERROR_NOT_INITIALIZED = -2,
    BAP_BROADCAST_SINK_ERROR_ALREADY_INITIALIZED = -3,
    BAP_BROADCAST_SINK_ERROR_NO_RESOURCES = -4,
    BAP_BROADCAST_SINK_ERROR_INVALID_STATE = -5,
    BAP_BROADCAST_SINK_ERROR_SCAN_FAILED = -6,
    BAP_BROADCAST_SINK_ERROR_PA_SYNC_FAILED = -7,
    BAP_BROADCAST_SINK_ERROR_BIG_SYNC_FAILED = -8,
    BAP_BROADCAST_SINK_ERROR_NO_BASE = -9,
    BAP_BROADCAST_SINK_ERROR_DECRYPT_FAILED = -10,
    BAP_BROADCAST_SINK_ERROR_TIMEOUT = -11
} bap_broadcast_sink_error_t;

/*******************************************************************************
 * Types - BASE Parser
 ******************************************************************************/

/* Note: bap_lc3_freq_t and bap_lc3_duration_t are defined in bap_broadcast.h */

/** Parsed codec configuration from LTV (for BASE parsing) */
typedef struct {
    uint8_t sampling_freq;          /**< LC3 sampling frequency */
    uint8_t frame_duration;         /**< LC3 frame duration */
    uint32_t audio_location;        /**< Audio channel allocation */
    uint16_t octets_per_frame;      /**< Octets per codec frame */
    uint8_t frames_per_sdu;         /**< Frames per SDU (default 1) */
} bap_sink_codec_config_t;

/** Parsed metadata from LTV */
typedef struct {
    uint16_t audio_context;         /**< Streaming audio context */
    char language[4];               /**< Language code (3 chars + null) */
    uint8_t raw_metadata[BAP_BASE_MAX_METADATA_LEN];
    uint8_t raw_metadata_len;
} bap_metadata_t;

/** BIS information from BASE */
typedef struct {
    uint8_t bis_index;              /**< BIS index (1-based) */
    bap_sink_codec_config_t codec_config;/**< BIS-specific codec config */
} bap_bis_info_t;

/** Subgroup information from BASE */
typedef struct {
    uint8_t num_bis;                /**< Number of BIS in subgroup */
    uint8_t codec_id;               /**< Codec ID (0x06 for LC3) */
    bap_sink_codec_config_t codec_config;/**< Subgroup-level codec config */
    bap_metadata_t metadata;        /**< Subgroup metadata */
    bap_bis_info_t bis[BAP_BASE_MAX_BIS]; /**< BIS array */
} bap_subgroup_info_t;

/** Parsed BASE structure */
typedef struct {
    uint32_t presentation_delay_us; /**< Presentation delay in microseconds */
    uint8_t num_subgroups;          /**< Number of subgroups */
    bap_subgroup_info_t subgroups[BAP_BASE_MAX_SUBGROUPS];
} bap_base_info_t;

/*******************************************************************************
 * Types - Broadcast Sink State Machine
 ******************************************************************************/

/** Broadcast sink state */
typedef enum {
    BAP_BROADCAST_SINK_STATE_IDLE = 0,      /**< Not active */
    BAP_BROADCAST_SINK_STATE_SCANNING,      /**< Scanning for broadcasts */
    BAP_BROADCAST_SINK_STATE_PA_SYNCING,    /**< Creating PA sync */
    BAP_BROADCAST_SINK_STATE_PA_SYNCED,     /**< PA synced, waiting for BIGInfo */
    BAP_BROADCAST_SINK_STATE_BIG_SYNCING,   /**< Creating BIG sync */
    BAP_BROADCAST_SINK_STATE_STREAMING,     /**< Receiving BIS audio */
    BAP_BROADCAST_SINK_STATE_ERROR          /**< Error state */
} bap_broadcast_sink_state_t;

/** Discovered broadcast source info */
typedef struct {
    uint8_t broadcast_id[3];        /**< 3-byte Broadcast_ID */
    char broadcast_name[BAP_BROADCAST_SINK_MAX_NAME];
    uint8_t addr[6];                /**< Advertiser address */
    uint8_t addr_type;              /**< Address type */
    uint8_t adv_sid;                /**< Advertising SID */
    int8_t rssi;                    /**< RSSI */
    bool encrypted;                 /**< Encryption enabled */
} bap_broadcast_source_t;

/** BIGInfo received from periodic advertising */
typedef struct {
    uint16_t sync_handle;           /**< PA sync handle */
    uint8_t num_bis;                /**< Number of BIS */
    uint8_t nse;                    /**< Number of subevents */
    uint16_t iso_interval;          /**< ISO interval (1.25ms units) */
    uint8_t bn;                     /**< Burst number */
    uint8_t pto;                    /**< Pre-transmission offset */
    uint8_t irc;                    /**< Immediate repetition count */
    uint16_t max_pdu;               /**< Maximum PDU size */
    uint32_t sdu_interval;          /**< SDU interval (us) */
    uint16_t max_sdu;               /**< Maximum SDU size */
    uint8_t phy;                    /**< PHY used */
    uint8_t framing;                /**< Framing */
    bool encrypted;                 /**< Encryption status */
} bap_biginfo_t;

/** Broadcast sink runtime info */
typedef struct {
    bap_broadcast_sink_state_t state;
    bap_broadcast_source_t source;  /**< Connected source info */
    uint16_t pa_sync_handle;        /**< Periodic advertising sync handle */
    uint8_t big_handle;             /**< BIG handle */
    uint8_t num_bis;                /**< Number of synced BIS */
    uint16_t bis_handles[BAP_BASE_MAX_BIS]; /**< BIS connection handles */
    bap_base_info_t base_info;      /**< Parsed BASE structure */
    bap_biginfo_t biginfo;          /**< BIGInfo from PA */
} bap_broadcast_sink_info_t;

/** Broadcast sink statistics */
typedef struct {
    uint32_t frames_received;       /**< Total frames received */
    uint32_t frames_lost;           /**< Frames lost (sequence gaps) */
    uint32_t bytes_received;        /**< Total bytes received */
    uint32_t crc_errors;            /**< CRC errors */
    uint32_t sync_losses;           /**< BIG sync lost count */
    uint32_t uptime_ms;             /**< Total streaming time */
} bap_broadcast_sink_stats_t;

/*******************************************************************************
 * Types - Events and Callbacks
 ******************************************************************************/

/** Broadcast sink event types */
typedef enum {
    BAP_BROADCAST_SINK_EVENT_STATE_CHANGED,     /**< State changed */
    BAP_BROADCAST_SINK_EVENT_SOURCE_FOUND,      /**< Broadcast source found */
    BAP_BROADCAST_SINK_EVENT_PA_SYNCED,         /**< PA sync established */
    BAP_BROADCAST_SINK_EVENT_PA_SYNC_LOST,      /**< PA sync lost */
    BAP_BROADCAST_SINK_EVENT_BASE_RECEIVED,     /**< BASE parsed */
    BAP_BROADCAST_SINK_EVENT_BIGINFO_RECEIVED,  /**< BIGInfo received */
    BAP_BROADCAST_SINK_EVENT_STREAMING_STARTED, /**< BIG sync established */
    BAP_BROADCAST_SINK_EVENT_STREAMING_STOPPED, /**< BIG sync terminated */
    BAP_BROADCAST_SINK_EVENT_AUDIO_FRAME,       /**< Audio frame received */
    BAP_BROADCAST_SINK_EVENT_ERROR              /**< Error occurred */
} bap_broadcast_sink_event_type_t;

/** Audio frame received event data */
typedef struct {
    uint16_t bis_handle;            /**< BIS handle */
    uint8_t bis_index;              /**< BIS index (1-based) */
    uint32_t timestamp;             /**< Presentation timestamp */
    uint16_t seq_num;               /**< Sequence number */
    const uint8_t *data;            /**< LC3 encoded data */
    uint16_t length;                /**< Data length */
} bap_audio_frame_t;

/** Broadcast sink event data */
typedef struct {
    bap_broadcast_sink_event_type_t type;
    union {
        bap_broadcast_sink_state_t new_state;
        bap_broadcast_source_t source;
        bap_base_info_t *base_info;
        bap_biginfo_t *biginfo;
        bap_audio_frame_t audio_frame;
        int error_code;
    } data;
} bap_broadcast_sink_event_t;

/** Event callback function type */
typedef void (*bap_broadcast_sink_callback_t)(const bap_broadcast_sink_event_t *event, void *user_data);

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize the broadcast sink module
 *
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_init(void);

/**
 * @brief Deinitialize the broadcast sink module
 */
void bap_broadcast_sink_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void bap_broadcast_sink_register_callback(bap_broadcast_sink_callback_t callback, void *user_data);

/*******************************************************************************
 * API Functions - Scanning
 ******************************************************************************/

/**
 * @brief Start scanning for broadcast sources
 *
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_start_scan(void);

/**
 * @brief Stop scanning
 *
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_stop_scan(void);

/*******************************************************************************
 * API Functions - Sync Control
 ******************************************************************************/

/**
 * @brief Sync to a discovered broadcast source (PA sync)
 *
 * @param source Pointer to source info from SOURCE_FOUND event
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_sync_to_pa(const bap_broadcast_source_t *source);

/**
 * @brief Sync to PA and automatically sync to BIG when ready
 *
 * Combines PA sync and BIG sync into a single call. After PA sync
 * establishes and BIGInfo is received, BIG sync is triggered
 * automatically using the provided broadcast code.
 *
 * @param source Pointer to source info from SOURCE_FOUND event
 * @param broadcast_code 16-byte encryption key (NULL if unencrypted)
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_sync_to_pa_auto_big(const bap_broadcast_source_t *source,
                                            const uint8_t *broadcast_code);

/**
 * @brief Sync to BIG (start receiving audio)
 *
 * @param broadcast_code 16-byte encryption key (NULL if unencrypted)
 * @param bis_indices Array of BIS indices to sync (NULL for all)
 * @param num_bis Number of BIS to sync (0 for all)
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_sync_to_big(const uint8_t *broadcast_code,
                                    const uint8_t *bis_indices, uint8_t num_bis);

/**
 * @brief Stop receiving and disconnect
 *
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_stop(void);

/*******************************************************************************
 * API Functions - State and Info
 ******************************************************************************/

/**
 * @brief Get current state
 *
 * @return Current state
 */
bap_broadcast_sink_state_t bap_broadcast_sink_get_state(void);

/**
 * @brief Get runtime info
 *
 * @param info Pointer to info structure to fill
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_get_info(bap_broadcast_sink_info_t *info);

/**
 * @brief Get statistics
 *
 * @param stats Pointer to stats structure to fill
 */
void bap_broadcast_sink_get_stats(bap_broadcast_sink_stats_t *stats);

/**
 * @brief Reset statistics
 */
void bap_broadcast_sink_reset_stats(void);

/*******************************************************************************
 * API Functions - BASE Parser
 ******************************************************************************/

/**
 * @brief Parse BASE structure from periodic advertising data
 *
 * @param data Raw BASE data (after Service Data UUID)
 * @param len Data length
 * @param base_info Pointer to structure to fill
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_parse_base(const uint8_t *data, uint16_t len, bap_base_info_t *base_info);

/**
 * @brief Convert LC3 frequency value to Hz
 *
 * @param lc3_freq LC3 frequency value
 * @return Sample rate in Hz
 */
uint32_t bap_lc3_freq_to_hz(uint8_t lc3_freq);

/**
 * @brief Convert LC3 duration value to microseconds
 *
 * @param lc3_duration LC3 duration value
 * @return Frame duration in microseconds
 */
uint32_t bap_lc3_duration_to_us(uint8_t lc3_duration);

/*******************************************************************************
 * API Functions - Demo/Test
 ******************************************************************************/

/**
 * @brief Demo: Auto-sync to first discovered broadcast
 *
 * Starts scanning and automatically syncs to the first Auracast
 * broadcast found. Useful for quick testing.
 *
 * @param broadcast_code 16-byte key for encrypted broadcasts (NULL if unencrypted)
 * @return BAP_BROADCAST_SINK_OK on success
 */
int bap_broadcast_sink_demo_auto_sync(const uint8_t *broadcast_code);

#ifdef __cplusplus
}
#endif

#endif /* BAP_BROADCAST_SINK_H */
