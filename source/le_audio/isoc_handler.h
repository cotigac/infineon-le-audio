/**
 * @file isoc_handler.h
 * @brief Isochronous Data Path Handler API
 *
 * This module provides high-level management of isochronous audio data
 * flow between the LC3 codec and HCI layer. It handles:
 *
 * - ISO data path setup and teardown
 * - Audio frame buffering with timing management
 * - TX/RX synchronization for full-duplex streams
 * - Integration with LC3 encoder/decoder
 * - Statistics and quality monitoring
 *
 * Architecture:
 * ┌─────────────┐    ┌──────────────┐    ┌─────────────┐
 * │ Audio Task  │◄──►│ ISOC Handler │◄──►│  HCI ISOC   │
 * │ (LC3 codec) │    │ (this module)│    │ (hci_isoc)  │
 * └─────────────┘    └──────────────┘    └─────────────┘
 *        │                  │                   │
 *        │   PCM frames     │   LC3 frames      │   HCI packets
 *        └──────────────────┴───────────────────┘
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ISOC_HANDLER_H
#define ISOC_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum number of ISO streams */
#define ISOC_HANDLER_MAX_STREAMS        8

/** Maximum LC3 frame size (155 octets for high quality) */
#define ISOC_HANDLER_MAX_FRAME_SIZE     155

/** Maximum frames per SDU */
#define ISOC_HANDLER_MAX_FRAMES_PER_SDU 2

/** Maximum SDU size */
#define ISOC_HANDLER_MAX_SDU_SIZE       (ISOC_HANDLER_MAX_FRAME_SIZE * ISOC_HANDLER_MAX_FRAMES_PER_SDU)

/** Default buffer depth (number of SDUs) */
#define ISOC_HANDLER_DEFAULT_BUFFER_DEPTH   4

/** Maximum buffer depth */
#define ISOC_HANDLER_MAX_BUFFER_DEPTH       8

/*******************************************************************************
 * Data Path Directions
 ******************************************************************************/

typedef enum {
    ISOC_PATH_DIRECTION_TX      = 0x00,     /**< Host to Controller (transmit) */
    ISOC_PATH_DIRECTION_RX      = 0x01,     /**< Controller to Host (receive) */
    ISOC_PATH_DIRECTION_BIDIR   = 0x02      /**< Bidirectional (full-duplex) */
} isoc_path_direction_t;

/*******************************************************************************
 * Stream Types
 ******************************************************************************/

typedef enum {
    ISOC_STREAM_TYPE_CIS,           /**< Connected Isochronous Stream (unicast) */
    ISOC_STREAM_TYPE_BIS_SOURCE,    /**< Broadcast Isochronous Stream (source) */
    ISOC_STREAM_TYPE_BIS_SINK       /**< Broadcast Isochronous Stream (sink) */
} isoc_stream_type_t;

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    ISOC_HANDLER_OK = 0,
    ISOC_HANDLER_ERROR_INVALID_PARAM = -1,
    ISOC_HANDLER_ERROR_NOT_INITIALIZED = -2,
    ISOC_HANDLER_ERROR_ALREADY_INITIALIZED = -3,
    ISOC_HANDLER_ERROR_NO_RESOURCES = -4,
    ISOC_HANDLER_ERROR_NOT_FOUND = -5,
    ISOC_HANDLER_ERROR_STREAM_ACTIVE = -6,
    ISOC_HANDLER_ERROR_BUFFER_FULL = -7,
    ISOC_HANDLER_ERROR_BUFFER_EMPTY = -8,
    ISOC_HANDLER_ERROR_INVALID_STATE = -9,
    ISOC_HANDLER_ERROR_HCI_ERROR = -10
} isoc_handler_error_t;

/*******************************************************************************
 * Stream State
 ******************************************************************************/

typedef enum {
    ISOC_STREAM_STATE_IDLE,             /**< Stream not configured */
    ISOC_STREAM_STATE_CONFIGURED,       /**< Data path configured */
    ISOC_STREAM_STATE_STARTING,         /**< Stream starting */
    ISOC_STREAM_STATE_ACTIVE,           /**< Stream active, data flowing */
    ISOC_STREAM_STATE_STOPPING,         /**< Stream stopping */
    ISOC_STREAM_STATE_ERROR             /**< Stream error */
} isoc_stream_state_t;

