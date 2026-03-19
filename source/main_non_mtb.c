/**
 * @file main_non_mtb.c
 * @brief Infineon LE Audio Demo - CM33 Main Application (Standalone/CMake Build)
 *
 * This is the CM33 core main application for standalone/CMake builds.
 * It handles:
 *   - Bluetooth LE Audio (ISOC transport, BAP, PACS)
 *   - USB MIDI and BLE MIDI
 *   - Wi-Fi data bridge
 *   - IPC communication with CM55 for LC3 codec
 *
 * Note: This file is for non-ModusToolbox builds. The MTB build uses
 * proj_cm33_ns/main.c and proj_cm55/main.c instead.
 *
 * Architecture (dual-core PSoC Edge E84):
 *   CM33: BLE stack, USB, Wi-Fi, MIDI, IPC (this file)
 *   CM55: LC3 codec, I2S audio DSP (separate main)
 *
 * Hardware: PSoC Edge E84 (CM33+CM55) + CYW55512 (BLE 6.0 + Wi-Fi 6)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Infineon PDL headers */
#include "cy_pdl.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/* Application headers */
#include "ipc/audio_ipc.h"
#include "le_audio/le_audio_manager.h"
#include "le_audio/isoc_handler.h"
#include "midi/midi_ble_service.h"
#include "midi/midi_usb.h"
#include "midi/midi_router.h"
#include "bluetooth/bt_init.h"
#include "wifi/wifi_bridge.h"

/* USB Composite and CDC headers */
#include "usb/usb_composite.h"
#include "cdc/at_system_cmds.h"
#include "cdc/at_bt_cmds.h"
#include "cdc/at_leaudio_cmds.h"
#include "cdc/at_wifi_cmds.h"
#include "cdc/at_parser.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Task stack sizes (words, not bytes on ARM) */
#define BLE_TASK_STACK_SIZE     (4096)
#define USB_TASK_STACK_SIZE     (2048)
#define MIDI_TASK_STACK_SIZE    (1024)
#define WIFI_TASK_STACK_SIZE    (4096)
#define ISOC_TASK_STACK_SIZE    (2048)

/** Task priorities (higher number = higher priority) */
#define BLE_TASK_PRIORITY       (5)
#define USB_TASK_PRIORITY       (4)
#define WIFI_TASK_PRIORITY      (3)
#define MIDI_TASK_PRIORITY      (2)
#define ISOC_TASK_PRIORITY      (6)

/*******************************************************************************
 * Global Variables
 ******************************************************************************/

/** Application state */
static volatile bool g_app_running = false;

/*******************************************************************************
 * Task Handles
 ******************************************************************************/

static TaskHandle_t g_ble_task_handle = NULL;
static TaskHandle_t g_usb_task_handle = NULL;
static TaskHandle_t g_midi_task_handle = NULL;
static TaskHandle_t g_wifi_task_handle = NULL;
static TaskHandle_t g_isoc_task_handle = NULL;

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

static int app_init(void);
static void ble_task(void *pvParameters);
static void usb_task(void *pvParameters);
static void midi_task(void *pvParameters);
static void wifi_task(void *pvParameters);
static void isoc_task(void *pvParameters);
static void le_audio_event_handler(const le_audio_event_t *event, void *user_data);

/*******************************************************************************
 * ISOC Handler Callbacks
 ******************************************************************************/

/**
 * @brief TX data callback - get LC3 data from CM55 via IPC
 */
static bool isoc_tx_data_callback(uint8_t stream_id, isoc_sdu_t *sdu, void *user_data)
{
    (void)stream_id;
    (void)user_data;

    audio_ipc_frame_t frame;

    /* Get encoded LC3 frame from CM55 via IPC */
    cy_rslt_t result = audio_ipc_receive_from_encoder(&frame);
    if (result != CY_RSLT_SUCCESS) {
        return false;  /* No data available */
    }

    /* Copy to SDU */
    if (frame.length > ISOC_HANDLER_MAX_SDU_SIZE) {
        return false;
    }

    memcpy(sdu->data, frame.data, frame.length);
    sdu->length = frame.length;
    sdu->sequence_number = frame.sequence;
    sdu->timestamp = frame.timestamp;
    sdu->num_frames = 1;
    sdu->valid = (frame.flags & AUDIO_IPC_FLAG_VALID) != 0;

    return true;
}

/**
 * @brief RX data callback - send LC3 data to CM55 via IPC for decoding
 */
