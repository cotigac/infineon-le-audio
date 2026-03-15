/**
 * @file audio_task.c
 * @brief Audio Processing Task Implementation
 *
 * Implements the main FreeRTOS audio processing task for LE Audio.
 * Handles LC3 encoding/decoding and coordinates data flow between
 * I2S peripheral and ISOC handler.
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_task.h"
#include "audio_buffers.h"
#include "lc3_wrapper.h"
#include "isoc_handler.h"

#include <string.h>
#include <stdlib.h>

/* TODO: Include FreeRTOS headers when integrating with real RTOS */
/* #include "FreeRTOS.h" */
/* #include "task.h" */
/* #include "semphr.h" */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum PCM samples per frame (48kHz * 10ms * stereo) */
#define MAX_PCM_SAMPLES_PER_FRAME       (480 * 2)

/** Maximum LC3 bytes per frame */
#define MAX_LC3_BYTES_PER_FRAME         155

/** Stream state flags */
#define STREAM_FLAG_CONFIGURED          (1 << 0)
#define STREAM_FLAG_STARTED             (1 << 1)
#define STREAM_FLAG_SUSPENDED           (1 << 2)
#define STREAM_FLAG_ERROR               (1 << 7)

/** Task notification wait timeout (ms) */
#define NOTIFICATION_TIMEOUT_MS         100

/** Timing measurement macros */
#define GET_TIME_US()                   0 /* TODO: Replace with actual timer read */

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief Internal stream context
 */
typedef struct {
    audio_stream_t info;                /**< Public stream info */
    uint8_t flags;                      /**< Internal state flags */

    /* LC3 codec handles */
    void *encoder;                      /**< LC3 encoder instance */
    void *decoder;                      /**< LC3 decoder instance */

    /* Buffers */
    audio_ring_buffer_t *pcm_buffer;    /**< PCM ring buffer */
    audio_ring_buffer_t *lc3_buffer;    /**< LC3 ring buffer */

    /* Timing */
    uint32_t last_process_time;         /**< Last processing timestamp */
    uint16_t sequence_number;           /**< Frame sequence number */

    /* Per-stream stats */
    uint32_t frames_processed;
    uint32_t frames_dropped;
    uint32_t codec_errors;
} stream_context_t;

/**
 * @brief Audio task context
 */
typedef struct {
    /* Initialization state */
    bool initialized;
    audio_task_state_t state;

    /* Configuration */
    audio_task_config_t config;

    /* FreeRTOS handles */
    void *task_handle;                  /**< Task handle */
    void *state_mutex;                  /**< State protection mutex */

    /* Streams */
    stream_context_t streams[AUDIO_TASK_MAX_STREAMS];
    uint8_t active_stream_count;

    /* Volume control */
    uint8_t volume_percent;
    bool muted;

    /* PCM work buffers */
    int16_t pcm_rx_buffer[MAX_PCM_SAMPLES_PER_FRAME];
    int16_t pcm_tx_buffer[MAX_PCM_SAMPLES_PER_FRAME];

    /* LC3 work buffers */
    uint8_t lc3_encode_buffer[MAX_LC3_BYTES_PER_FRAME];
    uint8_t lc3_decode_buffer[MAX_LC3_BYTES_PER_FRAME];

    /* Statistics */
    audio_task_stats_t stats;
    uint32_t total_encode_time;
    uint32_t total_decode_time;
    uint32_t encode_count;
    uint32_t decode_count;

    /* CPU usage tracking */
    uint32_t busy_time_us;
    uint32_t idle_time_us;
    uint32_t last_usage_calc;
} audio_task_ctx_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static audio_task_ctx_t g_audio_task;

/*******************************************************************************
 * Private Function Declarations
 ******************************************************************************/

static void audio_task_main(void *pvParameters);
static int process_tx_path(stream_context_t *stream);
static int process_rx_path(stream_context_t *stream);
static int encode_pcm_to_lc3(stream_context_t *stream, const int16_t *pcm,
                             uint8_t *lc3, uint16_t *lc3_len);
static int decode_lc3_to_pcm(stream_context_t *stream, const uint8_t *lc3,
                             uint16_t lc3_len, int16_t *pcm);
static void apply_volume(int16_t *samples, uint16_t num_samples);
static void update_statistics(void);
static uint16_t calculate_samples_per_frame(const audio_stream_config_t *config);
static int find_free_stream_slot(void);
static stream_context_t* get_stream(uint8_t stream_id);
static void notify_state_change(audio_task_state_t new_state);
static void notify_stream_event(uint8_t stream_id, int event);

