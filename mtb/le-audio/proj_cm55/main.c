/**
 * @file main.c
 * @brief CM55 Main - Audio DSP, LC3 Codec, I2S Streaming
 *
 * This is the main entry point for the CM55 core which handles:
 * - LC3 codec encoding/decoding (leveraging Helium DSP)
 * - I2S audio streaming with DMA ping-pong buffers
 * - Audio buffer management
 * - Inter-processor communication with CM33 for ISOC data
 *
 * Architecture:
 *   CM33: BT stack, GATT, ISOC control, USB, Wi-Fi, MIDI routing
 *   CM55: LC3 codec, I2S streaming, audio DSP (this core)
 *
 * The CM55 Cortex-M55 with Helium (MVE) provides efficient DSP operations
 * for real-time LC3 encoding/decoding at 48kHz sample rate.
 *
 * Based on Infineon's ISOC Peripheral example with custom LE Audio integration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*******************************************************************************
 * Header Files
 ******************************************************************************/

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Infineon Platform */
#include "cybsp.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "cy_time.h"

/* Custom audio modules */
#include "audio/lc3_wrapper.h"
#include "audio/i2s_stream.h"
#include "audio/audio_buffers.h"
#include "audio/audio_task.h"
#include "ipc/audio_ipc.h"

/*******************************************************************************
 * Macros
 ******************************************************************************/

/* LPTimer configuration for tickless idle */
#define LPTIMER_1_WAIT_TIME_USEC        (62U)
#define APP_LPTIMER_INTERRUPT_PRIORITY  (1U)

/* Audio task configuration */
#define AUDIO_TASK_NAME         "Audio"
#define AUDIO_TASK_STACK_SIZE   (4096U)
#define AUDIO_TASK_PRIORITY     (configMAX_PRIORITIES - 1U)  /* Highest priority */

/* IPC task for CM33 communication */
#define IPC_TASK_NAME           "IPC"
#define IPC_TASK_STACK_SIZE     (2048U)
#define IPC_TASK_PRIORITY       (configMAX_PRIORITIES - 2U)

/* Audio configuration */
#define AUDIO_SAMPLE_RATE       (48000U)
#define AUDIO_FRAME_DURATION_US (10000U)  /* 10ms frames */
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE * AUDIO_FRAME_DURATION_US / 1000000U)
#define AUDIO_CHANNELS          (1U)      /* Mono for LE Audio */

/* LC3 configuration */
#define LC3_BITRATE_BPS         (96000U)  /* 96 kbps for high quality */
#define LC3_FRAME_BYTES         (100U)    /* Encoded frame size */

/*******************************************************************************
 * Global Variables
 ******************************************************************************/

/* LPTimer HAL object */
static mtb_hal_lptimer_t lptimer_obj;

/* RTC HAL object */
static mtb_hal_rtc_t rtc_obj;

/* LC3 codec context */
static lc3_codec_ctx_t *g_lc3_ctx = NULL;

/* Application state */
static volatile bool g_audio_running = false;

/* Task handles */
static TaskHandle_t g_audio_task_handle = NULL;
static TaskHandle_t g_ipc_task_handle = NULL;

/* Audio buffers - ping-pong for DMA */
static int16_t g_pcm_rx_buffer_a[AUDIO_SAMPLES_PER_FRAME];
static int16_t g_pcm_rx_buffer_b[AUDIO_SAMPLES_PER_FRAME];
static int16_t g_pcm_tx_buffer_a[AUDIO_SAMPLES_PER_FRAME];
static int16_t g_pcm_tx_buffer_b[AUDIO_SAMPLES_PER_FRAME];

/* LC3 encoded frame buffers */
static uint8_t g_lc3_tx_buffer[LC3_FRAME_BYTES];
static uint8_t g_lc3_rx_buffer[LC3_FRAME_BYTES];

/* Synchronization */
static SemaphoreHandle_t g_audio_ready_sem = NULL;
/* LC3 frame queues are now handled via IPC (see ipc/audio_ipc.c) */

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

static void handle_app_error(void);
static void lptimer_interrupt_handler(void);
static void setup_clib_support(void);
static void setup_tickless_idle_timer(void);
static int init_audio_modules(void);
static void audio_task(void *pvParameters);
static void ipc_task(void *pvParameters);
static void i2s_rx_callback(int16_t *buffer, uint16_t sample_count, void *user_data);
static void i2s_tx_callback(int16_t *buffer, uint16_t sample_count, void *user_data);

