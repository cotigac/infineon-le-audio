/**
 * @file main.c
 * @brief Infineon LE Audio Demo - Main Application Entry
 *
 * This application demonstrates full-duplex LE Audio (LC3), Auracast broadcast,
 * and MIDI over BLE/USB on PSoC Edge E81 with CYW55511 Bluetooth.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Infineon PDL/HAL headers */
#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/* Application headers */
#include "audio/lc3_wrapper.h"
#include "audio/i2s_stream.h"
#include "le_audio/le_audio_manager.h"
#include "midi/midi_ble_service.h"
#include "midi/midi_usb.h"
#include "midi/midi_router.h"
#include "bluetooth/bt_init.h"
#include "wifi/wifi_bridge.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Task stack sizes */
#define AUDIO_TASK_STACK_SIZE   (4096)
#define BLE_TASK_STACK_SIZE     (4096)
#define USB_TASK_STACK_SIZE     (2048)
#define MIDI_TASK_STACK_SIZE    (1024)
#define WIFI_TASK_STACK_SIZE    (4096)

/** Task priorities (higher number = higher priority) */
#define I2S_TASK_PRIORITY       (7)
#define AUDIO_TASK_PRIORITY     (6)
#define BLE_TASK_PRIORITY       (5)
#define USB_TASK_PRIORITY       (4)
#define WIFI_TASK_PRIORITY      (3)
#define MIDI_TASK_PRIORITY      (2)

/** Audio configuration */
#define AUDIO_SAMPLE_RATE       (48000)
#define AUDIO_FRAME_DURATION_MS (10)
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE * AUDIO_FRAME_DURATION_MS / 1000)

/*******************************************************************************
 * Global Variables
 ******************************************************************************/

/** LC3 codec context */
static lc3_codec_ctx_t *g_lc3_ctx = NULL;

/** Application state */
static volatile bool g_app_running = false;

/*******************************************************************************
 * Task Handles
 ******************************************************************************/

static TaskHandle_t g_audio_task_handle = NULL;
static TaskHandle_t g_ble_task_handle = NULL;
static TaskHandle_t g_usb_task_handle = NULL;
static TaskHandle_t g_midi_task_handle = NULL;
static TaskHandle_t g_wifi_task_handle = NULL;

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

static int app_init(void);
static void audio_task(void *pvParameters);
static void ble_task(void *pvParameters);
static void usb_task(void *pvParameters);
static void midi_task(void *pvParameters);
static void wifi_task(void *pvParameters);
static void le_audio_event_handler(const le_audio_event_t *event, void *user_data);

/*******************************************************************************
 * Main Function
 ******************************************************************************/

int main(void)
{
    int result;

    /* Initialize the application */
    result = app_init();
    if (result != 0) {
        printf("Application initialization failed: %d\n", result);
        return result;
    }

    printf("\n");
    printf("==============================================\n");
    printf("   Infineon LE Audio Demo - PSoC Edge E82\n");
    printf("==============================================\n");
    printf("Hardware: PSoC Edge E82 + CYW55512\n");
    printf("Features:\n");
    printf("  - LE Audio Full-Duplex (LC3)\n");
    printf("  - Auracast Broadcast\n");
    printf("  - BLE MIDI\n");
    printf("  - USB MIDI (High-Speed)\n");
    printf("  - I2S Audio Streaming\n");
    printf("  - Wi-Fi Data Bridge\n");
    printf("\n");
    printf("Creating FreeRTOS tasks...\n");

    /* Create FreeRTOS tasks */
    BaseType_t task_result;

    task_result = xTaskCreate(audio_task, "Audio", AUDIO_TASK_STACK_SIZE, NULL,
                              AUDIO_TASK_PRIORITY, &g_audio_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create Audio task\n");
        return -10;
    }

    task_result = xTaskCreate(ble_task, "BLE", BLE_TASK_STACK_SIZE, NULL,
                              BLE_TASK_PRIORITY, &g_ble_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create BLE task\n");
        return -11;
    }

    task_result = xTaskCreate(usb_task, "USB", USB_TASK_STACK_SIZE, NULL,
                              USB_TASK_PRIORITY, &g_usb_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create USB task\n");
        return -12;
    }

    task_result = xTaskCreate(midi_task, "MIDI", MIDI_TASK_STACK_SIZE, NULL,
                              MIDI_TASK_PRIORITY, &g_midi_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create MIDI task\n");
        return -13;
    }

    task_result = xTaskCreate(wifi_task, "WiFi", WIFI_TASK_STACK_SIZE, NULL,
                              WIFI_TASK_PRIORITY, &g_wifi_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create Wi-Fi task\n");
        return -14;
    }

    printf("All tasks created successfully!\n");
    printf("Starting FreeRTOS scheduler...\n\n");

    /* Start FreeRTOS scheduler - this should never return */
    vTaskStartScheduler();

    /* Should never reach here */
    printf("FATAL ERROR: FreeRTOS scheduler returned!\n");
    CY_ASSERT(0);

    return -100;
}