/*******************************************************************************
 * Public Functions - Initialization
 ******************************************************************************/

int audio_task_init(const audio_task_config_t *config)
{
    if (g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_ALREADY_INITIALIZED;
    }

    if (config == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    /* Clear context */
    memset(&g_audio_task, 0, sizeof(g_audio_task));

    /* Copy configuration */
    memcpy(&g_audio_task.config, config, sizeof(audio_task_config_t));

    /* Initialize defaults */
    g_audio_task.volume_percent = 100;
    g_audio_task.muted = false;
    g_audio_task.state = AUDIO_TASK_STATE_IDLE;

    /* Initialize streams */
    for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
        g_audio_task.streams[i].info.stream_id = i;
        g_audio_task.streams[i].info.active = false;
        g_audio_task.streams[i].flags = 0;
    }

    /* TODO: Create FreeRTOS mutex */
    /* g_audio_task.state_mutex = xSemaphoreCreateMutex(); */
    /* if (g_audio_task.state_mutex == NULL) { */
    /*     return AUDIO_TASK_ERROR_NO_RESOURCES; */
    /* } */

    /* TODO: Create FreeRTOS task */
    /* BaseType_t result = xTaskCreate( */
    /*     audio_task_main, */
    /*     config->task_name ? config->task_name : "AudioTask", */
    /*     config->stack_size, */
    /*     NULL, */
    /*     config->priority, */
    /*     (TaskHandle_t*)&g_audio_task.task_handle */
    /* ); */
    /* if (result != pdPASS) { */
    /*     vSemaphoreDelete(g_audio_task.state_mutex); */
    /*     return AUDIO_TASK_ERROR_TASK_CREATE_FAILED; */
    /* } */

    g_audio_task.initialized = true;

    return AUDIO_TASK_OK;
}

void audio_task_deinit(void)
{
    if (!g_audio_task.initialized) {
        return;
    }

    /* Stop all streams */
    for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
        if (g_audio_task.streams[i].info.active) {
            audio_task_stop_stream(i);
            audio_task_destroy_stream(i);
        }
    }

    /* TODO: Delete FreeRTOS task */
    /* if (g_audio_task.task_handle != NULL) { */
    /*     vTaskDelete((TaskHandle_t)g_audio_task.task_handle); */
    /*     g_audio_task.task_handle = NULL; */
    /* } */

    /* TODO: Delete mutex */
    /* if (g_audio_task.state_mutex != NULL) { */
    /*     vSemaphoreDelete(g_audio_task.state_mutex); */
    /*     g_audio_task.state_mutex = NULL; */
    /* } */

    g_audio_task.initialized = false;
    g_audio_task.state = AUDIO_TASK_STATE_IDLE;
}

bool audio_task_is_initialized(void)
{
    return g_audio_task.initialized;
}

audio_task_state_t audio_task_get_state(void)
{
    return g_audio_task.state;
}

/*******************************************************************************
 * Public Functions - Stream Management
 ******************************************************************************/

