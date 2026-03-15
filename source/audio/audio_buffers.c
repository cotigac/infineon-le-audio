/**
 * @file audio_buffers.c
 * @brief Audio Buffer Management Implementation
 *
 * Thread-safe ring buffers for PCM and LC3 audio data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_buffers.h"
#include <string.h>
#include <stdlib.h>

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Ring buffer entry */
typedef struct {
    uint8_t *data;                  /**< Frame data */
    audio_frame_meta_t meta;        /**< Frame metadata */
} ring_entry_t;

/** Audio buffer structure */
struct audio_buffer_s {
    bool initialized;
    audio_buf_config_t config;

    /* Ring buffer */
    ring_entry_t *entries;          /**< Ring buffer entries */
    uint8_t *data_pool;             /**< Data storage pool */
    uint32_t pool_size;             /**< Total pool size */

    volatile uint32_t head;         /**< Write index */
    volatile uint32_t tail;         /**< Read index */
    uint32_t capacity;              /**< Number of entries */

    /* Sequence tracking */
    uint16_t write_sequence;        /**< Next write sequence number */
    uint16_t read_sequence;         /**< Expected read sequence number */

    /* Statistics */
    audio_buf_stats_t stats;

    /* Current timestamp for latency calc */
    uint32_t current_timestamp;
};

/** DMA double buffer structure */
struct audio_dma_buffer_s {
    bool initialized;
    audio_dma_config_t config;

    uint8_t *buffers[2];            /**< Double buffer pointers */
    volatile uint8_t active_tx;     /**< Active TX buffer index */
    volatile uint8_t active_rx;     /**< Active RX buffer index */
    volatile bool tx_ready[2];      /**< TX buffer ready flags */
    volatile bool rx_ready[2];      /**< RX buffer ready flags */
};

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static inline uint32_t ring_next(uint32_t index, uint32_t capacity);
static inline uint32_t ring_level(uint32_t head, uint32_t tail, uint32_t capacity);
static void check_thresholds(audio_buffer_t *buffer, uint32_t old_level, uint32_t new_level);

/*******************************************************************************
 * API Functions - Buffer Management
 ******************************************************************************/

audio_buffer_t* audio_buffer_create(const audio_buf_config_t *config)
{
    audio_buffer_t *buffer;
    uint32_t total_size;

    if (config == NULL || config->frame_size == 0 || config->depth == 0) {
        return NULL;
    }

    if (config->depth > AUDIO_MAX_BUFFER_DEPTH) {
        return NULL;
    }

    /* Allocate buffer structure */
    buffer = (audio_buffer_t*)malloc(sizeof(audio_buffer_t));
    if (buffer == NULL) {
        return NULL;
    }

    memset(buffer, 0, sizeof(audio_buffer_t));

    /* Copy configuration */
    memcpy(&buffer->config, config, sizeof(audio_buf_config_t));

    /* Capacity is depth + 1 for ring buffer (one slot always empty) */
    buffer->capacity = config->depth + 1;

    /* Allocate ring buffer entries */
    buffer->entries = (ring_entry_t*)malloc(sizeof(ring_entry_t) * buffer->capacity);
    if (buffer->entries == NULL) {
        free(buffer);
        return NULL;
    }
    memset(buffer->entries, 0, sizeof(ring_entry_t) * buffer->capacity);

    /* Allocate data pool */
    total_size = config->frame_size * buffer->capacity;
    buffer->data_pool = (uint8_t*)malloc(total_size);
    if (buffer->data_pool == NULL) {
        free(buffer->entries);
        free(buffer);
        return NULL;
    }
    memset(buffer->data_pool, 0, total_size);
    buffer->pool_size = total_size;

    /* Assign data pointers to entries */
    for (uint32_t i = 0; i < buffer->capacity; i++) {
        buffer->entries[i].data = buffer->data_pool + (i * config->frame_size);
    }

    buffer->head = 0;
    buffer->tail = 0;
    buffer->write_sequence = 0;
    buffer->read_sequence = 0;
    buffer->initialized = true;

    return buffer;
}

