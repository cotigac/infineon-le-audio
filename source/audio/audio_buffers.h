/**
 * @file audio_buffers.h
 * @brief Audio Buffer Management API
 *
 * This module provides thread-safe ring buffers for audio data flow:
 *
 * - PCM buffers for I2S audio (16/24/32-bit samples)
 * - LC3 frame buffers for encoded audio
 * - Double-buffering support for DMA operations
 * - Timestamp tracking for synchronization
 * - Statistics for underrun/overrun monitoring
 *
 * Buffer Flow:
 * ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
 * │   I2S RX    │───►│  PCM Ring   │───►│ LC3 Encoder │
 * │   (DMA)     │    │   Buffer    │    │             │
 * └─────────────┘    └─────────────┘    └─────────────┘
 *                                              │
 *                                              ▼
 *                                       ┌─────────────┐
 *                                       │  LC3 Ring   │
 *                                       │   Buffer    │
 *                                       └─────────────┘
 *                                              │
 *                                              ▼
 *                                       ┌─────────────┐
 *                                       │  HCI ISOC   │
 *                                       │    TX       │
 *                                       └─────────────┘
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_BUFFERS_H
#define AUDIO_BUFFERS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum PCM frame size (48kHz stereo, 10ms = 960 samples * 2 channels * 4 bytes) */
#define AUDIO_PCM_MAX_FRAME_SIZE        7680

/** Maximum LC3 frame size (155 octets for high quality) */
#define AUDIO_LC3_MAX_FRAME_SIZE        155

/** Default PCM buffer depth (number of frames) */
#define AUDIO_PCM_DEFAULT_DEPTH         8

/** Default LC3 buffer depth (number of frames) */
#define AUDIO_LC3_DEFAULT_DEPTH         8

/** Maximum buffer depth */
#define AUDIO_MAX_BUFFER_DEPTH          32

/** Maximum number of buffer instances */
#define AUDIO_MAX_BUFFER_INSTANCES      8

/*******************************************************************************
 * Sample Formats
 ******************************************************************************/

typedef enum {
    AUDIO_FORMAT_S16_LE,        /**< Signed 16-bit little-endian */
    AUDIO_FORMAT_S24_LE,        /**< Signed 24-bit little-endian (in 32-bit container) */
    AUDIO_FORMAT_S32_LE,        /**< Signed 32-bit little-endian */
    AUDIO_FORMAT_S24_3LE        /**< Signed 24-bit packed (3 bytes per sample) */
} audio_sample_format_t;

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    AUDIO_BUF_OK = 0,
    AUDIO_BUF_ERROR_INVALID_PARAM = -1,
    AUDIO_BUF_ERROR_NOT_INITIALIZED = -2,
    AUDIO_BUF_ERROR_NO_RESOURCES = -3,
    AUDIO_BUF_ERROR_FULL = -4,
    AUDIO_BUF_ERROR_EMPTY = -5,
    AUDIO_BUF_ERROR_INVALID_SIZE = -6,
    AUDIO_BUF_ERROR_NOT_FOUND = -7
} audio_buf_error_t;

/*******************************************************************************
 * Buffer Types
 ******************************************************************************/

typedef enum {
    AUDIO_BUF_TYPE_PCM,         /**< PCM audio samples */
    AUDIO_BUF_TYPE_LC3,         /**< LC3 encoded frames */
    AUDIO_BUF_TYPE_RAW          /**< Raw byte buffer */
} audio_buf_type_t;

/*******************************************************************************
 * PCM Configuration
 ******************************************************************************/

typedef struct {
    uint32_t sample_rate;           /**< Sample rate in Hz */
    uint8_t channels;               /**< Number of channels (1-8) */
    audio_sample_format_t format;   /**< Sample format */
    uint16_t frame_samples;         /**< Samples per frame (per channel) */
} audio_pcm_config_t;

/*******************************************************************************
 * LC3 Configuration
 ******************************************************************************/

typedef struct {
    uint32_t sample_rate;           /**< Sample rate in Hz */
    uint16_t frame_duration_us;     /**< Frame duration (7500 or 10000 us) */
    uint16_t octets_per_frame;      /**< Octets per LC3 frame */
    uint8_t channels;               /**< Number of channels */
} audio_lc3_config_t;

/*******************************************************************************
 * Buffer Frame Metadata
 ******************************************************************************/