int audio_task_create_stream(audio_stream_direction_t direction,
                             audio_stream_type_t type,
                             const audio_stream_config_t *config,
                             uint8_t *stream_id)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL || stream_id == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    /* Validate configuration */
    if (config->sample_rate < 8000 || config->sample_rate > 48000) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }
    if (config->channels < 1 || config->channels > 2) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }
    if (config->frame_duration_us != 7500 && config->frame_duration_us != 10000) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    /* Find free slot */
    int slot = find_free_stream_slot();
    if (slot < 0) {
        return AUDIO_TASK_ERROR_NO_RESOURCES;
    }

    stream_context_t *stream = &g_audio_task.streams[slot];

    /* Initialize stream info */
    stream->info.stream_id = slot;
    stream->info.direction = direction;
    stream->info.type = type;
    memcpy(&stream->info.config, config, sizeof(audio_stream_config_t));
    stream->info.active = false;
    stream->flags = 0;
    stream->sequence_number = 0;
    stream->frames_processed = 0;
    stream->frames_dropped = 0;
    stream->codec_errors = 0;

    /* Calculate buffer sizes */
    uint16_t samples_per_frame = calculate_samples_per_frame(config);
    uint16_t pcm_frame_size = samples_per_frame * config->channels * sizeof(int16_t);
    uint16_t lc3_frame_size = config->octets_per_frame;

    /* Create PCM buffer */
    audio_ring_buffer_config_t pcm_buf_config = {
        .frame_size = pcm_frame_size,
        .num_frames = g_audio_task.config.pcm_buffer_frames,
        .use_dma = false
    };
    stream->pcm_buffer = audio_ring_buffer_create(&pcm_buf_config);
    if (stream->pcm_buffer == NULL) {
        return AUDIO_TASK_ERROR_NO_RESOURCES;
    }

    /* Create LC3 buffer */
    audio_ring_buffer_config_t lc3_buf_config = {
        .frame_size = lc3_frame_size,
        .num_frames = g_audio_task.config.lc3_buffer_frames,
        .use_dma = false
    };
    stream->lc3_buffer = audio_ring_buffer_create(&lc3_buf_config);
    if (stream->lc3_buffer == NULL) {
        audio_ring_buffer_destroy(stream->pcm_buffer);
        stream->pcm_buffer = NULL;
        return AUDIO_TASK_ERROR_NO_RESOURCES;
    }

    /* TODO: Create LC3 encoder/decoder */
    /* if (direction == AUDIO_STREAM_DIRECTION_TX || */
    /*     direction == AUDIO_STREAM_DIRECTION_BIDIR) { */
    /*     stream->encoder = lc3_encoder_create( */
    /*         config->sample_rate, */
    /*         config->frame_duration_us == 10000 ? LC3_FRAME_10MS : LC3_FRAME_7_5MS, */
    /*         config->channels */
    /*     ); */
    /*     if (stream->encoder == NULL) { */
    /*         audio_ring_buffer_destroy(stream->pcm_buffer); */
    /*         audio_ring_buffer_destroy(stream->lc3_buffer); */
    /*         return AUDIO_TASK_ERROR_NO_RESOURCES; */
    /*     } */
    /* } */
    /* if (direction == AUDIO_STREAM_DIRECTION_RX || */
    /*     direction == AUDIO_STREAM_DIRECTION_BIDIR) { */
    /*     stream->decoder = lc3_decoder_create(...); */
    /* } */

    stream->flags |= STREAM_FLAG_CONFIGURED;
    g_audio_task.active_stream_count++;

    *stream_id = slot;

    return AUDIO_TASK_OK;
}

int audio_task_destroy_stream(uint8_t stream_id)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    stream_context_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    /* Stop if active */
    if (stream->info.active) {
        audio_task_stop_stream(stream_id);
    }

    /* Free buffers */
    if (stream->pcm_buffer != NULL) {
        audio_ring_buffer_destroy(stream->pcm_buffer);
        stream->pcm_buffer = NULL;
    }
    if (stream->lc3_buffer != NULL) {
        audio_ring_buffer_destroy(stream->lc3_buffer);
        stream->lc3_buffer = NULL;
    }

    /* TODO: Free LC3 codec instances */
    /* if (stream->encoder != NULL) { */
    /*     lc3_encoder_destroy(stream->encoder); */
    /*     stream->encoder = NULL; */
    /* } */
    /* if (stream->decoder != NULL) { */
    /*     lc3_decoder_destroy(stream->decoder); */
    /*     stream->decoder = NULL; */
    /* } */

    /* Clear stream */
    stream->flags = 0;
    stream->info.active = false;
    g_audio_task.active_stream_count--;

    return AUDIO_TASK_OK;
}

int audio_task_configure_unicast(uint8_t stream_id, uint16_t conn_handle,
                                 uint16_t cis_handle)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    stream_context_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    if (stream->info.type != AUDIO_STREAM_TYPE_UNICAST) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    stream->info.conn_handle = conn_handle;
    stream->info.cis_handle = cis_handle;

    return AUDIO_TASK_OK;
}

int audio_task_configure_broadcast(uint8_t stream_id, uint8_t big_handle,
                                   uint8_t bis_index)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    stream_context_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    if (stream->info.type != AUDIO_STREAM_TYPE_BROADCAST) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    stream->info.big_handle = big_handle;
    stream->info.bis_index = bis_index;

    return AUDIO_TASK_OK;
}

int audio_task_start_stream(uint8_t stream_id)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    stream_context_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    if (!(stream->flags & STREAM_FLAG_CONFIGURED)) {
        return AUDIO_TASK_ERROR_INVALID_STATE;
    }

    if (stream->info.active) {
        return AUDIO_TASK_OK; /* Already active */
    }

    /* Reset buffers */
    audio_ring_buffer_flush(stream->pcm_buffer);
    audio_ring_buffer_flush(stream->lc3_buffer);

    /* Reset sequence number */
    stream->sequence_number = 0;
    stream->last_process_time = GET_TIME_US();

    /* Mark as started */
    stream->flags |= STREAM_FLAG_STARTED;
    stream->info.active = true;

    /* Notify stream event */
    notify_stream_event(stream_id, 0); /* 0 = started */

    return AUDIO_TASK_OK;
}

