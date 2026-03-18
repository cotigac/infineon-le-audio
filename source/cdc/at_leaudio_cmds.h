/**
 * @file at_leaudio_cmds.h
 * @brief LE Audio AT Command Handlers
 *
 * Implements AT commands for LE Audio configuration and control:
 * - Initialization (AT+LEAINIT, AT+LEADEINIT)
 * - State/mode queries (AT+LEASTATE, AT+LEAMODE)
 * - Broadcast control (AT+LEABROADCAST)
 * - Unicast control (AT+LEAUNICAST)
 * - Codec configuration (AT+LEACODEC)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AT_LEAUDIO_CMDS_H
#define AT_LEAUDIO_CMDS_H

#include "at_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Get LE Audio command table
 *
 * @return Pointer to command table
 */
const at_cmd_entry_t *at_leaudio_cmds_get_table(void);

/**
 * @brief Get LE Audio command table size
 *
 * @return Number of commands in table
 */
size_t at_leaudio_cmds_get_count(void);

/**
 * @brief Register LE Audio commands with AT parser
 *
 * @return 0 on success
 */
int at_leaudio_cmds_register(void);

/**
 * @brief Process pending LE Audio async events
 *
 * Call periodically to send queued URCs (stream events, etc.)
 */
void at_leaudio_cmds_process(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_LEAUDIO_CMDS_H */
