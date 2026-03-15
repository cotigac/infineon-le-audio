/**
 * @file bap_broadcast.h
 * @brief BAP Broadcast Source API (Auracast)
 *
 * This module implements the Basic Audio Profile (BAP) Broadcast Source
 * role for Auracast transmission, per Bluetooth SIG specifications:
 * - BAP 1.0.1 (Basic Audio Profile)
 * - BASS 1.0 (Broadcast Audio Scan Service)
 * - PBP 1.0 (Public Broadcast Profile)
 *
 * Features:
 * - BASE (Broadcast Audio Source Endpoint) structure generation
 * - Extended and Periodic Advertising setup
 * - BIG (Broadcast Isochronous Group) management
 * - Multiple subgroups and BIS support
 * - Encryption (Broadcast Code) support
 * - Runtime metadata updates
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BAP_BROADCAST_H
#define BAP_BROADCAST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum broadcast name length */
#define BAP_BROADCAST_MAX_NAME_LEN      32

/** Maximum subgroups in a broadcast */
#define BAP_BROADCAST_MAX_SUBGROUPS     4

/** Maximum BIS per subgroup */
#define BAP_BROADCAST_MAX_BIS           4

/** Maximum BASE structure size */
#define BAP_BROADCAST_MAX_BASE_SIZE     256

/** Maximum metadata length */
#define BAP_BROADCAST_MAX_METADATA_LEN  64

/** Maximum codec configuration length */
#define BAP_BROADCAST_MAX_CODEC_CFG_LEN 32

/** Broadcast code size (for encryption) */
#define BAP_BROADCAST_CODE_SIZE         16

/** Presentation delay range */
#define BAP_BROADCAST_MIN_PRES_DELAY_US 20000
#define BAP_BROADCAST_MAX_PRES_DELAY_US 100000
#define BAP_BROADCAST_DEFAULT_PRES_DELAY_US 40000

/*******************************************************************************
 * LC3 Codec Specific Values (from Assigned Numbers)
 ******************************************************************************/

/** LC3 Codec ID */
#define BAP_CODEC_ID_LC3                0x06

/** LC3 Sampling Frequencies */
typedef enum {
    BAP_LC3_FREQ_8000   = 0x01,
    BAP_LC3_FREQ_11025  = 0x02,
    BAP_LC3_FREQ_16000  = 0x03,
    BAP_LC3_FREQ_22050  = 0x04,
    BAP_LC3_FREQ_24000  = 0x05,
    BAP_LC3_FREQ_32000  = 0x06,
    BAP_LC3_FREQ_44100  = 0x07,
    BAP_LC3_FREQ_48000  = 0x08,
    BAP_LC3_FREQ_88200  = 0x09,
    BAP_LC3_FREQ_96000  = 0x0A,
    BAP_LC3_FREQ_176400 = 0x0B,
    BAP_LC3_FREQ_192000 = 0x0C,
    BAP_LC3_FREQ_384000 = 0x0D
} bap_lc3_freq_t;

/** LC3 Frame Durations */
typedef enum {
    BAP_LC3_DURATION_7_5MS  = 0x00,
    BAP_LC3_DURATION_10MS   = 0x01
} bap_lc3_duration_t;

