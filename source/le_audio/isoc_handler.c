/**
 * @file isoc_handler.c
 * @brief Isochronous Data Path Handler Implementation
 *
 * Manages the flow of isochronous audio data between LC3 codec and HCI layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "isoc_handler.h"
#include <string.h>
#include <stdlib.h>

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Stream flags */
#define STREAM_FLAG_ACTIVE      0x01
#define STREAM_FLAG_LINKED      0x02
#define STREAM_FLAG_DATA_PATH   0x04

/** Ring buffer size calculation */
#define RING_BUFFER_SIZE(depth) ((depth) + 1)

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/** Ring buffer for SDUs */
typedef struct {
    isoc_sdu_t *buffer;             /**< SDU buffer array */
    uint8_t depth;                  /**< Buffer depth */
    volatile uint8_t head;          /**< Write index */
    volatile uint8_t tail;          /**< Read index */
} sdu_ring_buffer_t;

/** Stream context */
struct isoc_stream_s {
    bool in_use;                    /**< Stream slot is allocated */
    uint8_t id;                     /**< Stream ID */
    uint8_t flags;                  /**< Stream flags */
    isoc_stream_state_t state;      /**< Current state */
    isoc_stream_config_t config;    /**< Stream configuration */

    /* Buffers */
    sdu_ring_buffer_t tx_buffer;    /**< TX ring buffer */
    sdu_ring_buffer_t rx_buffer;    /**< RX ring buffer */
    isoc_sdu_t *tx_sdu_pool;        /**< TX SDU pool */
    isoc_sdu_t *rx_sdu_pool;        /**< RX SDU pool */

    /* Sequence numbers */
    uint16_t tx_seq_num;            /**< TX sequence number */
    uint16_t rx_seq_num;            /**< Expected RX sequence number */

    /* Timing */
    uint32_t last_tx_timestamp;     /**< Last TX timestamp */
    uint32_t last_rx_timestamp;     /**< Last RX timestamp */
    uint32_t next_tx_time;          /**< Next TX presentation time */
    int32_t clock_offset;           /**< Clock offset for sync */

    /* Linked streams */
    uint8_t link_group;             /**< Link group ID (0 = unlinked) */

    /* Statistics */
    isoc_stream_stats_t stats;      /**< Stream statistics */
};

/** Handler context */
typedef struct {
    bool initialized;
    isoc_handler_config_t config;
    isoc_stream_t streams[ISOC_HANDLER_MAX_STREAMS];
    uint8_t next_link_group;        /**< Next link group ID */

    /* Global timestamp reference */
    uint32_t base_timestamp;        /**< Base timestamp for synchronization */
} isoc_handler_context_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static isoc_handler_context_t handler_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static isoc_stream_t* alloc_stream(void);
static void free_stream(isoc_stream_t *stream);
static isoc_stream_t* get_stream(uint8_t stream_id);
static int init_buffers(isoc_stream_t *stream);
static void deinit_buffers(isoc_stream_t *stream);

/* Ring buffer operations */
static void ring_buffer_init(sdu_ring_buffer_t *rb, isoc_sdu_t *pool, uint8_t depth);
static bool ring_buffer_is_full(const sdu_ring_buffer_t *rb);
static bool ring_buffer_is_empty(const sdu_ring_buffer_t *rb);
static uint8_t ring_buffer_level(const sdu_ring_buffer_t *rb);
static isoc_sdu_t* ring_buffer_get_write_ptr(sdu_ring_buffer_t *rb);
static void ring_buffer_commit_write(sdu_ring_buffer_t *rb);
static isoc_sdu_t* ring_buffer_get_read_ptr(sdu_ring_buffer_t *rb);
static void ring_buffer_commit_read(sdu_ring_buffer_t *rb);

/* Data path operations */
static int send_iso_data(isoc_stream_t *stream, const isoc_sdu_t *sdu);
static void process_stream_tx(isoc_stream_t *stream);
static void process_stream_rx(isoc_stream_t *stream);

/* State management */
static void set_stream_state(isoc_stream_t *stream, isoc_stream_state_t state);
static void notify_state_change(isoc_stream_t *stream);
static void notify_error(isoc_stream_t *stream, int error);

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

int isoc_handler_init(const isoc_handler_config_t *config)
{
    if (handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_ALREADY_INITIALIZED;
    }

    memset(&handler_ctx, 0, sizeof(handler_ctx));

    if (config != NULL) {
        memcpy(&handler_ctx.config, config, sizeof(isoc_handler_config_t));
    }

    /* Initialize stream slots */
    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        handler_ctx.streams[i].id = i;
        handler_ctx.streams[i].in_use = false;
        handler_ctx.streams[i].state = ISOC_STREAM_STATE_IDLE;
    }

    handler_ctx.next_link_group = 1;  /* Group 0 is reserved for unlinked */
    handler_ctx.initialized = true;

    return ISOC_HANDLER_OK;
}

