/**
 * @file main.c
 * @brief CM33 Non-Secure Main - BT Stack, USB, Wi-Fi, MIDI Control
 *
 * This is the main entry point for the CM33 non-secure core which handles:
 * - Bluetooth stack (BTSTACK) and LE Audio control plane
 * - USB device (MIDI over USB)
 * - Wi-Fi bridge (SDIO to CYW55512)
 * - MIDI routing between BLE/USB/UART
 * - Inter-processor communication with CM55 for audio data
 *
 * Architecture:
 *   CM33: BT stack, GATT, ISOC control, USB, Wi-Fi, MIDI routing
 *   CM55: LC3 codec, I2S streaming, audio DSP (via IPC)
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
#include "cy_retarget_io.h"
#include "cy_pdl.h"
#include "cy_time.h"

/* Infineon BT Platform */
#include "cybt_platform_config.h"
#include "cybt_platform_trace.h"
#include "cybsp_bt_config.h"

/* Example's existing modules (proven working code) */
#include "bt.h"
#include "app.h"
#include "app_bt_bonding.h"
#include "button.h"
#include "retarget_io_init.h"

/* LE Audio bridge */
#include "app_le_audio.h"

/* Custom modules - Control plane only (no audio DSP on CM33) */
#include "midi/midi_ble_service.h"
#include "midi/midi_usb.h"
#include "midi/midi_router.h"
#include "wifi/wifi_bridge.h"
#include "le_audio/le_audio_manager.h"
#include "ipc/audio_ipc.h"

/* USB Composite and CDC/AT Command Interface */
#include "usb/usb_composite.h"
#include "cdc/cdc_acm.h"
#include "cdc/at_parser.h"
#include "cdc/at_system_cmds.h"
#include "cdc/at_bt_cmds.h"
#include "cdc/at_leaudio_cmds.h"
#include "cdc/at_wifi_cmds.h"

/*******************************************************************************
 * Macros
 ******************************************************************************/

/* Platform configuration (from Infineon example) */
#define CM55_APP_BOOT_ADDR              (CYMEM_CM33_0_m55_nvm_START + \
                                         CYBSP_MCUBOOT_HEADER_SIZE)
#define CM55_BOOT_WAIT_TIME_USEC        (10U)
#define LPTIMER_0_WAIT_TIME_USEC        (62U)
#define APP_LPTIMER_INTERRUPT_PRIORITY  (1U)

/* Task stack sizes */
#define BLE_TASK_STACK_SIZE     (4096)
#define USB_TASK_STACK_SIZE     (2048)
#define WIFI_TASK_STACK_SIZE    (4096)
#define MIDI_TASK_STACK_SIZE    (1024)

/* Task priorities (per architecture.md) */
#define BLE_TASK_PRIORITY       (5)
#define USB_TASK_PRIORITY       (4)
#define WIFI_TASK_PRIORITY      (3)
#define MIDI_TASK_PRIORITY      (2)

/*******************************************************************************
 * Global Variables
 ******************************************************************************/

/* LPTimer HAL object (for FreeRTOS tickless idle) */
static mtb_hal_lptimer_t lptimer_obj;

/* RTC HAL object (for CLIB time support) */
static mtb_hal_rtc_t rtc_obj;

/* BT ready semaphore - signaled when BTSTACK is initialized */
SemaphoreHandle_t g_bt_ready_sem = NULL;

/* Application state */
static volatile bool g_app_running = false;

/* Task handles */
static TaskHandle_t g_ble_task_handle = NULL;
static TaskHandle_t g_usb_task_handle = NULL;
static TaskHandle_t g_midi_task_handle = NULL;
static TaskHandle_t g_wifi_task_handle = NULL;

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

/* Platform setup (from Infineon example) */
static void lptimer_interrupt_handler(void);
static void setup_clib_support(void);
static void setup_tickless_idle_timer(void);

/* FreeRTOS tasks */
static void ble_task(void *pvParameters);
static void usb_task(void *pvParameters);
static void midi_task(void *pvParameters);
static void wifi_task(void *pvParameters);

/* Module initialization */
static int init_control_modules(void);

/*******************************************************************************
 * Platform Setup Functions (from Infineon example - DO NOT MODIFY)
 ******************************************************************************/