/*******************************************************************************
 * Platform Setup Functions (from Infineon example)
 ******************************************************************************/

static void handle_app_error(void)
{
    __disable_irq();
    CY_ASSERT(0);
    while (true) {
        /* Infinite loop */
    }
}

static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}

static void setup_clib_support(void)
{
    /* RTC initialization is done in CM33, just init CLIB support */
    mtb_clib_support_init(&rtc_obj);
}

static void setup_tickless_idle_timer(void)
{
    cy_stc_sysint_t lptimer_intr_cfg = {
        .intrSrc = CYBSP_CM55_LPTIMER_1_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    cy_en_sysint_status_t status = Cy_SysInt_Init(&lptimer_intr_cfg,
                                                   lptimer_interrupt_handler);
    if (CY_SYSINT_SUCCESS != status) {
        handle_app_error();
    }

    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    cy_en_mcwdt_status_t mcwdt_status = Cy_MCWDT_Init(CYBSP_CM55_LPTIMER_1_HW,
                                                      &CYBSP_CM55_LPTIMER_1_config);
    if (CY_MCWDT_SUCCESS != mcwdt_status) {
        handle_app_error();
    }

    Cy_MCWDT_Enable(CYBSP_CM55_LPTIMER_1_HW, CY_MCWDT_CTR_Msk,
                    LPTIMER_1_WAIT_TIME_USEC);

    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj,
                                             &CYBSP_CM55_LPTIMER_1_hal_config);
    if (CY_RSLT_SUCCESS != result) {
        handle_app_error();
    }

    cyabs_rtos_set_lptimer(&lptimer_obj);
}

/*******************************************************************************
 * Audio Module Initialization
 ******************************************************************************/

/**
 * @brief Initialize audio DSP modules (runs on CM55)
 *
 * Initializes LC3 codec and I2S streaming with DMA.
 */
static int init_audio_modules(void)
{
    int result;

    printf("[CM55] Initializing audio modules...\n");

    /* Initialize LC3 codec (leverages Helium DSP) */
    printf("  LC3 codec...\n");
    lc3_config_t lc3_config = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .frame_duration = LC3_FRAME_DURATION_10MS,
        .octets_per_frame = LC3_FRAME_BYTES,
        .channels = AUDIO_CHANNELS
    };

    g_lc3_ctx = lc3_wrapper_init(&lc3_config);
    if (g_lc3_ctx == NULL) {
        printf("  ERROR: LC3 codec init failed\n");
        return -1;
    }
    printf("  LC3 codec: OK (48kHz, 10ms, %d bytes/frame)\n", LC3_FRAME_BYTES);

    /* Initialize I2S stream with DMA ping-pong buffers */
    printf("  I2S stream...\n");
    i2s_stream_config_t i2s_config = {
        .direction = I2S_STREAM_DUPLEX,
        .sample_rate = I2S_SAMPLE_RATE_48000,
        .bit_depth = I2S_BITS_16,
        .channels = AUDIO_CHANNELS,
        .buffer_size_samples = AUDIO_SAMPLES_PER_FRAME
    };

    result = i2s_stream_init(&i2s_config);
    if (result != 0) {
        printf("  WARNING: I2S init failed: %d\n", result);
        /* Continue anyway - might work without hardware */
    } else {
        printf("  I2S stream: OK (48kHz, 16-bit, %d samples/frame)\n",
               AUDIO_SAMPLES_PER_FRAME);
    }

    /* Register I2S callbacks for DMA completion */
    i2s_stream_register_rx_callback(i2s_rx_callback, NULL);
    i2s_stream_register_tx_callback(i2s_tx_callback, NULL);

    /* Initialize audio buffers */
    printf("  Audio buffers...\n");
    result = audio_buffers_init();
    if (result != 0) {
        printf("  WARNING: Audio buffers init failed: %d\n", result);
    } else {
        printf("  Audio buffers: OK\n");
    }

    printf("[CM55] Audio module initialization complete\n\n");
    return 0;
}