/*******************************************************************************
 * LC3 Frame Configuration
 ******************************************************************************/

typedef struct {
    uint32_t sample_rate;           /**< Sample rate in Hz (8000-48000) */
    uint16_t frame_duration_us;     /**< Frame duration: 7500 or 10000 us */
    uint16_t octets_per_frame;      /**< Octets per LC3 frame (26-155) */
    uint8_t  frames_per_sdu;        /**< LC3 frames per SDU (1-2) */
    uint8_t  channels;              /**< Number of audio channels (1-2) */
} isoc_lc3_config_t;

/*******************************************************************************
 * Stream Configuration
 ******************************************************************************/

typedef struct {
    isoc_stream_type_t type;        /**< Stream type (CIS/BIS) */
    isoc_path_direction_t direction; /**< Data direction */
    uint16_t iso_handle;            /**< ISO connection handle (CIS or BIS) */

    /* LC3 configuration */
    isoc_lc3_config_t lc3_config;

    /* Timing */
    uint32_t sdu_interval_us;       /**< SDU interval in microseconds */
    uint32_t transport_latency_us;  /**< Transport latency */
    uint32_t presentation_delay_us; /**< Presentation delay */

    /* Buffering */
    uint8_t buffer_depth;           /**< Number of SDUs to buffer */

    /* For CIS: connection handle */
    uint16_t acl_handle;            /**< ACL connection handle (for CIS) */

    /* For BIS: BIG/BIS info */
    uint8_t big_handle;             /**< BIG handle (for BIS) */
    uint8_t bis_index;              /**< BIS index within BIG */
} isoc_stream_config_t;

/*******************************************************************************
 * Stream Statistics
 ******************************************************************************/

typedef struct {
    /* TX statistics */
    uint32_t tx_frames;             /**< Total frames transmitted */
    uint32_t tx_bytes;              /**< Total bytes transmitted */
    uint32_t tx_underruns;          /**< TX buffer underruns */
    uint32_t tx_late_frames;        /**< Frames sent late */

    /* RX statistics */
    uint32_t rx_frames;             /**< Total frames received */
    uint32_t rx_bytes;              /**< Total bytes received */
    uint32_t rx_overruns;           /**< RX buffer overruns */
    uint32_t rx_lost_frames;        /**< Lost/missed frames */
    uint32_t rx_crc_errors;         /**< CRC errors detected */

    /* Timing */
    uint32_t max_jitter_us;         /**< Maximum observed jitter */
    uint32_t avg_latency_us;        /**< Average latency */

    /* Quality */
    uint8_t flush_count;            /**< Number of flush events */
    uint8_t nak_count;              /**< NAK events (for CIS) */
} isoc_stream_stats_t;

/*******************************************************************************
 * SDU (Service Data Unit) Structure
 ******************************************************************************/

typedef struct {
    uint8_t data[ISOC_HANDLER_MAX_SDU_SIZE];  /**< SDU payload (LC3 frames) */
    uint16_t length;                /**< Actual payload length */
    uint16_t sequence_number;       /**< Packet sequence number */
    uint32_t timestamp;             /**< SDU timestamp */
    uint8_t num_frames;             /**< Number of LC3 frames in SDU */
    bool valid;                     /**< Data is valid */
} isoc_sdu_t;

/*******************************************************************************
 * Callback Types
 ******************************************************************************/

/**
 * @brief Callback when TX data is needed
 *
 * Called when the handler needs LC3 data for transmission.
 * Application should provide encoded LC3 frames.
 *
 * @param stream_id Stream identifier
 * @param sdu Output SDU to fill with LC3 data
 * @param user_data User context
 * @return true if data provided, false if no data available
 */
typedef bool (*isoc_tx_data_callback_t)(uint8_t stream_id, isoc_sdu_t *sdu,
                                         void *user_data);

/**
 * @brief Callback when RX data is available
 *
 * Called when LC3 data has been received and is ready for decoding.
 *
 * @param stream_id Stream identifier
 * @param sdu Received SDU containing LC3 frames
 * @param user_data User context
 */
typedef void (*isoc_rx_data_callback_t)(uint8_t stream_id, const isoc_sdu_t *sdu,
                                         void *user_data);

/**
 * @brief Callback for stream state changes
 *
 * @param stream_id Stream identifier
 * @param state New stream state
 * @param user_data User context
 */
