/**
 * @file lc3_wrapper.c
 * @brief LC3 Codec Wrapper Implementation
 *
 * This module provides a simplified interface to Google's liblc3 codec
 * for encoding and decoding LC3 audio frames used in Bluetooth LE Audio.
 *
 * The wrapper handles:
 * - Codec initialization and configuration
 * - Multi-channel encoding/decoding
 * - Memory management for codec state
 * - Packet loss concealment (PLC)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lc3_wrapper.h"
#include "../config/lc3_config.h"

#include <stdlib.h>
#include <string.h>

/* Include liblc3 header */
#include "lc3.h"

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Maximum supported channels */
#define LC3_MAX_CHANNELS    2

/** PCM format for liblc3 */
#define LC3_PCM_FORMAT      LC3_PCM_FORMAT_S16

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief LC3 codec context structure
 */
struct lc3_codec_ctx {
    /* Configuration */
    lc3_config_t config;

    /* Frame parameters */
    uint16_t samples_per_frame;
    uint32_t frame_duration_us;

    /* Encoder states (one per channel) */
    lc3_encoder_t encoder[LC3_MAX_CHANNELS];

    /* Decoder states (one per channel) */
    lc3_decoder_t decoder[LC3_MAX_CHANNELS];

    /* Memory for encoder/decoder states */
    void *encoder_mem[LC3_MAX_CHANNELS];
    void *decoder_mem[LC3_MAX_CHANNELS];

    /* Initialization flag */
    bool initialized;
};

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief Convert frame duration enum to microseconds
 */
static uint32_t frame_duration_to_us(lc3_frame_duration_t duration)
{
    switch (duration) {
        case LC3_FRAME_DURATION_7_5MS:
            return 7500;
        case LC3_FRAME_DURATION_10MS:
        default:
            return 10000;
    }
}

/**
 * @brief Convert frame duration enum to liblc3 format
 */
static int frame_duration_to_lc3(lc3_frame_duration_t duration)
{
    switch (duration) {
        case LC3_FRAME_DURATION_7_5MS:
            return LC3_DT_7M5;
        case LC3_FRAME_DURATION_10MS:
        default:
            return LC3_DT_10M;
    }
}

/**
 * @brief Calculate samples per frame
 */
static uint16_t calculate_samples_per_frame(uint32_t sample_rate, uint32_t frame_duration_us)
{
    return (uint16_t)((sample_rate * frame_duration_us) / 1000000);
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

lc3_codec_ctx_t* lc3_wrapper_init(const lc3_config_t *config)
{
    lc3_codec_ctx_t *ctx;
    int dt;
    unsigned int enc_size, dec_size;
    int i;

    if (config == NULL) {
        return NULL;
    }

    /* Validate configuration */
    if (config->channels == 0 || config->channels > LC3_MAX_CHANNELS) {
        return NULL;
    }

    /* Allocate context */
    ctx = (lc3_codec_ctx_t *)malloc(sizeof(lc3_codec_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    memset(ctx, 0, sizeof(lc3_codec_ctx_t));

    /* Store configuration */
    ctx->config = *config;
    ctx->frame_duration_us = frame_duration_to_us(config->frame_duration);
    ctx->samples_per_frame = calculate_samples_per_frame(
        config->sample_rate, ctx->frame_duration_us);

    /* Get liblc3 frame duration format */
    dt = frame_duration_to_lc3(config->frame_duration);

    /* Calculate memory requirements */
    enc_size = lc3_encoder_size(dt, config->sample_rate);
    dec_size = lc3_decoder_size(dt, config->sample_rate);

    if (enc_size == 0 || dec_size == 0) {
        free(ctx);
        return NULL;
    }

    /* Initialize encoders and decoders for each channel */
    for (i = 0; i < config->channels; i++) {
        /* Allocate encoder memory */
        ctx->encoder_mem[i] = malloc(enc_size);
        if (ctx->encoder_mem[i] == NULL) {
            goto error_cleanup;
        }

        /* Initialize encoder */
        ctx->encoder[i] = lc3_setup_encoder(
            dt,
            config->sample_rate,
            0,  /* Use same sample rate for input */
            ctx->encoder_mem[i]
        );

        if (ctx->encoder[i] == NULL) {
            goto error_cleanup;
        }

        /* Allocate decoder memory */
        ctx->decoder_mem[i] = malloc(dec_size);
        if (ctx->decoder_mem[i] == NULL) {
            goto error_cleanup;
        }

        /* Initialize decoder */
        ctx->decoder[i] = lc3_setup_decoder(
            dt,
            config->sample_rate,
            0,  /* Use same sample rate for output */
            ctx->decoder_mem[i]
        );

        if (ctx->decoder[i] == NULL) {
            goto error_cleanup;
        }
    }

    ctx->initialized = true;
    return ctx;

error_cleanup:
    /* Clean up on error */
    for (i = 0; i < LC3_MAX_CHANNELS; i++) {
        if (ctx->encoder_mem[i]) {
            free(ctx->encoder_mem[i]);
        }
        if (ctx->decoder_mem[i]) {
            free(ctx->decoder_mem[i]);
        }
    }
    free(ctx);
    return NULL;
}

void lc3_wrapper_deinit(lc3_codec_ctx_t *ctx)
{
    int i;

    if (ctx == NULL) {
        return;
    }

    /* Free encoder/decoder memory */
    for (i = 0; i < LC3_MAX_CHANNELS; i++) {
        if (ctx->encoder_mem[i]) {
            free(ctx->encoder_mem[i]);
        }
        if (ctx->decoder_mem[i]) {
            free(ctx->decoder_mem[i]);
        }
    }

    ctx->initialized = false;
    free(ctx);
}

int lc3_wrapper_encode(lc3_codec_ctx_t *ctx,
                       const int16_t *pcm_in,
                       uint8_t *lc3_out,
                       uint8_t channel)
{
    int result;

    if (ctx == NULL || !ctx->initialized) {
        return -1;
    }

    if (pcm_in == NULL || lc3_out == NULL) {
        return -2;
    }

    if (channel >= ctx->config.channels) {
        return -3;
    }

    /* Encode PCM to LC3 */
    result = lc3_encode(
        ctx->encoder[channel],
        LC3_PCM_FORMAT,
        pcm_in,
        1,  /* Stride (1 for mono, 2 for interleaved stereo) */
        ctx->config.octets_per_frame,
        lc3_out
    );

    return result;
}

int lc3_wrapper_decode(lc3_codec_ctx_t *ctx,
                       const uint8_t *lc3_in,
                       int16_t *pcm_out,
                       uint8_t channel)
{
    int result;

    if (ctx == NULL || !ctx->initialized) {
        return -1;
    }

    if (lc3_in == NULL || pcm_out == NULL) {
        return -2;
    }

    if (channel >= ctx->config.channels) {
        return -3;
    }

    /* Decode LC3 to PCM */
    result = lc3_decode(
        ctx->decoder[channel],
        lc3_in,
        ctx->config.octets_per_frame,
        LC3_PCM_FORMAT,
        pcm_out,
        1   /* Stride */
    );

    return result;
}

int lc3_wrapper_decode_plc(lc3_codec_ctx_t *ctx,
                           int16_t *pcm_out,
                           uint8_t channel)
{
    int result;

    if (ctx == NULL || !ctx->initialized) {
        return -1;
    }

    if (pcm_out == NULL) {
        return -2;
    }

    if (channel >= ctx->config.channels) {
        return -3;
    }

    /* Decode with packet loss concealment (pass NULL for input) */
    result = lc3_decode(
        ctx->decoder[channel],
        NULL,   /* NULL indicates packet loss */
        ctx->config.octets_per_frame,
        LC3_PCM_FORMAT,
        pcm_out,
        1   /* Stride */
    );

    return result;
}

uint16_t lc3_wrapper_get_samples_per_frame(const lc3_codec_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->initialized) {
        return 0;
    }
    return ctx->samples_per_frame;
}

uint16_t lc3_wrapper_get_frame_bytes(const lc3_codec_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->initialized) {
        return 0;
    }
    return ctx->config.octets_per_frame;
}

uint32_t lc3_wrapper_get_frame_duration_us(const lc3_codec_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->initialized) {
        return 0;
    }
    return ctx->frame_duration_us;
}

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * @brief Encode stereo PCM (interleaved) to two LC3 frames
 *
 * @param ctx           Codec context (must be configured for 2 channels)
 * @param pcm_stereo    Interleaved stereo PCM samples (L, R, L, R, ...)
 * @param lc3_left      Output buffer for left channel LC3 frame
 * @param lc3_right     Output buffer for right channel LC3 frame
 * @return 0 on success, negative error code on failure
 */
