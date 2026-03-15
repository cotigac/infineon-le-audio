/**
 * @file audio_task.h
 * @brief Audio Processing Task API
 *
 * This module implements the main FreeRTOS audio processing task that
 * coordinates all audio data flow:
 *
 * Transmit Path (I2S RX -> LC3 Encode -> ISOC TX):
 * ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
 * │   I2S RX    │───►│  PCM Buffer │───►│ LC3 Encode  │───►│  ISOC TX    │
 * │   (DMA)     │    │             │    │             │    │             │
 * └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
 *
 * Receive Path (ISOC RX -> LC3 Decode -> I2S TX):
 * ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
 * │  ISOC RX    │───►│  LC3 Buffer │───►│ LC3 Decode  │───►│   I2S TX    │
 * │             │    │             │    │             │    │   (DMA)     │
 * └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
 *
 * Features:
 * - FreeRTOS task with configurable priority
 * - Frame-synchronized processing (10ms or 7.5ms)
 * - Full-duplex audio support
 * - Automatic buffer management
 * - Statistics and monitoring
 * - Graceful start/stop handling
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_TASK_H
#define AUDIO_TASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Default audio task stack size (in words) */
#define AUDIO_TASK_STACK_SIZE           4096

/** Default audio task priority (high priority for real-time) */
#define AUDIO_TASK_PRIORITY             6

/** Maximum number of audio streams */
#define AUDIO_TASK_MAX_STREAMS          4

/** Default frame duration in microseconds */
#define AUDIO_TASK_FRAME_DURATION_US    10000

/** Audio task notification bits */
#define AUDIO_NOTIFY_I2S_RX_READY       (1 << 0)
#define AUDIO_NOTIFY_I2S_TX_READY       (1 << 1)
#define AUDIO_NOTIFY_ISOC_RX_READY      (1 << 2)
#define AUDIO_NOTIFY_ISOC_TX_READY      (1 << 3)
#define AUDIO_NOTIFY_START              (1 << 4)
#define AUDIO_NOTIFY_STOP               (1 << 5)
#define AUDIO_NOTIFY_CONFIG_CHANGE      (1 << 6)
#define AUDIO_NOTIFY_ERROR              (1 << 7)

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    AUDIO_TASK_OK = 0,
    AUDIO_TASK_ERROR_INVALID_PARAM = -1,
    AUDIO_TASK_ERROR_NOT_INITIALIZED = -2,
    AUDIO_TASK_ERROR_ALREADY_INITIALIZED = -3,
    AUDIO_TASK_ERROR_NO_RESOURCES = -4,
    AUDIO_TASK_ERROR_INVALID_STATE = -5,
    AUDIO_TASK_ERROR_CODEC_ERROR = -6,
    AUDIO_TASK_ERROR_BUFFER_ERROR = -7,
    AUDIO_TASK_ERROR_TASK_CREATE_FAILED = -8
} audio_task_error_t;

/*******************************************************************************
 * Task State
 ******************************************************************************/

typedef enum {
    AUDIO_TASK_STATE_IDLE,          /**< Task created but not processing */
    AUDIO_TASK_STATE_STARTING,      /**< Task starting up */
    AUDIO_TASK_STATE_RUNNING,       /**< Task actively processing audio */
    AUDIO_TASK_STATE_STOPPING,      /**< Task stopping */
    AUDIO_TASK_STATE_ERROR          /**< Task encountered error */
} audio_task_state_t;

/*******************************************************************************
 * Stream Direction
 ******************************************************************************/

typedef enum {
    AUDIO_STREAM_DIRECTION_TX,      /**< Transmit (I2S RX -> LC3 -> ISOC TX) */
    AUDIO_STREAM_DIRECTION_RX,      /**< Receive (ISOC RX -> LC3 -> I2S TX) */
    AUDIO_STREAM_DIRECTION_BIDIR    /**< Bidirectional (full-duplex) */
} audio_stream_direction_t;

/*******************************************************************************
 * Stream Type
 ******************************************************************************/

typedef enum {
    AUDIO_STREAM_TYPE_UNICAST,      /**< CIS unicast stream */
    AUDIO_STREAM_TYPE_BROADCAST     /**< BIS broadcast stream */
} audio_stream_type_t;

/*******************************************************************************
 * Audio Configuration
 ******************************************************************************/