/** Audio Locations (channel allocation) */
typedef enum {
    BAP_AUDIO_LOCATION_MONO             = 0x00000000,
    BAP_AUDIO_LOCATION_FRONT_LEFT       = 0x00000001,
    BAP_AUDIO_LOCATION_FRONT_RIGHT      = 0x00000002,
    BAP_AUDIO_LOCATION_FRONT_CENTER     = 0x00000004,
    BAP_AUDIO_LOCATION_LOW_FREQ         = 0x00000008,
    BAP_AUDIO_LOCATION_BACK_LEFT        = 0x00000010,
    BAP_AUDIO_LOCATION_BACK_RIGHT       = 0x00000020,
    BAP_AUDIO_LOCATION_FRONT_LEFT_CENTER  = 0x00000040,
    BAP_AUDIO_LOCATION_FRONT_RIGHT_CENTER = 0x00000080,
    BAP_AUDIO_LOCATION_BACK_CENTER      = 0x00000100,
    BAP_AUDIO_LOCATION_SIDE_LEFT        = 0x00000200,
    BAP_AUDIO_LOCATION_SIDE_RIGHT       = 0x00000400,
    BAP_AUDIO_LOCATION_TOP_FRONT_LEFT   = 0x00000800,
    BAP_AUDIO_LOCATION_TOP_FRONT_RIGHT  = 0x00001000,
    BAP_AUDIO_LOCATION_TOP_FRONT_CENTER = 0x00002000,
    BAP_AUDIO_LOCATION_TOP_CENTER       = 0x00004000,
    BAP_AUDIO_LOCATION_TOP_BACK_LEFT    = 0x00008000,
    BAP_AUDIO_LOCATION_TOP_BACK_RIGHT   = 0x00010000,
    BAP_AUDIO_LOCATION_TOP_SIDE_LEFT    = 0x00020000,
    BAP_AUDIO_LOCATION_TOP_SIDE_RIGHT   = 0x00040000,
    BAP_AUDIO_LOCATION_TOP_BACK_CENTER  = 0x00080000,
    BAP_AUDIO_LOCATION_BOTTOM_FRONT_CENTER = 0x00100000,
    BAP_AUDIO_LOCATION_BOTTOM_FRONT_LEFT   = 0x00200000,
    BAP_AUDIO_LOCATION_BOTTOM_FRONT_RIGHT  = 0x00400000,
    BAP_AUDIO_LOCATION_LEFT_SURROUND    = 0x00800000,
    BAP_AUDIO_LOCATION_RIGHT_SURROUND   = 0x01000000,
    BAP_AUDIO_LOCATION_STEREO           = 0x00000003  /* Left + Right */
} bap_audio_location_t;

/** Streaming Audio Contexts */
typedef enum {
    BAP_CONTEXT_UNSPECIFIED     = 0x0001,
    BAP_CONTEXT_CONVERSATIONAL  = 0x0002,
    BAP_CONTEXT_MEDIA           = 0x0004,
    BAP_CONTEXT_GAME            = 0x0008,
    BAP_CONTEXT_INSTRUCTIONAL   = 0x0010,
    BAP_CONTEXT_VOICE_ASSISTANT = 0x0020,
    BAP_CONTEXT_LIVE            = 0x0040,
    BAP_CONTEXT_SOUND_EFFECTS   = 0x0080,
    BAP_CONTEXT_NOTIFICATIONS   = 0x0100,
    BAP_CONTEXT_RINGTONE        = 0x0200,
    BAP_CONTEXT_ALERTS          = 0x0400,
    BAP_CONTEXT_EMERGENCY       = 0x0800
} bap_audio_context_t;

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    BAP_BROADCAST_OK = 0,
    BAP_BROADCAST_ERROR_INVALID_PARAM = -1,
    BAP_BROADCAST_ERROR_NOT_INITIALIZED = -2,
    BAP_BROADCAST_ERROR_ALREADY_INITIALIZED = -3,
    BAP_BROADCAST_ERROR_INVALID_STATE = -4,
    BAP_BROADCAST_ERROR_NO_RESOURCES = -5,
    BAP_BROADCAST_ERROR_ADV_FAILED = -6,
    BAP_BROADCAST_ERROR_BIG_FAILED = -7,
    BAP_BROADCAST_ERROR_CODEC_ERROR = -8,
    BAP_BROADCAST_ERROR_TIMEOUT = -9
} bap_broadcast_error_t;

/*******************************************************************************
 * Types
 ******************************************************************************/

/** Broadcast source state */
typedef enum {
    BAP_BROADCAST_STATE_IDLE = 0,       /**< Not broadcasting */
    BAP_BROADCAST_STATE_CONFIGURED,     /**< Configured, not advertising */
    BAP_BROADCAST_STATE_ADVERTISING,    /**< Extended + periodic advertising */
    BAP_BROADCAST_STATE_STREAMING,      /**< BIG active, streaming audio */
    BAP_BROADCAST_STATE_STOPPING        /**< Shutting down */
} bap_broadcast_state_t;

/** LC3 codec configuration */
typedef struct {
    bap_lc3_freq_t sampling_freq;       /**< Sampling frequency */
    bap_lc3_duration_t frame_duration;  /**< Frame duration */
    uint16_t octets_per_frame;          /**< Octets per codec frame */
    uint8_t frames_per_sdu;             /**< Codec frames per SDU */
} bap_lc3_config_t;