int audio_task_stop_stream(uint8_t stream_id)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    stream_context_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    if (!stream->info.active) {
        return AUDIO_TASK_OK; /* Already stopped */
    }

    /* Mark as stopped */
    stream->flags &= ~STREAM_FLAG_STARTED;
    stream->info.active = false;

    /* Notify stream event */
    notify_stream_event(stream_id, 1); /* 1 = stopped */

    return AUDIO_TASK_OK;
}

int audio_task_get_stream_info(uint8_t stream_id, audio_stream_t *stream)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (stream == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    stream_context_t *ctx = get_stream(stream_id);
    if (ctx == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    memcpy(stream, &ctx->info, sizeof(audio_stream_t));

    return AUDIO_TASK_OK;
}

bool audio_task_is_stream_active(uint8_t stream_id)
{
    stream_context_t *stream = get_stream(stream_id);
    if (stream == NULL) {
        return false;
    }
    return stream->info.active;
}

/*******************************************************************************
 * Public Functions - Task Control
 ******************************************************************************/

int audio_task_start(void)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (g_audio_task.state == AUDIO_TASK_STATE_RUNNING) {
        return AUDIO_TASK_OK; /* Already running */
    }

    /* Update state */
    g_audio_task.state = AUDIO_TASK_STATE_STARTING;
    notify_state_change(AUDIO_TASK_STATE_STARTING);

    /* TODO: Send start notification to task */
    /* xTaskNotify((TaskHandle_t)g_audio_task.task_handle, */
    /*             AUDIO_NOTIFY_START, eSetBits); */

    /* For now, directly set to running */
    g_audio_task.state = AUDIO_TASK_STATE_RUNNING;
    notify_state_change(AUDIO_TASK_STATE_RUNNING);

    return AUDIO_TASK_OK;
}

int audio_task_stop(void)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (g_audio_task.state == AUDIO_TASK_STATE_IDLE) {
        return AUDIO_TASK_OK; /* Already stopped */
    }

    /* Update state */
    g_audio_task.state = AUDIO_TASK_STATE_STOPPING;
    notify_state_change(AUDIO_TASK_STATE_STOPPING);

    /* Stop all active streams */
    for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
        if (g_audio_task.streams[i].info.active) {
            audio_task_stop_stream(i);
        }
    }

    /* TODO: Send stop notification to task */
    /* xTaskNotify((TaskHandle_t)g_audio_task.task_handle, */
    /*             AUDIO_NOTIFY_STOP, eSetBits); */

    g_audio_task.state = AUDIO_TASK_STATE_IDLE;
    notify_state_change(AUDIO_TASK_STATE_IDLE);

    return AUDIO_TASK_OK;
}

int audio_task_suspend(void)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (g_audio_task.state != AUDIO_TASK_STATE_RUNNING) {
        return AUDIO_TASK_ERROR_INVALID_STATE;
    }

    /* TODO: Suspend FreeRTOS task */
    /* vTaskSuspend((TaskHandle_t)g_audio_task.task_handle); */

    /* Mark all streams as suspended */
    for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
        if (g_audio_task.streams[i].info.active) {
            g_audio_task.streams[i].flags |= STREAM_FLAG_SUSPENDED;
        }
    }

    return AUDIO_TASK_OK;
}

int audio_task_resume(void)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    /* TODO: Resume FreeRTOS task */
    /* vTaskResume((TaskHandle_t)g_audio_task.task_handle); */

    /* Clear suspended flag from all streams */
    for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
        g_audio_task.streams[i].flags &= ~STREAM_FLAG_SUSPENDED;
    }

    return AUDIO_TASK_OK;
}

/*******************************************************************************
 * Public Functions - Notifications (from ISR)
 ******************************************************************************/

void audio_task_notify_i2s_rx_ready(void)
{
    audio_task_notify_from_isr(AUDIO_NOTIFY_I2S_RX_READY);
}

void audio_task_notify_i2s_tx_ready(void)
{
    audio_task_notify_from_isr(AUDIO_NOTIFY_I2S_TX_READY);
}

