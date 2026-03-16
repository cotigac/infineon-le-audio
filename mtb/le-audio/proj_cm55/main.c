/**
 * @file main.c
 * @brief CM55 Main - Audio DSP, LC3 Codec, I2S Streaming
 *
 * This is the main entry point for the CM55 core which handles:
 * - LC3 codec encoding/decoding (leveraging Helium DSP)
 * - I2S audio streaming with DMA
 * - Audio buffer management via audio_task module
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

/* Audio modules */
#include "audio/audio_task.h"
#include "audio/i2s_stream.h"
#include "ipc/audio_ipc.h"

/*******************************************************************************
 * Macros
 ******************************************************************************/

/* LPTimer configuration for tickless idle */
#define LPTIMER_1_WAIT_TIME_USEC        (62U)
#define APP_LPTIMER_INTERRUPT_PRIORITY  (1U)

/* IPC task for CM33 communication */
#define IPC_TASK_NAME           "IPC"
#define IPC_TASK_STACK_SIZE     (2048U)
#define IPC_TASK_PRIORITY       (configMAX_PRIORITIES - 2U)

/* Audio configuration */
#define AUDIO_SAMPLE_RATE       (48000U)
#define AUDIO_FRAME_DURATION_US (10000U)  /* 10ms frames */
#define AUDIO_CHANNELS          (1U)      /* Mono for LE Audio */

/* LC3 configuration */
#define LC3_FRAME_BYTES         (100U)    /* Encoded frame size at 96 kbps */

/*******************************************************************************
 * Global Variables
 ******************************************************************************/

/* LPTimer HAL object */
static mtb_hal_lptimer_t lptimer_obj;

/* RTC HAL object */
static mtb_hal_rtc_t rtc_obj;

/* Application state */
static volatile bool g_app_running = false;

/* Task handles */
static TaskHandle_t g_ipc_task_handle = NULL;

/* Audio stream ID (created by audio_task module) */
static uint8_t g_audio_stream_id = 0xFF;

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

static void handle_app_error(void);
static void lptimer_interrupt_handler(void);
static void setup_clib_support(void);
static void setup_tickless_idle_timer(void);
static int init_audio_system(void);
static void ipc_task(void *pvParameters);
static void audio_state_callback(audio_task_state_t state, void *user_data);
static void audio_stream_callback(uint8_t stream_id, int event, void *user_data);
static void audio_pcm_callback(int16_t *samples, uint16_t num_samples,
                               uint8_t channels, audio_stream_direction_t direction,
                               void *user_data);

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
 * Audio System Initialization
 ******************************************************************************/

/**
 * @brief Initialize the complete audio system
 *
 * This initializes:
 * 1. IPC with CM33 (must wait for CM33 to be ready)
 * 2. Audio task module (handles LC3, I2S, buffers)
 * 3. Creates an audio stream for LE Audio
 */
