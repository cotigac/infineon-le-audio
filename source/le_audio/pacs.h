/**
 * @file pacs.h
 * @brief Published Audio Capabilities Service (PACS) API
 *
 * This module implements PACS per Bluetooth SIG specification for
 * advertising audio codec capabilities to remote devices.
 *
 * PACS provides:
 * - Sink PAC: Supported audio sink configurations (we can receive)
 * - Source PAC: Supported audio source configurations (we can send)
 * - Sink Audio Locations: Supported speaker/output locations
 * - Source Audio Locations: Supported microphone/input locations
 * - Available Audio Contexts: Currently available streaming contexts
 * - Supported Audio Contexts: All supported streaming contexts
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PACS_H
#define PACS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** PACS Service UUID */
#define UUID_PACS_SERVICE               0x1850

/** PACS Characteristic UUIDs */
#define UUID_PACS_SINK_PAC              0x2BC9
#define UUID_PACS_SINK_AUDIO_LOCATIONS  0x2BCA
#define UUID_PACS_SOURCE_PAC            0x2BCB
#define UUID_PACS_SOURCE_AUDIO_LOCATIONS 0x2BCC
#define UUID_PACS_AVAILABLE_CONTEXTS    0x2BCD
#define UUID_PACS_SUPPORTED_CONTEXTS    0x2BCE

/** Maximum PAC records */
#define PACS_MAX_PAC_RECORDS            4

/** Maximum codec specific capabilities length */
#define PACS_MAX_CODEC_CAP_LEN          64

/** Maximum metadata length */
#define PACS_MAX_METADATA_LEN           32

/*******************************************************************************
 * LC3 Codec Capabilities (LTV Types)
 ******************************************************************************/

/** Codec Specific Capabilities LTV Types */
#define PACS_LTV_SUPPORTED_FREQ         0x01    /**< Supported Sampling Frequencies */
#define PACS_LTV_SUPPORTED_DURATION     0x02    /**< Supported Frame Durations */
#define PACS_LTV_SUPPORTED_CHANNELS     0x03    /**< Supported Audio Channel Counts */
#define PACS_LTV_SUPPORTED_OCTETS_MIN   0x04    /**< Supported Octets per Frame (min-max) */
#define PACS_LTV_SUPPORTED_FRAMES_MAX   0x05    /**< Max Supported Codec Frames per SDU */

/** Supported Sampling Frequencies Bitmask */
typedef enum {
    PACS_FREQ_8000      = 0x0001,
    PACS_FREQ_11025     = 0x0002,
    PACS_FREQ_16000     = 0x0004,
    PACS_FREQ_22050     = 0x0008,
    PACS_FREQ_24000     = 0x0010,
    PACS_FREQ_32000     = 0x0020,
    PACS_FREQ_44100     = 0x0040,
    PACS_FREQ_48000     = 0x0080,
    PACS_FREQ_88200     = 0x0100,
    PACS_FREQ_96000     = 0x0200,
    PACS_FREQ_176400    = 0x0400,
    PACS_FREQ_192000    = 0x0800,
    PACS_FREQ_384000    = 0x1000,
    PACS_FREQ_ALL_STANDARD = 0x00FF  /**< 8-48kHz */
} pacs_supported_freq_t;

/** Supported Frame Durations Bitmask */
typedef enum {
    PACS_DURATION_7_5MS     = 0x01,     /**< 7.5ms supported */
    PACS_DURATION_10MS      = 0x02,     /**< 10ms supported */
    PACS_DURATION_7_5MS_PREF = 0x10,    /**< 7.5ms preferred */
    PACS_DURATION_10MS_PREF  = 0x20,    /**< 10ms preferred */
    PACS_DURATION_BOTH      = 0x03,     /**< Both durations supported */
    PACS_DURATION_BOTH_10MS_PREF = 0x23 /**< Both, prefer 10ms */
} pacs_supported_duration_t;