typedef struct {
    uint32_t timestamp;             /**< Frame timestamp */
    uint16_t sequence_number;       /**< Sequence number */
    uint16_t length;                /**< Actual data length */
    uint8_t channels;               /**< Number of channels in frame */
    bool valid;                     /**< Frame data is valid */
    uint8_t flags;                  /**< Additional flags */
} audio_frame_meta_t;

/** Frame flags */
#define AUDIO_FRAME_FLAG_FIRST      0x01    /**< First frame of stream */
#define AUDIO_FRAME_FLAG_LAST       0x02    /**< Last frame of stream */
#define AUDIO_FRAME_FLAG_SILENCE    0x04    /**< Silence/comfort noise */
#define AUDIO_FRAME_FLAG_PLC        0x08    /**< Packet loss concealment */

/*******************************************************************************
 * Buffer Statistics
 ******************************************************************************/

typedef struct {
    uint32_t frames_written;        /**< Total frames written */
    uint32_t frames_read;           /**< Total frames read */
    uint32_t bytes_written;         /**< Total bytes written */
    uint32_t bytes_read;            /**< Total bytes read */
    uint32_t overruns;              /**< Write overruns (buffer full) */
    uint32_t underruns;             /**< Read underruns (buffer empty) */
    uint32_t peak_level;            /**< Peak buffer level */
    uint32_t total_latency_us;      /**< Total accumulated latency */
    uint32_t frame_count;           /**< Frames for latency calculation */
} audio_buf_stats_t;

/*******************************************************************************
 * Buffer Handle
 ******************************************************************************/

typedef struct audio_buffer_s audio_buffer_t;

/*******************************************************************************
 * Callback Types
 ******************************************************************************/

/**
 * @brief Callback when buffer reaches threshold
 *
 * @param buffer Buffer handle
 * @param level Current buffer level (frames)
 * @param user_data User context
 */
typedef void (*audio_buf_threshold_cb_t)(audio_buffer_t *buffer,
                                          uint32_t level, void *user_data);

/*******************************************************************************
 * Buffer Configuration
 ******************************************************************************/

typedef struct {
    audio_buf_type_t type;          /**< Buffer type */
    uint16_t frame_size;            /**< Maximum frame size in bytes */
    uint8_t depth;                  /**< Number of frames to buffer */
    bool track_metadata;            /**< Track frame metadata */

    /* Optional callbacks */
    audio_buf_threshold_cb_t high_cb;   /**< High watermark callback */
    audio_buf_threshold_cb_t low_cb;    /**< Low watermark callback */
    uint8_t high_threshold;         /**< High watermark (frames) */
    uint8_t low_threshold;          /**< Low watermark (frames) */
    void *user_data;                /**< Callback user data */
} audio_buf_config_t;

/*******************************************************************************
 * Default Configurations
 ******************************************************************************/

/** Default PCM buffer config (48kHz stereo, 10ms frames, 8 frame depth) */
#define AUDIO_PCM_BUFFER_CONFIG_DEFAULT { \
    .type = AUDIO_BUF_TYPE_PCM, \
    .frame_size = 1920, /* 48kHz * 10ms * 2ch * 2bytes */ \
    .depth = AUDIO_PCM_DEFAULT_DEPTH, \
    .track_metadata = true, \
    .high_cb = NULL, \
    .low_cb = NULL, \
    .high_threshold = 6, \
    .low_threshold = 2, \
    .user_data = NULL \
}

/** Default LC3 buffer config (100 octets per frame, 8 frame depth) */
#define AUDIO_LC3_BUFFER_CONFIG_DEFAULT { \
    .type = AUDIO_BUF_TYPE_LC3, \
    .frame_size = 100, \
    .depth = AUDIO_LC3_DEFAULT_DEPTH, \
    .track_metadata = true, \
    .high_cb = NULL, \
    .low_cb = NULL, \
    .high_threshold = 6, \
    .low_threshold = 2, \
    .user_data = NULL \
}

/*******************************************************************************
 * API Functions - Buffer Management
 ******************************************************************************/

/**
 * @brief Create audio buffer
 *
 * @param config Buffer configuration
 * @return Buffer handle, or NULL on failure
 */
audio_buffer_t* audio_buffer_create(const audio_buf_config_t *config);

/**
 * @brief Destroy audio buffer
 *
 * @param buffer Buffer handle
 */