void audio_task_notify_isoc_rx_ready(void)
{
    audio_task_notify_from_isr(AUDIO_NOTIFY_ISOC_RX_READY);
}

void audio_task_notify_isoc_tx_ready(void)
{
    audio_task_notify_from_isr(AUDIO_NOTIFY_ISOC_TX_READY);
}

void audio_task_notify_from_isr(uint32_t notification_bits)
{
    if (!g_audio_task.initialized || g_audio_task.task_handle == NULL) {
        return;
    }

    /* TODO: Send notification from ISR */
    /* BaseType_t xHigherPriorityTaskWoken = pdFALSE; */
    /* xTaskNotifyFromISR((TaskHandle_t)g_audio_task.task_handle, */
    /*                    notification_bits, */
    /*                    eSetBits, */
    /*                    &xHigherPriorityTaskWoken); */
    /* portYIELD_FROM_ISR(xHigherPriorityTaskWoken); */
}

/*******************************************************************************
 * Public Functions - Statistics
 ******************************************************************************/

int audio_task_get_stats(audio_task_stats_t *stats)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (stats == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    /* Update averages */
    if (g_audio_task.encode_count > 0) {
        g_audio_task.stats.avg_encode_time_us =
            g_audio_task.total_encode_time / g_audio_task.encode_count;
    }
    if (g_audio_task.decode_count > 0) {
        g_audio_task.stats.avg_decode_time_us =
            g_audio_task.total_decode_time / g_audio_task.decode_count;
    }

    memcpy(stats, &g_audio_task.stats, sizeof(audio_task_stats_t));

    return AUDIO_TASK_OK;
}

int audio_task_reset_stats(void)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    memset(&g_audio_task.stats, 0, sizeof(audio_task_stats_t));
    g_audio_task.total_encode_time = 0;
    g_audio_task.total_decode_time = 0;
    g_audio_task.encode_count = 0;
    g_audio_task.decode_count = 0;
    g_audio_task.busy_time_us = 0;
    g_audio_task.idle_time_us = 0;

    /* Reset per-stream stats */
    for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
        g_audio_task.streams[i].frames_processed = 0;
        g_audio_task.streams[i].frames_dropped = 0;
        g_audio_task.streams[i].codec_errors = 0;
    }

    return AUDIO_TASK_OK;
}

uint8_t audio_task_get_cpu_usage(void)
{
    uint32_t total = g_audio_task.busy_time_us + g_audio_task.idle_time_us;
    if (total == 0) {
        return 0;
    }
    return (uint8_t)((g_audio_task.busy_time_us * 100) / total);
}

/*******************************************************************************
 * Public Functions - Configuration
 ******************************************************************************/

int audio_task_set_default_config(const audio_stream_config_t *config)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    memcpy(&g_audio_task.config.default_config, config,
           sizeof(audio_stream_config_t));

    return AUDIO_TASK_OK;
}

int audio_task_get_default_config(audio_stream_config_t *config)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    memcpy(config, &g_audio_task.config.default_config,
           sizeof(audio_stream_config_t));

    return AUDIO_TASK_OK;
}

int audio_task_set_volume(uint8_t volume_percent)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    if (volume_percent > 100) {
        volume_percent = 100;
    }

    g_audio_task.volume_percent = volume_percent;

    return AUDIO_TASK_OK;
}

uint8_t audio_task_get_volume(void)
{
    return g_audio_task.volume_percent;
}

int audio_task_set_mute(bool mute)
{
    if (!g_audio_task.initialized) {
        return AUDIO_TASK_ERROR_NOT_INITIALIZED;
    }

    g_audio_task.muted = mute;

    return AUDIO_TASK_OK;
}

bool audio_task_is_muted(void)
{
    return g_audio_task.muted;
}

/*******************************************************************************
 * Public Functions - Debug
 ******************************************************************************/

void* audio_task_get_handle(void)
{
    return g_audio_task.task_handle;
}

void audio_task_print_debug(void)
{
    /* TODO: Implement debug printing */
    /* Print state, active streams, buffer levels, etc. */
}

/*******************************************************************************
 * Private Functions - Main Task
 ******************************************************************************/

/**
 * @brief Main audio processing task
 *
 * This task waits for notifications from ISRs (I2S DMA, ISOC handler)
 * and processes audio frames accordingly.
 */