void isoc_handler_deinit(void)
{
    if (!handler_ctx.initialized) {
        return;
    }

    /* Destroy all streams */
    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        if (handler_ctx.streams[i].in_use) {
            isoc_handler_destroy_stream(i);
        }
    }

    handler_ctx.initialized = false;
}

bool isoc_handler_is_initialized(void)
{
    return handler_ctx.initialized;
}

/*******************************************************************************
 * API Functions - Stream Management
 ******************************************************************************/

int isoc_handler_create_stream(const isoc_stream_config_t *config,
                                uint8_t *stream_id)
{
    isoc_stream_t *stream;
    int result;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL || stream_id == NULL) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    /* Allocate stream */
    stream = alloc_stream();
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NO_RESOURCES;
    }

    /* Copy configuration */
    memcpy(&stream->config, config, sizeof(isoc_stream_config_t));

    /* Set default buffer depth if not specified */
    if (stream->config.buffer_depth == 0) {
        stream->config.buffer_depth = ISOC_HANDLER_DEFAULT_BUFFER_DEPTH;
    }
    if (stream->config.buffer_depth > ISOC_HANDLER_MAX_BUFFER_DEPTH) {
        stream->config.buffer_depth = ISOC_HANDLER_MAX_BUFFER_DEPTH;
    }

    /* Initialize buffers */
    result = init_buffers(stream);
    if (result != ISOC_HANDLER_OK) {
        free_stream(stream);
        return result;
    }

    /* Initialize sequence numbers */
    stream->tx_seq_num = 0;
    stream->rx_seq_num = 0;

    /* Set state */
    set_stream_state(stream, ISOC_STREAM_STATE_CONFIGURED);

    *stream_id = stream->id;
    return ISOC_HANDLER_OK;
}

int isoc_handler_destroy_stream(uint8_t stream_id)
{
    isoc_stream_t *stream;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    /* Stop stream if active */
    if (stream->state == ISOC_STREAM_STATE_ACTIVE) {
        isoc_handler_stop_stream(stream_id);
    }

    /* Remove data path if configured */
    if (stream->flags & STREAM_FLAG_DATA_PATH) {
        isoc_handler_remove_data_path(stream_id);
    }

    /* Free buffers */
    deinit_buffers(stream);

    /* Free stream */
    free_stream(stream);

    return ISOC_HANDLER_OK;
}

int isoc_handler_start_stream(uint8_t stream_id)
{
    isoc_stream_t *stream;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    if (stream->state != ISOC_STREAM_STATE_CONFIGURED) {
        return ISOC_HANDLER_ERROR_INVALID_STATE;
    }

    /* Verify data path is set up */
    if (!(stream->flags & STREAM_FLAG_DATA_PATH)) {
        return ISOC_HANDLER_ERROR_INVALID_STATE;
    }

    set_stream_state(stream, ISOC_STREAM_STATE_STARTING);

    /* Initialize timing */
    stream->last_tx_timestamp = isoc_handler_get_timestamp();
    stream->next_tx_time = stream->last_tx_timestamp + stream->config.sdu_interval_us;

    /* Mark as active */
    stream->flags |= STREAM_FLAG_ACTIVE;
    set_stream_state(stream, ISOC_STREAM_STATE_ACTIVE);

    return ISOC_HANDLER_OK;
}

int isoc_handler_stop_stream(uint8_t stream_id)
{
    isoc_stream_t *stream;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    if (stream->state != ISOC_STREAM_STATE_ACTIVE &&
        stream->state != ISOC_STREAM_STATE_STARTING) {
        return ISOC_HANDLER_ERROR_INVALID_STATE;
    }

    set_stream_state(stream, ISOC_STREAM_STATE_STOPPING);

    /* Clear active flag */
    stream->flags &= ~STREAM_FLAG_ACTIVE;

    set_stream_state(stream, ISOC_STREAM_STATE_CONFIGURED);

    return ISOC_HANDLER_OK;
}

isoc_stream_state_t isoc_handler_get_stream_state(uint8_t stream_id)
{
    isoc_stream_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_STREAM_STATE_IDLE;
    }
    return stream->state;
}

int isoc_handler_get_stream_stats(uint8_t stream_id, isoc_stream_stats_t *stats)
{
    isoc_stream_t *stream;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (stats == NULL) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    memcpy(stats, &stream->stats, sizeof(isoc_stream_stats_t));
    return ISOC_HANDLER_OK;
}

int isoc_handler_reset_stream_stats(uint8_t stream_id)
{
    isoc_stream_t *stream;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    memset(&stream->stats, 0, sizeof(isoc_stream_stats_t));
    return ISOC_HANDLER_OK;
}

/*******************************************************************************
 * API Functions - Data Path Setup
 ******************************************************************************/