void audio_buffer_destroy(audio_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }

    if (buffer->data_pool != NULL) {
        free(buffer->data_pool);
    }

    if (buffer->entries != NULL) {
        free(buffer->entries);
    }

    free(buffer);
}

void audio_buffer_reset(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return;
    }

    buffer->head = 0;
    buffer->tail = 0;
    buffer->write_sequence = 0;
    buffer->read_sequence = 0;

    /* Clear metadata */
    for (uint32_t i = 0; i < buffer->capacity; i++) {
        buffer->entries[i].meta.valid = false;
        buffer->entries[i].meta.length = 0;
    }
}

int audio_buffer_get_config(audio_buffer_t *buffer, audio_buf_config_t *config)
{
    if (buffer == NULL || !buffer->initialized || config == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    memcpy(config, &buffer->config, sizeof(audio_buf_config_t));
    return AUDIO_BUF_OK;
}

/*******************************************************************************
 * API Functions - Write Operations
 ******************************************************************************/

int audio_buffer_write(audio_buffer_t *buffer, const uint8_t *data,
                        uint16_t len, const audio_frame_meta_t *meta)
{
    uint32_t next_head;
    uint32_t old_level, new_level;

    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || len == 0) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    if (len > buffer->config.frame_size) {
        return AUDIO_BUF_ERROR_INVALID_SIZE;
    }

    /* Check if full */
    next_head = ring_next(buffer->head, buffer->capacity);
    if (next_head == buffer->tail) {
        buffer->stats.overruns++;
        return AUDIO_BUF_ERROR_FULL;
    }

    old_level = ring_level(buffer->head, buffer->tail, buffer->capacity);

    /* Copy data */
    ring_entry_t *entry = &buffer->entries[buffer->head];
    memcpy(entry->data, data, len);

    /* Set metadata */
    if (meta != NULL) {
        memcpy(&entry->meta, meta, sizeof(audio_frame_meta_t));
    } else {
        entry->meta.timestamp = buffer->current_timestamp;
        entry->meta.sequence_number = buffer->write_sequence;
        entry->meta.length = len;
        entry->meta.channels = 1;
        entry->meta.valid = true;
        entry->meta.flags = 0;
    }

    entry->meta.length = len;

    /* Update sequence */
    buffer->write_sequence++;

    /* Commit write */
    buffer->head = next_head;

    /* Update statistics */
    buffer->stats.frames_written++;
    buffer->stats.bytes_written += len;

    new_level = ring_level(buffer->head, buffer->tail, buffer->capacity);
    if (new_level > buffer->stats.peak_level) {
        buffer->stats.peak_level = new_level;
    }

    /* Check thresholds */
    check_thresholds(buffer, old_level, new_level);

    return AUDIO_BUF_OK;
}