static void isoc_rx_data_callback(uint8_t stream_id, const isoc_sdu_t *sdu, void *user_data)
{
    (void)stream_id;
    (void)user_data;

    audio_ipc_frame_t frame;

    /* Build IPC frame */
    if (sdu->length > AUDIO_IPC_MAX_LC3_FRAME_SIZE) {
        return;
    }

    memcpy(frame.data, sdu->data, sdu->length);
    frame.length = sdu->length;
    frame.sequence = sdu->sequence_number;
    frame.timestamp = sdu->timestamp;
    frame.flags = sdu->valid ? AUDIO_IPC_FLAG_VALID : 0;

    /* Send to CM55 for decoding */
    audio_ipc_send_to_decoder(&frame);
}

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
    printf("   Infineon LE Audio Demo - PSoC Edge E84\n");
    printf("        CM33 Core (Standalone Build)\n");
    printf("==============================================\n");
    printf("Hardware: PSoC Edge E84 + CYW55512\n");
    printf("Features:\n");
    printf("  - LE Audio Full-Duplex (ISOC transport)\n");
    printf("  - Auracast Broadcast (BIS)\n");
    printf("  - BLE MIDI 1.0\n");
    printf("  - USB MIDI (High-Speed 480 Mbps)\n");
    printf("  - USB CDC/ACM AT Command Interface\n");
    printf("  - Wi-Fi 6 Data Bridge\n");
    printf("  - IPC to CM55 for LC3 codec\n");
    printf("\n");
    printf("Creating FreeRTOS tasks...\n");

    /* Create FreeRTOS tasks */
    BaseType_t task_result;

    task_result = xTaskCreate(ble_task, "BLE", BLE_TASK_STACK_SIZE, NULL,
                              BLE_TASK_PRIORITY, &g_ble_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create BLE task\n");
        return -10;
    }

    task_result = xTaskCreate(usb_task, "USB", USB_TASK_STACK_SIZE, NULL,
                              USB_TASK_PRIORITY, &g_usb_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create USB task\n");
        return -11;
    }

    task_result = xTaskCreate(midi_task, "MIDI", MIDI_TASK_STACK_SIZE, NULL,
                              MIDI_TASK_PRIORITY, &g_midi_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create MIDI task\n");
        return -12;
    }

    task_result = xTaskCreate(wifi_task, "WiFi", WIFI_TASK_STACK_SIZE, NULL,
                              WIFI_TASK_PRIORITY, &g_wifi_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create Wi-Fi task\n");
        return -13;
    }

    task_result = xTaskCreate(isoc_task, "ISOC", ISOC_TASK_STACK_SIZE, NULL,
                              ISOC_TASK_PRIORITY, &g_isoc_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create ISOC task\n");
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
    printf("Board initialized: PSoC Edge E84 (CM33 core)\n");
    printf("Debug UART ready at %lu baud\n", (unsigned long)CY_RETARGET_IO_BAUDRATE);

    /***************************************************************************
     * Phase 2: IPC Initialization (must be before CM55 boots)
     **************************************************************************/

    printf("Initializing IPC (CM33 primary)...\n");
    cy_result = audio_ipc_init_primary();
    if (cy_result != CY_RSLT_SUCCESS) {
        printf("ERROR: IPC initialization failed: 0x%08lx\n", (unsigned long)cy_result);
        return -2;
    }
    printf("  IPC: OK (waiting for CM55)\n");

    /***************************************************************************
     * Phase 3: Application Module Initialization
     **************************************************************************/

    /* Initialize LE Audio manager */
    printf("Initializing LE Audio manager...\n");
    le_audio_codec_config_t le_codec_config = LE_AUDIO_CODEC_CONFIG_DEFAULT;
    result = le_audio_init(&le_codec_config);
    if (result != 0) {
        printf("ERROR: LE Audio initialization failed: %d\n", result);
        return -3;
    }
    printf("  LE Audio manager: OK\n");

    /* Register LE Audio event callback */
    le_audio_register_callback(le_audio_event_handler, NULL);

    /* Initialize ISOC handler with IPC callbacks */
    printf("Initializing ISOC handler...\n");
    isoc_handler_config_t isoc_config = {
        .tx_callback = isoc_tx_data_callback,
        .rx_callback = isoc_rx_data_callback,
        .state_callback = NULL,
        .error_callback = NULL,
        .user_data = NULL
    };
    result = isoc_handler_init(&isoc_config);
    if (result != 0) {
        printf("ERROR: ISOC handler initialization failed: %d\n", result);
        return -4;
    }
    printf("  ISOC handler: OK\n");

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

    /* Initialize USB composite device (MIDI + CDC + Wi-Fi Bridge)
     * This creates all USB endpoints as part of the composite device descriptor.
     * Wi-Fi bridge USB endpoints are created here but WHD is initialized later.
     */
    printf("Initializing USB composite device...\n");
    result = usb_composite_init(NULL);
    if (result != 0) {
        printf("WARNING: USB composite initialization failed: %d\n", result);
    } else {
        printf("  USB composite (MIDI + CDC + Wi-Fi): OK\n");
    }

    /* Initialize Wi-Fi bridge (WHD/SDIO only - USB handle set separately)
     * This must happen BEFORE usb_composite_start() so that the Wi-Fi bridge
     * can receive the USB handle from the composite device.
     */
    printf("Initializing Wi-Fi bridge...\n");
    result = wifi_bridge_init(NULL);
    if (result != 0) {
        printf("WARNING: Wi-Fi bridge initialization failed: %d\n", result);
    } else {
        printf("  Wi-Fi bridge (WHD/SDIO): OK\n");

        /* Connect Wi-Fi bridge to USB composite endpoint */
        int wifi_handle = usb_composite_get_wifi_bridge_handle();
        if (wifi_handle != 0) {
            result = wifi_bridge_set_handle(wifi_handle);
            if (result != 0) {
                printf("WARNING: Wi-Fi bridge USB handle set failed: %d\n", result);
            } else {
                printf("  Wi-Fi bridge USB endpoint: OK\n");
            }
        }
    }

    /* Register AT system commands */
    printf("Registering AT commands...\n");
    result = at_system_cmds_register();
    if (result != 0) {
        printf("WARNING: AT system commands registration failed: %d\n", result);
    } else {
        printf("  AT system commands: OK\n");
    }

    /* Register AT Bluetooth commands */
    result = at_bt_cmds_register();
    if (result != 0) {
        printf("WARNING: AT BT commands registration failed: %d\n", result);
    } else {
        printf("  AT BT commands: OK\n");
    }

    /* Register AT LE Audio commands */
    result = at_leaudio_cmds_register();
    if (result != 0) {
        printf("WARNING: AT LE Audio commands registration failed: %d\n", result);
    } else {
        printf("  AT LE Audio commands: OK\n");
    }

    /* Register AT Wi-Fi commands */
    result = at_wifi_cmds_register();
    if (result != 0) {
        printf("WARNING: AT Wi-Fi commands registration failed: %d\n", result);
    } else {
        printf("  AT Wi-Fi commands: OK\n");
    }

    /* Start USB device enumeration
     * MUST be called after all USB endpoints are configured (MIDI, CDC, Wi-Fi)
     * This finalizes the USB device descriptor and starts enumeration.
     */
    result = usb_composite_start();
    if (result != 0) {
        printf("WARNING: USB composite start failed: %d\n", result);
    } else {
        printf("  USB started: OK\n");
    }

    /* Initialize BLE MIDI */
    printf("Initializing BLE MIDI...\n");
    result = midi_ble_init(NULL);
    if (result != 0) {
        printf("WARNING: BLE MIDI initialization failed: %d\n", result);
    } else {
        printf("  BLE MIDI: OK\n");
    }

    g_app_running = true;
    printf("\nCM33 initialization complete!\n");
    printf("Note: CM55 must be running for audio (LC3/I2S)\n");

    return 0;
}

