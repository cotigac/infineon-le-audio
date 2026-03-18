/**
 * @file at_wifi_cmds.h
 * @brief Wi-Fi AT Command Handlers
 *
 * Implements AT commands for Wi-Fi configuration and control:
 * - Initialization (AT+WIFIINIT, AT+WIFIDEINIT)
 * - Connection (AT+WIFISCAN, AT+WIFIJOIN, AT+WIFILEAVE)
 * - Status queries (AT+WIFISTATE, AT+WIFIRSSI, AT+WIFIIP)
 * - Bridge control (AT+WIFIBRIDGE)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AT_WIFI_CMDS_H
#define AT_WIFI_CMDS_H

#include "at_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Get Wi-Fi command table
 *
 * @return Pointer to command table
 */
const at_cmd_entry_t *at_wifi_cmds_get_table(void);

/**
 * @brief Get Wi-Fi command table size
 *
 * @return Number of commands in table
 */
size_t at_wifi_cmds_get_count(void);

/**
 * @brief Register Wi-Fi commands with AT parser
 *
 * @return 0 on success
 */
int at_wifi_cmds_register(void);

/**
 * @brief Process pending Wi-Fi async events
 *
 * Call periodically to send queued URCs (scan results, etc.)
 */
void at_wifi_cmds_process(void);

/**
 * @brief Set WHD handles for AT commands
 *
 * Call this after WHD initialization to enable Wi-Fi AT commands.
 * Include whd.h before calling this function.
 *
 * @param driver WHD driver handle (whd_driver_t)
 * @param iface  WHD interface handle (whd_interface_t)
 */
void at_wifi_cmds_set_whd_handles(void *driver, void *iface);

#ifdef __cplusplus
}
#endif

#endif /* AT_WIFI_CMDS_H */
