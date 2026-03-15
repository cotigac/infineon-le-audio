/**
 * @file lc3_config.h
 * @brief LC3 Codec Configuration
 *
 * Configuration parameters for the LC3 audio codec.
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LC3_CONFIG_H
#define LC3_CONFIG_H

/*******************************************************************************
 * LC3 Codec Parameters
 ******************************************************************************/

/** Sample rate in Hz */
#define LC3_CFG_SAMPLE_RATE         48000

/** Frame duration: 0 = 7.5ms, 1 = 10ms */
#define LC3_CFG_FRAME_DURATION      1

/** Frame duration in microseconds */
#if LC3_CFG_FRAME_DURATION == 0
#define LC3_CFG_FRAME_DURATION_US   7500
#else
#define LC3_CFG_FRAME_DURATION_US   10000
#endif

/** Samples per frame */
#define LC3_CFG_SAMPLES_PER_FRAME   (LC3_CFG_SAMPLE_RATE * LC3_CFG_FRAME_DURATION_US / 1000000)

/** Encoded frame size in bytes (determines bitrate) */
#define LC3_CFG_OCTETS_PER_FRAME    100

/** Bitrate calculation: octets_per_frame * 8 * 1000000 / frame_duration_us */
#define LC3_CFG_BITRATE_BPS         ((LC3_CFG_OCTETS_PER_FRAME * 8 * 1000000) / LC3_CFG_FRAME_DURATION_US)

/** Number of audio channels */
#define LC3_CFG_CHANNELS            1

/*******************************************************************************
 * Buffer Configuration
 ******************************************************************************/

/** Number of LC3 frame buffers for TX */
#define LC3_CFG_TX_BUFFER_COUNT     4

/** Number of LC3 frame buffers for RX */
#define LC3_CFG_RX_BUFFER_COUNT     4

/** PCM buffer size (samples per channel) */
#define LC3_CFG_PCM_BUFFER_SAMPLES  (LC3_CFG_SAMPLES_PER_FRAME * 2)

/*******************************************************************************
 * Quality Settings
 ******************************************************************************/

/**
 * Bitrate presets (octets per frame at 10ms, 48kHz):
 *   64 bytes = 51.2 kbps (low quality)
 *   80 bytes = 64.0 kbps (medium quality)
 *  100 bytes = 80.0 kbps (high quality, default)
 *  120 bytes = 96.0 kbps (very high quality)
 *  155 bytes = 124.0 kbps (maximum quality)
 */

#endif /* LC3_CONFIG_H */