/*******************************************************************************
 * Initialization
 ******************************************************************************/

static int app_init(void)
{
    cy_rslt_t cy_result;
    int result;

    /***************************************************************************
     * Phase 1: Board Support Package Initialization
     **************************************************************************/

    /* Initialize the device and board peripherals */
    cy_result = cybsp_init();
    if (cy_result != CY_RSLT_SUCCESS) {
        /* Cannot use printf yet - no retarget IO */
        CY_ASSERT(0);
        return -1;
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize retarget-io to use the debug UART port */
    cy_result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                     CY_RETARGET_IO_BAUDRATE);
    if (cy_result != CY_RSLT_SUCCESS) {
        CY_ASSERT(0);
        return -1;
    }

    /* Now printf works - print startup message */
    printf("\n\n");
    printf("Board initialized: PSoC Edge E82\n");
    printf("Debug UART ready at %lu baud\n", (unsigned long)CY_RETARGET_IO_BAUDRATE);

    /***************************************************************************
     * Phase 2: Application Module Initialization
     **************************************************************************/

    /* Initialize LC3 codec */
    printf("Initializing LC3 codec...\n");
    lc3_config_t lc3_config = LC3_CONFIG_DEFAULT;
    g_lc3_ctx = lc3_wrapper_init(&lc3_config);
    if (g_lc3_ctx == NULL) {
        printf("ERROR: LC3 codec initialization failed\n");
        return -2;
    }
    printf("  LC3 codec: OK\n");

    /* Initialize I2S stream */
    printf("Initializing I2S stream...\n");
    i2s_stream_config_t i2s_config = I2S_STREAM_CONFIG_DEFAULT;
    result = i2s_stream_init(&i2s_config);
    if (result != 0) {
        printf("ERROR: I2S initialization failed: %d\n", result);
        return -3;
    }
    printf("  I2S stream: OK\n");

    /* Initialize LE Audio manager */
    printf("Initializing LE Audio manager...\n");
    le_audio_codec_config_t le_codec_config = LE_AUDIO_CODEC_CONFIG_DEFAULT;
    result = le_audio_init(&le_codec_config);
    if (result != 0) {
        printf("ERROR: LE Audio initialization failed: %d\n", result);
        return -4;
    }
    printf("  LE Audio manager: OK\n");

    /* Register LE Audio event callback */
    le_audio_register_callback(le_audio_event_handler, NULL);

    /* Initialize Bluetooth stack */
    printf("Initializing Bluetooth stack...\n");
    result = bt_init();
    if (result != 0) {
        printf("WARNING: Bluetooth initialization failed: %d (continuing anyway)\n", result);
        /* Don't return error - allow system to boot for debugging */
    } else {
        printf("  Bluetooth stack: OK\n");
    }

    /* Initialize MIDI router */
    printf("Initializing MIDI router...\n");
    result = midi_router_init(NULL);
    if (result != 0) {
        printf("WARNING: MIDI router initialization failed: %d\n", result);
    } else {
        printf("  MIDI router: OK\n");
    }

    /* Initialize USB MIDI */
    printf("Initializing USB MIDI...\n");
    result = midi_usb_init(NULL);
    if (result != 0) {
        printf("WARNING: USB MIDI initialization failed: %d\n", result);
    } else {
        printf("  USB MIDI: OK\n");
    }

    /* Initialize BLE MIDI */
    printf("Initializing BLE MIDI...\n");
    result = midi_ble_init(NULL);
    if (result != 0) {
        printf("WARNING: BLE MIDI initialization failed: %d\n", result);
    } else {
        printf("  BLE MIDI: OK\n");
    }

    /* Initialize Wi-Fi bridge */
    printf("Initializing Wi-Fi bridge...\n");
    result = wifi_bridge_init(NULL);
    if (result != 0) {
        printf("WARNING: Wi-Fi bridge initialization failed: %d\n", result);
    } else {
        printf("  Wi-Fi bridge: OK\n");
    }

    g_app_running = true;
    printf("\nApplication initialization complete!\n");

    return 0;
}