/*******************************************************************************
 * I2S Callbacks (called from DMA ISR context)
 ******************************************************************************/

/**
 * @brief I2S RX callback - called when PCM data received from codec
 *
 * Encodes PCM to LC3 and sends to CM33 via IPC for ISOC transmission.
 */
static void i2s_rx_callback(int16_t *buffer, uint16_t sample_count, void *user_data)
{
    (void)user_data;
    (void)sample_count;
    static uint16_t tx_sequence = 0;

    if (!g_audio_running || g_lc3_ctx == NULL) {
        return;
    }

    /* Encode PCM to LC3 using Helium-optimized codec */
    int result = lc3_wrapper_encode(g_lc3_ctx, buffer, g_lc3_tx_buffer, 0);
    if (result == 0) {
        /* Prepare IPC frame and send to CM33 */
        audio_ipc_frame_t frame;
        frame.length = LC3_FRAME_BYTES;
        frame.sequence = tx_sequence++;
        frame.timestamp = xTaskGetTickCountFromISR() * 1000 / configTICK_RATE_HZ;
        frame.flags = AUDIO_IPC_FLAG_VALID;
        memcpy(frame.data, g_lc3_tx_buffer, LC3_FRAME_BYTES);

        /* Send to CM33 via IPC (non-blocking) */
        audio_ipc_send_encoded_frame(&frame);
    }
}

/**
 * @brief I2S TX callback - called when codec needs PCM data
 *
 * Receives LC3 from CM33 via IPC and decodes to PCM for playback.
 */
static void i2s_tx_callback(int16_t *buffer, uint16_t sample_count, void *user_data)
{
    (void)user_data;
    (void)sample_count;

    if (!g_audio_running || g_lc3_ctx == NULL) {
        /* Output silence if not running */
        memset(buffer, 0, sample_count * sizeof(int16_t));
        return;
    }

    /* Try to get LC3 frame from CM33 via IPC */
    audio_ipc_frame_t frame;
    if (audio_ipc_receive_for_decode(&frame) == CY_RSLT_SUCCESS) {
        /* Copy to local buffer and decode */
        memcpy(g_lc3_rx_buffer, frame.data, frame.length);

        /* Decode LC3 to PCM using Helium-optimized codec */
        int result = lc3_wrapper_decode(g_lc3_ctx, g_lc3_rx_buffer, buffer, 0);
        if (result != 0) {
            /* Decode failed - use PLC (Packet Loss Concealment) */
            lc3_wrapper_decode_plc(g_lc3_ctx, buffer, 0);
        }
    } else {
        /* No data available - use PLC */
        lc3_wrapper_decode_plc(g_lc3_ctx, buffer, 0);
    }
}

/*******************************************************************************
 * FreeRTOS Tasks
 ******************************************************************************/

/**
 * @brief Audio task - manages I2S streaming and LC3 processing
 *
 * Highest priority task on CM55. Handles real-time audio requirements.
 */
static void audio_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[Audio] Task started on CM55\n");

    /* Initialize IPC with CM33 (must wait for CM33 to init first) */
    printf("[Audio] Waiting for IPC with CM33...\n");
    if (audio_ipc_init_secondary() != CY_RSLT_SUCCESS) {
        printf("[Audio] ERROR: IPC init failed - CM33 not ready?\n");
        vTaskDelete(NULL);
        return;
    }
    printf("[Audio] IPC with CM33 established\n");

    /* Initialize audio modules */
    if (init_audio_modules() != 0) {
        printf("[Audio] ERROR: Module init failed\n");
        vTaskDelete(NULL);
        return;
    }

    /* Signal that audio is ready */
    xSemaphoreGive(g_audio_ready_sem);

    /* Start I2S streaming */
    printf("[Audio] Starting I2S stream\n");
    i2s_stream_start();
    g_audio_running = true;

    /* Main audio processing loop */
    while (g_audio_running) {
        /* Most work is done in ISR callbacks
         * This loop handles any deferred processing */

        /* Check for audio buffer statistics */
        i2s_stream_stats_t stats;
        i2s_stream_get_stats(&stats);

        if (stats.buffer_underruns > 0 || stats.buffer_overruns > 0) {
            /* Log buffer issues (rate limit this in production) */
        }

        /* Yield to allow other tasks to run */
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* Cleanup */
    i2s_stream_stop();
    printf("[Audio] Task stopped\n");
}