int audio_buffer_write_pcm(audio_buffer_t *buffer, const int16_t *samples,
                            uint16_t num_samples, uint8_t channels,
                            uint32_t timestamp)
{
    audio_frame_meta_t meta;
    uint16_t len;

    if (buffer == NULL || samples == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    len = num_samples * channels * sizeof(int16_t);
    if (len > buffer->config.frame_size) {
        return AUDIO_BUF_ERROR_INVALID_SIZE;
    }

    meta.timestamp = timestamp;
    meta.sequence_number = buffer->write_sequence;
    meta.length = len;
    meta.channels = channels;
    meta.valid = true;
    meta.flags = 0;

    return audio_buffer_write(buffer, (const uint8_t*)samples, len, &meta);
}

int audio_buffer_write_lc3(audio_buffer_t *buffer, const uint8_t *lc3_data,
                            uint16_t lc3_len, uint32_t timestamp,
                            uint16_t sequence)
{
    audio_frame_meta_t meta;

    if (buffer == NULL || lc3_data == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    meta.timestamp = timestamp;
    meta.sequence_number = sequence;
    meta.length = lc3_len;
    meta.channels = 1;
    meta.valid = true;
    meta.flags = 0;

    return audio_buffer_write(buffer, lc3_data, lc3_len, &meta);
}

int audio_buffer_get_write_ptr(audio_buffer_t *buffer, uint8_t **data,
                                uint16_t *max_len)
{
    uint32_t next_head;

    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || max_len == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    /* Check if full */
    next_head = ring_next(buffer->head, buffer->capacity);
    if (next_head == buffer->tail) {
        return AUDIO_BUF_ERROR_FULL;
    }

    *data = buffer->entries[buffer->head].data;
    *max_len = buffer->config.frame_size;

    return AUDIO_BUF_OK;
}

int audio_buffer_commit_write(audio_buffer_t *buffer, uint16_t len,
                               const audio_frame_meta_t *meta)
{
    uint32_t next_head;
    ring_entry_t *entry;

    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    if (len == 0 || len > buffer->config.frame_size) {
        return AUDIO_BUF_ERROR_INVALID_SIZE;
    }

    next_head = ring_next(buffer->head, buffer->capacity);
    if (next_head == buffer->tail) {
        return AUDIO_BUF_ERROR_FULL;
    }

    entry = &buffer->entries[buffer->head];

    if (meta != NULL) {
        memcpy(&entry->meta, meta, sizeof(audio_frame_meta_t));
    } else {
        entry->meta.timestamp = buffer->current_timestamp;
        entry->meta.sequence_number = buffer->write_sequence;
        entry->meta.channels = 1;
        entry->meta.valid = true;
        entry->meta.flags = 0;
    }
    entry->meta.length = len;

    buffer->write_sequence++;
    buffer->head = next_head;
    buffer->stats.frames_written++;
    buffer->stats.bytes_written += len;

    return AUDIO_BUF_OK;
}

/*******************************************************************************
 * API Functions - Read Operations
 ******************************************************************************/

int audio_buffer_read(audio_buffer_t *buffer, uint8_t *data, uint16_t max_len,
                       uint16_t *len, audio_frame_meta_t *meta)
{
    ring_entry_t *entry;
    uint16_t copy_len;
    uint32_t old_level, new_level;

    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || len == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    /* Check if empty */
    if (buffer->head == buffer->tail) {
        buffer->stats.underruns++;
        return AUDIO_BUF_ERROR_EMPTY;
    }

    old_level = ring_level(buffer->head, buffer->tail, buffer->capacity);

    entry = &buffer->entries[buffer->tail];
    copy_len = (entry->meta.length < max_len) ? entry->meta.length : max_len;

    memcpy(data, entry->data, copy_len);
    *len = copy_len;

    if (meta != NULL) {
        memcpy(meta, &entry->meta, sizeof(audio_frame_meta_t));
    }

    /* Calculate latency */
    if (buffer->config.track_metadata && entry->meta.valid) {
        uint32_t latency = buffer->current_timestamp - entry->meta.timestamp;
        buffer->stats.total_latency_us += latency;
        buffer->stats.frame_count++;
    }

    /* Commit read */
    buffer->tail = ring_next(buffer->tail, buffer->capacity);
    buffer->stats.frames_read++;
    buffer->stats.bytes_read += copy_len;

    new_level = ring_level(buffer->head, buffer->tail, buffer->capacity);
    check_thresholds(buffer, old_level, new_level);

    return AUDIO_BUF_OK;
}

int audio_buffer_read_pcm(audio_buffer_t *buffer, int16_t *samples,
                           uint16_t max_samples, uint16_t *num_samples,
                           uint32_t *timestamp)
{
    uint8_t *data;
    uint16_t len;
    audio_frame_meta_t meta;
    int result;

    if (buffer == NULL || samples == NULL || num_samples == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    result = audio_buffer_read(buffer, (uint8_t*)samples,
                               max_samples * sizeof(int16_t), &len, &meta);
    if (result != AUDIO_BUF_OK) {
        return result;
    }

    *num_samples = len / sizeof(int16_t);
    if (timestamp != NULL) {
        *timestamp = meta.timestamp;
    }

    return AUDIO_BUF_OK;
}

int audio_buffer_read_lc3(audio_buffer_t *buffer, uint8_t *lc3_data,
                           uint16_t max_len, uint16_t *lc3_len,
                           uint32_t *timestamp, uint16_t *sequence)
{
    audio_frame_meta_t meta;
    int result;

    if (buffer == NULL || lc3_data == NULL || lc3_len == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    result = audio_buffer_read(buffer, lc3_data, max_len, lc3_len, &meta);
    if (result != AUDIO_BUF_OK) {
        return result;
    }

    if (timestamp != NULL) {
        *timestamp = meta.timestamp;
    }
    if (sequence != NULL) {
        *sequence = meta.sequence_number;
    }

    return AUDIO_BUF_OK;
}

int audio_buffer_get_read_ptr(audio_buffer_t *buffer, const uint8_t **data,
                               uint16_t *len, audio_frame_meta_t *meta)
{
    ring_entry_t *entry;

    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || len == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    if (buffer->head == buffer->tail) {
        return AUDIO_BUF_ERROR_EMPTY;
    }

    entry = &buffer->entries[buffer->tail];
    *data = entry->data;
    *len = entry->meta.length;

    if (meta != NULL) {
        memcpy(meta, &entry->meta, sizeof(audio_frame_meta_t));
    }

    return AUDIO_BUF_OK;
}

int audio_buffer_commit_read(audio_buffer_t *buffer)
{
    ring_entry_t *entry;

    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    if (buffer->head == buffer->tail) {
        return AUDIO_BUF_ERROR_EMPTY;
    }

    entry = &buffer->entries[buffer->tail];
    buffer->stats.frames_read++;
    buffer->stats.bytes_read += entry->meta.length;

    buffer->tail = ring_next(buffer->tail, buffer->capacity);

    return AUDIO_BUF_OK;
}

int audio_buffer_peek(audio_buffer_t *buffer, uint8_t *data, uint16_t max_len,
                       uint16_t *len, audio_frame_meta_t *meta)
{
    ring_entry_t *entry;
    uint16_t copy_len;

    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || len == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    if (buffer->head == buffer->tail) {
        return AUDIO_BUF_ERROR_EMPTY;
    }

    entry = &buffer->entries[buffer->tail];
    copy_len = (entry->meta.length < max_len) ? entry->meta.length : max_len;

    memcpy(data, entry->data, copy_len);
    *len = copy_len;

    if (meta != NULL) {
        memcpy(meta, &entry->meta, sizeof(audio_frame_meta_t));
    }

    return AUDIO_BUF_OK;
}

int audio_buffer_skip(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    if (buffer->head == buffer->tail) {
        return AUDIO_BUF_ERROR_EMPTY;
    }

    buffer->tail = ring_next(buffer->tail, buffer->capacity);

    return AUDIO_BUF_OK;
}

/*******************************************************************************
 * API Functions - Buffer Status
 ******************************************************************************/

bool audio_buffer_is_empty(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return true;
    }
    return buffer->head == buffer->tail;
}

bool audio_buffer_is_full(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return true;
    }
    return ring_next(buffer->head, buffer->capacity) == buffer->tail;
}

uint32_t audio_buffer_get_level(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return 0;
    }
    return ring_level(buffer->head, buffer->tail, buffer->capacity);
}

uint32_t audio_buffer_get_space(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return 0;
    }
    return buffer->config.depth - ring_level(buffer->head, buffer->tail, buffer->capacity);
}