static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}

static void setup_clib_support(void)
{
    Cy_RTC_Init(&CYBSP_RTC_config);
    Cy_RTC_SetDateAndTime(&CYBSP_RTC_config);
    mtb_clib_support_init(&rtc_obj);
}

static void setup_tickless_idle_timer(void)
{
    cy_stc_sysint_t lptimer_intr_cfg = {
        .intrSrc = CYBSP_CM33_LPTIMER_0_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    cy_en_sysint_status_t status = Cy_SysInt_Init(&lptimer_intr_cfg,
                                                   lptimer_interrupt_handler);
    if (CY_SYSINT_SUCCESS != status) {
        handle_app_error();
    }

    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    cy_en_mcwdt_status_t mcwdt_status = Cy_MCWDT_Init(CYBSP_CM33_LPTIMER_0_HW,
                                                      &CYBSP_CM33_LPTIMER_0_config);
    if (CY_MCWDT_SUCCESS != mcwdt_status) {
        handle_app_error();
    }

    Cy_MCWDT_Enable(CYBSP_CM33_LPTIMER_0_HW, CY_MCWDT_CTR_Msk,
                    LPTIMER_0_WAIT_TIME_USEC);

    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj,
                                             &CYBSP_CM33_LPTIMER_0_hal_config);
    if (CY_RSLT_SUCCESS != result) {
        handle_app_error();
    }

    cyabs_rtos_set_lptimer(&lptimer_obj);
}

/*******************************************************************************
 * Module Initialization
 ******************************************************************************/

/**
 * @brief Initialize control-plane modules (runs on CM33)
 *
 * Called from ble_task after BT stack is ready.
 * Audio DSP modules (LC3, I2S) are initialized on CM55.
 */
static int init_control_modules(void)
{
    int result;

    printf("\n[CM33] Initializing control modules...\n");

    /* Check IPC with CM55 */
    printf("  Checking IPC with CM55...\n");
    if (audio_ipc_is_ready()) {
        printf("  IPC: CM33 <-> CM55 connection established!\n");
    } else {
        printf("  IPC: WARNING - CM55 not ready yet (may initialize later)\n");
    }

    /* Initialize LE Audio manager (control plane only) */
    printf("  LE Audio manager...\n");
    le_audio_codec_config_t le_codec_config = LE_AUDIO_CODEC_CONFIG_DEFAULT;
    result = le_audio_init(&le_codec_config);
    if (result != 0) {
        printf("  WARNING: LE Audio init failed: %d\n", result);
    } else {
        printf("  LE Audio manager: OK\n");
    }

    /* Initialize MIDI router */
    printf("  MIDI router...\n");
    result = midi_router_init(NULL);
    if (result != 0) {
        printf("  WARNING: MIDI router init failed: %d\n", result);
    } else {
        printf("  MIDI router: OK\n");
    }

    /* Initialize USB composite device (MIDI + CDC/ACM) */
    printf("  USB composite (MIDI + CDC)...\n");
    result = usb_composite_init(NULL);
    if (result != 0) {
        printf("  WARNING: USB composite init failed: %d\n", result);
    } else {
        printf("  USB composite: OK\n");
    }

    /* Register AT system commands */
    printf("  AT commands...\n");
    result = at_system_cmds_register();
    if (result != 0) {
        printf("  WARNING: AT system commands registration failed: %d\n", result);
    } else {
        printf("  AT system commands: OK\n");
    }

    /* Register AT Bluetooth commands */
    result = at_bt_cmds_register();
    if (result != 0) {
        printf("  WARNING: AT BT commands registration failed: %d\n", result);
    } else {
        printf("  AT BT commands: OK\n");
    }

    /* Register AT LE Audio commands */
    result = at_leaudio_cmds_register();
    if (result != 0) {
        printf("  WARNING: AT LE Audio commands registration failed: %d\n", result);
    } else {
        printf("  AT LE Audio commands: OK\n");
    }

    /* Register AT Wi-Fi commands */
    result = at_wifi_cmds_register();
    if (result != 0) {
        printf("  WARNING: AT Wi-Fi commands registration failed: %d\n", result);
    } else {
        printf("  AT Wi-Fi commands: OK\n");
    }

    /* Start USB device enumeration */
    result = usb_composite_start();
    if (result != 0) {
        printf("  WARNING: USB composite start failed: %d\n", result);
    } else {
        printf("  USB started: OK\n");
    }

    /* Initialize BLE MIDI service */
    printf("  BLE MIDI...\n");
    result = midi_ble_init(NULL);
    if (result != 0) {
        printf("  WARNING: BLE MIDI init failed: %d\n", result);
    } else {
        printf("  BLE MIDI: OK\n");
    }

    /* Initialize Wi-Fi bridge (WHD + packet forwarding)
     * Note: SDIO is initialized internally by WHD via cyhal_sdio_init() */
    printf("  Wi-Fi bridge...\n");
    result = wifi_bridge_init(NULL);
    if (result != 0) {
        printf("  WARNING: Wi-Fi bridge init failed: %d\n", result);
    } else {
        printf("  Wi-Fi bridge: OK\n");
    }

    printf("[CM33] Control module initialization complete\n\n");
    return 0;
}