int isoc_handler_setup_data_path(uint8_t stream_id)
{
    isoc_stream_t *stream;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    if (stream->state != ISOC_STREAM_STATE_CONFIGURED) {
        return ISOC_HANDLER_ERROR_INVALID_STATE;
    }

    /*
     * TODO: Setup HCI ISO data path
     *
     * For CIS (unicast):
     * hci_isoc_setup_data_path(
     *     stream->config.iso_handle,
     *     stream->config.direction == ISOC_PATH_DIRECTION_TX ? 0 : 1,
     *     HCI_ISO_DATA_PATH_HCI,  // Use HCI transport
     *     0x06,  // LC3 codec ID
     *     0,     // No controller delay
     *     NULL, 0  // No codec config
     * );
     *
     * For bidirectional, setup both directions:
     * if (stream->config.direction == ISOC_PATH_DIRECTION_BIDIR) {
     *     hci_isoc_setup_data_path(handle, 0, ...);  // TX
     *     hci_isoc_setup_data_path(handle, 1, ...);  // RX
     * }
     */

    stream->flags |= STREAM_FLAG_DATA_PATH;

    return ISOC_HANDLER_OK;
}

int isoc_handler_remove_data_path(uint8_t stream_id)
{
    isoc_stream_t *stream;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    if (!(stream->flags & STREAM_FLAG_DATA_PATH)) {
        return ISOC_HANDLER_OK;  /* Already removed */
    }

    /*
     * TODO: Remove HCI ISO data path
     *
     * hci_isoc_remove_data_path(stream->config.iso_handle, direction);
     */

    stream->flags &= ~STREAM_FLAG_DATA_PATH;

    return ISOC_HANDLER_OK;
}

/*******************************************************************************
 * API Functions - Data Transmission (TX)
 ******************************************************************************/

int isoc_handler_tx_frame(uint8_t stream_id, const uint8_t *lc3_data,
                           uint16_t lc3_len, uint32_t timestamp)
{
    isoc_stream_t *stream;
    isoc_sdu_t *sdu;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (lc3_data == NULL || lc3_len == 0 || lc3_len > ISOC_HANDLER_MAX_SDU_SIZE) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    if (stream->state != ISOC_STREAM_STATE_ACTIVE) {
        return ISOC_HANDLER_ERROR_INVALID_STATE;
    }

    /* Check buffer space */
    if (ring_buffer_is_full(&stream->tx_buffer)) {
        stream->stats.tx_underruns++;
        return ISOC_HANDLER_ERROR_BUFFER_FULL;
    }

    /* Get write slot */
    sdu = ring_buffer_get_write_ptr(&stream->tx_buffer);
    if (sdu == NULL) {
        return ISOC_HANDLER_ERROR_BUFFER_FULL;
    }

    /* Fill SDU */
    memcpy(sdu->data, lc3_data, lc3_len);
    sdu->length = lc3_len;
    sdu->sequence_number = stream->tx_seq_num++;
    sdu->timestamp = (timestamp != 0) ? timestamp : isoc_handler_get_timestamp();
    sdu->num_frames = 1;
    sdu->valid = true;

    /* Commit to buffer */
    ring_buffer_commit_write(&stream->tx_buffer);

    return ISOC_HANDLER_OK;
}

int isoc_handler_tx_sdu(uint8_t stream_id, const isoc_sdu_t *sdu)
{
    isoc_stream_t *stream;
    isoc_sdu_t *dest;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (sdu == NULL) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    if (stream->state != ISOC_STREAM_STATE_ACTIVE) {
        return ISOC_HANDLER_ERROR_INVALID_STATE;
    }

    if (ring_buffer_is_full(&stream->tx_buffer)) {
        stream->stats.tx_underruns++;
        return ISOC_HANDLER_ERROR_BUFFER_FULL;
    }

    dest = ring_buffer_get_write_ptr(&stream->tx_buffer);
    if (dest == NULL) {
        return ISOC_HANDLER_ERROR_BUFFER_FULL;
    }

    memcpy(dest, sdu, sizeof(isoc_sdu_t));
    dest->sequence_number = stream->tx_seq_num++;

    ring_buffer_commit_write(&stream->tx_buffer);

    return ISOC_HANDLER_OK;
}

int isoc_handler_get_tx_buffer_level(uint8_t stream_id)
{
    isoc_stream_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }
    return ring_buffer_level(&stream->tx_buffer);
}

bool isoc_handler_tx_buffer_has_space(uint8_t stream_id)
{
    isoc_stream_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return false;
    }
    return !ring_buffer_is_full(&stream->tx_buffer);
}

/*******************************************************************************
 * API Functions - Data Reception (RX)
 ******************************************************************************/