void audio_buffer_destroy(audio_buffer_t *buffer);

/**
 * @brief Reset buffer to empty state
 *
 * @param buffer Buffer handle
 */
void audio_buffer_reset(audio_buffer_t *buffer);

/**
 * @brief Get buffer configuration
 *
 * @param buffer Buffer handle
 * @param config Output configuration
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_get_config(audio_buffer_t *buffer, audio_buf_config_t *config);

/*******************************************************************************
 * API Functions - Write Operations
 ******************************************************************************/

/**
 * @brief Write frame to buffer
 *
 * @param buffer Buffer handle
 * @param data Frame data
 * @param len Data length in bytes
 * @param meta Optional frame metadata (NULL to auto-generate)
 * @return AUDIO_BUF_OK on success, AUDIO_BUF_ERROR_FULL if buffer full
 */
int audio_buffer_write(audio_buffer_t *buffer, const uint8_t *data,
                        uint16_t len, const audio_frame_meta_t *meta);

/**
 * @brief Write PCM samples to buffer
 *
 * Convenience function for PCM data with automatic framing.
 *
 * @param buffer Buffer handle
 * @param samples PCM sample data
 * @param num_samples Number of samples (per channel)
 * @param channels Number of channels
 * @param timestamp Frame timestamp
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_write_pcm(audio_buffer_t *buffer, const int16_t *samples,
                            uint16_t num_samples, uint8_t channels,
                            uint32_t timestamp);

/**
 * @brief Write LC3 frame to buffer
 *
 * @param buffer Buffer handle
 * @param lc3_data LC3 encoded data
 * @param lc3_len Data length
 * @param timestamp Frame timestamp
 * @param sequence Sequence number
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_write_lc3(audio_buffer_t *buffer, const uint8_t *lc3_data,
                            uint16_t lc3_len, uint32_t timestamp,
                            uint16_t sequence);

/**
 * @brief Get write pointer for zero-copy write
 *
 * @param buffer Buffer handle
 * @param data Output: pointer to write location
 * @param max_len Output: maximum writable length
 * @return AUDIO_BUF_OK on success, AUDIO_BUF_ERROR_FULL if no space
 */
int audio_buffer_get_write_ptr(audio_buffer_t *buffer, uint8_t **data,
                                uint16_t *max_len);

/**
 * @brief Commit write after using write pointer
 *
 * @param buffer Buffer handle
 * @param len Actual bytes written
 * @param meta Optional frame metadata
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_commit_write(audio_buffer_t *buffer, uint16_t len,
                               const audio_frame_meta_t *meta);

/*******************************************************************************
 * API Functions - Read Operations
 ******************************************************************************/

/**
 * @brief Read frame from buffer
 *
 * @param buffer Buffer handle
 * @param data Output buffer
 * @param max_len Maximum bytes to read
 * @param len Output: actual bytes read
 * @param meta Output: frame metadata (can be NULL)
 * @return AUDIO_BUF_OK on success, AUDIO_BUF_ERROR_EMPTY if buffer empty
 */
int audio_buffer_read(audio_buffer_t *buffer, uint8_t *data, uint16_t max_len,
                       uint16_t *len, audio_frame_meta_t *meta);

/**
 * @brief Read PCM samples from buffer
 *
 * @param buffer Buffer handle
 * @param samples Output sample buffer
 * @param max_samples Maximum samples (per channel)
 * @param num_samples Output: actual samples read
 * @param timestamp Output: frame timestamp
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_read_pcm(audio_buffer_t *buffer, int16_t *samples,
                           uint16_t max_samples, uint16_t *num_samples,
                           uint32_t *timestamp);

/**
 * @brief Read LC3 frame from buffer
 *
 * @param buffer Buffer handle
 * @param lc3_data Output buffer
 * @param max_len Maximum length
 * @param lc3_len Output: actual length
 * @param timestamp Output: frame timestamp
 * @param sequence Output: sequence number
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_read_lc3(audio_buffer_t *buffer, uint8_t *lc3_data,
                           uint16_t max_len, uint16_t *lc3_len,
                           uint32_t *timestamp, uint16_t *sequence);

/**
 * @brief Get read pointer for zero-copy read
 *
 * @param buffer Buffer handle
 * @param data Output: pointer to read location
 * @param len Output: available data length
 * @param meta Output: frame metadata (can be NULL)
 * @return AUDIO_BUF_OK on success, AUDIO_BUF_ERROR_EMPTY if no data
 */
