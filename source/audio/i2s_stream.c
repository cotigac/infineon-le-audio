/**
 * @file i2s_stream.c
 * @brief I2S Audio Streaming Implementation
 *
 * This module provides DMA-based I2S audio streaming with ping-pong
 * buffering for continuous audio transfer to/from the main controller.
 *
 * Architecture:
 * - Uses double (ping-pong) DMA buffers for glitch-free streaming
 * - Ring buffers for application-level read/write operations
 * - Supports callback notification from ISR context
 * - Thread-safe for FreeRTOS environment
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i2s_stream.h"
#include "../config/lc3_config.h"

#include <stdlib.h>
#include <string.h>

/* TODO: Include Infineon PDL/HAL headers when integrating with hardware */
/* #include "cy_pdl.h" */
/* #include "cyhal.h" */
/* #include "cyhal_i2s.h" */

/* TODO: Include FreeRTOS headers */
/* #include "FreeRTOS.h" */
/* #include "semphr.h" */
/* #include "task.h" */

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Number of ping-pong buffers */
#define I2S_NUM_BUFFERS         2

/** Maximum buffer size in samples */
#define I2S_MAX_BUFFER_SAMPLES  960

/** Ring buffer size (number of frames to buffer) */
#define I2S_RING_BUFFER_FRAMES  4

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief Ring buffer structure for audio samples
 */
typedef struct {
    int16_t *buffer;            /**< Buffer memory */
    uint32_t size;              /**< Total size in samples */
    volatile uint32_t head;     /**< Write position */
    volatile uint32_t tail;     /**< Read position */
    volatile uint32_t count;    /**< Number of samples in buffer */
} ring_buffer_t;

/**
 * @brief DMA ping-pong buffer structure
 */
typedef struct {
    int16_t *buffers[I2S_NUM_BUFFERS];  /**< Ping-pong buffers */
    volatile uint8_t active_buffer;      /**< Currently active buffer (0 or 1) */
    uint16_t buffer_size_samples;        /**< Size of each buffer in samples */
} dma_buffer_t;

/**
 * @brief I2S stream context structure
 */
typedef struct {
    /* Configuration */
    i2s_stream_config_t config;

    /* State */
    volatile bool initialized;
    volatile bool running;

    /* DMA ping-pong buffers */
    dma_buffer_t tx_dma;
    dma_buffer_t rx_dma;

    /* Application ring buffers */
    ring_buffer_t tx_ring;
    ring_buffer_t rx_ring;

    /* Callbacks */
    i2s_buffer_callback_t rx_callback;
    void *rx_callback_user_data;
    i2s_buffer_callback_t tx_callback;
    void *tx_callback_user_data;

    /* Statistics */
    i2s_stream_stats_t stats;

    /* Synchronization (FreeRTOS) */
    /* SemaphoreHandle_t tx_sem; */
    /* SemaphoreHandle_t rx_sem; */
    /* SemaphoreHandle_t mutex; */

    /* Hardware handles (Infineon HAL) */
    /* cyhal_i2s_t i2s_obj; */

} i2s_stream_ctx_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Global I2S stream context */
static i2s_stream_ctx_t g_i2s_ctx;

/** Static buffer memory for DMA (aligned for DMA access) */
static int16_t g_tx_dma_buffer_0[I2S_MAX_BUFFER_SAMPLES] __attribute__((aligned(4)));
static int16_t g_tx_dma_buffer_1[I2S_MAX_BUFFER_SAMPLES] __attribute__((aligned(4)));
static int16_t g_rx_dma_buffer_0[I2S_MAX_BUFFER_SAMPLES] __attribute__((aligned(4)));
static int16_t g_rx_dma_buffer_1[I2S_MAX_BUFFER_SAMPLES] __attribute__((aligned(4)));

/** Static ring buffer memory */
static int16_t g_tx_ring_buffer[I2S_MAX_BUFFER_SAMPLES * I2S_RING_BUFFER_FRAMES];
static int16_t g_rx_ring_buffer[I2S_MAX_BUFFER_SAMPLES * I2S_RING_BUFFER_FRAMES];

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static int ring_buffer_init(ring_buffer_t *rb, int16_t *buffer, uint32_t size);
static uint32_t ring_buffer_write(ring_buffer_t *rb, const int16_t *data, uint32_t count);
static uint32_t ring_buffer_read(ring_buffer_t *rb, int16_t *data, uint32_t count);
static uint32_t ring_buffer_available(const ring_buffer_t *rb);
static uint32_t ring_buffer_free(const ring_buffer_t *rb);
static void ring_buffer_clear(ring_buffer_t *rb);