typedef struct {
    uint32_t sample_rate;           /**< Sample rate in Hz (8000-48000) */
    uint8_t channels;               /**< Number of channels (1-2) */
    uint8_t bits_per_sample;        /**< Bits per sample (16, 24, 32) */
    uint16_t frame_duration_us;     /**< Frame duration (7500 or 10000) */
    uint16_t octets_per_frame;      /**< LC3 octets per frame (26-155) */
} audio_stream_config_t;

/*******************************************************************************
 * Stream Handle
 ******************************************************************************/

typedef struct {
    uint8_t stream_id;              /**< Stream identifier */
    audio_stream_direction_t direction;
    audio_stream_type_t type;
    audio_stream_config_t config;
    bool active;                    /**< Stream is active */

    /* Associated handles */
    uint8_t isoc_stream_id;         /**< ISOC handler stream ID */
    uint8_t i2s_channel;            /**< I2S channel (0=left, 1=right) */

    /* For unicast */
    uint16_t conn_handle;           /**< ACL connection handle */
    uint16_t cis_handle;            /**< CIS handle */

    /* For broadcast */
    uint8_t big_handle;             /**< BIG handle */
    uint8_t bis_index;              /**< BIS index */
} audio_stream_t;

/*******************************************************************************
 * Task Statistics
 ******************************************************************************/

typedef struct {
    /* Processing counts */
    uint32_t frames_encoded;        /**< Total frames encoded */
    uint32_t frames_decoded;        /**< Total frames decoded */
    uint32_t frames_dropped_tx;     /**< TX frames dropped */
    uint32_t frames_dropped_rx;     /**< RX frames dropped */

    /* Timing */
    uint32_t max_encode_time_us;    /**< Max encode time */
    uint32_t max_decode_time_us;    /**< Max decode time */
    uint32_t avg_encode_time_us;    /**< Average encode time */
    uint32_t avg_decode_time_us;    /**< Average decode time */

    /* Buffer status */
    uint32_t tx_underruns;          /**< TX buffer underruns */
    uint32_t rx_overruns;           /**< RX buffer overruns */

    /* Errors */
    uint32_t encode_errors;         /**< Encoding errors */
    uint32_t decode_errors;         /**< Decoding errors */
    uint32_t plc_frames;            /**< Packet loss concealment frames */

    /* Task info */
    uint32_t task_wakeups;          /**< Number of task wakeups */
    uint32_t max_latency_us;        /**< Maximum observed latency */
} audio_task_stats_t;

/*******************************************************************************
 * Callback Types
 ******************************************************************************/

/**
 * @brief Callback for audio task state changes
 *
 * @param state New task state
 * @param user_data User context
 */
typedef void (*audio_task_state_cb_t)(audio_task_state_t state, void *user_data);

/**
 * @brief Callback for stream events
 *
 * @param stream_id Stream identifier
 * @param event Event type (start, stop, error)
 * @param user_data User context
 */
typedef void (*audio_stream_event_cb_t)(uint8_t stream_id, int event, void *user_data);

/**
 * @brief Callback for PCM data (optional processing)
 *
 * Called after I2S RX or before I2S TX for custom processing.
 *
 * @param samples PCM sample buffer
 * @param num_samples Number of samples
 * @param channels Number of channels
 * @param direction TX or RX direction
 * @param user_data User context
 */
typedef void (*audio_pcm_callback_t)(int16_t *samples, uint16_t num_samples,
                                      uint8_t channels,
                                      audio_stream_direction_t direction,
                                      void *user_data);

/*******************************************************************************
 * Task Configuration
 ******************************************************************************/

typedef struct {
    /* Task parameters */
    uint32_t stack_size;            /**< Task stack size (words) */
    uint8_t priority;               /**< Task priority */
    const char *task_name;          /**< Task name */

    /* Audio defaults */
    audio_stream_config_t default_config;

    /* Buffer sizes */
    uint8_t pcm_buffer_frames;      /**< PCM buffer depth (frames) */
    uint8_t lc3_buffer_frames;      /**< LC3 buffer depth (frames) */

    /* Callbacks */
    audio_task_state_cb_t state_callback;
    audio_stream_event_cb_t stream_callback;
    audio_pcm_callback_t pcm_callback;
    void *user_data;

    /* Options */
    bool enable_plc;                /**< Enable packet loss concealment */
    bool enable_stats;              /**< Enable statistics collection */
} audio_task_config_t;

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