static void audio_task_main(void *pvParameters)
{
    (void)pvParameters;

    uint32_t notification_value;
    uint32_t process_start_time;
    uint32_t process_end_time;

    while (1) {
        /* Wait for notification with timeout */
        /* TODO: Use FreeRTOS notification */
        /* xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, */
        /*                 pdMS_TO_TICKS(NOTIFICATION_TIMEOUT_MS)); */
        notification_value = 0; /* Placeholder */

        g_audio_task.stats.task_wakeups++;
        process_start_time = GET_TIME_US();

        /* Handle start/stop commands */
        if (notification_value & AUDIO_NOTIFY_START) {
            g_audio_task.state = AUDIO_TASK_STATE_RUNNING;
            notify_state_change(AUDIO_TASK_STATE_RUNNING);
        }

        if (notification_value & AUDIO_NOTIFY_STOP) {
            g_audio_task.state = AUDIO_TASK_STATE_IDLE;
            notify_state_change(AUDIO_TASK_STATE_IDLE);
            continue;
        }

        /* Only process if running */
        if (g_audio_task.state != AUDIO_TASK_STATE_RUNNING) {
            continue;
        }

        /* Process I2S RX -> LC3 encode -> ISOC TX (transmit path) */
        if (notification_value & AUDIO_NOTIFY_I2S_RX_READY) {
            for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
                stream_context_t *stream = &g_audio_task.streams[i];
                if (stream->info.active &&
                    (stream->info.direction == AUDIO_STREAM_DIRECTION_TX ||
                     stream->info.direction == AUDIO_STREAM_DIRECTION_BIDIR)) {
                    process_tx_path(stream);
                }
            }
        }

        /* Process ISOC RX -> LC3 decode -> I2S TX (receive path) */
        if (notification_value & AUDIO_NOTIFY_ISOC_RX_READY) {
            for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
                stream_context_t *stream = &g_audio_task.streams[i];
                if (stream->info.active &&
                    (stream->info.direction == AUDIO_STREAM_DIRECTION_RX ||
                     stream->info.direction == AUDIO_STREAM_DIRECTION_BIDIR)) {
                    process_rx_path(stream);
                }
            }
        }

        /* Update timing stats */
        process_end_time = GET_TIME_US();
        g_audio_task.busy_time_us += (process_end_time - process_start_time);

        /* Track max latency */
        uint32_t latency = process_end_time - process_start_time;
        if (latency > g_audio_task.stats.max_latency_us) {
            g_audio_task.stats.max_latency_us = latency;
        }
    }
}

/*******************************************************************************
 * Private Functions - Audio Processing
 ******************************************************************************/

/**
 * @brief Process transmit path: I2S RX -> LC3 encode -> ISOC TX
 */
static int process_tx_path(stream_context_t *stream)
{
    if (stream == NULL || !stream->info.active) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    const audio_stream_config_t *config = &stream->info.config;
    uint16_t samples_per_frame = calculate_samples_per_frame(config);
    uint16_t pcm_bytes = samples_per_frame * config->channels * sizeof(int16_t);

    /* Read PCM from I2S buffer */
    /* TODO: Integrate with actual I2S driver */
    /* i2s_read(g_audio_task.pcm_rx_buffer, pcm_bytes); */

    /* Apply PCM callback if registered */
    if (g_audio_task.config.pcm_callback != NULL) {
        g_audio_task.config.pcm_callback(
            g_audio_task.pcm_rx_buffer,
            samples_per_frame,
            config->channels,
            AUDIO_STREAM_DIRECTION_TX,
            g_audio_task.config.user_data
        );
    }

    /* Apply volume (before encoding) */
    if (!g_audio_task.muted && g_audio_task.volume_percent < 100) {
        apply_volume(g_audio_task.pcm_rx_buffer,
                     samples_per_frame * config->channels);
    }

    /* Mute: zero samples */
    if (g_audio_task.muted) {
        memset(g_audio_task.pcm_rx_buffer, 0, pcm_bytes);
    }

    /* Encode PCM to LC3 */
    uint32_t encode_start = GET_TIME_US();
    uint16_t lc3_len;
    int result = encode_pcm_to_lc3(stream, g_audio_task.pcm_rx_buffer,
                                   g_audio_task.lc3_encode_buffer, &lc3_len);
    uint32_t encode_time = GET_TIME_US() - encode_start;

    /* Update timing stats */
    g_audio_task.total_encode_time += encode_time;
    g_audio_task.encode_count++;
    if (encode_time > g_audio_task.stats.max_encode_time_us) {
        g_audio_task.stats.max_encode_time_us = encode_time;
    }

    if (result != AUDIO_TASK_OK) {
        g_audio_task.stats.encode_errors++;
        stream->codec_errors++;
        return result;
    }

    g_audio_task.stats.frames_encoded++;
    stream->frames_processed++;

    /* Queue LC3 frame to ISOC handler */
    audio_frame_meta_t meta = {
        .timestamp = GET_TIME_US(),
        .sequence_number = stream->sequence_number++,
        .length = lc3_len,
        .channels = config->channels,
        .valid = true,
        .flags = 0
    };

    int write_result = audio_ring_buffer_write_frame(stream->lc3_buffer,
                                                      g_audio_task.lc3_encode_buffer,
                                                      &meta);
    if (write_result != 0) {
        g_audio_task.stats.frames_dropped_tx++;
        stream->frames_dropped++;
    }

    /* TODO: Send to ISOC handler */
    /* isoc_handler_send_sdu(stream->info.isoc_stream_id, */
    /*                       g_audio_task.lc3_encode_buffer, lc3_len, */
    /*                       meta.timestamp); */

    return AUDIO_TASK_OK;
}