static int dma_buffer_init(dma_buffer_t *db, int16_t *buf0, int16_t *buf1, uint16_t size);
static int16_t* dma_buffer_get_active(dma_buffer_t *db);
static int16_t* dma_buffer_get_inactive(dma_buffer_t *db);
static void dma_buffer_swap(dma_buffer_t *db);

static void i2s_tx_dma_callback(void);
static void i2s_rx_dma_callback(void);
static int i2s_hw_init(const i2s_stream_config_t *config);
static void i2s_hw_deinit(void);
static int i2s_hw_start(void);
static int i2s_hw_stop(void);

/*******************************************************************************
 * Ring Buffer Implementation
 ******************************************************************************/

static int ring_buffer_init(ring_buffer_t *rb, int16_t *buffer, uint32_t size)
{
    if (rb == NULL || buffer == NULL || size == 0) {
        return -1;
    }

    rb->buffer = buffer;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;

    return 0;
}

static uint32_t ring_buffer_write(ring_buffer_t *rb, const int16_t *data, uint32_t count)
{
    uint32_t written = 0;
    uint32_t free_space;

    if (rb == NULL || data == NULL || count == 0) {
        return 0;
    }

    free_space = ring_buffer_free(rb);
    if (count > free_space) {
        count = free_space;
    }

    while (written < count) {
        rb->buffer[rb->head] = data[written];
        rb->head = (rb->head + 1) % rb->size;
        written++;
    }

    /* Atomic update of count */
    /* TODO: Use critical section or atomic operation */
    rb->count += written;

    return written;
}

static uint32_t ring_buffer_read(ring_buffer_t *rb, int16_t *data, uint32_t count)
{
    uint32_t read_count = 0;
    uint32_t available;

    if (rb == NULL || data == NULL || count == 0) {
        return 0;
    }

    available = ring_buffer_available(rb);
    if (count > available) {
        count = available;
    }

    while (read_count < count) {
        data[read_count] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
        read_count++;
    }

    /* Atomic update of count */
    /* TODO: Use critical section or atomic operation */
    rb->count -= read_count;

    return read_count;
}

static uint32_t ring_buffer_available(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    return rb->count;
}

static uint32_t ring_buffer_free(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    return rb->size - rb->count;
}

static void ring_buffer_clear(ring_buffer_t *rb)
{
    if (rb != NULL) {
        rb->head = 0;
        rb->tail = 0;
        rb->count = 0;
    }
}

/*******************************************************************************
 * DMA Buffer Implementation
 ******************************************************************************/

static int dma_buffer_init(dma_buffer_t *db, int16_t *buf0, int16_t *buf1, uint16_t size)
{
    if (db == NULL || buf0 == NULL || buf1 == NULL || size == 0) {
        return -1;
    }

    db->buffers[0] = buf0;
    db->buffers[1] = buf1;
    db->active_buffer = 0;
    db->buffer_size_samples = size;

    /* Clear buffers */
    memset(buf0, 0, size * sizeof(int16_t));
    memset(buf1, 0, size * sizeof(int16_t));

    return 0;
}

static int16_t* dma_buffer_get_active(dma_buffer_t *db)
{
    if (db == NULL) {
        return NULL;
    }
    return db->buffers[db->active_buffer];
}

static int16_t* dma_buffer_get_inactive(dma_buffer_t *db)
{
    if (db == NULL) {
        return NULL;
    }
    return db->buffers[1 - db->active_buffer];
}

static void dma_buffer_swap(dma_buffer_t *db)
{
    if (db != NULL) {
        db->active_buffer = 1 - db->active_buffer;
    }
}

/*******************************************************************************
 * Hardware Abstraction Layer
 ******************************************************************************/

/**
 * @brief Initialize I2S hardware
 *
 * TODO: Implement using Infineon PDL/HAL for PSoC Edge
 */
