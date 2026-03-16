/*******************************************************************************
 * File Name: app_le_audio.h
 *
 * Description: Bridge between the example ISOC peripheral and the user's
 *              LE Audio manager implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#ifndef APP_LE_AUDIO_H
#define APP_LE_AUDIO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

/**
 * @brief Initialize LE Audio subsystem
 *
 * Called from app_init() after GATT and ISOC are initialized.
 *
 * @return 0 on success, negative error code on failure
 */
int app_le_audio_init(void);

/**
 * @brief Signal that BT stack is ready
 *
 * Called from app_init() after all BT-related initialization is complete.
 * This releases FreeRTOS tasks that are waiting for BT to be ready.
 */
void app_le_audio_on_bt_ready(void);

/**
 * @brief Get LE Audio initialization status
 *
 * @return true if LE Audio is initialized
 */
bool app_le_audio_is_initialized(void);

/**
 * @brief Process LE Audio state machine
 *
 * Should be called periodically from the BLE task.
 */
void app_le_audio_process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LE_AUDIO_H */