/**
 * @brief Process receive path: ISOC RX -> LC3 decode -> I2S TX
 */
static int process_rx_path(stream_context_t *stream)
{
    if (stream == NULL || !stream->info.active) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    const audio_stream_config_t *config = &stream->info.config;
    uint16_t samples_per_frame = calculate_samples_per_frame(config);

    /* Read LC3 frame from ISOC handler */
    audio_frame_meta_t meta;
    int read_result = audio_ring_buffer_read_frame(stream->lc3_buffer,
                                                    g_audio_task.lc3_decode_buffer,
                                                    &meta);

    /* Handle missing frames with PLC */
    bool use_plc = false;
    if (read_result != 0 || !meta.valid) {
        if (g_audio_task.config.enable_plc) {
            use_plc = true;
            g_audio_task.stats.plc_frames++;
        } else {
            /* No data and no PLC - output silence */
            memset(g_audio_task.pcm_tx_buffer, 0,
                   samples_per_frame * config->channels * sizeof(int16_t));
            g_audio_task.stats.rx_overruns++;
            /* TODO: Write silence to I2S */
            return AUDIO_TASK_OK;
        }
    }

    /* Decode LC3 to PCM */
    uint32_t decode_start = GET_TIME_US();
    int result;
    if (use_plc) {
        /* Packet loss concealment - decode with NULL input */
        result = decode_lc3_to_pcm(stream, NULL, 0, g_audio_task.pcm_tx_buffer);
    } else {
        result = decode_lc3_to_pcm(stream, g_audio_task.lc3_decode_buffer,
                                   meta.length, g_audio_task.pcm_tx_buffer);
    }
    uint32_t decode_time = GET_TIME_US() - decode_start;

    /* Update timing stats */
    g_audio_task.total_decode_time += decode_time;
    g_audio_task.decode_count++;
    if (decode_time > g_audio_task.stats.max_decode_time_us) {
        g_audio_task.stats.max_decode_time_us = decode_time;
    }

    if (result != AUDIO_TASK_OK) {
        g_audio_task.stats.decode_errors++;
        stream->codec_errors++;
        /* Output silence on error */
        memset(g_audio_task.pcm_tx_buffer, 0,
               samples_per_frame * config->channels * sizeof(int16_t));
    } else {
        g_audio_task.stats.frames_decoded++;
        stream->frames_processed++;
    }

    /* Apply volume (after decoding) */
    if (!g_audio_task.muted && g_audio_task.volume_percent < 100) {
        apply_volume(g_audio_task.pcm_tx_buffer,
                     samples_per_frame * config->channels);
    }

    /* Mute: zero samples */
    if (g_audio_task.muted) {
        memset(g_audio_task.pcm_tx_buffer, 0,
               samples_per_frame * config->channels * sizeof(int16_t));
    }

    /* Apply PCM callback if registered */
    if (g_audio_task.config.pcm_callback != NULL) {
        g_audio_task.config.pcm_callback(
            g_audio_task.pcm_tx_buffer,
            samples_per_frame,
            config->channels,
            AUDIO_STREAM_DIRECTION_RX,
            g_audio_task.config.user_data
        );
    }

    /* Write PCM to I2S buffer */
    /* TODO: Integrate with actual I2S driver */
    /* i2s_write(g_audio_task.pcm_tx_buffer, */
    /*           samples_per_frame * config->channels * sizeof(int16_t)); */

    return AUDIO_TASK_OK;
}