/**
 * @brief IPC task - monitors IPC health and statistics
 *
 * The actual LC3 frame transfer is handled in I2S callbacks using
 * the audio_ipc module. This task monitors IPC health and logs stats.
 */
static void ipc_task(void *pvParameters)
{
    (void)pvParameters;
    audio_ipc_stats_t stats;
    audio_ipc_stats_t prev_stats = {0};

    printf("[IPC] Task started on CM55\n");

    /* Wait for audio to be ready */
    xSemaphoreTake(g_audio_ready_sem, portMAX_DELAY);
    xSemaphoreGive(g_audio_ready_sem);

    printf("[IPC] Audio ready, monitoring IPC\n");

    while (g_audio_running) {
        /* Check IPC health */
        if (!audio_ipc_is_ready()) {
            printf("[IPC] WARNING: IPC connection lost!\n");
        }

        /* Get and log statistics periodically */
        audio_ipc_get_stats(&stats);

        /* Log if there are issues */
        if (stats.tx_queue_full > prev_stats.tx_queue_full) {
            printf("[IPC] TX queue full (dropped %lu frames)\n",
                   stats.tx_queue_full - prev_stats.tx_queue_full);
        }
        if (stats.rx_queue_empty > prev_stats.rx_queue_empty) {
            printf("[IPC] RX underrun (%lu events)\n",
                   stats.rx_queue_empty - prev_stats.rx_queue_empty);
        }

        prev_stats = stats;

        /* Check every 100ms */
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("[IPC] Task stopped\n");
}

/*******************************************************************************
 * Main Entry Point
 ******************************************************************************/

int main(void)
{
    cy_rslt_t result;
    BaseType_t task_result;

    /***************************************************************************
     * Phase 1: Platform Initialization
     **************************************************************************/

    /* Initialize BSP */
    result = cybsp_init();
    if (CY_RSLT_SUCCESS != result) {
        handle_app_error();
    }

    /* Setup CLIB support (time functions) */
    setup_clib_support();

    /* Setup LPTimer for tickless idle */
    setup_tickless_idle_timer();

    /* Enable global interrupts */
    __enable_irq();

    /***************************************************************************
     * Phase 2: Print Banner
     **************************************************************************/

    printf("\n");
    printf("==============================================================\n");
    printf("   CM55 Audio DSP - LC3 Codec + I2S Streaming\n");
    printf("==============================================================\n");
    printf("Cortex-M55 with Helium (MVE) for efficient DSP\n");
    printf("LC3: 48kHz, 10ms frames, %d bytes encoded\n", LC3_FRAME_BYTES);
    printf("==============================================================\n\n");

    /***************************************************************************
     * Phase 3: Create Synchronization Primitives
     **************************************************************************/

    g_audio_ready_sem = xSemaphoreCreateBinary();
    if (g_audio_ready_sem == NULL) {
        handle_app_error();
    }

    /* Note: LC3 frame queues are handled via IPC (audio_ipc.c)
     * IPC initialization happens in audio_task after CM33 is ready */

    /***************************************************************************
     * Phase 4: Create FreeRTOS Tasks
     **************************************************************************/

    printf("Creating FreeRTOS tasks (CM55 audio DSP)...\n");

    task_result = xTaskCreate(audio_task, AUDIO_TASK_NAME,
                              AUDIO_TASK_STACK_SIZE, NULL,
                              AUDIO_TASK_PRIORITY, &g_audio_task_handle);
    if (task_result != pdPASS) {
        handle_app_error();
    }

    task_result = xTaskCreate(ipc_task, IPC_TASK_NAME,
                              IPC_TASK_STACK_SIZE, NULL,
                              IPC_TASK_PRIORITY, &g_ipc_task_handle);
    if (task_result != pdPASS) {
        handle_app_error();
    }

    printf("CM55 tasks created!\n\n");

    /***************************************************************************
     * Phase 5: Start FreeRTOS Scheduler
     **************************************************************************/

    printf("Starting FreeRTOS scheduler on CM55...\n\n");
    vTaskStartScheduler();

    /* Should never reach here */
    handle_app_error();

    return 0;
}

/* end of file */