/** BIS (Broadcast Isochronous Stream) configuration */
typedef struct {
    uint8_t bis_index;                  /**< BIS index (1-31) */
    uint32_t audio_location;            /**< Audio channel location */
    uint8_t codec_cfg[BAP_BROADCAST_MAX_CODEC_CFG_LEN];  /**< BIS-specific codec config */
    uint8_t codec_cfg_len;              /**< Codec config length */
} bap_bis_config_t;

/** Subgroup configuration */
typedef struct {
    bap_lc3_config_t codec_config;      /**< LC3 codec configuration */
    uint16_t audio_context;             /**< Streaming audio context */
    char language[4];                   /**< Language code (ISO 639-3) */
    uint8_t metadata[BAP_BROADCAST_MAX_METADATA_LEN];  /**< Additional metadata */
    uint8_t metadata_len;               /**< Metadata length */
    uint8_t num_bis;                    /**< Number of BIS in subgroup */
    bap_bis_config_t bis[BAP_BROADCAST_MAX_BIS];  /**< BIS configurations */
} bap_subgroup_config_t;

/** Broadcast source configuration */
typedef struct {
    /* Identity */
    uint8_t broadcast_id[3];            /**< 3-byte Broadcast_ID */
    char broadcast_name[BAP_BROADCAST_MAX_NAME_LEN];  /**< Human-readable name */

    /* Encryption */
    bool encrypted;                     /**< Enable encryption */
    uint8_t broadcast_code[BAP_BROADCAST_CODE_SIZE];  /**< Broadcast Code */

    /* Timing */
    uint32_t presentation_delay_us;     /**< Presentation delay (microseconds) */

    /* QoS parameters */
    uint16_t max_transport_latency_ms;  /**< Max transport latency */
    uint8_t rtn;                        /**< Retransmission number */
    uint8_t phy;                        /**< PHY (1=1M, 2=2M, 4=Coded) */

    /* Advertising parameters */
    uint16_t adv_interval_min;          /**< Min adv interval (0.625ms units) */
    uint16_t adv_interval_max;          /**< Max adv interval */
    int8_t tx_power;                    /**< TX power in dBm */

    /* Subgroups */
    uint8_t num_subgroups;              /**< Number of subgroups */
    bap_subgroup_config_t subgroups[BAP_BROADCAST_MAX_SUBGROUPS];
} bap_broadcast_config_t;

/** Broadcast source runtime info */
typedef struct {
    bap_broadcast_state_t state;        /**< Current state */
    uint8_t broadcast_id[3];            /**< Active Broadcast_ID */
    uint8_t adv_handle;                 /**< Extended advertising handle */
    uint8_t big_handle;                 /**< BIG handle */
    uint8_t num_bis;                    /**< Total number of BIS */
    uint16_t bis_handles[BAP_BROADCAST_MAX_SUBGROUPS * BAP_BROADCAST_MAX_BIS];
    uint32_t big_sync_delay;            /**< BIG sync delay (us) */
    uint32_t transport_latency;         /**< Transport latency (us) */
    uint16_t iso_interval;              /**< ISO interval (1.25ms units) */
    uint32_t sdu_interval;              /**< SDU interval (us) */
    uint16_t max_sdu;                   /**< Max SDU size */
} bap_broadcast_info_t;

/** Broadcast event types */
typedef enum {
    BAP_BROADCAST_EVENT_STATE_CHANGED,  /**< State changed */
    BAP_BROADCAST_EVENT_STARTED,        /**< Broadcast started */
    BAP_BROADCAST_EVENT_STOPPED,        /**< Broadcast stopped */
    BAP_BROADCAST_EVENT_BIG_CREATED,    /**< BIG created */
    BAP_BROADCAST_EVENT_BIG_TERMINATED, /**< BIG terminated */
    BAP_BROADCAST_EVENT_ERROR           /**< Error occurred */
} bap_broadcast_event_type_t;

/** Broadcast event data */
typedef struct {
    bap_broadcast_event_type_t type;
    union {
        bap_broadcast_state_t new_state;
        bap_broadcast_info_t info;
        int error_code;
    } data;
} bap_broadcast_event_t;