uint32_t audio_buffer_get_capacity(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return 0;
    }
    return buffer->config.depth;
}

uint32_t audio_buffer_get_bytes(audio_buffer_t *buffer)
{
    uint32_t total = 0;
    uint32_t idx;

    if (buffer == NULL || !buffer->initialized) {
        return 0;
    }

    idx = buffer->tail;
    while (idx != buffer->head) {
        total += buffer->entries[idx].meta.length;
        idx = ring_next(idx, buffer->capacity);
    }

    return total;
}

/*******************************************************************************
 * API Functions - Statistics
 ******************************************************************************/

int audio_buffer_get_stats(audio_buffer_t *buffer, audio_buf_stats_t *stats)
{
    if (buffer == NULL || !buffer->initialized || stats == NULL) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    memcpy(stats, &buffer->stats, sizeof(audio_buf_stats_t));
    return AUDIO_BUF_OK;
}

int audio_buffer_reset_stats(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return AUDIO_BUF_ERROR_NOT_INITIALIZED;
    }

    memset(&buffer->stats, 0, sizeof(audio_buf_stats_t));
    return AUDIO_BUF_OK;
}

/*******************************************************************************
 * API Functions - Timestamps and Synchronization
 ******************************************************************************/

uint32_t audio_buffer_get_oldest_timestamp(audio_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->initialized) {
        return 0;
    }

    if (buffer->head == buffer->tail) {
        return 0;
    }

    return buffer->entries[buffer->tail].meta.timestamp;
}