/*******************************************************************************
 * FreeRTOS Tasks
 ******************************************************************************/

/**
 * @brief Audio task - handles LC3 encoding/decoding and I2S streaming
 */
static void audio_task(void *pvParameters)
{
    (void)pvParameters;

    int16_t pcm_rx_buffer[AUDIO_SAMPLES_PER_FRAME];
    int16_t pcm_tx_buffer[AUDIO_SAMPLES_PER_FRAME];
    uint8_t lc3_buffer[100];  /* Encoded LC3 frame */

    printf("Audio task started\n");

    /* Start I2S streaming */
    i2s_stream_start();

    while (g_app_running) {
        /* Read PCM samples from I2S (from main controller) */
        int samples = i2s_stream_read(pcm_rx_buffer, AUDIO_SAMPLES_PER_FRAME, 20);
        if (samples > 0) {
            /* Encode to LC3 */
            int result = lc3_wrapper_encode(g_lc3_ctx, pcm_rx_buffer, lc3_buffer, 0);
            if (result == 0) {
                /* Send LC3 frame to LE Audio for transmission */
                le_audio_send_audio(pcm_rx_buffer, samples);
            }
        }

        /* Check for received LE Audio data */
        int received = le_audio_receive_audio(pcm_tx_buffer, AUDIO_SAMPLES_PER_FRAME, 0);
        if (received > 0) {
            /* Write to I2S (to main controller) */
            i2s_stream_write(pcm_tx_buffer, received, 10);
        }

        /* Short delay - audio timing is driven by I2S DMA interrupts */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief BLE task - handles Bluetooth stack and LE Audio profiles
 */
static void ble_task(void *pvParameters)
{
    (void)pvParameters;

    printf("BLE task started\n");

    while (g_app_running) {
        /* Process Bluetooth stack events */
        bt_process();

        /* Process LE Audio state machine */
        le_audio_process();

        /* Yield to other tasks */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief USB task - handles USB enumeration and MIDI
 */
static void usb_task(void *pvParameters)
{
    (void)pvParameters;

    printf("USB task started\n");

    while (g_app_running) {
        /* Process USB MIDI events */
        midi_usb_process();

        /* Yield to other tasks */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief MIDI task - routes MIDI between BLE, USB, and main controller
 */
static void midi_task(void *pvParameters)
{
    (void)pvParameters;

    printf("MIDI task started\n");

    while (g_app_running) {
        /* Process MIDI routing between interfaces */
        midi_router_process();

        /* Yield to other tasks */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/**
 * @brief Wi-Fi task - handles USB-to-Wi-Fi data bridge
 */
static void wifi_task(void *pvParameters)
{
    (void)pvParameters;

    printf("Wi-Fi task started\n");

    /* Start the Wi-Fi bridge */
    int result = wifi_bridge_start();
    if (result != 0) {
        printf("WARNING: Wi-Fi bridge start failed: %d\n", result);
    }

    while (g_app_running) {
        /* Process Wi-Fi bridge data transfers */
        wifi_bridge_process();

        /* Yield to other tasks */
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* Stop Wi-Fi bridge on task exit */
    wifi_bridge_stop();
}

/*******************************************************************************
 * Event Handlers
 ******************************************************************************/

/**
 * @brief LE Audio event handler
 */
static void le_audio_event_handler(const le_audio_event_t *event, void *user_data)
{
    (void)user_data;

    switch (event->type) {
        case LE_AUDIO_EVENT_STATE_CHANGED:
            printf("LE Audio state changed: %d\n", event->data.new_state);
            break;

        case LE_AUDIO_EVENT_STREAM_STARTED:
            printf("LE Audio stream started\n");
            break;

        case LE_AUDIO_EVENT_STREAM_STOPPED:
            printf("LE Audio stream stopped\n");
            break;

        case LE_AUDIO_EVENT_ERROR:
            printf("LE Audio error: %d\n", event->data.error_code);
            break;

        default:
            break;
    }
}