/** Broadcast event callback */
typedef void (*bap_broadcast_callback_t)(const bap_broadcast_event_t *event, void *user_data);

/** Audio frame for transmission */
typedef struct {
    uint8_t subgroup;                   /**< Subgroup index */
    uint8_t bis_index;                  /**< BIS index within subgroup */
    const uint8_t *data;                /**< LC3 encoded frame */
    uint16_t length;                    /**< Frame length */
    uint32_t timestamp;                 /**< Timestamp (us) */
    uint16_t seq_num;                   /**< Sequence number */
} bap_broadcast_frame_t;

/** Broadcast statistics */
typedef struct {
    uint32_t frames_sent;               /**< Total frames sent */
    uint32_t frames_dropped;            /**< Frames dropped (buffer full) */
    uint32_t bytes_sent;                /**< Total bytes sent */
    uint32_t big_events;                /**< BIG events processed */
    uint32_t retransmissions;           /**< Retransmitted packets */
    uint32_t uptime_ms;                 /**< Broadcasting time (ms) */
} bap_broadcast_stats_t;

/*******************************************************************************
 * Default Configurations
 ******************************************************************************/

/** Default LC3 configuration (48kHz, 10ms, 100 octets = ~80kbps) */
#define BAP_LC3_CONFIG_48_10_100 {      \
    .sampling_freq = BAP_LC3_FREQ_48000,\
    .frame_duration = BAP_LC3_DURATION_10MS, \
    .octets_per_frame = 100,            \
    .frames_per_sdu = 1                 \
}

/** Default LC3 configuration (24kHz, 10ms, 60 octets = ~48kbps) */
#define BAP_LC3_CONFIG_24_10_60 {       \
    .sampling_freq = BAP_LC3_FREQ_24000,\
    .frame_duration = BAP_LC3_DURATION_10MS, \
    .octets_per_frame = 60,             \
    .frames_per_sdu = 1                 \
}

/** Default mono BIS configuration */
#define BAP_BIS_CONFIG_MONO {           \
    .bis_index = 1,                     \
    .audio_location = BAP_AUDIO_LOCATION_MONO, \
    .codec_cfg_len = 0                  \
}

/** Default stereo subgroup (left + right BIS) */
#define BAP_SUBGROUP_STEREO_DEFAULT {   \
    .codec_config = BAP_LC3_CONFIG_48_10_100, \
    .audio_context = BAP_CONTEXT_MEDIA, \
    .language = "eng",                  \
    .metadata_len = 0,                  \
    .num_bis = 2,                       \
    .bis = {                            \
        { .bis_index = 1, .audio_location = BAP_AUDIO_LOCATION_FRONT_LEFT }, \
        { .bis_index = 2, .audio_location = BAP_AUDIO_LOCATION_FRONT_RIGHT } \
    }                                   \
}

/** Default broadcast configuration */
#define BAP_BROADCAST_CONFIG_DEFAULT {  \
    .broadcast_id = {0x01, 0x02, 0x03}, \
    .broadcast_name = "Infineon Auracast", \
    .encrypted = false,                 \
    .broadcast_code = {0},              \
    .presentation_delay_us = 40000,     \
    .max_transport_latency_ms = 40,     \
    .rtn = 2,                           \
    .phy = 2,                           \
    .adv_interval_min = 160,            \
    .adv_interval_max = 160,            \
    .tx_power = 0,                      \
    .num_subgroups = 1                  \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize BAP Broadcast Source
 *
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_init(void);

/**
 * @brief Deinitialize BAP Broadcast Source
 */
void bap_broadcast_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void bap_broadcast_register_callback(bap_broadcast_callback_t callback, void *user_data);

/*******************************************************************************
 * API Functions - Broadcast Control
 ******************************************************************************/

/**
 * @brief Configure broadcast source
 *
 * Sets up the broadcast configuration. Call before starting broadcast.
 *
 * @param config Pointer to broadcast configuration
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_configure(const bap_broadcast_config_t *config);

/**
 * @brief Start broadcasting
 *
 * Starts extended advertising, periodic advertising with BASE,
 * and creates the BIG for audio streaming.
 *
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_start(void);

/**
 * @brief Stop broadcasting
 *
 * Terminates the BIG and stops advertising.
 *
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_stop(void);

/**
 * @brief Get current broadcast state
 *
 * @return Current state
 */