uint32_t audio_buffer_get_newest_timestamp(audio_buffer_t *buffer)
{
    uint32_t prev_head;

    if (buffer == NULL || !buffer->initialized) {
        return 0;
    }

    if (buffer->head == buffer->tail) {
        return 0;
    }

    /* Get previous head (most recent write) */
    prev_head = (buffer->head == 0) ? buffer->capacity - 1 : buffer->head - 1;
    return buffer->entries[prev_head].meta.timestamp;
}

uint32_t audio_buffer_get_latency_us(audio_buffer_t *buffer)
{
    uint32_t oldest, newest;

    if (buffer == NULL || !buffer->initialized) {
        return 0;
    }

    oldest = audio_buffer_get_oldest_timestamp(buffer);
    newest = audio_buffer_get_newest_timestamp(buffer);

    if (oldest == 0 || newest == 0) {
        return 0;
    }

    return newest - oldest;
}

uint32_t audio_buffer_discard_before(audio_buffer_t *buffer, uint32_t timestamp)
{
    uint32_t discarded = 0;

    if (buffer == NULL || !buffer->initialized) {
        return 0;
    }

    while (buffer->head != buffer->tail) {
        if (buffer->entries[buffer->tail].meta.timestamp >= timestamp) {
            break;
        }
        buffer->tail = ring_next(buffer->tail, buffer->capacity);
        discarded++;
    }

    return discarded;
}

/*******************************************************************************
 * API Functions - DMA Double Buffering
 ******************************************************************************/

audio_dma_buffer_t* audio_dma_buffer_create(const audio_dma_config_t *config)
{
    audio_dma_buffer_t *dma;

    if (config == NULL || config->buffer_size == 0 || config->num_buffers < 2) {
        return NULL;
    }

    dma = (audio_dma_buffer_t*)malloc(sizeof(audio_dma_buffer_t));
    if (dma == NULL) {
        return NULL;
    }

    memset(dma, 0, sizeof(audio_dma_buffer_t));
    memcpy(&dma->config, config, sizeof(audio_dma_config_t));

    /* Allocate buffers (aligned for DMA) */
    for (int i = 0; i < 2; i++) {
        dma->buffers[i] = (uint8_t*)malloc(config->buffer_size);
        if (dma->buffers[i] == NULL) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                free(dma->buffers[j]);
            }
            free(dma);
            return NULL;
        }
        memset(dma->buffers[i], 0, config->buffer_size);
    }

    dma->active_tx = 0;
    dma->active_rx = 0;
    dma->initialized = true;

    return dma;
}

void audio_dma_buffer_destroy(audio_dma_buffer_t *dma)
{
    if (dma == NULL) {
        return;
    }

    for (int i = 0; i < 2; i++) {
        if (dma->buffers[i] != NULL) {
            free(dma->buffers[i]);
        }
    }

    free(dma);
}

uint8_t* audio_dma_get_buffer(audio_dma_buffer_t *dma, bool is_tx)
{
    if (dma == NULL || !dma->initialized) {
        return NULL;
    }

    if (is_tx) {
        return dma->buffers[dma->active_tx];
    } else {
        return dma->buffers[dma->active_rx];
    }
}

void audio_dma_swap_buffer(audio_dma_buffer_t *dma, bool is_tx)
{
    if (dma == NULL || !dma->initialized) {
        return;
    }

    if (is_tx) {
        dma->tx_ready[dma->active_tx] = true;
        dma->active_tx = 1 - dma->active_tx;
    } else {
        dma->rx_ready[dma->active_rx] = true;
        dma->active_rx = 1 - dma->active_rx;
    }
}