/** Supported Audio Channel Counts Bitmask */
typedef enum {
    PACS_CHANNELS_1     = 0x01,         /**< 1 channel (mono) */
    PACS_CHANNELS_2     = 0x02,         /**< 2 channels (stereo) */
    PACS_CHANNELS_3     = 0x04,
    PACS_CHANNELS_4     = 0x08,
    PACS_CHANNELS_5     = 0x10,
    PACS_CHANNELS_6     = 0x20,
    PACS_CHANNELS_7     = 0x40,
    PACS_CHANNELS_8     = 0x80,
    PACS_CHANNELS_MONO_STEREO = 0x03    /**< Mono and stereo */
} pacs_supported_channels_t;

/*******************************************************************************
 * Audio Locations (from Assigned Numbers)
 ******************************************************************************/

typedef enum {
    PACS_LOCATION_MONO              = 0x00000000,
    PACS_LOCATION_FRONT_LEFT        = 0x00000001,
    PACS_LOCATION_FRONT_RIGHT       = 0x00000002,
    PACS_LOCATION_FRONT_CENTER      = 0x00000004,
    PACS_LOCATION_LOW_FREQ_EFFECTS_1 = 0x00000008,
    PACS_LOCATION_BACK_LEFT         = 0x00000010,
    PACS_LOCATION_BACK_RIGHT        = 0x00000020,
    PACS_LOCATION_FRONT_LEFT_CENTER = 0x00000040,
    PACS_LOCATION_FRONT_RIGHT_CENTER = 0x00000080,
    PACS_LOCATION_BACK_CENTER       = 0x00000100,
    PACS_LOCATION_LOW_FREQ_EFFECTS_2 = 0x00000200,
    PACS_LOCATION_SIDE_LEFT         = 0x00000400,
    PACS_LOCATION_SIDE_RIGHT        = 0x00000800,
    PACS_LOCATION_TOP_FRONT_LEFT    = 0x00001000,
    PACS_LOCATION_TOP_FRONT_RIGHT   = 0x00002000,
    PACS_LOCATION_TOP_FRONT_CENTER  = 0x00004000,
    PACS_LOCATION_TOP_CENTER        = 0x00008000,
    PACS_LOCATION_TOP_BACK_LEFT     = 0x00010000,
    PACS_LOCATION_TOP_BACK_RIGHT    = 0x00020000,
    PACS_LOCATION_TOP_SIDE_LEFT     = 0x00040000,
    PACS_LOCATION_TOP_SIDE_RIGHT    = 0x00080000,
    PACS_LOCATION_TOP_BACK_CENTER   = 0x00100000,
    PACS_LOCATION_BOTTOM_FRONT_CENTER = 0x00200000,
    PACS_LOCATION_BOTTOM_FRONT_LEFT = 0x00400000,
    PACS_LOCATION_BOTTOM_FRONT_RIGHT = 0x00800000,
    PACS_LOCATION_FRONT_LEFT_WIDE   = 0x01000000,
    PACS_LOCATION_FRONT_RIGHT_WIDE  = 0x02000000,
    PACS_LOCATION_LEFT_SURROUND     = 0x04000000,
    PACS_LOCATION_RIGHT_SURROUND    = 0x08000000,
    /* Common combinations */
    PACS_LOCATION_STEREO            = 0x00000003,  /**< FL + FR */
    PACS_LOCATION_SURROUND_5_1      = 0x0000003F,  /**< FL, FR, FC, LFE, BL, BR */
    PACS_LOCATION_SURROUND_7_1      = 0x00000C3F   /**< 5.1 + SL, SR */
} pacs_audio_location_t;

/*******************************************************************************
 * Audio Contexts (from Assigned Numbers)
 ******************************************************************************/

