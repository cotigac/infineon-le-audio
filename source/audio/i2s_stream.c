/**
 * @file i2s_stream.c
 * @brief I2S Audio Streaming Implementation using TDM PDL Driver
 *
 * This module provides interrupt-driven I2S audio streaming with ping-pong
 * buffering for continuous audio transfer to/from the main controller.
 *
 * Architecture:
 * - Uses double (ping-pong) DMA buffers for glitch-free streaming
 * - Ring buffers for application-level read/write operations
 * - Supports callback notification from ISR context
 * - Thread-safe for FreeRTOS environment
 *
 * PSoC Edge E84 uses the TDM peripheral for I2S operations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i2s_stream.h"
#include "lc3_config.h"

#include <stdlib.h>
#include <string.h>

/* Infineon PDL headers for TDM/I2S */
#include "cy_pdl.h"
#include "cy_tdm.h"
#include "cy_sysint.h"

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/*******************************************************************************
 * TDM/I2S Configuration (PSoC Edge E84)
 *
 * The TDM peripheral is used for I2S operations.
 ******************************************************************************/

/* TDM instance to use - TDM0, TDM_STRUCT[0]
 * TDM0_TDM_STRUCT0 = TDM_STRUCT for Init/DeInit
 * TDM0_TDM_STRUCT0_TDM_TX_STRUCT = TX operations
 * TDM0_TDM_STRUCT0_TDM_RX_STRUCT = RX operations
 */
#ifndef I2S_TDM_STRUCT
#define I2S_TDM_STRUCT          TDM0_TDM_STRUCT0
#endif

#ifndef I2S_TDM_TX
#define I2S_TDM_TX              TDM0_TDM_STRUCT0_TDM_TX_STRUCT
#endif

#ifndef I2S_TDM_RX
#define I2S_TDM_RX              TDM0_TDM_STRUCT0_TDM_RX_STRUCT
#endif

/* TDM interrupt configuration */
#ifndef I2S_TDM_TX_IRQ
#define I2S_TDM_TX_IRQ          tdm_0_interrupts_tx_0_IRQn
#endif

#ifndef I2S_TDM_RX_IRQ
#define I2S_TDM_RX_IRQ          tdm_0_interrupts_rx_0_IRQn
#endif

/* Interrupt priorities */
#define I2S_TX_IRQ_PRIORITY     3
#define I2S_RX_IRQ_PRIORITY     3

/* FIFO trigger levels */
#define I2S_TX_FIFO_TRIGGER     64  /* Trigger when FIFO has less than 64 entries */
#define I2S_RX_FIFO_TRIGGER     64  /* Trigger when FIFO has more than 64 entries */

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

    /* Buffer indices for FIFO operations */
    volatile uint32_t tx_buffer_index;
    volatile uint32_t rx_buffer_index;

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
    SemaphoreHandle_t tx_sem;
    SemaphoreHandle_t rx_sem;
    SemaphoreHandle_t mutex;

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

/** TDM configuration structures */
static cy_stc_tdm_config_tx_t g_tdm_tx_config;
static cy_stc_tdm_config_rx_t g_tdm_rx_config;
static cy_stc_tdm_config_t g_tdm_config;

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

static void i2s_tx_isr(void);
static void i2s_rx_isr(void);
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

    /* Atomic update of count using critical section */
    taskENTER_CRITICAL();
    rb->count += written;
    taskEXIT_CRITICAL();

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

    /* Atomic update of count using critical section */
    taskENTER_CRITICAL();
    rb->count -= read_count;
    taskEXIT_CRITICAL();

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
 * Hardware Abstraction Layer - TDM PDL Driver
 ******************************************************************************/

/**
 * @brief TX FIFO trigger interrupt handler
 *
 * Called when TX FIFO needs more data.
 */