uint8_t* audio_dma_get_ready_buffer(audio_dma_buffer_t *dma, bool is_tx,
                                     uint16_t *len)
{
    uint8_t ready_idx;

    if (dma == NULL || !dma->initialized || len == NULL) {
        return NULL;
    }

    if (is_tx) {
        /* For TX, get the buffer that's NOT active (being filled by CPU) */
        ready_idx = 1 - dma->active_tx;
        if (!dma->tx_ready[ready_idx]) {
            return NULL;
        }
    } else {
        /* For RX, get the buffer that's NOT active (contains received data) */
        ready_idx = 1 - dma->active_rx;
        if (!dma->rx_ready[ready_idx]) {
            return NULL;
        }
    }

    *len = dma->config.buffer_size;
    return dma->buffers[ready_idx];
}

void audio_dma_mark_processed(audio_dma_buffer_t *dma, bool is_tx)
{
    uint8_t ready_idx;

    if (dma == NULL || !dma->initialized) {
        return;
    }

    if (is_tx) {
        ready_idx = 1 - dma->active_tx;
        dma->tx_ready[ready_idx] = false;
    } else {
        ready_idx = 1 - dma->active_rx;
        dma->rx_ready[ready_idx] = false;
    }
}

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

uint8_t audio_format_get_bytes_per_sample(audio_sample_format_t format)
{
    switch (format) {
        case AUDIO_FORMAT_S16_LE:
            return 2;
        case AUDIO_FORMAT_S24_LE:
        case AUDIO_FORMAT_S32_LE:
            return 4;
        case AUDIO_FORMAT_S24_3LE:
            return 3;
        default:
            return 2;
    }
}

uint32_t audio_calculate_frame_size(uint32_t sample_rate, uint8_t channels,
                                     audio_sample_format_t format,
                                     uint32_t frame_duration_us)
{
    uint32_t samples_per_frame;
    uint8_t bytes_per_sample;

    samples_per_frame = (sample_rate * frame_duration_us) / 1000000;
    bytes_per_sample = audio_format_get_bytes_per_sample(format);

    return samples_per_frame * channels * bytes_per_sample;
}

int audio_convert_format(const void *src, audio_sample_format_t src_format,
                          void *dst, audio_sample_format_t dst_format,
                          uint32_t num_samples)
{
    if (src == NULL || dst == NULL || num_samples == 0) {
        return AUDIO_BUF_ERROR_INVALID_PARAM;
    }

    /* S16 to S16 (copy) */
    if (src_format == AUDIO_FORMAT_S16_LE && dst_format == AUDIO_FORMAT_S16_LE) {
        memcpy(dst, src, num_samples * 2);
        return AUDIO_BUF_OK;
    }

    /* S24 (in 32-bit) to S16 */
    if (src_format == AUDIO_FORMAT_S24_LE && dst_format == AUDIO_FORMAT_S16_LE) {
        const int32_t *s = (const int32_t*)src;
        int16_t *d = (int16_t*)dst;
        for (uint32_t i = 0; i < num_samples; i++) {
            d[i] = (int16_t)(s[i] >> 8);
        }
        return AUDIO_BUF_OK;
    }

    /* S32 to S16 */
    if (src_format == AUDIO_FORMAT_S32_LE && dst_format == AUDIO_FORMAT_S16_LE) {
        const int32_t *s = (const int32_t*)src;
        int16_t *d = (int16_t*)dst;
        for (uint32_t i = 0; i < num_samples; i++) {
            d[i] = (int16_t)(s[i] >> 16);
        }
        return AUDIO_BUF_OK;
    }

    /* S16 to S24 (in 32-bit) */
    if (src_format == AUDIO_FORMAT_S16_LE && dst_format == AUDIO_FORMAT_S24_LE) {
        const int16_t *s = (const int16_t*)src;
        int32_t *d = (int32_t*)dst;
        for (uint32_t i = 0; i < num_samples; i++) {
            d[i] = ((int32_t)s[i]) << 8;
        }
        return AUDIO_BUF_OK;
    }

    /* S16 to S32 */
    if (src_format == AUDIO_FORMAT_S16_LE && dst_format == AUDIO_FORMAT_S32_LE) {
        const int16_t *s = (const int16_t*)src;
        int32_t *d = (int32_t*)dst;
        for (uint32_t i = 0; i < num_samples; i++) {
            d[i] = ((int32_t)s[i]) << 16;
        }
        return AUDIO_BUF_OK;
    }

    return AUDIO_BUF_ERROR_INVALID_PARAM;
}