int audio_buffer_get_read_ptr(audio_buffer_t *buffer, const uint8_t **data,
                               uint16_t *len, audio_frame_meta_t *meta);

/**
 * @brief Commit read after using read pointer
 *
 * @param buffer Buffer handle
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_commit_read(audio_buffer_t *buffer);

/**
 * @brief Peek at next frame without removing
 *
 * @param buffer Buffer handle
 * @param data Output buffer
 * @param max_len Maximum bytes
 * @param len Output: actual bytes
 * @param meta Output: frame metadata (can be NULL)
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_peek(audio_buffer_t *buffer, uint8_t *data, uint16_t max_len,
                       uint16_t *len, audio_frame_meta_t *meta);

/**
 * @brief Skip/discard next frame
 *
 * @param buffer Buffer handle
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_skip(audio_buffer_t *buffer);

/*******************************************************************************
 * API Functions - Buffer Status
 ******************************************************************************/

/**
 * @brief Check if buffer is empty
 *
 * @param buffer Buffer handle
 * @return true if empty
 */
bool audio_buffer_is_empty(audio_buffer_t *buffer);

/**
 * @brief Check if buffer is full
 *
 * @param buffer Buffer handle
 * @return true if full
 */
bool audio_buffer_is_full(audio_buffer_t *buffer);

/**
 * @brief Get number of frames in buffer
 *
 * @param buffer Buffer handle
 * @return Number of frames
 */
uint32_t audio_buffer_get_level(audio_buffer_t *buffer);

/**
 * @brief Get available space in frames
 *
 * @param buffer Buffer handle
 * @return Available frames
 */
uint32_t audio_buffer_get_space(audio_buffer_t *buffer);

/**
 * @brief Get buffer capacity in frames
 *
 * @param buffer Buffer handle
 * @return Total capacity
 */
uint32_t audio_buffer_get_capacity(audio_buffer_t *buffer);

/**
 * @brief Get total bytes in buffer
 *
 * @param buffer Buffer handle
 * @return Total bytes
 */
uint32_t audio_buffer_get_bytes(audio_buffer_t *buffer);

/*******************************************************************************
 * API Functions - Statistics
 ******************************************************************************/

/**
 * @brief Get buffer statistics
 *
 * @param buffer Buffer handle
 * @param stats Output statistics
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_get_stats(audio_buffer_t *buffer, audio_buf_stats_t *stats);

/**
 * @brief Reset buffer statistics
 *
 * @param buffer Buffer handle
 * @return AUDIO_BUF_OK on success
 */
int audio_buffer_reset_stats(audio_buffer_t *buffer);

/*******************************************************************************
 * API Functions - Timestamps and Synchronization
 ******************************************************************************/

/**
 * @brief Get timestamp of oldest frame
 *
 * @param buffer Buffer handle
 * @return Timestamp, or 0 if empty
 */
uint32_t audio_buffer_get_oldest_timestamp(audio_buffer_t *buffer);

/**
 * @brief Get timestamp of newest frame
 *
 * @param buffer Buffer handle
 * @return Timestamp, or 0 if empty
 */
uint32_t audio_buffer_get_newest_timestamp(audio_buffer_t *buffer);

/**
 * @brief Calculate buffer latency
 *
 * @param buffer Buffer handle
 * @return Latency in microseconds
 */
uint32_t audio_buffer_get_latency_us(audio_buffer_t *buffer);

/**
 * @brief Discard frames older than timestamp
 *
 * @param buffer Buffer handle
 * @param timestamp Cutoff timestamp
 * @return Number of frames discarded
 */
uint32_t audio_buffer_discard_before(audio_buffer_t *buffer, uint32_t timestamp);

/*******************************************************************************
 * API Functions - DMA Double Buffering
 ******************************************************************************/

/**
 * @brief DMA buffer handle
 */
typedef struct audio_dma_buffer_s audio_dma_buffer_t;

/**
 * @brief DMA buffer configuration
 */
typedef struct {
    uint16_t buffer_size;           /**< Size of each DMA buffer */
    uint8_t num_buffers;            /**< Number of buffers (typically 2) */
    audio_sample_format_t format;   /**< Sample format */
    uint8_t channels;               /**< Number of channels */
} audio_dma_config_t;

