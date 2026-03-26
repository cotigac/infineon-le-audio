/**
 * @file le_audio_manager.h
 * @brief LE Audio Manager - Top-level Control Interface
 *
 * This module provides the main control interface for LE Audio functionality
 * including unicast streaming (CIS) and broadcast (BIS/Auracast).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LE_AUDIO_MANAGER_H
#define LE_AUDIO_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** LE Audio operating mode */
typedef enum {
    LE_AUDIO_MODE_IDLE = 0,         /**< No audio streaming */
    LE_AUDIO_MODE_UNICAST_SOURCE,   /**< Unicast audio source (CIS) */
    LE_AUDIO_MODE_UNICAST_SINK,     /**< Unicast audio sink (CIS) */
    LE_AUDIO_MODE_UNICAST_DUPLEX,   /**< Unicast full-duplex (CIS) */
    LE_AUDIO_MODE_BROADCAST_SOURCE, /**< Broadcast source (BIS/Auracast) */
    LE_AUDIO_MODE_BROADCAST_SINK    /**< Broadcast sink (BIS) */
} le_audio_mode_t;

/** LE Audio stream state */
typedef enum {
    LE_AUDIO_STATE_IDLE = 0,
    LE_AUDIO_STATE_CONFIGURED,
    LE_AUDIO_STATE_QOS_CONFIGURED,
    LE_AUDIO_STATE_ENABLING,
    LE_AUDIO_STATE_STREAMING,
    LE_AUDIO_STATE_DISABLING,
    LE_AUDIO_STATE_ERROR
} le_audio_state_t;

/** Audio context types (per BAP specification) */
typedef enum {
    LE_AUDIO_CONTEXT_UNSPECIFIED     = 0x0001,
    LE_AUDIO_CONTEXT_CONVERSATIONAL  = 0x0002,
    LE_AUDIO_CONTEXT_MEDIA           = 0x0004,
    LE_AUDIO_CONTEXT_GAME            = 0x0008,
    LE_AUDIO_CONTEXT_INSTRUCTIONAL   = 0x0010,
    LE_AUDIO_CONTEXT_VOICE_ASSISTANT = 0x0020,
    LE_AUDIO_CONTEXT_LIVE            = 0x0040,
    LE_AUDIO_CONTEXT_SOUND_EFFECTS   = 0x0080,
    LE_AUDIO_CONTEXT_NOTIFICATIONS   = 0x0100,
    LE_AUDIO_CONTEXT_RINGTONE        = 0x0200,
    LE_AUDIO_CONTEXT_ALERTS          = 0x0400,
    LE_AUDIO_CONTEXT_EMERGENCY       = 0x0800
} le_audio_context_t;

/** Broadcast configuration */
typedef struct {
    uint8_t broadcast_id[3];        /**< 3-byte broadcast ID */
    uint8_t broadcast_code[16];     /**< Encryption key (optional) */
    bool encrypted;                 /**< Enable broadcast encryption */
    char broadcast_name[32];        /**< Human-readable name */
    uint16_t audio_context;         /**< Audio context bitmask */
    uint8_t num_subgroups;          /**< Number of subgroups */
    uint8_t num_bis_per_subgroup;   /**< BIS per subgroup */
    uint32_t presentation_delay_us; /**< Presentation delay in microseconds */
    uint8_t target_latency_ms;      /**< Target latency in milliseconds */
    uint8_t retransmissions;        /**< Number of retransmissions (RTN) */
} le_audio_broadcast_config_t;

/** Unicast configuration */
typedef struct {
    uint16_t conn_handle;           /**< ACL connection handle */
    uint8_t ase_id;                 /**< ASE identifier on remote device */
    uint16_t audio_context;         /**< Audio context bitmask */
    uint8_t target_latency_ms;      /**< Target latency in milliseconds */
    uint8_t retransmissions;        /**< Number of retransmissions (RTN) */
    uint32_t presentation_delay_us; /**< Presentation delay in microseconds */
    bool bidirectional;             /**< Enable full-duplex */
} le_audio_unicast_config_t;

/** Codec configuration (LC3) */
typedef struct {
    uint32_t sample_rate;           /**< Sample rate in Hz */
    uint16_t frame_duration_us;     /**< Frame duration (7500 or 10000) */
    uint16_t octets_per_frame;      /**< Encoded frame size */
    uint8_t channels;               /**< Number of channels */
} le_audio_codec_config_t;

/** LE Audio event types */
typedef enum {
    LE_AUDIO_EVENT_STATE_CHANGED,
    LE_AUDIO_EVENT_STREAM_STARTED,
    LE_AUDIO_EVENT_STREAM_STOPPED,
    LE_AUDIO_EVENT_DEVICE_CONNECTED,
    LE_AUDIO_EVENT_DEVICE_DISCONNECTED,
    LE_AUDIO_EVENT_ERROR
} le_audio_event_type_t;

/** LE Audio event data */
typedef struct {
    le_audio_event_type_t type;
    union {
        le_audio_state_t new_state;
        int error_code;
    } data;
} le_audio_event_t;

/** Event callback function type */
typedef void (*le_audio_event_callback_t)(const le_audio_event_t *event, void *user_data);

/*******************************************************************************
 * Default Configurations
 ******************************************************************************/

