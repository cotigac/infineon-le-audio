/**
 * @file lc3_wrapper.h
 * @brief LC3 Codec Wrapper API for LE Audio
 *
 * This module provides a simplified interface to Google's liblc3 codec
 * for encoding and decoding LC3 audio frames used in Bluetooth LE Audio.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LC3_WRAPPER_H
#define LC3_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** LC3 frame duration options */
typedef enum {
    LC3_FRAME_DURATION_7_5MS = 0,  /**< 7.5ms frame duration */
    LC3_FRAME_DURATION_10MS  = 1   /**< 10ms frame duration (recommended) */
} lc3_frame_duration_t;

/** LC3 sample rate options */
typedef enum {
    LC3_SAMPLE_RATE_8000  = 8000,
    LC3_SAMPLE_RATE_16000 = 16000,
    LC3_SAMPLE_RATE_24000 = 24000,
    LC3_SAMPLE_RATE_32000 = 32000,
    LC3_SAMPLE_RATE_48000 = 48000  /**< Standard LE Audio rate */
} lc3_sample_rate_t;

/** LC3 configuration structure */
typedef struct {
    lc3_sample_rate_t sample_rate;      /**< Sample rate in Hz */
    lc3_frame_duration_t frame_duration; /**< Frame duration */
    uint16_t octets_per_frame;          /**< Encoded frame size in bytes */
    uint8_t channels;                    /**< Number of channels (1 or 2) */
} lc3_config_t;

/** LC3 codec context (opaque) */
typedef struct lc3_codec_ctx lc3_codec_ctx_t;

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default LC3 configuration for LE Audio */
#define LC3_CONFIG_DEFAULT {            \
    .sample_rate = LC3_SAMPLE_RATE_48000, \
    .frame_duration = LC3_FRAME_DURATION_10MS, \
    .octets_per_frame = 100,            \
    .channels = 1                        \
}

/** Samples per frame at 48kHz with 10ms duration */
#define LC3_SAMPLES_PER_FRAME_48K_10MS  480

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize the LC3 codec
 *
 * @param config Pointer to LC3 configuration
 * @return Pointer to codec context, or NULL on failure
 */
lc3_codec_ctx_t* lc3_wrapper_init(const lc3_config_t *config);

/**
 * @brief Deinitialize the LC3 codec and free resources
 *
 * @param ctx Codec context to deinitialize
 */
void lc3_wrapper_deinit(lc3_codec_ctx_t *ctx);

/**
 * @brief Encode PCM samples to LC3
 *
 * @param ctx       Codec context
 * @param pcm_in    Input PCM samples (signed 16-bit)
 * @param lc3_out   Output LC3 encoded frame
 * @param channel   Channel index (0 for mono/left, 1 for right)
 * @return 0 on success, negative error code on failure
 */
int lc3_wrapper_encode(lc3_codec_ctx_t *ctx,
                       const int16_t *pcm_in,
                       uint8_t *lc3_out,
                       uint8_t channel);

/**
 * @brief Decode LC3 frame to PCM samples
 *
 * @param ctx       Codec context
 * @param lc3_in    Input LC3 encoded frame
 * @param pcm_out   Output PCM samples (signed 16-bit)
 * @param channel   Channel index (0 for mono/left, 1 for right)
 * @return 0 on success, negative error code on failure
 */
int lc3_wrapper_decode(lc3_codec_ctx_t *ctx,
                       const uint8_t *lc3_in,
                       int16_t *pcm_out,
                       uint8_t channel);

/**
 * @brief Decode LC3 frame with packet loss concealment (PLC)
 *
 * Call this when an LC3 frame is lost to generate concealment audio.
 *
 * @param ctx       Codec context
 * @param pcm_out   Output PCM samples (concealed)
 * @param channel   Channel index
 * @return 0 on success, negative error code on failure
 */
int lc3_wrapper_decode_plc(lc3_codec_ctx_t *ctx,
                           int16_t *pcm_out,
                           uint8_t channel);

/**
 * @brief Get the number of samples per frame for current configuration
 *
 * @param ctx Codec context
 * @return Number of PCM samples per frame
 */
uint16_t lc3_wrapper_get_samples_per_frame(const lc3_codec_ctx_t *ctx);

/**
 * @brief Get the encoded frame size in bytes
 *
 * @param ctx Codec context
 * @return Size of encoded LC3 frame in bytes
 */
uint16_t lc3_wrapper_get_frame_bytes(const lc3_codec_ctx_t *ctx);

/**
 * @brief Get the frame duration in microseconds
 *
 * @param ctx Codec context
 * @return Frame duration in microseconds
 */
uint32_t lc3_wrapper_get_frame_duration_us(const lc3_codec_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LC3_WRAPPER_H */