int isoc_handler_rx_frame(uint8_t stream_id, uint8_t *lc3_data,
                           uint16_t max_len, uint16_t *lc3_len,
                           uint32_t *timestamp)
{
    isoc_stream_t *stream;
    isoc_sdu_t *sdu;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (lc3_data == NULL || lc3_len == NULL) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    if (ring_buffer_is_empty(&stream->rx_buffer)) {
        return ISOC_HANDLER_ERROR_BUFFER_EMPTY;
    }

    sdu = ring_buffer_get_read_ptr(&stream->rx_buffer);
    if (sdu == NULL || !sdu->valid) {
        return ISOC_HANDLER_ERROR_BUFFER_EMPTY;
    }

    /* Copy data */
    uint16_t copy_len = (sdu->length < max_len) ? sdu->length : max_len;
    memcpy(lc3_data, sdu->data, copy_len);
    *lc3_len = copy_len;

    if (timestamp != NULL) {
        *timestamp = sdu->timestamp;
    }

    ring_buffer_commit_read(&stream->rx_buffer);

    return ISOC_HANDLER_OK;
}

int isoc_handler_rx_sdu(uint8_t stream_id, isoc_sdu_t *sdu)
{
    isoc_stream_t *stream;
    isoc_sdu_t *src;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (sdu == NULL) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    if (ring_buffer_is_empty(&stream->rx_buffer)) {
        return ISOC_HANDLER_ERROR_BUFFER_EMPTY;
    }

    src = ring_buffer_get_read_ptr(&stream->rx_buffer);
    if (src == NULL || !src->valid) {
        return ISOC_HANDLER_ERROR_BUFFER_EMPTY;
    }

    memcpy(sdu, src, sizeof(isoc_sdu_t));
    ring_buffer_commit_read(&stream->rx_buffer);

    return ISOC_HANDLER_OK;
}

int isoc_handler_get_rx_buffer_level(uint8_t stream_id)
{
    isoc_stream_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }
    return ring_buffer_level(&stream->rx_buffer);
}

bool isoc_handler_rx_data_available(uint8_t stream_id)
{
    isoc_stream_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return false;
    }
    return !ring_buffer_is_empty(&stream->rx_buffer);
}

/*******************************************************************************
 * API Functions - Timing and Synchronization
 ******************************************************************************/

uint32_t isoc_handler_get_timestamp(void)
{
    /*
     * TODO: Get actual Bluetooth ISO timestamp from controller
     *
     * This should use the controller's ISO reference clock.
     * For now, use system tick as placeholder.
     *
     * return hci_isoc_get_timestamp();
     */

    /* Placeholder: use system time in microseconds */
    static uint32_t fake_timestamp = 0;
    fake_timestamp += 10000;  /* Increment by 10ms */
    return fake_timestamp;
}

uint32_t isoc_handler_get_next_tx_time(uint8_t stream_id)
{
    isoc_stream_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return 0;
    }
    return stream->next_tx_time;
}

int isoc_handler_sync_timing(uint8_t stream_id, uint32_t reference_timestamp)
{
    isoc_stream_t *stream;
    uint32_t local_time;
    int32_t offset;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return ISOC_HANDLER_ERROR_NOT_FOUND;
    }

    /* Calculate clock offset */
    local_time = isoc_handler_get_timestamp();
    offset = (int32_t)(reference_timestamp - local_time);

    /* Apply smoothing filter to avoid sudden jumps */
    stream->clock_offset = (stream->clock_offset * 7 + offset) / 8;

    return ISOC_HANDLER_OK;
}

/*******************************************************************************
 * API Functions - Multi-Stream Coordination
 ******************************************************************************/

int isoc_handler_link_streams(const uint8_t *stream_ids, uint8_t count)
{
    uint8_t group_id;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (stream_ids == NULL || count < 2) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    /* Verify all streams exist */
    for (int i = 0; i < count; i++) {
        if (get_stream(stream_ids[i]) == NULL) {
            return ISOC_HANDLER_ERROR_NOT_FOUND;
        }
    }

    /* Allocate link group */
    group_id = handler_ctx.next_link_group++;
    if (handler_ctx.next_link_group == 0) {
        handler_ctx.next_link_group = 1;  /* Skip 0 */
    }

    /* Link all streams */
    for (int i = 0; i < count; i++) {
        isoc_stream_t *stream = get_stream(stream_ids[i]);
        stream->link_group = group_id;
        stream->flags |= STREAM_FLAG_LINKED;
    }

    return ISOC_HANDLER_OK;
}

int isoc_handler_unlink_streams(const uint8_t *stream_ids, uint8_t count)
{
    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (stream_ids == NULL || count == 0) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    for (int i = 0; i < count; i++) {
        isoc_stream_t *stream = get_stream(stream_ids[i]);
        if (stream != NULL) {
            stream->link_group = 0;
            stream->flags &= ~STREAM_FLAG_LINKED;
        }
    }

    return ISOC_HANDLER_OK;
}