typedef enum {
    PACS_CONTEXT_UNSPECIFIED        = 0x0001,
    PACS_CONTEXT_CONVERSATIONAL     = 0x0002,   /**< Phone calls, VoIP */
    PACS_CONTEXT_MEDIA              = 0x0004,   /**< Music, podcasts */
    PACS_CONTEXT_GAME               = 0x0008,   /**< Gaming audio */
    PACS_CONTEXT_INSTRUCTIONAL      = 0x0010,   /**< Navigation, announcements */
    PACS_CONTEXT_VOICE_ASSISTANTS   = 0x0020,   /**< Voice commands */
    PACS_CONTEXT_LIVE               = 0x0040,   /**< Live audio */
    PACS_CONTEXT_SOUND_EFFECTS      = 0x0080,   /**< Sound effects, UI sounds */
    PACS_CONTEXT_NOTIFICATIONS      = 0x0100,   /**< Notification sounds */
    PACS_CONTEXT_RINGTONE           = 0x0200,   /**< Ringtones */
    PACS_CONTEXT_ALERTS             = 0x0400,   /**< Alert sounds */
    PACS_CONTEXT_EMERGENCY_ALARM    = 0x0800,   /**< Emergency sounds */
    /* Common combinations */
    PACS_CONTEXT_ALL_STANDARD       = 0x0FFF,   /**< All standard contexts */
    PACS_CONTEXT_MEDIA_GAME         = 0x000C,   /**< Media + Game */
    PACS_CONTEXT_CALLS_MEDIA        = 0x0006    /**< Calls + Media */
} pacs_audio_context_t;

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    PACS_OK = 0,
    PACS_ERROR_INVALID_PARAM = -1,
    PACS_ERROR_NOT_INITIALIZED = -2,
    PACS_ERROR_ALREADY_INITIALIZED = -3,
    PACS_ERROR_NO_RESOURCES = -4,
    PACS_ERROR_NOT_FOUND = -5,
    PACS_ERROR_GATT_ERROR = -6
} pacs_error_t;

/*******************************************************************************
 * Types
 ******************************************************************************/

/** LC3 Codec Capabilities */
typedef struct {
    uint16_t supported_frequencies;     /**< Bitmask of supported sampling frequencies */
    uint8_t supported_durations;        /**< Bitmask of supported frame durations */
    uint8_t supported_channels;         /**< Bitmask of supported channel counts */
    uint16_t min_octets_per_frame;      /**< Minimum octets per codec frame */
    uint16_t max_octets_per_frame;      /**< Maximum octets per codec frame */
    uint8_t max_frames_per_sdu;         /**< Max codec frames per SDU */
} pacs_lc3_capabilities_t;

/** PAC Record (Published Audio Capability) */
typedef struct {
    uint8_t codec_id[5];                /**< Codec ID (format + company + vendor) */
    uint8_t codec_specific_cap[PACS_MAX_CODEC_CAP_LEN];  /**< Codec specific capabilities */
    uint8_t codec_specific_cap_len;     /**< Capabilities length */
    uint8_t metadata[PACS_MAX_METADATA_LEN];  /**< Metadata */
    uint8_t metadata_len;               /**< Metadata length */
    /* Parsed LC3 capabilities (if LC3 codec) */
    bool is_lc3;
    pacs_lc3_capabilities_t lc3_cap;
} pacs_pac_record_t;

/** PACS Configuration (for server) */
typedef struct {
    /* Sink capabilities (what we can receive) */
    uint8_t num_sink_pac;
    pacs_pac_record_t sink_pac[PACS_MAX_PAC_RECORDS];
    uint32_t sink_audio_locations;

    /* Source capabilities (what we can send) */
    uint8_t num_source_pac;
    pacs_pac_record_t source_pac[PACS_MAX_PAC_RECORDS];
    uint32_t source_audio_locations;

    /* Audio contexts */
    uint16_t available_sink_contexts;
    uint16_t available_source_contexts;
    uint16_t supported_sink_contexts;
    uint16_t supported_source_contexts;
} pacs_config_t;

/** Remote device PACS info (from discovery) */
typedef struct {
    uint16_t conn_handle;
    bool discovered;

    /* Remote capabilities */
    uint8_t num_sink_pac;
    pacs_pac_record_t sink_pac[PACS_MAX_PAC_RECORDS];
    uint32_t sink_audio_locations;

    uint8_t num_source_pac;
    pacs_pac_record_t source_pac[PACS_MAX_PAC_RECORDS];
    uint32_t source_audio_locations;

    uint16_t available_sink_contexts;
    uint16_t available_source_contexts;
    uint16_t supported_sink_contexts;
    uint16_t supported_source_contexts;
} pacs_remote_info_t;