typedef void (*isoc_state_callback_t)(uint8_t stream_id, isoc_stream_state_t state,
                                       void *user_data);

/**
 * @brief Callback for stream errors
 *
 * @param stream_id Stream identifier
 * @param error Error code
 * @param user_data User context
 */
typedef void (*isoc_error_callback_t)(uint8_t stream_id, int error,
                                       void *user_data);

/*******************************************************************************
 * Handler Configuration
 ******************************************************************************/

typedef struct {
    isoc_tx_data_callback_t tx_callback;    /**< TX data request callback */
    isoc_rx_data_callback_t rx_callback;    /**< RX data ready callback */
    isoc_state_callback_t state_callback;   /**< State change callback */
    isoc_error_callback_t error_callback;   /**< Error callback */
    void *user_data;                        /**< User context for callbacks */
} isoc_handler_config_t;

/*******************************************************************************
 * Stream Handle (opaque)
 ******************************************************************************/

typedef struct isoc_stream_s isoc_stream_t;

/*******************************************************************************
 * Default Configurations
 ******************************************************************************/

/** Default LC3 config for high quality music (48kHz, 10ms, 100 octets) */
#define ISOC_LC3_CONFIG_MUSIC_48K { \
    .sample_rate = 48000, \
    .frame_duration_us = 10000, \
    .octets_per_frame = 100, \
    .frames_per_sdu = 1, \
    .channels = 2 \
}

/** Default LC3 config for voice (16kHz, 10ms, 40 octets) */
#define ISOC_LC3_CONFIG_VOICE_16K { \
    .sample_rate = 16000, \
    .frame_duration_us = 10000, \
    .octets_per_frame = 40, \
    .frames_per_sdu = 1, \
    .channels = 1 \
}

/** Default LC3 config for low latency gaming (48kHz, 7.5ms) */
#define ISOC_LC3_CONFIG_GAMING_48K { \
    .sample_rate = 48000, \
    .frame_duration_us = 7500, \
    .octets_per_frame = 75, \
    .frames_per_sdu = 1, \
    .channels = 2 \
}

/** Default LC3 config for broadcast (48kHz, 10ms, mono) */
#define ISOC_LC3_CONFIG_BROADCAST { \
    .sample_rate = 48000, \
    .frame_duration_us = 10000, \
    .octets_per_frame = 100, \
    .frames_per_sdu = 1, \
    .channels = 1 \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize ISOC handler module
 *
 * @param config Handler configuration with callbacks
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_init(const isoc_handler_config_t *config);

/**
 * @brief Deinitialize ISOC handler module
 */
void isoc_handler_deinit(void);

/**
 * @brief Check if handler is initialized
 *
 * @return true if initialized
 */
bool isoc_handler_is_initialized(void);

/*******************************************************************************
 * API Functions - Stream Management
 ******************************************************************************/

/**
 * @brief Create an ISO stream
 *
 * Allocates and configures a new ISO stream.
 *
 * @param config Stream configuration
 * @param stream_id Output: assigned stream ID
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_create_stream(const isoc_stream_config_t *config,
                                uint8_t *stream_id);

/**
 * @brief Destroy an ISO stream
 *
 * @param stream_id Stream to destroy
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_destroy_stream(uint8_t stream_id);

/**
 * @brief Start an ISO stream
 *
 * Begins data flow on the stream.
 *
 * @param stream_id Stream to start
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_start_stream(uint8_t stream_id);

/**
 * @brief Stop an ISO stream
 *
 * @param stream_id Stream to stop
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_stop_stream(uint8_t stream_id);

/**
 * @brief Get stream state
 *
 * @param stream_id Stream ID
 * @return Current stream state
 */
isoc_stream_state_t isoc_handler_get_stream_state(uint8_t stream_id);

/**
 * @brief Get stream statistics
 *
 * @param stream_id Stream ID
 * @param stats Output statistics structure
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_get_stream_stats(uint8_t stream_id, isoc_stream_stats_t *stats);

/**
 * @brief Reset stream statistics
 *
 * @param stream_id Stream ID
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_reset_stream_stats(uint8_t stream_id);

/*******************************************************************************
 * API Functions - Data Path Setup
 ******************************************************************************/