int isoc_handler_start_linked_streams(const uint8_t *stream_ids, uint8_t count)
{
    int result;

    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (stream_ids == NULL || count == 0) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    /* Start all streams atomically */
    for (int i = 0; i < count; i++) {
        result = isoc_handler_start_stream(stream_ids[i]);
        if (result != ISOC_HANDLER_OK) {
            /* Rollback on failure */
            for (int j = 0; j < i; j++) {
                isoc_handler_stop_stream(stream_ids[j]);
            }
            return result;
        }
    }

    /* Synchronize timing across linked streams */
    uint32_t sync_time = isoc_handler_get_timestamp();
    for (int i = 0; i < count; i++) {
        isoc_stream_t *stream = get_stream(stream_ids[i]);
        if (stream != NULL) {
            stream->next_tx_time = sync_time + stream->config.sdu_interval_us;
        }
    }

    return ISOC_HANDLER_OK;
}

int isoc_handler_stop_linked_streams(const uint8_t *stream_ids, uint8_t count)
{
    if (!handler_ctx.initialized) {
        return ISOC_HANDLER_ERROR_NOT_INITIALIZED;
    }

    if (stream_ids == NULL || count == 0) {
        return ISOC_HANDLER_ERROR_INVALID_PARAM;
    }

    for (int i = 0; i < count; i++) {
        isoc_handler_stop_stream(stream_ids[i]);
    }

    return ISOC_HANDLER_OK;
}

/*******************************************************************************
 * API Functions - Stream Lookup
 ******************************************************************************/

int isoc_handler_find_by_iso_handle(uint16_t iso_handle)
{
    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        if (handler_ctx.streams[i].in_use &&
            handler_ctx.streams[i].config.iso_handle == iso_handle) {
            return i;
        }
    }
    return ISOC_HANDLER_ERROR_NOT_FOUND;
}

int isoc_handler_find_by_acl_handle(uint16_t acl_handle)
{
    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        if (handler_ctx.streams[i].in_use &&
            handler_ctx.streams[i].config.type == ISOC_STREAM_TYPE_CIS &&
            handler_ctx.streams[i].config.acl_handle == acl_handle) {
            return i;
        }
    }
    return ISOC_HANDLER_ERROR_NOT_FOUND;
}

int isoc_handler_find_by_big_bis(uint8_t big_handle, uint8_t bis_index)
{
    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        if (handler_ctx.streams[i].in_use &&
            (handler_ctx.streams[i].config.type == ISOC_STREAM_TYPE_BIS_SOURCE ||
             handler_ctx.streams[i].config.type == ISOC_STREAM_TYPE_BIS_SINK) &&
            handler_ctx.streams[i].config.big_handle == big_handle &&
            handler_ctx.streams[i].config.bis_index == bis_index) {
            return i;
        }
    }
    return ISOC_HANDLER_ERROR_NOT_FOUND;
}

/*******************************************************************************
 * API Functions - Processing
 ******************************************************************************/

void isoc_handler_process_tx(void)
{
    if (!handler_ctx.initialized) {
        return;
    }

    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        if (handler_ctx.streams[i].in_use &&
            (handler_ctx.streams[i].flags & STREAM_FLAG_ACTIVE) &&
            (handler_ctx.streams[i].config.direction == ISOC_PATH_DIRECTION_TX ||
             handler_ctx.streams[i].config.direction == ISOC_PATH_DIRECTION_BIDIR)) {
            process_stream_tx(&handler_ctx.streams[i]);
        }
    }
}

void isoc_handler_process_rx(void)
{
    if (!handler_ctx.initialized) {
        return;
    }

    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        if (handler_ctx.streams[i].in_use &&
            (handler_ctx.streams[i].flags & STREAM_FLAG_ACTIVE) &&
            (handler_ctx.streams[i].config.direction == ISOC_PATH_DIRECTION_RX ||
             handler_ctx.streams[i].config.direction == ISOC_PATH_DIRECTION_BIDIR)) {
            process_stream_rx(&handler_ctx.streams[i]);
        }
    }
}

void isoc_handler_process(void)
{
    isoc_handler_process_tx();
    isoc_handler_process_rx();
}

/*******************************************************************************
 * API Functions - HCI Event Handlers
 ******************************************************************************/