#define AUDIO_TASK_CONFIG_DEFAULT { \
    .stack_size = AUDIO_TASK_STACK_SIZE, \
    .priority = AUDIO_TASK_PRIORITY, \
    .task_name = "AudioTask", \
    .default_config = { \
        .sample_rate = 48000, \
        .channels = 2, \
        .bits_per_sample = 16, \
        .frame_duration_us = 10000, \
        .octets_per_frame = 100 \
    }, \
    .pcm_buffer_frames = 4, \
    .lc3_buffer_frames = 4, \
    .state_callback = NULL, \
    .stream_callback = NULL, \
    .pcm_callback = NULL, \
    .user_data = NULL, \
    .enable_plc = true, \
    .enable_stats = true \
}

/** Voice call configuration (16kHz mono) */
#define AUDIO_TASK_CONFIG_VOICE { \
    .stack_size = AUDIO_TASK_STACK_SIZE, \
    .priority = AUDIO_TASK_PRIORITY, \
    .task_name = "AudioTask", \
    .default_config = { \
        .sample_rate = 16000, \
        .channels = 1, \
        .bits_per_sample = 16, \
        .frame_duration_us = 10000, \
        .octets_per_frame = 40 \
    }, \
    .pcm_buffer_frames = 4, \
    .lc3_buffer_frames = 4, \
    .state_callback = NULL, \
    .stream_callback = NULL, \
    .pcm_callback = NULL, \
    .user_data = NULL, \
    .enable_plc = true, \
    .enable_stats = true \
}

/** Gaming configuration (48kHz stereo, 7.5ms low latency) */
#define AUDIO_TASK_CONFIG_GAMING { \
    .stack_size = AUDIO_TASK_STACK_SIZE, \
    .priority = AUDIO_TASK_PRIORITY + 1, \
    .task_name = "AudioTask", \
    .default_config = { \
        .sample_rate = 48000, \
        .channels = 2, \
        .bits_per_sample = 16, \
        .frame_duration_us = 7500, \
        .octets_per_frame = 75 \
    }, \
    .pcm_buffer_frames = 3, \
    .lc3_buffer_frames = 3, \
    .state_callback = NULL, \
    .stream_callback = NULL, \
    .pcm_callback = NULL, \
    .user_data = NULL, \
    .enable_plc = true, \
    .enable_stats = true \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize audio task module
 *
 * Creates the FreeRTOS task and initializes all subsystems.
 *
 * @param config Task configuration
 * @return AUDIO_TASK_OK on success
 */
int audio_task_init(const audio_task_config_t *config);

/**
 * @brief Deinitialize audio task module
 *
 * Stops processing and deletes the task.
 */
void audio_task_deinit(void);

/**
 * @brief Check if audio task is initialized
 *
 * @return true if initialized
 */
bool audio_task_is_initialized(void);

/**
 * @brief Get current task state
 *
 * @return Current task state
 */
audio_task_state_t audio_task_get_state(void);

/*******************************************************************************
 * API Functions - Stream Management
 ******************************************************************************/

/**
 * @brief Create audio stream
 *
 * @param direction Stream direction
 * @param type Stream type (unicast/broadcast)
 * @param config Stream configuration
 * @param stream_id Output: assigned stream ID
 * @return AUDIO_TASK_OK on success
 */
int audio_task_create_stream(audio_stream_direction_t direction,
                              audio_stream_type_t type,
                              const audio_stream_config_t *config,
                              uint8_t *stream_id);

/**
 * @brief Destroy audio stream
 *
 * @param stream_id Stream to destroy
 * @return AUDIO_TASK_OK on success
 */
int audio_task_destroy_stream(uint8_t stream_id);

/**
 * @brief Configure stream for unicast
 *
 * @param stream_id Stream ID
 * @param conn_handle ACL connection handle
 * @param cis_handle CIS handle
 * @return AUDIO_TASK_OK on success
 */
int audio_task_configure_unicast(uint8_t stream_id, uint16_t conn_handle,
                                  uint16_t cis_handle);

/**
 * @brief Configure stream for broadcast
 *
 * @param stream_id Stream ID
 * @param big_handle BIG handle
 * @param bis_index BIS index
 * @return AUDIO_TASK_OK on success
 */
int audio_task_configure_broadcast(uint8_t stream_id, uint8_t big_handle,
                                    uint8_t bis_index);

/**
 * @brief Start audio stream
 *
 * @param stream_id Stream to start
 * @return AUDIO_TASK_OK on success
 */