static void i2s_tx_isr(void)
{
    int16_t *buffer;
    uint32_t samples_read;
    uint32_t fifo_space;
    uint32_t i;

    if (!g_i2s_ctx.running) {
        Cy_AudioTDM_ClearTxInterrupt(I2S_TDM_TX, CY_TDM_INTR_TX_FIFO_TRIGGER);
        return;
    }

    /* Get inactive buffer and fill from ring buffer */
    buffer = dma_buffer_get_inactive(&g_i2s_ctx.tx_dma);

    /* Read from ring buffer */
    samples_read = ring_buffer_read(&g_i2s_ctx.tx_ring,
                                    buffer,
                                    g_i2s_ctx.tx_dma.buffer_size_samples);

    /* If not enough samples, fill remainder with silence */
    if (samples_read < g_i2s_ctx.tx_dma.buffer_size_samples) {
        memset(&buffer[samples_read], 0,
               (g_i2s_ctx.tx_dma.buffer_size_samples - samples_read) * sizeof(int16_t));
        g_i2s_ctx.stats.buffer_underruns++;
    }

    /* Write samples to TX FIFO */
    fifo_space = 128 - Cy_AudioTDM_GetNumInTxFifo(I2S_TDM_TX);
    for (i = 0; i < g_i2s_ctx.tx_dma.buffer_size_samples && i < fifo_space; i++) {
        Cy_AudioTDM_WriteTxData(I2S_TDM_TX, (uint32_t)buffer[i]);
    }

    /* Call user callback if registered */
    if (g_i2s_ctx.tx_callback != NULL) {
        g_i2s_ctx.tx_callback(buffer,
                              g_i2s_ctx.tx_dma.buffer_size_samples,
                              g_i2s_ctx.tx_callback_user_data);
    }

    /* Update statistics */
    g_i2s_ctx.stats.frames_transferred++;

    /* Swap buffers */
    dma_buffer_swap(&g_i2s_ctx.tx_dma);

    /* Signal semaphore for blocking write */
    if (g_i2s_ctx.tx_sem != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(g_i2s_ctx.tx_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    /* Clear interrupt */
    Cy_AudioTDM_ClearTxInterrupt(I2S_TDM_TX, CY_TDM_INTR_TX_FIFO_TRIGGER);
}

/**
 * @brief RX FIFO trigger interrupt handler
 *
 * Called when RX FIFO has data available.
 */
static void i2s_rx_isr(void)
{
    int16_t *buffer;
    uint32_t samples_written;
    uint32_t fifo_level;
    uint32_t i;

    if (!g_i2s_ctx.running) {
        Cy_AudioTDM_ClearRxInterrupt(I2S_TDM_RX, CY_TDM_INTR_RX_FIFO_TRIGGER);
        return;
    }

    /* Get inactive buffer */
    buffer = dma_buffer_get_inactive(&g_i2s_ctx.rx_dma);

    /* Read samples from RX FIFO */
    fifo_level = Cy_AudioTDM_GetNumInRxFifo(I2S_TDM_RX);
    for (i = 0; i < g_i2s_ctx.rx_dma.buffer_size_samples && i < fifo_level; i++) {
        buffer[i] = (int16_t)Cy_AudioTDM_ReadRxData(I2S_TDM_RX);
    }

    /* Fill remainder with zeros if needed */
    for (; i < g_i2s_ctx.rx_dma.buffer_size_samples; i++) {
        buffer[i] = 0;
    }

    /* Call user callback if registered */
    if (g_i2s_ctx.rx_callback != NULL) {
        g_i2s_ctx.rx_callback(buffer,
                              g_i2s_ctx.rx_dma.buffer_size_samples,
                              g_i2s_ctx.rx_callback_user_data);
    }

    /* Push to ring buffer */
    samples_written = ring_buffer_write(&g_i2s_ctx.rx_ring,
                                        buffer,
                                        g_i2s_ctx.rx_dma.buffer_size_samples);

    /* If ring buffer is full, count overrun */
    if (samples_written < g_i2s_ctx.rx_dma.buffer_size_samples) {
        g_i2s_ctx.stats.buffer_overruns++;
    }

    /* Update statistics */
    g_i2s_ctx.stats.frames_transferred++;

    /* Swap buffers */
    dma_buffer_swap(&g_i2s_ctx.rx_dma);

    /* Signal semaphore for blocking read */
    if (g_i2s_ctx.rx_sem != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(g_i2s_ctx.rx_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    /* Clear interrupt */
    Cy_AudioTDM_ClearRxInterrupt(I2S_TDM_RX, CY_TDM_INTR_RX_FIFO_TRIGGER);
}

/**
 * @brief Map word size to TDM enum
 */
static cy_en_tdm_ws_t get_tdm_word_size(uint8_t bit_depth)
{
    switch (bit_depth) {
        case 8:  return CY_TDM_SIZE_8;
        case 10: return CY_TDM_SIZE_10;
        case 12: return CY_TDM_SIZE_12;
        case 14: return CY_TDM_SIZE_14;
        case 16: return CY_TDM_SIZE_16;
        case 18: return CY_TDM_SIZE_18;
        case 20: return CY_TDM_SIZE_20;
        case 24: return CY_TDM_SIZE_24;
        case 32: return CY_TDM_SIZE_32;
        default: return CY_TDM_SIZE_16;
    }
}

/**
 * @brief Calculate clock divider for desired sample rate
 *
 * For I2S: BCLK = sample_rate * channels * bits_per_channel
 * Clock divider = source_clock / BCLK
 */
static uint16_t calculate_clock_div(uint32_t sample_rate, uint8_t channels, uint8_t bit_depth)
{
    /* Assume 48 MHz source clock (typical for audio) */
    uint32_t source_clk = 48000000;
    uint32_t bclk = sample_rate * channels * bit_depth;
    uint16_t div = (uint16_t)(source_clk / bclk);

    /* Ensure even divider for 50/50 duty cycle */
    if (div & 1) {
        div++;
    }

    /* Clamp to valid range */
    if (div < 2) div = 2;
    if (div > 256) div = 256;

    return div;
}

/**
 * @brief Initialize I2S hardware using TDM PDL driver
 */
static int i2s_hw_init(const i2s_stream_config_t *config)
{
    cy_en_tdm_status_t result;
    uint16_t clk_div;

    /* Calculate clock divider */
    clk_div = calculate_clock_div(config->sample_rate, config->channels, config->bit_depth);

    /* Configure TX */
    g_tdm_tx_config.enable = true;
    g_tdm_tx_config.masterMode = CY_TDM_DEVICE_MASTER;
    g_tdm_tx_config.wordSize = get_tdm_word_size(config->bit_depth);
    g_tdm_tx_config.format = CY_TDM_LEFT_DELAYED;  /* Standard I2S format */
    g_tdm_tx_config.clkDiv = clk_div;
    g_tdm_tx_config.clkSel = CY_TDM_SEL_SRSS_CLK0;
    g_tdm_tx_config.sckPolarity = CY_TDM_CLK;
    g_tdm_tx_config.fsyncPolarity = CY_TDM_SIGN;
    g_tdm_tx_config.fsyncFormat = CY_TDM_CH_PERIOD;
    g_tdm_tx_config.channelNum = config->channels;
    g_tdm_tx_config.channelSize = 32;  /* 32-bit channel slots */
    g_tdm_tx_config.fifoTriggerLevel = I2S_TX_FIFO_TRIGGER;
    g_tdm_tx_config.chEn = (1U << config->channels) - 1;  /* Enable all channels */
    g_tdm_tx_config.signalInput = 0;  /* Independent signaling */
    g_tdm_tx_config.i2sMode = true;   /* I2S mode */

    /* Configure RX (similar to TX) */
    g_tdm_rx_config.enable = true;
    g_tdm_rx_config.masterMode = CY_TDM_DEVICE_MASTER;
    g_tdm_rx_config.wordSize = get_tdm_word_size(config->bit_depth);
    g_tdm_rx_config.signExtend = CY_SIGN_EXTEND;
    g_tdm_rx_config.format = CY_TDM_LEFT_DELAYED;
    g_tdm_rx_config.clkDiv = clk_div;
    g_tdm_rx_config.clkSel = CY_TDM_SEL_SRSS_CLK0;
    g_tdm_rx_config.sckPolarity = CY_TDM_CLK;
    g_tdm_rx_config.fsyncPolarity = CY_TDM_SIGN;
    g_tdm_rx_config.lateSample = false;
    g_tdm_rx_config.fsyncFormat = CY_TDM_CH_PERIOD;
    g_tdm_rx_config.channelNum = config->channels;
    g_tdm_rx_config.channelSize = 32;
    g_tdm_rx_config.chEn = (1U << config->channels) - 1;
    g_tdm_rx_config.fifoTriggerLevel = I2S_RX_FIFO_TRIGGER;
    g_tdm_rx_config.signalInput = 2;  /* RX uses TX master clocks */
    g_tdm_rx_config.i2sMode = true;

    /* Combined config */
    g_tdm_config.tx_config = &g_tdm_tx_config;
    g_tdm_config.rx_config = &g_tdm_rx_config;

    /* Initialize TDM */
    result = Cy_AudioTDM_Init(I2S_TDM_STRUCT, &g_tdm_config);
    if (result != CY_TDM_SUCCESS) {
        return -1;
    }

    /* Set up TX interrupt */
    Cy_AudioTDM_SetTxInterruptMask(I2S_TDM_TX, CY_TDM_INTR_TX_FIFO_TRIGGER);

    /* Set up RX interrupt */
    Cy_AudioTDM_SetRxInterruptMask(I2S_TDM_RX, CY_TDM_INTR_RX_FIFO_TRIGGER);

    /* Note: Interrupt vectors should be configured in the system startup
     * or via Device Configurator. The ISR functions i2s_tx_isr and i2s_rx_isr
     * should be registered as handlers for I2S_TDM_TX_IRQ and I2S_TDM_RX_IRQ */

    return 0;
}

/**
 * @brief Deinitialize I2S hardware
 */
static void i2s_hw_deinit(void)
{
    Cy_AudioTDM_DeInit(I2S_TDM_STRUCT);
}

/**
 * @brief Start I2S hardware streaming
 */
static int i2s_hw_start(void)
{
    /* Enable TX and RX */
    Cy_AudioTDM_EnableTx(I2S_TDM_TX);
    Cy_AudioTDM_EnableRx(I2S_TDM_RX);

    /* Pre-fill TX FIFO with silence */
    for (uint32_t i = 0; i < I2S_TX_FIFO_TRIGGER; i++) {
        Cy_AudioTDM_WriteTxData(I2S_TDM_TX, 0);
    }

    /* Activate TX and RX */
    Cy_AudioTDM_ActivateTx(I2S_TDM_TX);
    Cy_AudioTDM_ActivateRx(I2S_TDM_RX);

    return 0;
}

/**
 * @brief Stop I2S hardware streaming
 */
static int i2s_hw_stop(void)
{
    /* Deactivate TX and RX */
    Cy_AudioTDM_DeActivateTx(I2S_TDM_TX);
    Cy_AudioTDM_DeActivateRx(I2S_TDM_RX);

    /* Disable TX and RX */
    Cy_AudioTDM_DisableTx(I2S_TDM_TX);
    Cy_AudioTDM_DisableRx(I2S_TDM_RX);

    return 0;
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

    /* Create FreeRTOS synchronization primitives */
    g_i2s_ctx.tx_sem = xSemaphoreCreateBinary();
    g_i2s_ctx.rx_sem = xSemaphoreCreateBinary();
    g_i2s_ctx.mutex = xSemaphoreCreateMutex();

    if (g_i2s_ctx.tx_sem == NULL || g_i2s_ctx.rx_sem == NULL ||
        g_i2s_ctx.mutex == NULL) {
        /* Clean up any successfully created semaphores */
        if (g_i2s_ctx.tx_sem != NULL) {
            vSemaphoreDelete(g_i2s_ctx.tx_sem);
        }
        if (g_i2s_ctx.rx_sem != NULL) {
            vSemaphoreDelete(g_i2s_ctx.rx_sem);
        }
        if (g_i2s_ctx.mutex != NULL) {
            vSemaphoreDelete(g_i2s_ctx.mutex);
        }
        return -9;
    }

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

    /* Delete FreeRTOS synchronization primitives */
    if (g_i2s_ctx.tx_sem != NULL) {
        vSemaphoreDelete(g_i2s_ctx.tx_sem);
        g_i2s_ctx.tx_sem = NULL;
    }
    if (g_i2s_ctx.rx_sem != NULL) {
        vSemaphoreDelete(g_i2s_ctx.rx_sem);
        g_i2s_ctx.rx_sem = NULL;
    }
    if (g_i2s_ctx.mutex != NULL) {
        vSemaphoreDelete(g_i2s_ctx.mutex);
        g_i2s_ctx.mutex = NULL;
    }

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

    /* Convert timeout to ticks (0 means non-blocking, portMAX_DELAY for infinite) */
    TickType_t ticks;
    if (timeout_ms == 0) {
        ticks = 0;
    } else if (timeout_ms == UINT32_MAX) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }

    TickType_t start_ticks = xTaskGetTickCount();

    while (samples_read < sample_count) {
        /* Try to read available samples */
        remaining = sample_count - samples_read;
        samples_read += ring_buffer_read(&g_i2s_ctx.rx_ring,
                                         &buffer[samples_read],
                                         remaining);

        if (samples_read >= sample_count) {
            break;
        }

        /* Non-blocking case: return immediately with what we have */
        if (ticks == 0) {
            break;
        }

        /* Check for timeout */
        TickType_t elapsed = xTaskGetTickCount() - start_ticks;
        if (elapsed >= ticks && ticks != portMAX_DELAY) {
            break;  /* Timeout */
        }

        /* Wait for more samples from RX ISR */
        TickType_t remaining_ticks = (ticks == portMAX_DELAY) ? portMAX_DELAY : (ticks - elapsed);
        xSemaphoreTake(g_i2s_ctx.rx_sem, remaining_ticks);
    }

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

    /* Convert timeout to ticks (0 means non-blocking, portMAX_DELAY for infinite) */
    TickType_t ticks;
    if (timeout_ms == 0) {
        ticks = 0;
    } else if (timeout_ms == UINT32_MAX) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }

    TickType_t start_ticks = xTaskGetTickCount();

    while (samples_written < sample_count) {
        /* Try to write available space */
        remaining = sample_count - samples_written;
        samples_written += ring_buffer_write(&g_i2s_ctx.tx_ring,
                                             &buffer[samples_written],
                                             remaining);

        if (samples_written >= sample_count) {
            break;
        }

        /* Non-blocking case: return immediately with what we wrote */
        if (ticks == 0) {
            break;
        }

        /* Check for timeout */
        TickType_t elapsed = xTaskGetTickCount() - start_ticks;
        if (elapsed >= ticks && ticks != portMAX_DELAY) {
            break;  /* Timeout */
        }

        /* Wait for space from TX ISR */
        TickType_t remaining_ticks = (ticks == portMAX_DELAY) ? portMAX_DELAY : (ticks - elapsed);
        xSemaphoreTake(g_i2s_ctx.tx_sem, remaining_ticks);
    }

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

/*******************************************************************************
 * ISR Registration (weak symbols for user override)
 ******************************************************************************/

/**
 * @brief TDM TX interrupt handler wrapper
 *
 * This function should be registered as the interrupt handler for I2S_TDM_TX_IRQ.
 * You can do this via Device Configurator or manually in startup code.
 */
void TDM_0_interrupts_tx_0_IRQHandler(void)
{
    i2s_tx_isr();
}

/**
 * @brief TDM RX interrupt handler wrapper
 *
 * This function should be registered as the interrupt handler for I2S_TDM_RX_IRQ.
 */
void TDM_0_interrupts_rx_0_IRQHandler(void)
{
    i2s_rx_isr();
}