void isoc_handler_on_data_received(uint16_t iso_handle, const uint8_t *data,
                                    uint16_t len, uint32_t timestamp,
                                    uint16_t seq_num, uint8_t status)
{
    int stream_id;
    isoc_stream_t *stream;
    isoc_sdu_t *sdu;

    stream_id = isoc_handler_find_by_iso_handle(iso_handle);
    if (stream_id < 0) {
        return;
    }

    stream = get_stream(stream_id);
    if (stream == NULL || !(stream->flags & STREAM_FLAG_ACTIVE)) {
        return;
    }

    /* Check for lost frames */
    if (seq_num != stream->rx_seq_num) {
        uint16_t lost = seq_num - stream->rx_seq_num;
        stream->stats.rx_lost_frames += lost;
    }
    stream->rx_seq_num = seq_num + 1;

    /* Check status for errors */
    if (status != 0) {
        stream->stats.rx_crc_errors++;
        /* Still try to decode - LC3 can handle some errors */
    }

    /* Queue received data */
    if (ring_buffer_is_full(&stream->rx_buffer)) {
        stream->stats.rx_overruns++;
        /* Drop oldest frame to make room */
        ring_buffer_commit_read(&stream->rx_buffer);
    }

    sdu = ring_buffer_get_write_ptr(&stream->rx_buffer);
    if (sdu == NULL) {
        return;
    }

    /* Copy data to buffer */
    uint16_t copy_len = (len < ISOC_HANDLER_MAX_SDU_SIZE) ? len : ISOC_HANDLER_MAX_SDU_SIZE;
    memcpy(sdu->data, data, copy_len);
    sdu->length = copy_len;
    sdu->sequence_number = seq_num;
    sdu->timestamp = timestamp;
    sdu->num_frames = 1;  /* Assume single frame for now */
    sdu->valid = (status == 0);

    ring_buffer_commit_write(&stream->rx_buffer);

    /* Update statistics */
    stream->stats.rx_frames++;
    stream->stats.rx_bytes += len;
    stream->last_rx_timestamp = timestamp;

    /* Calculate jitter */
    if (stream->stats.rx_frames > 1) {
        int32_t expected_delta = stream->config.sdu_interval_us;
        int32_t actual_delta = timestamp - stream->last_rx_timestamp;
        int32_t jitter = (actual_delta > expected_delta) ?
                         (actual_delta - expected_delta) :
                         (expected_delta - actual_delta);
        if ((uint32_t)jitter > stream->stats.max_jitter_us) {
            stream->stats.max_jitter_us = jitter;
        }
    }

    /* Deliver via callback if registered */
    if (handler_ctx.config.rx_callback != NULL) {
        handler_ctx.config.rx_callback(stream_id, sdu,
                                        handler_ctx.config.user_data);
    }
}

void isoc_handler_on_tx_complete(uint16_t iso_handle, uint8_t num_completed)
{
    int stream_id;
    isoc_stream_t *stream;

    stream_id = isoc_handler_find_by_iso_handle(iso_handle);
    if (stream_id < 0) {
        return;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return;
    }

    /* Update statistics */
    stream->stats.tx_frames += num_completed;

    /* Could trigger more TX if buffer has data */
    (void)num_completed;
}

void isoc_handler_on_cis_established(uint16_t acl_handle, uint16_t cis_handle)
{
    int stream_id;
    isoc_stream_t *stream;

    stream_id = isoc_handler_find_by_acl_handle(acl_handle);
    if (stream_id < 0) {
        return;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return;
    }

    /* Update ISO handle */
    stream->config.iso_handle = cis_handle;

    /* Setup data path automatically */
    isoc_handler_setup_data_path(stream_id);
}

void isoc_handler_on_cis_disconnected(uint16_t cis_handle, uint8_t reason)
{
    int stream_id;
    isoc_stream_t *stream;

    (void)reason;

    stream_id = isoc_handler_find_by_iso_handle(cis_handle);
    if (stream_id < 0) {
        return;
    }

    stream = get_stream(stream_id);
    if (stream == NULL) {
        return;
    }

    /* Stop stream */
    if (stream->flags & STREAM_FLAG_ACTIVE) {
        isoc_handler_stop_stream(stream_id);
    }

    /* Remove data path */
    isoc_handler_remove_data_path(stream_id);

    /* Set error state */
    set_stream_state(stream, ISOC_STREAM_STATE_ERROR);
    notify_error(stream, reason);
}

void isoc_handler_on_big_created(uint8_t big_handle, const uint16_t *bis_handles,
                                  uint8_t num_bis)
{
    /* Update BIS handles for matching streams */
    for (int i = 0; i < num_bis && i < ISOC_HANDLER_MAX_STREAMS; i++) {
        int stream_id = isoc_handler_find_by_big_bis(big_handle, i);
        if (stream_id >= 0) {
            isoc_stream_t *stream = get_stream(stream_id);
            if (stream != NULL) {
                stream->config.iso_handle = bis_handles[i];
                isoc_handler_setup_data_path(stream_id);
            }
        }
    }
}

void isoc_handler_on_big_terminated(uint8_t big_handle, uint8_t reason)
{
    (void)reason;

    /* Stop all streams in this BIG */
    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        isoc_stream_t *stream = &handler_ctx.streams[i];
        if (stream->in_use &&
            stream->config.type == ISOC_STREAM_TYPE_BIS_SOURCE &&
            stream->config.big_handle == big_handle) {
            if (stream->flags & STREAM_FLAG_ACTIVE) {
                isoc_handler_stop_stream(i);
            }
            isoc_handler_remove_data_path(i);
            set_stream_state(stream, ISOC_STREAM_STATE_CONFIGURED);
        }
    }
}