/** PACS event types */
typedef enum {
    PACS_EVENT_DISCOVERY_COMPLETE,      /**< Remote PACS discovered */
    PACS_EVENT_CONTEXTS_CHANGED,        /**< Remote contexts changed */
    PACS_EVENT_LOCATIONS_CHANGED,       /**< Remote locations changed */
    PACS_EVENT_ERROR
} pacs_event_type_t;

/** PACS event data */
typedef struct {
    pacs_event_type_t type;
    uint16_t conn_handle;
    union {
        pacs_remote_info_t remote_info;
        int error_code;
    } data;
} pacs_event_t;

/** PACS event callback */
typedef void (*pacs_callback_t)(const pacs_event_t *event, void *user_data);

/*******************************************************************************
 * Default Configurations
 ******************************************************************************/

/** Default LC3 capabilities (8-48kHz, both durations, mono/stereo) */
#define PACS_LC3_CAPABILITIES_DEFAULT { \
    .supported_frequencies = PACS_FREQ_ALL_STANDARD, \
    .supported_durations = PACS_DURATION_BOTH_10MS_PREF, \
    .supported_channels = PACS_CHANNELS_MONO_STEREO, \
    .min_octets_per_frame = 26, \
    .max_octets_per_frame = 155, \
    .max_frames_per_sdu = 2 \
}

/** Default LC3 capabilities for music (48kHz, 10ms, 100 octets) */
#define PACS_LC3_CAPABILITIES_MUSIC { \
    .supported_frequencies = PACS_FREQ_48000, \
    .supported_durations = PACS_DURATION_10MS | PACS_DURATION_10MS_PREF, \
    .supported_channels = PACS_CHANNELS_MONO_STEREO, \
    .min_octets_per_frame = 75, \
    .max_octets_per_frame = 155, \
    .max_frames_per_sdu = 1 \
}

/** Default LC3 capabilities for voice (16kHz, 10ms) */
#define PACS_LC3_CAPABILITIES_VOICE { \
    .supported_frequencies = PACS_FREQ_16000 | PACS_FREQ_24000, \
    .supported_durations = PACS_DURATION_BOTH, \
    .supported_channels = PACS_CHANNELS_1, \
    .min_octets_per_frame = 30, \
    .max_octets_per_frame = 60, \
    .max_frames_per_sdu = 1 \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize PACS module
 *
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_init(void);

/**
 * @brief Deinitialize PACS module
 */
void pacs_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void pacs_register_callback(pacs_callback_t callback, void *user_data);

/*******************************************************************************
 * API Functions - Server Role (Local Capabilities)
 ******************************************************************************/

/**
 * @brief Configure PACS server
 *
 * Sets up the local PACS characteristics with specified capabilities.
 *
 * @param config Pointer to PACS configuration
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_configure(const pacs_config_t *config);

/**
 * @brief Add Sink PAC record
 *
 * @param record PAC record to add
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_add_sink_pac(const pacs_pac_record_t *record);

/**
 * @brief Add Source PAC record
 *
 * @param record PAC record to add
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_add_source_pac(const pacs_pac_record_t *record);

/**
 * @brief Add LC3 Sink PAC record
 *
 * Convenience function to add an LC3 codec PAC record.
 *
 * @param capabilities LC3 capabilities
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_add_sink_lc3(const pacs_lc3_capabilities_t *capabilities);

/**
 * @brief Add LC3 Source PAC record
 *
 * @param capabilities LC3 capabilities
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_add_source_lc3(const pacs_lc3_capabilities_t *capabilities);

/**
 * @brief Set sink audio locations
 *
 * @param locations Audio location bitmask
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_set_sink_locations(uint32_t locations);

/**
 * @brief Set source audio locations
 *
 * @param locations Audio location bitmask
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_set_source_locations(uint32_t locations);

/**
 * @brief Set available audio contexts
 *
 * Updates the Available Audio Contexts characteristic.
 * Should be called when contexts become available/unavailable.
 *
 * @param sink_contexts Available sink contexts
 * @param source_contexts Available source contexts
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_set_available_contexts(uint16_t sink_contexts, uint16_t source_contexts);

/**
 * @brief Set supported audio contexts
 *
 * @param sink_contexts Supported sink contexts
 * @param source_contexts Supported source contexts
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_set_supported_contexts(uint16_t sink_contexts, uint16_t source_contexts);

/**
 * @brief Notify context change to connected clients
 *
 * Sends notifications for Available Audio Contexts characteristic.
 *
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_notify_contexts_changed(void);

/*******************************************************************************
 * API Functions - Client Role (Remote Discovery)
 ******************************************************************************/