bap_broadcast_state_t bap_broadcast_get_state(void);

/**
 * @brief Get broadcast info
 *
 * @param info Pointer to structure to fill
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_get_info(bap_broadcast_info_t *info);

/*******************************************************************************
 * API Functions - Audio Transmission
 ******************************************************************************/

/**
 * @brief Send LC3 encoded audio frame
 *
 * Sends a pre-encoded LC3 frame over the specified BIS.
 *
 * @param frame Pointer to frame structure
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_send_frame(const bap_broadcast_frame_t *frame);

/**
 * @brief Send LC3 frame to all BIS
 *
 * Convenience function to send the same frame to all BIS (mono broadcast).
 *
 * @param data LC3 encoded frame data
 * @param length Frame length
 * @param timestamp Frame timestamp (microseconds)
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_send_all(const uint8_t *data, uint16_t length, uint32_t timestamp);

/**
 * @brief Get next BIS handle for transmission
 *
 * Returns the next BIS handle in round-robin order.
 *
 * @return BIS handle, or 0xFFFF if not streaming
 */
uint16_t bap_broadcast_get_next_bis_handle(void);

/*******************************************************************************
 * API Functions - Metadata Updates
 ******************************************************************************/

/**
 * @brief Update broadcast name
 *
 * Updates the broadcast name in advertising data.
 * Can be called while broadcasting.
 *
 * @param name New broadcast name (null-terminated)
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_update_name(const char *name);

/**
 * @brief Update streaming context
 *
 * Updates the streaming audio context in BASE.
 * Can be called while broadcasting.
 *
 * @param subgroup Subgroup index
 * @param context New audio context
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_update_context(uint8_t subgroup, uint16_t context);

/**
 * @brief Update subgroup metadata
 *
 * Updates metadata for a subgroup in BASE.
 *
 * @param subgroup Subgroup index
 * @param metadata Metadata bytes
 * @param length Metadata length
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_update_metadata(uint8_t subgroup, const uint8_t *metadata, uint8_t length);

/*******************************************************************************
 * API Functions - BASE Generation
 ******************************************************************************/

/**
 * @brief Generate BASE structure
 *
 * Creates the Broadcast Audio Source Endpoint structure for
 * inclusion in periodic advertising.
 *
 * @param config Broadcast configuration
 * @param base_out Buffer for BASE data
 * @param base_size Buffer size
 * @param base_len_out Actual BASE length
 * @return BAP_BROADCAST_OK on success, negative error code on failure
 */
int bap_broadcast_generate_base(const bap_broadcast_config_t *config,
                                 uint8_t *base_out, uint16_t base_size,
                                 uint16_t *base_len_out);

/*******************************************************************************
 * API Functions - Statistics
 ******************************************************************************/

/**
 * @brief Get broadcast statistics
 *
 * @param stats Pointer to statistics structure
 */
void bap_broadcast_get_stats(bap_broadcast_stats_t *stats);

/**
 * @brief Reset broadcast statistics
 */
void bap_broadcast_reset_stats(void);

/*******************************************************************************
 * API Functions - Utilities
 ******************************************************************************/

/**
 * @brief Convert sample rate to LC3 frequency code
 *
 * @param sample_rate Sample rate in Hz
 * @return LC3 frequency code, or 0 if unsupported
 */
uint8_t bap_broadcast_sample_rate_to_lc3(uint32_t sample_rate);

/**
 * @brief Convert LC3 frequency code to sample rate
 *
 * @param lc3_freq LC3 frequency code
 * @return Sample rate in Hz, or 0 if invalid
 */
uint32_t bap_broadcast_lc3_to_sample_rate(uint8_t lc3_freq);

/**
 * @brief Generate random Broadcast_ID
 *
 * @param broadcast_id Buffer for 3-byte Broadcast_ID
 */
void bap_broadcast_generate_id(uint8_t broadcast_id[3]);

#ifdef __cplusplus
}
#endif

#endif /* BAP_BROADCAST_H */