/*******************************************************************************
 * FreeRTOS Tasks
 ******************************************************************************/

/**
 * @brief BLE task - BTSTACK and LE Audio profile processing
 *
 * Priority 5: Handles BT stack events, LE Audio state machine, ISOC control
 */
static void ble_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[BLE] Task started, waiting for BT stack...\n");

    /* Wait for BT stack to be ready (signaled from app_init via app_le_audio) */
    if (xSemaphoreTake(g_bt_ready_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
        printf("[BLE] ERROR: BT stack init timeout!\n");
        vTaskDelete(NULL);
        return;
    }
    xSemaphoreGive(g_bt_ready_sem);  /* Allow other tasks to proceed */

    printf("[BLE] BT stack ready, initializing control modules\n");
    init_control_modules();

    printf("[BLE] Starting main loop\n");
    g_app_running = true;

    while (g_app_running) {
        /* Process LE Audio state machine */
        le_audio_process();

        /* Process app_le_audio bridge */
        app_le_audio_process();

        /* BTSTACK events are processed via callbacks */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief USB task - USB composite device (MIDI + CDC/ACM AT commands)
 *
 * Priority 4: Handles USB High-Speed device, MIDI over USB, CDC/ACM AT commands
 */
static void usb_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[USB] Task started, waiting for BT ready...\n");

    xSemaphoreTake(g_bt_ready_sem, portMAX_DELAY);
    xSemaphoreGive(g_bt_ready_sem);

    printf("[USB] Starting USB composite processing (MIDI + CDC/AT)\n");

    while (g_app_running) {
        /* Process USB composite device (handles CDC state) */
        usb_composite_process();

        /* Process USB MIDI events */
        midi_usb_process();

        /* Process BT async events (scan results, connection events) */
        at_bt_cmds_process();

        /* Process LE Audio async events (state changes, stream events) */
        at_leaudio_cmds_process();

        /* Process Wi-Fi async events (scan results, etc.) */
        at_wifi_cmds_process();

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief MIDI task - Routes MIDI between BLE, USB, and UART
 *
 * Priority 2: Lowest priority control task
 */
static void midi_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[MIDI] Task started, waiting for BT ready...\n");

    xSemaphoreTake(g_bt_ready_sem, portMAX_DELAY);
    xSemaphoreGive(g_bt_ready_sem);

    printf("[MIDI] Starting MIDI routing\n");

    while (g_app_running) {
        midi_router_process();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/**
 * @brief Wi-Fi task - SDIO communication and WHD packet forwarding
 *
 * Priority 3: Handles USB-to-Wi-Fi bridge
 */
static void wifi_task(void *pvParameters)
{
    (void)pvParameters;
    int result;

    printf("[WiFi] Task started, waiting for BT ready...\n");

    xSemaphoreTake(g_bt_ready_sem, portMAX_DELAY);
    xSemaphoreGive(g_bt_ready_sem);

    printf("[WiFi] Starting Wi-Fi bridge\n");
    result = wifi_bridge_start();
    if (result != 0) {
        printf("[WiFi] WARNING: Bridge start failed: %d\n", result);
    }

    while (g_app_running) {
        wifi_bridge_process();
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    wifi_bridge_stop();
}

/*******************************************************************************
 * Main Entry Point
 ******************************************************************************/

int main(void)
{
    BaseType_t task_result;

    /***************************************************************************
     * Phase 1: Platform Initialization (from Infineon example)
     **************************************************************************/

    /* Initialize BSP */
    if (CY_RSLT_SUCCESS != cybsp_init()) {
        handle_app_error();
    }

    /* Initialize retarget-io for debug UART */
    init_retarget_io();

    /* Setup CLIB support (RTC) */
    setup_clib_support();

    /* Setup LPTimer for tickless idle (deep sleep) */
    setup_tickless_idle_timer();

    /* Initialize KV-store for BT bonding */
    app_kv_store_init();

    /* Initialize button handling */
    button_lib_init();

    /* Initialize IPC for audio frames (must be before CM55 boot) */
    printf("Initializing IPC for CM55 audio...\n");
    if (CY_RSLT_SUCCESS != audio_ipc_init_primary()) {
        printf("ERROR: Failed to initialize audio IPC\n");
        handle_app_error();
    }
    printf("IPC primary initialized, shared memory ready\n");

    /* Boot CM55 core (audio DSP runs there) */
    printf("Booting CM55 core (audio DSP)...\n");
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);
    printf("CM55 boot initiated\n");

    /* Enable global interrupts */
    __enable_irq();

    /***************************************************************************
     * Phase 2: Print Banner
     **************************************************************************/

    printf("\x1b[2J\x1b[;H");  /* Clear terminal */
    printf("==============================================================\n");
    printf("   Infineon LE Audio - PSoC Edge E84 + CYW55512\n");
    printf("==============================================================\n");
    printf("CM33 Core: BT Stack, USB, Wi-Fi, MIDI, CDC/AT Commands\n");
    printf("CM55 Core: LC3 Codec, I2S Audio DSP\n");
    printf("==============================================================\n");
    printf("USB Interfaces: MIDI + CDC/ACM (AT command console)\n");
    printf("==============================================================\n\n");

    /***************************************************************************
     * Phase 3: Create Synchronization Primitives
     **************************************************************************/

    g_bt_ready_sem = xSemaphoreCreateBinary();
    if (g_bt_ready_sem == NULL) {
        printf("ERROR: Failed to create BT ready semaphore\n");
        handle_app_error();
    }

    /***************************************************************************
     * Phase 4: Create FreeRTOS Tasks
     **************************************************************************/

    printf("Creating FreeRTOS tasks (CM33 control plane)...\n");

    task_result = xTaskCreate(ble_task, "BLE", BLE_TASK_STACK_SIZE, NULL,
                              BLE_TASK_PRIORITY, &g_ble_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create BLE task\n");
        handle_app_error();
    }

    task_result = xTaskCreate(usb_task, "USB", USB_TASK_STACK_SIZE, NULL,
                              USB_TASK_PRIORITY, &g_usb_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create USB task\n");
        handle_app_error();
    }

    task_result = xTaskCreate(midi_task, "MIDI", MIDI_TASK_STACK_SIZE, NULL,
                              MIDI_TASK_PRIORITY, &g_midi_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create MIDI task\n");
        handle_app_error();
    }

    task_result = xTaskCreate(wifi_task, "WiFi", WIFI_TASK_STACK_SIZE, NULL,
                              WIFI_TASK_PRIORITY, &g_wifi_task_handle);
    if (task_result != pdPASS) {
        printf("ERROR: Failed to create WiFi task\n");
        handle_app_error();
    }

    printf("All CM33 tasks created!\n\n");

    /***************************************************************************
     * Phase 5: Start Bluetooth Stack (Asynchronous)
     *
     * application_start() calls bt_init() which registers callbacks.
     * When BTM_ENABLED_EVT fires, app_init() is called, which then calls
     * app_le_audio_on_bt_ready() to signal waiting tasks.
     **************************************************************************/

    printf("Starting Bluetooth stack...\n");
    application_start();

    /***************************************************************************
     * Phase 6: Start FreeRTOS Scheduler
     **************************************************************************/

    printf("Starting FreeRTOS scheduler...\n\n");
    vTaskStartScheduler();

    /* Should never reach here */
    printf("FATAL: FreeRTOS scheduler returned!\n");
    handle_app_error();

    return -1;
}

/* end of file */