static int i2s_hw_init(const i2s_stream_config_t *config)
{
    (void)config;

    /*
     * TODO: Initialize I2S peripheral using Infineon HAL
     *
     * Example using cyhal_i2s:
     *
     * cyhal_i2s_config_t i2s_config = {
     *     .is_tx_slave = false,
     *     .is_rx_slave = false,
     *     .mclk_hz = config->sample_rate * 256,
     *     .channel_length = 32,
     *     .word_length = config->bit_depth,
     *     .sample_rate_hz = config->sample_rate,
     * };
     *
     * cy_rslt_t result = cyhal_i2s_init(&g_i2s_ctx.i2s_obj,
     *                                    &i2s_config,
     *                                    I2S_TX_SCK_PIN,
     *                                    I2S_TX_WS_PIN,
     *                                    I2S_TX_SDO_PIN,
     *                                    I2S_RX_SCK_PIN,
     *                                    I2S_RX_WS_PIN,
     *                                    I2S_RX_SDI_PIN,
     *                                    NC,  // MCLK
     *                                    NULL);
     *
     * if (result != CY_RSLT_SUCCESS) {
     *     return -1;
     * }
     *
     * // Register DMA callbacks
     * cyhal_i2s_register_callback(&g_i2s_ctx.i2s_obj, i2s_event_handler, NULL);
     * cyhal_i2s_enable_event(&g_i2s_ctx.i2s_obj,
     *                        CYHAL_I2S_ASYNC_TX_COMPLETE | CYHAL_I2S_ASYNC_RX_COMPLETE,
     *                        CYHAL_ISR_PRIORITY_DEFAULT,
     *                        true);
     */

    return 0;
}

/**
 * @brief Deinitialize I2S hardware
 */
static void i2s_hw_deinit(void)
{
    /*
     * TODO: Deinitialize I2S peripheral
     *
     * cyhal_i2s_free(&g_i2s_ctx.i2s_obj);
     */
}

/**
 * @brief Start I2S hardware streaming
 */
static int i2s_hw_start(void)
{
    /*
     * TODO: Start DMA transfers
     *
     * // Start TX DMA with first buffer
     * cy_rslt_t result = cyhal_i2s_write_async(&g_i2s_ctx.i2s_obj,
     *                                          dma_buffer_get_active(&g_i2s_ctx.tx_dma),
     *                                          g_i2s_ctx.tx_dma.buffer_size_samples);
     * if (result != CY_RSLT_SUCCESS) {
     *     return -1;
     * }
     *
     * // Start RX DMA with first buffer
     * result = cyhal_i2s_read_async(&g_i2s_ctx.i2s_obj,
     *                               dma_buffer_get_active(&g_i2s_ctx.rx_dma),
     *                               g_i2s_ctx.rx_dma.buffer_size_samples);
     * if (result != CY_RSLT_SUCCESS) {
     *     return -2;
     * }
     *
     * // Start I2S clock
     * cyhal_i2s_start_tx(&g_i2s_ctx.i2s_obj);
     * cyhal_i2s_start_rx(&g_i2s_ctx.i2s_obj);
     */

    return 0;
}

/**
 * @brief Stop I2S hardware streaming
 */
static int i2s_hw_stop(void)
{
    /*
     * TODO: Stop DMA transfers and I2S clock
     *
     * cyhal_i2s_stop_tx(&g_i2s_ctx.i2s_obj);
     * cyhal_i2s_stop_rx(&g_i2s_ctx.i2s_obj);
     * cyhal_i2s_abort_async(&g_i2s_ctx.i2s_obj);
     */

    return 0;
}

/*******************************************************************************
 * DMA Callbacks (called from ISR context)
 ******************************************************************************/

/**
 * @brief TX DMA complete callback
 *
 * Called when a TX DMA buffer has been fully transmitted.
 * Swaps to the next buffer and refills from the ring buffer.
 */