void audio_interleave_stereo(const int16_t *left, const int16_t *right,
                              int16_t *stereo, uint32_t num_samples)
{
    if (left == NULL || right == NULL || stereo == NULL) {
        return;
    }

    for (uint32_t i = 0; i < num_samples; i++) {
        stereo[i * 2] = left[i];
        stereo[i * 2 + 1] = right[i];
    }
}

void audio_deinterleave_stereo(const int16_t *stereo, int16_t *left,
                                int16_t *right, uint32_t num_samples)
{
    if (stereo == NULL || left == NULL || right == NULL) {
        return;
    }

    for (uint32_t i = 0; i < num_samples; i++) {
        left[i] = stereo[i * 2];
        right[i] = stereo[i * 2 + 1];
    }
}

void audio_mix_to_mono(const int16_t *input, int16_t *output,
                        uint32_t num_samples, uint8_t channels)
{
    if (input == NULL || output == NULL || channels == 0) {
        return;
    }

    if (channels == 1) {
        memcpy(output, input, num_samples * sizeof(int16_t));
        return;
    }

    for (uint32_t i = 0; i < num_samples; i++) {
        int32_t sum = 0;
        for (uint8_t ch = 0; ch < channels; ch++) {
            sum += input[i * channels + ch];
        }
        output[i] = (int16_t)(sum / channels);
    }
}

void audio_apply_gain(int16_t *samples, uint32_t num_samples, int8_t gain_db)
{
    int32_t multiplier;
    int32_t sample;

    if (samples == NULL || num_samples == 0 || gain_db == 0) {
        return;
    }

    /* Simple linear approximation: 6dB ~= 2x */
    if (gain_db > 0) {
        multiplier = 1 << (gain_db / 6);
    } else {
        multiplier = 1;
        int shift = (-gain_db) / 6;
        for (uint32_t i = 0; i < num_samples; i++) {
            samples[i] = samples[i] >> shift;
        }
        return;
    }

    for (uint32_t i = 0; i < num_samples; i++) {
        sample = (int32_t)samples[i] * multiplier;
        /* Clamp to int16 range */
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        samples[i] = (int16_t)sample;
    }
}

void audio_generate_silence(void *buffer, uint32_t num_samples,
                             audio_sample_format_t format)
{
    uint32_t size;

    if (buffer == NULL || num_samples == 0) {
        return;
    }

    size = num_samples * audio_format_get_bytes_per_sample(format);
    memset(buffer, 0, size);
}

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

static inline uint32_t ring_next(uint32_t index, uint32_t capacity)
{
    return (index + 1) % capacity;
}

static inline uint32_t ring_level(uint32_t head, uint32_t tail, uint32_t capacity)
{
    if (head >= tail) {
        return head - tail;
    } else {
        return capacity - tail + head;
    }
}

static void check_thresholds(audio_buffer_t *buffer, uint32_t old_level, uint32_t new_level)
{
    /* Check high threshold (crossed upward) */
    if (buffer->config.high_cb != NULL &&
        old_level < buffer->config.high_threshold &&
        new_level >= buffer->config.high_threshold) {
        buffer->config.high_cb(buffer, new_level, buffer->config.user_data);
    }

    /* Check low threshold (crossed downward) */
    if (buffer->config.low_cb != NULL &&
        old_level > buffer->config.low_threshold &&
        new_level <= buffer->config.low_threshold) {
        buffer->config.low_cb(buffer, new_level, buffer->config.user_data);
    }
}