static int init_audio_system(void)
{
    int result;

    printf("[CM55] Initializing audio system...\n");

    /***************************************************************************
     * Step 1: Initialize IPC with CM33
     **************************************************************************/
    printf("  IPC with CM33...\n");
    if (audio_ipc_init_secondary() != CY_RSLT_SUCCESS) {
        printf("  ERROR: IPC init failed - CM33 not ready?\n");
        return -1;
    }
    printf("  IPC: OK\n");

    /***************************************************************************
     * Step 2: Initialize I2S stream
     **************************************************************************/
    printf("  I2S stream...\n");
    i2s_stream_config_t i2s_config = {
        .direction = I2S_STREAM_DUPLEX,
        .sample_rate = I2S_SAMPLE_RATE_48000,
        .bit_depth = I2S_BITS_16,
        .channels = AUDIO_CHANNELS,
        .buffer_size_samples = AUDIO_SAMPLE_RATE * AUDIO_FRAME_DURATION_US / 1000000
    };

    result = i2s_stream_init(&i2s_config);
    if (result != 0) {
        printf("  WARNING: I2S init returned %d (may work without hardware)\n", result);
    } else {
        printf("  I2S: OK (48kHz, 16-bit, %lu samples/frame)\n",
               (unsigned long)(AUDIO_SAMPLE_RATE * AUDIO_FRAME_DURATION_US / 1000000));
    }

    /***************************************************************************
     * Step 3: Initialize audio task module
     **************************************************************************/
    printf("  Audio task module...\n");

    /* Default stream configuration for LE Audio */
    audio_stream_config_t default_stream_config = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .frame_duration_us = AUDIO_FRAME_DURATION_US,
        .channels = AUDIO_CHANNELS,
        .octets_per_frame = LC3_FRAME_BYTES
    };

    audio_task_config_t audio_config = {
        .task_name = "AudioDSP",
        .stack_size = AUDIO_TASK_STACK_SIZE,
        .priority = AUDIO_TASK_PRIORITY,
        .pcm_buffer_frames = 4,      /* 4 frames of PCM buffering */
        .lc3_buffer_frames = 4,      /* 4 frames of LC3 buffering */
        .enable_plc = true,          /* Enable packet loss concealment */
        .default_config = default_stream_config,
        .state_callback = audio_state_callback,
        .stream_callback = audio_stream_callback,
        .pcm_callback = audio_pcm_callback,
        .user_data = NULL
    };

    result = audio_task_init(&audio_config);
    if (result != AUDIO_TASK_OK) {
        printf("  ERROR: Audio task init failed: %d\n", result);
        return -2;
    }
    printf("  Audio task: OK\n");

    /***************************************************************************
     * Step 4: Create audio stream for LE Audio
     **************************************************************************/
    printf("  Creating audio stream...\n");

    audio_stream_config_t stream_config = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .frame_duration_us = AUDIO_FRAME_DURATION_US,
        .channels = AUDIO_CHANNELS,
        .octets_per_frame = LC3_FRAME_BYTES
    };

    result = audio_task_create_stream(
        AUDIO_STREAM_DIRECTION_BIDIR,  /* Full duplex */
        AUDIO_STREAM_TYPE_UNICAST,     /* CIS unicast (default) */
        &stream_config,
        &g_audio_stream_id
    );

    if (result != AUDIO_TASK_OK) {
        printf("  ERROR: Stream creation failed: %d\n", result);
        return -3;
    }
    printf("  Audio stream: OK (ID=%d, bidir, 48kHz, %d bytes/frame)\n",
           g_audio_stream_id, LC3_FRAME_BYTES);

    /***************************************************************************
     * Step 5: Start audio processing
     **************************************************************************/
    printf("  Starting audio task...\n");
    result = audio_task_start();
    if (result != AUDIO_TASK_OK) {
        printf("  ERROR: Audio task start failed: %d\n", result);
        return -4;
    }

    result = audio_task_start_stream(g_audio_stream_id);
    if (result != AUDIO_TASK_OK) {
        printf("  ERROR: Stream start failed: %d\n", result);
        return -5;
    }

    /* Start I2S hardware */
    i2s_stream_start();

    g_app_running = true;
    printf("[CM55] Audio system initialized and running!\n\n");

    return 0;
}

/*******************************************************************************
 * Audio Task Callbacks
 ******************************************************************************/

/**
 * @brief Audio task state change callback
 */
static void audio_state_callback(audio_task_state_t state, void *user_data)
{
    (void)user_data;

    const char *state_names[] = {
        "IDLE", "STARTING", "RUNNING", "STOPPING", "ERROR"
    };

    if (state < sizeof(state_names)/sizeof(state_names[0])) {
        printf("[Audio] State: %s\n", state_names[state]);
    }
}

/**
 * @brief Audio stream event callback
 */
static void audio_stream_callback(uint8_t stream_id, int event, void *user_data)
{
    (void)user_data;

    printf("[Audio] Stream %d event: %d\n", stream_id, event);
}