static void i2s_tx_dma_callback(void)
{
    int16_t *inactive_buffer;
    uint32_t samples_read;

    if (!g_i2s_ctx.running) {
        return;
    }

    /* Swap to the next buffer */
    dma_buffer_swap(&g_i2s_ctx.tx_dma);

    /* Get the buffer that just finished (now inactive) and refill it */
    inactive_buffer = dma_buffer_get_inactive(&g_i2s_ctx.tx_dma);

    /* Try to fill from ring buffer */
    samples_read = ring_buffer_read(&g_i2s_ctx.tx_ring,
                                    inactive_buffer,
                                    g_i2s_ctx.tx_dma.buffer_size_samples);

    /* If not enough samples, fill remainder with silence and count underrun */
    if (samples_read < g_i2s_ctx.tx_dma.buffer_size_samples) {
        memset(&inactive_buffer[samples_read], 0,
               (g_i2s_ctx.tx_dma.buffer_size_samples - samples_read) * sizeof(int16_t));
        g_i2s_ctx.stats.buffer_underruns++;
    }

    /* Call user callback if registered */
    if (g_i2s_ctx.tx_callback != NULL) {
        g_i2s_ctx.tx_callback(inactive_buffer,
                              g_i2s_ctx.tx_dma.buffer_size_samples,
                              g_i2s_ctx.tx_callback_user_data);
    }

    /* Update statistics */
    g_i2s_ctx.stats.frames_transferred++;

    /*
     * TODO: Start next DMA transfer
     *
     * cyhal_i2s_write_async(&g_i2s_ctx.i2s_obj,
     *                       dma_buffer_get_active(&g_i2s_ctx.tx_dma),
     *                       g_i2s_ctx.tx_dma.buffer_size_samples);
     */

    /* TODO: Signal semaphore for blocking write */
    /* BaseType_t xHigherPriorityTaskWoken = pdFALSE; */
    /* xSemaphoreGiveFromISR(g_i2s_ctx.tx_sem, &xHigherPriorityTaskWoken); */
    /* portYIELD_FROM_ISR(xHigherPriorityTaskWoken); */
}

/**
 * @brief RX DMA complete callback
 *
 * Called when a RX DMA buffer has been fully received.
 * Swaps to the next buffer and pushes data to the ring buffer.
 */