void isoc_handler_on_big_sync_established(uint8_t big_handle,
                                           const uint16_t *bis_handles,
                                           uint8_t num_bis)
{
    /* Similar to big_created but for sink role */
    for (int i = 0; i < num_bis && i < ISOC_HANDLER_MAX_STREAMS; i++) {
        int stream_id = isoc_handler_find_by_big_bis(big_handle, i);
        if (stream_id >= 0) {
            isoc_stream_t *stream = get_stream(stream_id);
            if (stream != NULL && stream->config.type == ISOC_STREAM_TYPE_BIS_SINK) {
                stream->config.iso_handle = bis_handles[i];
                isoc_handler_setup_data_path(stream_id);
            }
        }
    }
}

void isoc_handler_on_big_sync_lost(uint8_t big_handle, uint8_t reason)
{
    (void)reason;

    /* Stop all sink streams in this BIG */
    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        isoc_stream_t *stream = &handler_ctx.streams[i];
        if (stream->in_use &&
            stream->config.type == ISOC_STREAM_TYPE_BIS_SINK &&
            stream->config.big_handle == big_handle) {
            if (stream->flags & STREAM_FLAG_ACTIVE) {
                isoc_handler_stop_stream(i);
            }
            set_stream_state(stream, ISOC_STREAM_STATE_ERROR);
            notify_error(stream, reason);
        }
    }
}

/*******************************************************************************
 * Private Functions - Stream Management
 ******************************************************************************/

static isoc_stream_t* alloc_stream(void)
{
    for (int i = 0; i < ISOC_HANDLER_MAX_STREAMS; i++) {
        if (!handler_ctx.streams[i].in_use) {
            memset(&handler_ctx.streams[i], 0, sizeof(isoc_stream_t));
            handler_ctx.streams[i].id = i;
            handler_ctx.streams[i].in_use = true;
            handler_ctx.streams[i].state = ISOC_STREAM_STATE_IDLE;
            return &handler_ctx.streams[i];
        }
    }
    return NULL;
}

static void free_stream(isoc_stream_t *stream)
{
    if (stream != NULL) {
        stream->in_use = false;
        stream->state = ISOC_STREAM_STATE_IDLE;
        stream->flags = 0;
    }
}

static isoc_stream_t* get_stream(uint8_t stream_id)
{
    if (stream_id >= ISOC_HANDLER_MAX_STREAMS) {
        return NULL;
    }
    if (!handler_ctx.streams[stream_id].in_use) {
        return NULL;
    }
    return &handler_ctx.streams[stream_id];
}

static int init_buffers(isoc_stream_t *stream)
{
    uint8_t depth = stream->config.buffer_depth;
    uint8_t ring_size = RING_BUFFER_SIZE(depth);

    /* Allocate TX buffer pool */
    stream->tx_sdu_pool = (isoc_sdu_t*)malloc(sizeof(isoc_sdu_t) * ring_size);
    if (stream->tx_sdu_pool == NULL) {
        return ISOC_HANDLER_ERROR_NO_RESOURCES;
    }
    memset(stream->tx_sdu_pool, 0, sizeof(isoc_sdu_t) * ring_size);

    /* Allocate RX buffer pool */
    stream->rx_sdu_pool = (isoc_sdu_t*)malloc(sizeof(isoc_sdu_t) * ring_size);
    if (stream->rx_sdu_pool == NULL) {
        free(stream->tx_sdu_pool);
        stream->tx_sdu_pool = NULL;
        return ISOC_HANDLER_ERROR_NO_RESOURCES;
    }
    memset(stream->rx_sdu_pool, 0, sizeof(isoc_sdu_t) * ring_size);

    /* Initialize ring buffers */
    ring_buffer_init(&stream->tx_buffer, stream->tx_sdu_pool, depth);
    ring_buffer_init(&stream->rx_buffer, stream->rx_sdu_pool, depth);

    return ISOC_HANDLER_OK;
}

static void deinit_buffers(isoc_stream_t *stream)
{
    if (stream->tx_sdu_pool != NULL) {
        free(stream->tx_sdu_pool);
        stream->tx_sdu_pool = NULL;
    }
    if (stream->rx_sdu_pool != NULL) {
        free(stream->rx_sdu_pool);
        stream->rx_sdu_pool = NULL;
    }
}

/*******************************************************************************
 * Private Functions - Ring Buffer Operations
 ******************************************************************************/

static void ring_buffer_init(sdu_ring_buffer_t *rb, isoc_sdu_t *pool, uint8_t depth)
{
    rb->buffer = pool;
    rb->depth = depth;
    rb->head = 0;
    rb->tail = 0;
}