/*******************************************************************************
 * FreeRTOS Tasks
 ******************************************************************************/

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
 * @brief USB task - handles USB composite device (MIDI + CDC/ACM)
 */
static void usb_task(void *pvParameters)
{
    (void)pvParameters;

    printf("USB task started (MIDI + CDC/ACM)\n");

    while (g_app_running) {
        /* Process USB composite device (CDC + MIDI) */
        usb_composite_process();

        /* Process USB MIDI events */
        midi_usb_process();

        /* Process BT async events (scan results, connection events) */
        at_bt_cmds_process();

        /* Process LE Audio async events (state changes, stream events) */
        at_leaudio_cmds_process();

        /* Process Wi-Fi async events (scan results, etc.) */
        at_wifi_cmds_process();

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

/**
 * @brief ISOC task - handles isochronous audio data path via IPC
 *
 * This task bridges the ISOC handler (BLE isochronous transport) with
 * the CM55 core (LC3 codec) via IPC.
 *
 * TX Path: CM55 encodes PCM -> IPC -> ISOC TX -> BLE
 * RX Path: BLE -> ISOC RX -> IPC -> CM55 decodes to PCM
 */
static void isoc_task(void *pvParameters)
{
    (void)pvParameters;

    printf("ISOC task started (CM33 <-> CM55 IPC bridge)\n");

    /* Wait for IPC to be ready (CM55 must initialize) */
    uint32_t timeout = 0;
    while (!audio_ipc_is_ready() && timeout < 5000) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout += 10;
    }

    if (audio_ipc_is_ready()) {
        printf("  IPC ready - CM55 connected\n");
    } else {
        printf("  WARNING: IPC not ready - CM55 may not be running\n");
    }

    while (g_app_running) {
        /* Process ISOC TX/RX data via IPC
         * This calls the callbacks registered in app_init() */
        isoc_handler_process();

        /* Check for IPC frames and forward to ISOC streams */
        uint32_t frames_available = audio_ipc_encoder_frames_available();
        if (frames_available > 0) {
            /* Frames available from CM55 encoder - process TX */
            isoc_handler_process_tx();
        }

        /* Yield - ISOC timing is driven by BLE controller */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
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