/**
 * @brief PCM data callback - integrates with IPC
 *
 * This callback is called by audio_task after encoding (TX direction)
 * or before decoding (RX direction). We use it to transfer data via IPC.
 */
static void audio_pcm_callback(int16_t *samples, uint16_t num_samples,
                               uint8_t channels, audio_stream_direction_t direction,
                               void *user_data)
{
    (void)user_data;
    (void)samples;
    (void)num_samples;
    (void)channels;
    (void)direction;

    /* Note: The actual IPC transfer is handled internally by audio_task
     * when it interacts with isoc_handler. This callback is for any
     * additional processing needed (e.g., monitoring, side effects). */
}

/*******************************************************************************
 * IPC Task
 ******************************************************************************/

/**
 * @brief IPC task - monitors IPC health and audio statistics
 *
 * This task monitors the IPC connection with CM33 and logs
 * audio processing statistics periodically.
 */
static void ipc_task(void *pvParameters)
{
    (void)pvParameters;
    audio_ipc_stats_t ipc_stats;
    audio_ipc_stats_t prev_ipc_stats = {0};
    audio_task_stats_t audio_stats;
    uint32_t print_counter = 0;

    printf("[IPC] Task started on CM55\n");

    /* Wait for audio system to initialize */
    while (!g_app_running) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("[IPC] Audio running, monitoring IPC and stats\n");

    while (g_app_running) {
        /* Check IPC health */
        if (!audio_ipc_is_ready()) {
            printf("[IPC] WARNING: IPC connection lost!\n");
        }

        /* Get IPC statistics */
        audio_ipc_get_stats(&ipc_stats);

        /* Log IPC issues */
        if (ipc_stats.tx_queue_full > prev_ipc_stats.tx_queue_full) {
            printf("[IPC] TX queue full (dropped %lu frames)\n",
                   ipc_stats.tx_queue_full - prev_ipc_stats.tx_queue_full);
        }
        if (ipc_stats.rx_queue_empty > prev_ipc_stats.rx_queue_empty) {
            printf("[IPC] RX underrun (%lu events)\n",
                   ipc_stats.rx_queue_empty - prev_ipc_stats.rx_queue_empty);
        }

        prev_ipc_stats = ipc_stats;

        /* Periodically print audio statistics (every 10 seconds) */
        print_counter++;
        if (print_counter >= 100) {  /* 100 * 100ms = 10s */
            print_counter = 0;

            if (audio_task_get_stats(&audio_stats) == AUDIO_TASK_OK) {
                printf("[Stats] Encoded: %lu, Decoded: %lu, CPU: %d%%\n",
                       (unsigned long)audio_stats.frames_encoded,
                       (unsigned long)audio_stats.frames_decoded,
                       audio_task_get_cpu_usage());

                if (audio_stats.encode_errors > 0 || audio_stats.decode_errors > 0) {
                    printf("[Stats] Errors - Encode: %lu, Decode: %lu\n",
                           (unsigned long)audio_stats.encode_errors,
                           (unsigned long)audio_stats.decode_errors);
                }
            }
        }

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
    printf("Using audio_task module for stream management\n");
    printf("LC3: 48kHz, 10ms frames, %d bytes encoded\n", LC3_FRAME_BYTES);
    printf("==============================================================\n\n");

    /***************************************************************************
     * Phase 3: Initialize Audio System
     **************************************************************************/

    if (init_audio_system() != 0) {
        printf("FATAL: Audio system initialization failed!\n");
        handle_app_error();
    }

    /***************************************************************************
     * Phase 4: Create IPC Monitoring Task
     **************************************************************************/

    printf("Creating IPC monitoring task...\n");

    task_result = xTaskCreate(ipc_task, IPC_TASK_NAME,
                              IPC_TASK_STACK_SIZE, NULL,
                              IPC_TASK_PRIORITY, &g_ipc_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create IPC task\n");
        handle_app_error();
    }

    printf("CM55 ready!\n\n");

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