static bool ring_buffer_is_full(const sdu_ring_buffer_t *rb)
{
    uint8_t next = (rb->head + 1) % RING_BUFFER_SIZE(rb->depth);
    return (next == rb->tail);
}

static bool ring_buffer_is_empty(const sdu_ring_buffer_t *rb)
{
    return (rb->head == rb->tail);
}

static uint8_t ring_buffer_level(const sdu_ring_buffer_t *rb)
{
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    } else {
        return RING_BUFFER_SIZE(rb->depth) - rb->tail + rb->head;
    }
}

static isoc_sdu_t* ring_buffer_get_write_ptr(sdu_ring_buffer_t *rb)
{
    if (ring_buffer_is_full(rb)) {
        return NULL;
    }
    return &rb->buffer[rb->head];
}

static void ring_buffer_commit_write(sdu_ring_buffer_t *rb)
{
    rb->head = (rb->head + 1) % RING_BUFFER_SIZE(rb->depth);
}

static isoc_sdu_t* ring_buffer_get_read_ptr(sdu_ring_buffer_t *rb)
{
    if (ring_buffer_is_empty(rb)) {
        return NULL;
    }
    return &rb->buffer[rb->tail];
}

static void ring_buffer_commit_read(sdu_ring_buffer_t *rb)
{
    rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE(rb->depth);
}

/*******************************************************************************
 * Private Functions - Data Path Operations
 ******************************************************************************/

static int send_iso_data(isoc_stream_t *stream, const isoc_sdu_t *sdu)
{
    /*
     * TODO: Send ISO data via HCI
     *
     * hci_isoc_send_data(
     *     stream->config.iso_handle,
     *     sdu->timestamp,
     *     sdu->sequence_number,
     *     sdu->data,
     *     sdu->length
     * );
     */

    (void)stream;
    (void)sdu;

    return ISOC_HANDLER_OK;
}

static void process_stream_tx(isoc_stream_t *stream)
{
    uint32_t current_time = isoc_handler_get_timestamp();
    isoc_sdu_t sdu;

    /* Check if it's time to send */
    if ((int32_t)(stream->next_tx_time - current_time) > 0) {
        return;  /* Not yet time */
    }

    /* Try to get data from buffer */
    if (!ring_buffer_is_empty(&stream->tx_buffer)) {
        isoc_sdu_t *queued = ring_buffer_get_read_ptr(&stream->tx_buffer);
        if (queued != NULL && queued->valid) {
            send_iso_data(stream, queued);
            stream->stats.tx_bytes += queued->length;
            ring_buffer_commit_read(&stream->tx_buffer);
        }
    } else if (handler_ctx.config.tx_callback != NULL) {
        /* Request data via callback */
        memset(&sdu, 0, sizeof(sdu));
        if (handler_ctx.config.tx_callback(stream->id, &sdu,
                                            handler_ctx.config.user_data)) {
            sdu.sequence_number = stream->tx_seq_num++;
            sdu.timestamp = current_time;
            send_iso_data(stream, &sdu);
            stream->stats.tx_bytes += sdu.length;
        } else {
            /* No data available - underrun */
            stream->stats.tx_underruns++;
        }
    }

    /* Schedule next TX */
    stream->last_tx_timestamp = current_time;
    stream->next_tx_time += stream->config.sdu_interval_us;

    /* Handle timing drift - if we're behind, catch up */
    if ((int32_t)(stream->next_tx_time - current_time) < 0) {
        stream->stats.tx_late_frames++;
        stream->next_tx_time = current_time + stream->config.sdu_interval_us;
    }
}

static void process_stream_rx(isoc_stream_t *stream)
{
    /* RX is event-driven via isoc_handler_on_data_received */
    /* This function can be used for periodic maintenance */

    /* Update average latency calculation */
    if (stream->stats.rx_frames > 0) {
        /* Simple moving average for latency */
        uint32_t current_latency = isoc_handler_get_timestamp() - stream->last_rx_timestamp;
        stream->stats.avg_latency_us =
            (stream->stats.avg_latency_us * 7 + current_latency) / 8;
    }
}

/*******************************************************************************
 * Private Functions - State Management
 ******************************************************************************/

static void set_stream_state(isoc_stream_t *stream, isoc_stream_state_t state)
{
    if (stream->state != state) {
        stream->state = state;
        notify_state_change(stream);
    }
}

static void notify_state_change(isoc_stream_t *stream)
{
    if (handler_ctx.config.state_callback != NULL) {
        handler_ctx.config.state_callback(stream->id, stream->state,
                                           handler_ctx.config.user_data);
    }
}

static void notify_error(isoc_stream_t *stream, int error)
{
    if (handler_ctx.config.error_callback != NULL) {
        handler_ctx.config.error_callback(stream->id, error,
                                           handler_ctx.config.user_data);
    }
}