int audio_task_start_stream(uint8_t stream_id);

/**
 * @brief Stop audio stream
 *
 * @param stream_id Stream to stop
 * @return AUDIO_TASK_OK on success
 */
int audio_task_stop_stream(uint8_t stream_id);

/**
 * @brief Get stream information
 *
 * @param stream_id Stream ID
 * @param stream Output stream info
 * @return AUDIO_TASK_OK on success
 */
int audio_task_get_stream_info(uint8_t stream_id, audio_stream_t *stream);

/**
 * @brief Check if stream is active
 *
 * @param stream_id Stream ID
 * @return true if active
 */
bool audio_task_is_stream_active(uint8_t stream_id);

/*******************************************************************************
 * API Functions - Task Control
 ******************************************************************************/

/**
 * @brief Start audio processing
 *
 * Begins processing all configured streams.
 *
 * @return AUDIO_TASK_OK on success
 */
int audio_task_start(void);

/**
 * @brief Stop audio processing
 *
 * Stops all streams and processing.
 *
 * @return AUDIO_TASK_OK on success
 */
int audio_task_stop(void);

/**
 * @brief Suspend audio task
 *
 * Temporarily suspends processing (e.g., for power saving).
 *
 * @return AUDIO_TASK_OK on success
 */
int audio_task_suspend(void);

/**
 * @brief Resume audio task
 *
 * @return AUDIO_TASK_OK on success
 */
int audio_task_resume(void);

/*******************************************************************************
 * API Functions - Notifications (from ISR)
 ******************************************************************************/

/**
 * @brief Notify task that I2S RX data is ready
 *
 * Call from I2S DMA complete ISR.
 */
void audio_task_notify_i2s_rx_ready(void);

/**
 * @brief Notify task that I2S TX buffer needs data
 *
 * Call from I2S DMA complete ISR.
 */
void audio_task_notify_i2s_tx_ready(void);

/**
 * @brief Notify task that ISOC RX data is available
 *
 * Call from ISOC handler when data is received.
 */
void audio_task_notify_isoc_rx_ready(void);

/**
 * @brief Notify task that ISOC TX buffer has space
 *
 * Call from ISOC handler when TX completes.
 */
void audio_task_notify_isoc_tx_ready(void);

/**
 * @brief Generic notification from ISR
 *
 * @param notification_bits Notification flags
 */
void audio_task_notify_from_isr(uint32_t notification_bits);

/*******************************************************************************
 * API Functions - Statistics
 ******************************************************************************/

/**
 * @brief Get task statistics
 *
 * @param stats Output statistics
 * @return AUDIO_TASK_OK on success
 */
int audio_task_get_stats(audio_task_stats_t *stats);

/**
 * @brief Reset task statistics
 *
 * @return AUDIO_TASK_OK on success
 */
int audio_task_reset_stats(void);

/**
 * @brief Get CPU usage percentage
 *
 * @return CPU usage (0-100)
 */
uint8_t audio_task_get_cpu_usage(void);

/*******************************************************************************
 * API Functions - Configuration
 ******************************************************************************/

/**
 * @brief Update default audio configuration
 *
 * @param config New default configuration
 * @return AUDIO_TASK_OK on success
 */
int audio_task_set_default_config(const audio_stream_config_t *config);

/**
 * @brief Get current default configuration
 *
 * @param config Output configuration
 * @return AUDIO_TASK_OK on success
 */
int audio_task_get_default_config(audio_stream_config_t *config);

/**
 * @brief Set volume level
 *
 * @param volume_percent Volume (0-100)
 * @return AUDIO_TASK_OK on success
 */
int audio_task_set_volume(uint8_t volume_percent);

/**
 * @brief Get volume level
 *
 * @return Volume (0-100)
 */
uint8_t audio_task_get_volume(void);

/**
 * @brief Mute/unmute audio
 *
 * @param mute true to mute
 * @return AUDIO_TASK_OK on success
 */
int audio_task_set_mute(bool mute);

/**
 * @brief Check if muted
 *
 * @return true if muted
 */
bool audio_task_is_muted(void);

/*******************************************************************************
 * API Functions - Debug
 ******************************************************************************/

/**
 * @brief Get task handle for debugging
 *
 * @return FreeRTOS task handle
 */
void* audio_task_get_handle(void);

/**
 * @brief Print debug information
 */
void audio_task_print_debug(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_TASK_H */