/**
 * @brief Setup ISO data path for stream
 *
 * Configures the HCI ISO data path for the specified stream.
 * Must be called after CIS/BIS is established.
 *
 * @param stream_id Stream ID
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_setup_data_path(uint8_t stream_id);

/**
 * @brief Remove ISO data path for stream
 *
 * @param stream_id Stream ID
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_remove_data_path(uint8_t stream_id);

/*******************************************************************************
 * API Functions - Data Transmission (TX)
 ******************************************************************************/

/**
 * @brief Queue LC3 frame for transmission
 *
 * Queues an encoded LC3 frame for transmission on the stream.
 *
 * @param stream_id Stream ID
 * @param lc3_data LC3 encoded frame data
 * @param lc3_len Frame length in bytes
 * @param timestamp Optional timestamp (0 for auto)
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_tx_frame(uint8_t stream_id, const uint8_t *lc3_data,
                           uint16_t lc3_len, uint32_t timestamp);

/**
 * @brief Queue complete SDU for transmission
 *
 * @param stream_id Stream ID
 * @param sdu SDU to transmit
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_tx_sdu(uint8_t stream_id, const isoc_sdu_t *sdu);

/**
 * @brief Get TX buffer level
 *
 * @param stream_id Stream ID
 * @return Number of SDUs in TX buffer, or negative on error
 */
int isoc_handler_get_tx_buffer_level(uint8_t stream_id);

/**
 * @brief Check if TX buffer has space
 *
 * @param stream_id Stream ID
 * @return true if space available
 */
bool isoc_handler_tx_buffer_has_space(uint8_t stream_id);

/*******************************************************************************
 * API Functions - Data Reception (RX)
 ******************************************************************************/

/**
 * @brief Get received LC3 frame
 *
 * Retrieves the next received LC3 frame from the buffer.
 *
 * @param stream_id Stream ID
 * @param lc3_data Output buffer for LC3 data
 * @param max_len Maximum buffer size
 * @param lc3_len Output: actual data length
 * @param timestamp Output: frame timestamp
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_rx_frame(uint8_t stream_id, uint8_t *lc3_data,
                           uint16_t max_len, uint16_t *lc3_len,
                           uint32_t *timestamp);

/**
 * @brief Get received SDU
 *
 * @param stream_id Stream ID
 * @param sdu Output SDU structure
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_rx_sdu(uint8_t stream_id, isoc_sdu_t *sdu);

/**
 * @brief Get RX buffer level
 *
 * @param stream_id Stream ID
 * @return Number of SDUs in RX buffer, or negative on error
 */
int isoc_handler_get_rx_buffer_level(uint8_t stream_id);

/**
 * @brief Check if RX data is available
 *
 * @param stream_id Stream ID
 * @return true if data available
 */
bool isoc_handler_rx_data_available(uint8_t stream_id);

/*******************************************************************************
 * API Functions - Timing and Synchronization
 ******************************************************************************/

/**
 * @brief Get current ISO timestamp
 *
 * Returns the current Bluetooth ISO timestamp for synchronization.
 *
 * @return Current timestamp in microseconds
 */
uint32_t isoc_handler_get_timestamp(void);

/**
 * @brief Get presentation time for next TX frame
 *
 * Calculates when the next frame should be presented for transmission.
 *
 * @param stream_id Stream ID
 * @return Presentation timestamp
 */
uint32_t isoc_handler_get_next_tx_time(uint8_t stream_id);

/**
 * @brief Synchronize stream timing
 *
 * Adjusts stream timing based on received timestamps.
 * Used for clock recovery in receiver mode.
 *
 * @param stream_id Stream ID
 * @param reference_timestamp Reference timestamp from remote
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_sync_timing(uint8_t stream_id, uint32_t reference_timestamp);

/*******************************************************************************
 * API Functions - Multi-Stream Coordination
 ******************************************************************************/

/**
 * @brief Link multiple streams for synchronized operation
 *
 * Links streams to operate together (e.g., left and right stereo channels).
 *
 * @param stream_ids Array of stream IDs to link
 * @param count Number of streams
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_link_streams(const uint8_t *stream_ids, uint8_t count);

/**
 * @brief Unlink streams
 *
 * @param stream_ids Array of stream IDs to unlink
 * @param count Number of streams
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_unlink_streams(const uint8_t *stream_ids, uint8_t count);

/**
 * @brief Start multiple linked streams simultaneously
 *
 * @param stream_ids Array of stream IDs
 * @param count Number of streams
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_start_linked_streams(const uint8_t *stream_ids, uint8_t count);

/**
 * @brief Stop multiple linked streams simultaneously
 *
 * @param stream_ids Array of stream IDs
 * @param count Number of streams
 * @return ISOC_HANDLER_OK on success
 */
