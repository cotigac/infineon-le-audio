/**
 * @file i2s_stream.h
 * @brief I2S Audio Streaming Interface
 *
 * This module provides DMA-based I2S audio streaming with ping-pong
 * buffering for continuous audio transfer to/from the main controller.
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I2S_STREAM_H
#define I2S_STREAM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** I2S stream direction */
typedef enum {
    I2S_STREAM_TX = 0,  /**< Transmit (to main controller) */
    I2S_STREAM_RX = 1,  /**< Receive (from main controller) */
    I2S_STREAM_DUPLEX   /**< Full duplex (both TX and RX) */
} i2s_stream_direction_t;

/** I2S sample rate options */
typedef enum {
    I2S_SAMPLE_RATE_8000  = 8000,
    I2S_SAMPLE_RATE_16000 = 16000,
    I2S_SAMPLE_RATE_24000 = 24000,
    I2S_SAMPLE_RATE_32000 = 32000,
    I2S_SAMPLE_RATE_44100 = 44100,
    I2S_SAMPLE_RATE_48000 = 48000,
    I2S_SAMPLE_RATE_96000 = 96000
} i2s_sample_rate_t;

/** I2S bit depth options */
typedef enum {
    I2S_BITS_16 = 16,
    I2S_BITS_24 = 24,
    I2S_BITS_32 = 32
} i2s_bit_depth_t;

/** I2S configuration structure */
typedef struct {
    i2s_stream_direction_t direction;  /**< Stream direction */
    i2s_sample_rate_t sample_rate;     /**< Sample rate in Hz */
    i2s_bit_depth_t bit_depth;         /**< Bits per sample */
    uint8_t channels;                  /**< Number of channels (1 or 2) */
    uint16_t buffer_size_samples;      /**< Buffer size in samples */
} i2s_stream_config_t;

/** I2S stream statistics */
typedef struct {
    uint32_t frames_transferred;  /**< Total frames transferred */
    uint32_t buffer_underruns;    /**< TX buffer underrun count */
    uint32_t buffer_overruns;     /**< RX buffer overrun count */
    uint32_t dma_errors;          /**< DMA error count */
} i2s_stream_stats_t;

/** Callback function type for buffer ready events */
typedef void (*i2s_buffer_callback_t)(int16_t *buffer, uint16_t sample_count, void *user_data);

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default I2S configuration for LE Audio */
#define I2S_STREAM_CONFIG_DEFAULT {         \
    .direction = I2S_STREAM_DUPLEX,         \
    .sample_rate = I2S_SAMPLE_RATE_48000,   \
    .bit_depth = I2S_BITS_16,               \
    .channels = 1,                          \
    .buffer_size_samples = 480              \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize the I2S stream interface
 *
 * @param config Pointer to I2S configuration
 * @return 0 on success, negative error code on failure
 */
int i2s_stream_init(const i2s_stream_config_t *config);

/**
 * @brief Deinitialize the I2S stream interface
 */
void i2s_stream_deinit(void);

/**
 * @brief Start I2S streaming
 *
 * @return 0 on success, negative error code on failure
 */
int i2s_stream_start(void);

/**
 * @brief Stop I2S streaming
 *
 * @return 0 on success, negative error code on failure
 */
int i2s_stream_stop(void);

/**
 * @brief Register callback for RX buffer ready
 *
 * The callback is called from ISR context when a new buffer of
 * samples has been received from the main controller.
 *
 * @param callback  Function to call when buffer is ready
 * @param user_data User data passed to callback
 */
void i2s_stream_register_rx_callback(i2s_buffer_callback_t callback, void *user_data);

/**
 * @brief Register callback for TX buffer request
 *
 * The callback is called from ISR context when the next buffer
 * of samples is needed for transmission to the main controller.
 *
 * @param callback  Function to call when buffer is needed
 * @param user_data User data passed to callback
 */
void i2s_stream_register_tx_callback(i2s_buffer_callback_t callback, void *user_data);

/**
 * @brief Read samples from I2S RX buffer (blocking)
 *
 * @param buffer        Output buffer for samples
 * @param sample_count  Number of samples to read
 * @param timeout_ms    Timeout in milliseconds
 * @return Number of samples read, or negative error code
 */
int i2s_stream_read(int16_t *buffer, uint16_t sample_count, uint32_t timeout_ms);

/**
 * @brief Write samples to I2S TX buffer (blocking)
 *
 * @param buffer        Input buffer with samples
 * @param sample_count  Number of samples to write
 * @param timeout_ms    Timeout in milliseconds
 * @return Number of samples written, or negative error code
 */
int i2s_stream_write(const int16_t *buffer, uint16_t sample_count, uint32_t timeout_ms);

/**
 * @brief Get current stream statistics
 *
 * @param stats Pointer to statistics structure to fill
 */
void i2s_stream_get_stats(i2s_stream_stats_t *stats);

/**
 * @brief Reset stream statistics
 */
void i2s_stream_reset_stats(void);

/**
 * @brief Check if I2S stream is running
 *
 * @return true if streaming is active
 */
bool i2s_stream_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* I2S_STREAM_H */