/**
 * @brief Discover PACS on remote device
 *
 * @param conn_handle ACL connection handle
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_discover(uint16_t conn_handle);

/**
 * @brief Read remote Sink PAC
 *
 * @param conn_handle ACL connection handle
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_read_sink_pac(uint16_t conn_handle);

/**
 * @brief Read remote Source PAC
 *
 * @param conn_handle ACL connection handle
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_read_source_pac(uint16_t conn_handle);

/**
 * @brief Read remote audio locations
 *
 * @param conn_handle ACL connection handle
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_read_audio_locations(uint16_t conn_handle);

/**
 * @brief Read remote audio contexts
 *
 * @param conn_handle ACL connection handle
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_read_audio_contexts(uint16_t conn_handle);

/**
 * @brief Get remote device PACS info
 *
 * @param conn_handle ACL connection handle
 * @param info Pointer to structure to fill
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_get_remote_info(uint16_t conn_handle, pacs_remote_info_t *info);

/*******************************************************************************
 * API Functions - Utilities
 ******************************************************************************/

/**
 * @brief Build PAC record from LC3 capabilities
 *
 * @param capabilities LC3 capabilities
 * @param record Output PAC record
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_build_lc3_pac_record(const pacs_lc3_capabilities_t *capabilities,
                               pacs_pac_record_t *record);

/**
 * @brief Parse PAC record data
 *
 * Parses raw PAC characteristic data into PAC records.
 *
 * @param data Raw PAC data
 * @param len Data length
 * @param records Output array of PAC records
 * @param max_records Maximum records to parse
 * @param num_records Output: number of records parsed
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_parse_pac_data(const uint8_t *data, uint16_t len,
                         pacs_pac_record_t *records, uint8_t max_records,
                         uint8_t *num_records);

/**
 * @brief Check if codec configuration is supported by PAC
 *
 * @param pac PAC record
 * @param freq Sampling frequency code
 * @param duration Frame duration code
 * @param channels Number of channels
 * @param octets Octets per frame
 * @return true if supported
 */
bool pacs_is_config_supported(const pacs_pac_record_t *pac,
                               uint8_t freq, uint8_t duration,
                               uint8_t channels, uint16_t octets);

/**
 * @brief Get preferred codec configuration from PAC
 *
 * Selects the best matching configuration from capabilities.
 *
 * @param pac PAC record
 * @param freq Output: preferred frequency
 * @param duration Output: preferred duration
 * @param channels Output: preferred channels
 * @param octets Output: preferred octets per frame
 * @return PACS_OK on success, negative error code on failure
 */
int pacs_get_preferred_config(const pacs_pac_record_t *pac,
                               uint8_t *freq, uint8_t *duration,
                               uint8_t *channels, uint16_t *octets);

/**
 * @brief Convert frequency bitmask to LC3 frequency code
 *
 * @param freq_bitmask Single frequency bit from supported_frequencies
 * @return LC3 frequency code (for codec config)
 */
uint8_t pacs_freq_bitmask_to_code(uint16_t freq_bitmask);

/**
 * @brief Convert LC3 frequency code to bitmask
 *
 * @param freq_code LC3 frequency code
 * @return Frequency bitmask
 */
uint16_t pacs_freq_code_to_bitmask(uint8_t freq_code);

/**
 * @brief Get frequency value in Hz from bitmask
 *
 * @param freq_bitmask Frequency bitmask
 * @return Frequency in Hz
 */
uint32_t pacs_freq_bitmask_to_hz(uint16_t freq_bitmask);

#ifdef __cplusplus
}
#endif

#endif /* PACS_H */