/**
 * @brief Encode PCM samples to LC3
 */
static int encode_pcm_to_lc3(stream_context_t *stream, const int16_t *pcm,
                             uint8_t *lc3, uint16_t *lc3_len)
{
    if (stream == NULL || pcm == NULL || lc3 == NULL || lc3_len == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    /* TODO: Call liblc3 encoder */
    /* int result = lc3_encode(stream->encoder, pcm, lc3, lc3_len); */
    /* if (result != 0) { */
    /*     return AUDIO_TASK_ERROR_CODEC_ERROR; */
    /* } */

    /* Placeholder: copy octets_per_frame bytes */
    *lc3_len = stream->info.config.octets_per_frame;
    memset(lc3, 0, *lc3_len);

    return AUDIO_TASK_OK;
}

/**
 * @brief Decode LC3 to PCM samples
 */
static int decode_lc3_to_pcm(stream_context_t *stream, const uint8_t *lc3,
                             uint16_t lc3_len, int16_t *pcm)
{
    if (stream == NULL || pcm == NULL) {
        return AUDIO_TASK_ERROR_INVALID_PARAM;
    }

    const audio_stream_config_t *config = &stream->info.config;
    uint16_t samples_per_frame = calculate_samples_per_frame(config);

    /* TODO: Call liblc3 decoder */
    /* int result; */
    /* if (lc3 == NULL) { */
    /*     // PLC mode */
    /*     result = lc3_decode_plc(stream->decoder, pcm); */
    /* } else { */
    /*     result = lc3_decode(stream->decoder, lc3, lc3_len, pcm); */
    /* } */
    /* if (result != 0) { */
    /*     return AUDIO_TASK_ERROR_CODEC_ERROR; */
    /* } */

    /* Placeholder: zero output */
    memset(pcm, 0, samples_per_frame * config->channels * sizeof(int16_t));

    return AUDIO_TASK_OK;
}

/**
 * @brief Apply volume to PCM samples
 */
static void apply_volume(int16_t *samples, uint16_t num_samples)
{
    if (samples == NULL || num_samples == 0) {
        return;
    }

    if (g_audio_task.volume_percent == 100) {
        return; /* No change needed */
    }

    if (g_audio_task.volume_percent == 0) {
        memset(samples, 0, num_samples * sizeof(int16_t));
        return;
    }

    /* Apply volume scaling */
    uint16_t volume = g_audio_task.volume_percent;
    for (uint16_t i = 0; i < num_samples; i++) {
        int32_t sample = samples[i];
        sample = (sample * volume) / 100;
        samples[i] = (int16_t)sample;
    }
}

/**
 * @brief Update global statistics
 */
static void update_statistics(void)
{
    /* Aggregate per-stream stats */
    /* This can be called periodically */
}

/*******************************************************************************
 * Private Functions - Utilities
 ******************************************************************************/

/**
 * @brief Calculate number of samples per frame
 */
static uint16_t calculate_samples_per_frame(const audio_stream_config_t *config)
{
    if (config == NULL) {
        return 0;
    }

    /* samples = sample_rate * frame_duration / 1000000 */
    uint32_t samples = (config->sample_rate * config->frame_duration_us) / 1000000;
    return (uint16_t)samples;
}

/**
 * @brief Find free stream slot
 */
static int find_free_stream_slot(void)
{
    for (int i = 0; i < AUDIO_TASK_MAX_STREAMS; i++) {
        if (!(g_audio_task.streams[i].flags & STREAM_FLAG_CONFIGURED)) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Get stream by ID
 */
static stream_context_t* get_stream(uint8_t stream_id)
{
    if (stream_id >= AUDIO_TASK_MAX_STREAMS) {
        return NULL;
    }

    stream_context_t *stream = &g_audio_task.streams[stream_id];
    if (!(stream->flags & STREAM_FLAG_CONFIGURED)) {
        return NULL;
    }

    return stream;
}

/**
 * @brief Notify state change callback
 */
static void notify_state_change(audio_task_state_t new_state)
{
    if (g_audio_task.config.state_callback != NULL) {
        g_audio_task.config.state_callback(new_state,
                                           g_audio_task.config.user_data);
    }
}

/**
 * @brief Notify stream event callback
 */
static void notify_stream_event(uint8_t stream_id, int event)
{
    if (g_audio_task.config.stream_callback != NULL) {
        g_audio_task.config.stream_callback(stream_id, event,
                                            g_audio_task.config.user_data);
    }
}