/** Default LC3 codec configuration */
#define LE_AUDIO_CODEC_CONFIG_DEFAULT { \
    .sample_rate = 48000,               \
    .frame_duration_us = 10000,         \
    .octets_per_frame = 100,            \
    .channels = 1                       \
}

/** Default broadcast configuration */
#define LE_AUDIO_BROADCAST_CONFIG_DEFAULT { \
    .broadcast_id = {0x01, 0x02, 0x03},     \
    .encrypted = false,                      \
    .broadcast_name = "Infineon LE Audio",   \
    .audio_context = LE_AUDIO_CONTEXT_MEDIA, \
    .num_subgroups = 1,                      \
    .num_bis_per_subgroup = 1,               \
    .presentation_delay_us = 40000           \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize the LE Audio manager
 *
 * @param codec_config Pointer to LC3 codec configuration
 * @return 0 on success, negative error code on failure
 */
int le_audio_init(const le_audio_codec_config_t *codec_config);

/**
 * @brief Deinitialize the LE Audio manager
 */
void le_audio_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback  Function to call on events
 * @param user_data User data passed to callback
 */
void le_audio_register_callback(le_audio_event_callback_t callback, void *user_data);

/**
 * @brief Get current LE Audio state
 *
 * @return Current state
 */
le_audio_state_t le_audio_get_state(void);

/**
 * @brief Get current operating mode
 *
 * @return Current mode
 */
le_audio_mode_t le_audio_get_mode(void);

/*******************************************************************************
 * Unicast API
 ******************************************************************************/

/**
 * @brief Start unicast audio streaming
 *
 * @param config Pointer to unicast configuration
 * @return 0 on success, negative error code on failure
 */
int le_audio_unicast_start(const le_audio_unicast_config_t *config);

/**
 * @brief Stop unicast audio streaming
 *
 * @return 0 on success, negative error code on failure
 */
int le_audio_unicast_stop(void);

/*******************************************************************************
 * Broadcast (Auracast) API
 ******************************************************************************/

/**
 * @brief Start broadcast audio (Auracast)
 *
 * @param config Pointer to broadcast configuration
 * @return 0 on success, negative error code on failure
 */
int le_audio_broadcast_start(const le_audio_broadcast_config_t *config);

/**
 * @brief Stop broadcast audio
 *
 * @return 0 on success, negative error code on failure
 */
int le_audio_broadcast_stop(void);

/**
 * @brief Update broadcast metadata
 *
 * @param name New broadcast name (or NULL to keep current)
 * @param context New audio context (or 0 to keep current)
 * @return 0 on success, negative error code on failure
 */
int le_audio_broadcast_update_metadata(const char *name, uint16_t context);

/*******************************************************************************
 * Broadcast Sink (Auracast RX) API
 ******************************************************************************/

/**
 * @brief Start scanning for Auracast broadcasts
 *
 * Discovered broadcasts will be reported via LE_AUDIO_EVENT_BROADCAST_FOUND.
 *
 * @return 0 on success, negative error code on failure
 */
int le_audio_broadcast_sink_start_scan(void);

/**
 * @brief Stop scanning for broadcasts
 *
 * @return 0 on success, negative error code on failure
 */
int le_audio_broadcast_sink_stop_scan(void);

/**
 * @brief Sync to a discovered broadcast and start receiving audio
 *
 * @param broadcast_id 3-byte Broadcast_ID of the source
 * @param broadcast_code 16-byte decryption key (NULL if unencrypted)
 * @return 0 on success, negative error code on failure
 */
int le_audio_broadcast_sink_sync(const uint8_t *broadcast_id,
                                  const uint8_t *broadcast_code);

/**
 * @brief Stop receiving broadcast audio
 *
 * @return 0 on success, negative error code on failure
 */
int le_audio_broadcast_sink_stop(void);

/**
 * @brief Demo: Auto-sync to first discovered broadcast
 *
 * Convenience function that starts scanning and automatically syncs
 * to the first Auracast broadcast found. Useful for quick testing.
 *
 * @param broadcast_code 16-byte key for encrypted broadcasts (NULL if unencrypted)
 * @return 0 on success, negative error code on failure
 */
int le_audio_broadcast_sink_demo_auto_sync(const uint8_t *broadcast_code);

/*******************************************************************************
 * Audio Data API
 ******************************************************************************/

/**
 * @brief Send PCM audio data for encoding and transmission
 *
 * @param pcm_data    PCM samples (signed 16-bit)
 * @param sample_count Number of samples
 * @return 0 on success, negative error code on failure
 */
int le_audio_send_audio(const int16_t *pcm_data, uint16_t sample_count);

/**
 * @brief Receive decoded PCM audio data
 *
 * @param pcm_data     Buffer for PCM samples
 * @param sample_count Maximum samples to receive
 * @param timeout_ms   Timeout in milliseconds
 * @return Number of samples received, or negative error code
 */
int le_audio_receive_audio(int16_t *pcm_data, uint16_t sample_count, uint32_t timeout_ms);

/**
 * @brief Process LE Audio state machine
 *
 * Non-blocking function to process pending LE Audio events and state changes.
 * Call this periodically from a FreeRTOS task.
 */
void le_audio_process(void);

#ifdef __cplusplus
}
#endif

#endif /* LE_AUDIO_MANAGER_H */