static void i2s_rx_dma_callback(void)
{
    int16_t *inactive_buffer;
    uint32_t samples_written;

    if (!g_i2s_ctx.running) {
        return;
    }

    /* Swap to the next buffer */
    dma_buffer_swap(&g_i2s_ctx.rx_dma);

    /* Get the buffer that just filled (now inactive) */
    inactive_buffer = dma_buffer_get_inactive(&g_i2s_ctx.rx_dma);

    /* Call user callback if registered */
    if (g_i2s_ctx.rx_callback != NULL) {
        g_i2s_ctx.rx_callback(inactive_buffer,
                              g_i2s_ctx.rx_dma.buffer_size_samples,
                              g_i2s_ctx.rx_callback_user_data);
    }

    /* Push to ring buffer */
    samples_written = ring_buffer_write(&g_i2s_ctx.rx_ring,
                                        inactive_buffer,
                                        g_i2s_ctx.rx_dma.buffer_size_samples);

    /* If ring buffer is full, count overrun */
    if (samples_written < g_i2s_ctx.rx_dma.buffer_size_samples) {
        g_i2s_ctx.stats.buffer_overruns++;
    }

    /* Update statistics */
    g_i2s_ctx.stats.frames_transferred++;

    /*
     * TODO: Start next DMA transfer
     *
     * cyhal_i2s_read_async(&g_i2s_ctx.i2s_obj,
     *                      dma_buffer_get_active(&g_i2s_ctx.rx_dma),
     *                      g_i2s_ctx.rx_dma.buffer_size_samples);
     */

    /* TODO: Signal semaphore for blocking read */
    /* BaseType_t xHigherPriorityTaskWoken = pdFALSE; */
    /* xSemaphoreGiveFromISR(g_i2s_ctx.rx_sem, &xHigherPriorityTaskWoken); */
    /* portYIELD_FROM_ISR(xHigherPriorityTaskWoken); */
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int i2s_stream_init(const i2s_stream_config_t *config)
{
    int result;

    if (config == NULL) {
        return -1;
    }

    if (g_i2s_ctx.initialized) {
        return -2;  /* Already initialized */
    }

    /* Validate configuration */
    if (config->buffer_size_samples > I2S_MAX_BUFFER_SAMPLES) {
        return -3;
    }

    if (config->channels == 0 || config->channels > 2) {
        return -4;
    }

    /* Clear context */
    memset(&g_i2s_ctx, 0, sizeof(g_i2s_ctx));

    /* Store configuration */
    g_i2s_ctx.config = *config;

    /* Initialize DMA buffers */
    result = dma_buffer_init(&g_i2s_ctx.tx_dma,
                             g_tx_dma_buffer_0,
                             g_tx_dma_buffer_1,
                             config->buffer_size_samples);
    if (result != 0) {
        return -5;
    }

    result = dma_buffer_init(&g_i2s_ctx.rx_dma,
                             g_rx_dma_buffer_0,
                             g_rx_dma_buffer_1,
                             config->buffer_size_samples);
    if (result != 0) {
        return -6;
    }

    /* Initialize ring buffers */
    result = ring_buffer_init(&g_i2s_ctx.tx_ring,
                              g_tx_ring_buffer,
                              config->buffer_size_samples * I2S_RING_BUFFER_FRAMES);
    if (result != 0) {
        return -7;
    }

    result = ring_buffer_init(&g_i2s_ctx.rx_ring,
                              g_rx_ring_buffer,
                              config->buffer_size_samples * I2S_RING_BUFFER_FRAMES);
    if (result != 0) {
        return -8;
    }

    /*
     * TODO: Create FreeRTOS synchronization primitives
     *
     * g_i2s_ctx.tx_sem = xSemaphoreCreateBinary();
     * g_i2s_ctx.rx_sem = xSemaphoreCreateBinary();
     * g_i2s_ctx.mutex = xSemaphoreCreateMutex();
     *
     * if (g_i2s_ctx.tx_sem == NULL || g_i2s_ctx.rx_sem == NULL ||
     *     g_i2s_ctx.mutex == NULL) {
     *     return -9;
     * }
     */

    /* Initialize hardware */
    result = i2s_hw_init(config);
    if (result != 0) {
        return -10;
    }

    g_i2s_ctx.initialized = true;

    return 0;
}

void i2s_stream_deinit(void)
{
    if (!g_i2s_ctx.initialized) {
        return;
    }

    /* Stop streaming if running */
    if (g_i2s_ctx.running) {
        i2s_stream_stop();
    }

    /* Deinitialize hardware */
    i2s_hw_deinit();

    /*
     * TODO: Delete FreeRTOS synchronization primitives
     *
     * if (g_i2s_ctx.tx_sem != NULL) {
     *     vSemaphoreDelete(g_i2s_ctx.tx_sem);
     * }
     * if (g_i2s_ctx.rx_sem != NULL) {
     *     vSemaphoreDelete(g_i2s_ctx.rx_sem);
     * }
     * if (g_i2s_ctx.mutex != NULL) {
     *     vSemaphoreDelete(g_i2s_ctx.mutex);
     * }
     */

    g_i2s_ctx.initialized = false;
}

int i2s_stream_start(void)
{
    int result;

    if (!g_i2s_ctx.initialized) {
        return -1;
    }

    if (g_i2s_ctx.running) {
        return 0;  /* Already running */
    }

    /* Clear buffers */
    ring_buffer_clear(&g_i2s_ctx.tx_ring);
    ring_buffer_clear(&g_i2s_ctx.rx_ring);

    /* Reset statistics */
    memset(&g_i2s_ctx.stats, 0, sizeof(g_i2s_ctx.stats));

    /* Pre-fill TX buffers with silence */
    memset(g_tx_dma_buffer_0, 0,
           g_i2s_ctx.tx_dma.buffer_size_samples * sizeof(int16_t));
    memset(g_tx_dma_buffer_1, 0,
           g_i2s_ctx.tx_dma.buffer_size_samples * sizeof(int16_t));

    g_i2s_ctx.running = true;

    /* Start hardware */
    result = i2s_hw_start();
    if (result != 0) {
        g_i2s_ctx.running = false;
        return -2;
    }

    return 0;
}

int i2s_stream_stop(void)
{
    if (!g_i2s_ctx.initialized) {
        return -1;
    }

    if (!g_i2s_ctx.running) {
        return 0;  /* Already stopped */
    }

    g_i2s_ctx.running = false;

    /* Stop hardware */
    i2s_hw_stop();

    return 0;
}

void i2s_stream_register_rx_callback(i2s_buffer_callback_t callback, void *user_data)
{
    g_i2s_ctx.rx_callback = callback;
    g_i2s_ctx.rx_callback_user_data = user_data;
}

void i2s_stream_register_tx_callback(i2s_buffer_callback_t callback, void *user_data)
{
    g_i2s_ctx.tx_callback = callback;
    g_i2s_ctx.tx_callback_user_data = user_data;
}

int i2s_stream_read(int16_t *buffer, uint16_t sample_count, uint32_t timeout_ms)
{
    uint32_t samples_read = 0;
    uint32_t remaining;

    if (!g_i2s_ctx.initialized || buffer == NULL || sample_count == 0) {
        return -1;
    }

    if (!g_i2s_ctx.running) {
        return -2;
    }

    (void)timeout_ms;  /* TODO: Implement timeout with FreeRTOS semaphore */

    /*
     * TODO: Implement blocking read with timeout
     *
     * TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
     * TickType_t start_ticks = xTaskGetTickCount();
     *
     * while (samples_read < sample_count) {
     *     // Try to read available samples
     *     remaining = sample_count - samples_read;
     *     samples_read += ring_buffer_read(&g_i2s_ctx.rx_ring,
     *                                      &buffer[samples_read],
     *                                      remaining);
     *
     *     if (samples_read >= sample_count) {
     *         break;
     *     }
     *
     *     // Wait for more samples
     *     TickType_t elapsed = xTaskGetTickCount() - start_ticks;
     *     if (elapsed >= ticks && ticks != portMAX_DELAY) {
     *         break;  // Timeout
     *     }
     *
     *     TickType_t remaining_ticks = ticks - elapsed;
     *     xSemaphoreTake(g_i2s_ctx.rx_sem, remaining_ticks);
     * }
     */

    /* Non-blocking read for now */
    remaining = sample_count;
    samples_read = ring_buffer_read(&g_i2s_ctx.rx_ring, buffer, remaining);

    return (int)samples_read;
}

int i2s_stream_write(const int16_t *buffer, uint16_t sample_count, uint32_t timeout_ms)
{
    uint32_t samples_written = 0;
    uint32_t remaining;

    if (!g_i2s_ctx.initialized || buffer == NULL || sample_count == 0) {
        return -1;
    }

    if (!g_i2s_ctx.running) {
        return -2;
    }

    (void)timeout_ms;  /* TODO: Implement timeout with FreeRTOS semaphore */

    /*
     * TODO: Implement blocking write with timeout
     *
     * TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
     * TickType_t start_ticks = xTaskGetTickCount();
     *
     * while (samples_written < sample_count) {
     *     // Try to write available space
     *     remaining = sample_count - samples_written;
     *     samples_written += ring_buffer_write(&g_i2s_ctx.tx_ring,
     *                                          &buffer[samples_written],
     *                                          remaining);
     *
     *     if (samples_written >= sample_count) {
     *         break;
     *     }
     *
     *     // Wait for space
     *     TickType_t elapsed = xTaskGetTickCount() - start_ticks;
     *     if (elapsed >= ticks && ticks != portMAX_DELAY) {
     *         break;  // Timeout
     *     }
     *
     *     TickType_t remaining_ticks = ticks - elapsed;
     *     xSemaphoreTake(g_i2s_ctx.tx_sem, remaining_ticks);
     * }
     */

    /* Non-blocking write for now */
    remaining = sample_count;
    samples_written = ring_buffer_write(&g_i2s_ctx.tx_ring, buffer, remaining);

    return (int)samples_written;
}

void i2s_stream_get_stats(i2s_stream_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_i2s_ctx.stats;
    }
}

