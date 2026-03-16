/*******************************************************************************
 * File Name: app_le_audio.c
 *
 * Description: Bridge between the example ISOC peripheral and the user's
 *              LE Audio manager implementation.
 *
 * This module:
 * - Initializes the LE Audio manager when BT stack is ready
 * - Signals FreeRTOS tasks that they can proceed
 * - Forwards ISOC events to le_audio_manager
 *
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#include "app_le_audio.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* LE Audio Manager from user source */
#include "le_audio/le_audio_manager.h"

/* ISOC peripheral from example */
#include "app_bt/isoc_peripheral.h"

/* Infineon headers */
#include "wiced_bt_trace.h"
#include "cybt_platform_trace.h"

/*******************************************************************************
 * External Variables
 ******************************************************************************/

/* BT ready semaphore - declared in main_merged.c */
extern SemaphoreHandle_t g_bt_ready_sem;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

static bool g_le_audio_initialized = false;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static void le_audio_event_handler(const le_audio_event_t *event, void *user_data);

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

/**
 * @brief Initialize LE Audio bridge
 *
 * Called from app_init() after GATT and ISOC are initialized.
 * Note: Full LE Audio module initialization is done by init_custom_modules()
 * in main_merged.c after BT is ready and tasks are running.
 *
 * @return 0 on success
 */
int app_le_audio_init(void)
{
    printf("LE Audio: Bridge ready\r\n");
    g_le_audio_initialized = true;
    return 0;
}

/**
 * @brief Signal that BT stack is ready
 *
 * Called from app_init() after all BT-related initialization is complete.
 * This releases FreeRTOS tasks that are waiting for BT to be ready.
 */
void app_le_audio_on_bt_ready(void)
{
    printf("LE Audio: BT stack ready, signaling tasks\r\n");

    /* Signal waiting tasks */
    if (g_bt_ready_sem != NULL) {
        xSemaphoreGive(g_bt_ready_sem);
    }
}

/**
 * @brief Get LE Audio initialization status
 *
 * @return true if LE Audio is initialized
 */
bool app_le_audio_is_initialized(void)
{
    return g_le_audio_initialized;
}

/**
 * @brief Process LE Audio state machine
 *
 * Should be called periodically from the BLE task.
 */
void app_le_audio_process(void)
{
    if (!g_le_audio_initialized) {
        return;
    }

    le_audio_process();
}

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief LE Audio event handler callback
 */
static void le_audio_event_handler(const le_audio_event_t *event, void *user_data)
{
    (void)user_data;

    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case LE_AUDIO_EVENT_STATE_CHANGED:
            printf("LE Audio: State changed to %d\r\n", event->data.new_state);
            break;

        case LE_AUDIO_EVENT_STREAM_STARTED:
            printf("LE Audio: Stream started\r\n");
            break;

        case LE_AUDIO_EVENT_STREAM_STOPPED:
            printf("LE Audio: Stream stopped\r\n");
            break;

        case LE_AUDIO_EVENT_DEVICE_CONNECTED:
            printf("LE Audio: Device connected\r\n");
            break;

        case LE_AUDIO_EVENT_DEVICE_DISCONNECTED:
            printf("LE Audio: Device disconnected\r\n");
            break;

        case LE_AUDIO_EVENT_ERROR:
            printf("LE Audio: Error %d\r\n", event->data.error_code);
            break;

        default:
            break;
    }
}

/* end of file */