/**
 * @brief Create DMA double buffer
 *
 * @param config DMA buffer configuration
 * @return DMA buffer handle, or NULL on failure
 */
audio_dma_buffer_t* audio_dma_buffer_create(const audio_dma_config_t *config);

/**
 * @brief Destroy DMA buffer
 *
 * @param dma DMA buffer handle
 */
void audio_dma_buffer_destroy(audio_dma_buffer_t *dma);

/**
 * @brief Get current DMA buffer for hardware
 *
 * @param dma DMA buffer handle
 * @param is_tx true for TX buffer, false for RX
 * @return Pointer to current buffer
 */
uint8_t* audio_dma_get_buffer(audio_dma_buffer_t *dma, bool is_tx);

/**
 * @brief Swap DMA buffers (call from DMA complete ISR)
 *
 * @param dma DMA buffer handle
 * @param is_tx true for TX buffer, false for RX
 */
void audio_dma_swap_buffer(audio_dma_buffer_t *dma, bool is_tx);

/**
 * @brief Get buffer ready for processing (opposite of DMA active)
 *
 * @param dma DMA buffer handle
 * @param is_tx true for TX buffer, false for RX
 * @param len Output: buffer length
 * @return Pointer to ready buffer
 */
uint8_t* audio_dma_get_ready_buffer(audio_dma_buffer_t *dma, bool is_tx,
                                     uint16_t *len);

/**
 * @brief Mark buffer as processed
 *
 * @param dma DMA buffer handle
 * @param is_tx true for TX buffer, false for RX
 */
void audio_dma_mark_processed(audio_dma_buffer_t *dma, bool is_tx);

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * @brief Get bytes per sample for format
 *
 * @param format Sample format
 * @return Bytes per sample
 */
uint8_t audio_format_get_bytes_per_sample(audio_sample_format_t format);

/**
 * @brief Calculate frame size in bytes
 *
 * @param sample_rate Sample rate
 * @param channels Number of channels
 * @param format Sample format
 * @param frame_duration_us Frame duration in microseconds
 * @return Frame size in bytes
 */
uint32_t audio_calculate_frame_size(uint32_t sample_rate, uint8_t channels,
                                     audio_sample_format_t format,
                                     uint32_t frame_duration_us);

/**
 * @brief Convert PCM format (e.g., S24 to S16)
 *
 * @param src Source samples
 * @param src_format Source format
 * @param dst Destination buffer
 * @param dst_format Destination format
 * @param num_samples Number of samples
 * @return AUDIO_BUF_OK on success
 */
int audio_convert_format(const void *src, audio_sample_format_t src_format,
                          void *dst, audio_sample_format_t dst_format,
                          uint32_t num_samples);

/**
 * @brief Interleave mono channels to stereo
 *
 * @param left Left channel samples
 * @param right Right channel samples
 * @param stereo Output interleaved stereo
 * @param num_samples Samples per channel
 */
void audio_interleave_stereo(const int16_t *left, const int16_t *right,
                              int16_t *stereo, uint32_t num_samples);

/**
 * @brief Deinterleave stereo to mono channels
 *
 * @param stereo Interleaved stereo samples
 * @param left Output left channel
 * @param right Output right channel
 * @param num_samples Samples per channel
 */
void audio_deinterleave_stereo(const int16_t *stereo, int16_t *left,
                                int16_t *right, uint32_t num_samples);

/**
 * @brief Mix multiple channels to mono
 *
 * @param input Interleaved input samples
 * @param output Mono output
 * @param num_samples Samples per channel
 * @param channels Number of input channels
 */
void audio_mix_to_mono(const int16_t *input, int16_t *output,
                        uint32_t num_samples, uint8_t channels);

/**
 * @brief Apply gain to samples
 *
 * @param samples Sample buffer (in-place)
 * @param num_samples Number of samples
 * @param gain_db Gain in dB (negative for attenuation)
 */
void audio_apply_gain(int16_t *samples, uint32_t num_samples, int8_t gain_db);

/**
 * @brief Generate silence samples
 *
 * @param buffer Output buffer
 * @param num_samples Number of samples
 * @param format Sample format
 */
void audio_generate_silence(void *buffer, uint32_t num_samples,
                             audio_sample_format_t format);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_BUFFERS_H */