void i2s_stream_reset_stats(void)
{
    memset(&g_i2s_ctx.stats, 0, sizeof(g_i2s_ctx.stats));
}

bool i2s_stream_is_running(void)
{
    return g_i2s_ctx.running;
}

/*******************************************************************************
 * Additional Utility Functions
 ******************************************************************************/

/**
 * @brief Get the number of samples available to read
 *
 * @return Number of samples available in RX buffer
 */
uint32_t i2s_stream_rx_available(void)
{
    return ring_buffer_available(&g_i2s_ctx.rx_ring);
}

/**
 * @brief Get the number of samples that can be written
 *
 * @return Number of samples that can be written to TX buffer
 */
uint32_t i2s_stream_tx_free(void)
{
    return ring_buffer_free(&g_i2s_ctx.tx_ring);
}

/**
 * @brief Get current buffer latency in milliseconds
 *
 * @return Estimated latency based on buffered samples
 */
uint32_t i2s_stream_get_latency_ms(void)
{
    uint32_t buffered_samples;
    uint32_t sample_rate;

    if (!g_i2s_ctx.initialized) {
        return 0;
    }

    /* Total samples in TX and RX ring buffers plus DMA buffers */
    buffered_samples = ring_buffer_available(&g_i2s_ctx.tx_ring);
    buffered_samples += ring_buffer_available(&g_i2s_ctx.rx_ring);
    buffered_samples += g_i2s_ctx.tx_dma.buffer_size_samples * 2;  /* 2 DMA buffers */

    sample_rate = g_i2s_ctx.config.sample_rate;
    if (sample_rate == 0) {
        return 0;
    }

    /* Latency in ms = samples / sample_rate * 1000 */
    return (buffered_samples * 1000) / sample_rate;
}