int isoc_handler_stop_linked_streams(const uint8_t *stream_ids, uint8_t count);

/*******************************************************************************
 * API Functions - Stream Lookup
 ******************************************************************************/

/**
 * @brief Find stream by ISO handle
 *
 * @param iso_handle ISO connection handle
 * @return Stream ID, or negative error code
 */
int isoc_handler_find_by_iso_handle(uint16_t iso_handle);

/**
 * @brief Find stream by ACL handle (for CIS)
 *
 * @param acl_handle ACL connection handle
 * @return Stream ID, or negative error code
 */
int isoc_handler_find_by_acl_handle(uint16_t acl_handle);

/**
 * @brief Find stream by BIG/BIS (for BIS)
 *
 * @param big_handle BIG handle
 * @param bis_index BIS index
 * @return Stream ID, or negative error code
 */
int isoc_handler_find_by_big_bis(uint8_t big_handle, uint8_t bis_index);

/*******************************************************************************
 * API Functions - Processing (call from task context)
 ******************************************************************************/

/**
 * @brief Process pending TX data
 *
 * Should be called periodically from the audio task to process TX buffers.
 * This function requests data via callback and sends to HCI.
 */
void isoc_handler_process_tx(void);

/**
 * @brief Process pending RX data
 *
 * Should be called periodically from the audio task to process RX buffers.
 * This function delivers received data via callback.
 */
void isoc_handler_process_rx(void);

/**
 * @brief Process all streams
 *
 * Combined TX and RX processing.
 */
void isoc_handler_process(void);

/*******************************************************************************
 * API Functions - HCI Event Handlers (called from BT stack)
 ******************************************************************************/

/**
 * @brief Handle HCI ISO data received event
 *
 * Called by HCI layer when ISO data packet is received.
 *
 * @param iso_handle ISO handle
 * @param data Received data
 * @param len Data length
 * @param timestamp Packet timestamp
 * @param seq_num Sequence number
 * @param status Packet status
 */
void isoc_handler_on_data_received(uint16_t iso_handle, const uint8_t *data,
                                    uint16_t len, uint32_t timestamp,
                                    uint16_t seq_num, uint8_t status);

/**
 * @brief Handle ISO data TX complete
 *
 * Called when ISO data has been transmitted.
 *
 * @param iso_handle ISO handle
 * @param num_completed Number of completed packets
 */
void isoc_handler_on_tx_complete(uint16_t iso_handle, uint8_t num_completed);

/**
 * @brief Handle CIS established event
 *
 * @param acl_handle ACL handle
 * @param cis_handle CIS handle
 */
void isoc_handler_on_cis_established(uint16_t acl_handle, uint16_t cis_handle);

/**
 * @brief Handle CIS disconnected event
 *
 * @param cis_handle CIS handle
 * @param reason Disconnect reason
 */
void isoc_handler_on_cis_disconnected(uint16_t cis_handle, uint8_t reason);

/**
 * @brief Handle BIG created event
 *
 * @param big_handle BIG handle
 * @param bis_handles Array of BIS handles
 * @param num_bis Number of BIS handles
 */
void isoc_handler_on_big_created(uint8_t big_handle, const uint16_t *bis_handles,
                                  uint8_t num_bis);

/**
 * @brief Handle BIG terminated event
 *
 * @param big_handle BIG handle
 * @param reason Termination reason
 */
void isoc_handler_on_big_terminated(uint8_t big_handle, uint8_t reason);

/**
 * @brief Handle BIG sync established (for sink)
 *
 * @param big_handle BIG handle
 * @param bis_handles Array of synced BIS handles
 * @param num_bis Number of BIS handles
 */
void isoc_handler_on_big_sync_established(uint8_t big_handle,
                                           const uint16_t *bis_handles,
                                           uint8_t num_bis);

/**
 * @brief Handle BIG sync lost (for sink)
 *
 * @param big_handle BIG handle
 * @param reason Reason for sync loss
 */
void isoc_handler_on_big_sync_lost(uint8_t big_handle, uint8_t reason);

#ifdef __cplusplus
}
#endif

#endif /* ISOC_HANDLER_H */