int lc3_wrapper_encode_stereo(lc3_codec_ctx_t *ctx,
                              const int16_t *pcm_stereo,
                              uint8_t *lc3_left,
                              uint8_t *lc3_right)
{
    int result;

    if (ctx == NULL || !ctx->initialized) {
        return -1;
    }

    if (ctx->config.channels < 2) {
        return -4;  /* Not configured for stereo */
    }

    /* Encode left channel (even samples) */
    result = lc3_encode(
        ctx->encoder[0],
        LC3_PCM_FORMAT,
        pcm_stereo,
        2,  /* Stride of 2 for interleaved stereo */
        ctx->config.octets_per_frame,
        lc3_left
    );

    if (result != 0) {
        return result;
    }

    /* Encode right channel (odd samples) */
    result = lc3_encode(
        ctx->encoder[1],
        LC3_PCM_FORMAT,
        pcm_stereo + 1,  /* Start at second sample (right channel) */
        2,  /* Stride of 2 for interleaved stereo */
        ctx->config.octets_per_frame,
        lc3_right
    );

    return result;
}

/**
 * @brief Decode two LC3 frames to stereo PCM (interleaved)
 *
 * @param ctx           Codec context (must be configured for 2 channels)
 * @param lc3_left      Left channel LC3 frame
 * @param lc3_right     Right channel LC3 frame
 * @param pcm_stereo    Output buffer for interleaved stereo PCM
 * @return 0 on success, negative error code on failure
 */
int lc3_wrapper_decode_stereo(lc3_codec_ctx_t *ctx,
                              const uint8_t *lc3_left,
                              const uint8_t *lc3_right,
                              int16_t *pcm_stereo)
{
    int result;

    if (ctx == NULL || !ctx->initialized) {
        return -1;
    }

    if (ctx->config.channels < 2) {
        return -4;  /* Not configured for stereo */
    }

    /* Decode left channel (even samples) */
    result = lc3_decode(
        ctx->decoder[0],
        lc3_left,
        ctx->config.octets_per_frame,
        LC3_PCM_FORMAT,
        pcm_stereo,
        2   /* Stride of 2 for interleaved stereo */
    );

    if (result != 0) {
        return result;
    }

    /* Decode right channel (odd samples) */
    result = lc3_decode(
        ctx->decoder[1],
        lc3_right,
        ctx->config.octets_per_frame,
        LC3_PCM_FORMAT,
        pcm_stereo + 1,  /* Start at second sample (right channel) */
        2   /* Stride of 2 for interleaved stereo */
    );

    return result;
}
