/**
 * @file at_bt_cmds.h
 * @brief Bluetooth AT Command Handlers
 *
 * Implements AT commands for Bluetooth configuration and control:
 * - Stack initialization (AT+BTINIT, AT+BTDEINIT)
 * - Device configuration (AT+BTADDR, AT+BTNAME, AT+BTPWR)
 * - GAP advertising (AT+GAPADVSTART, AT+GAPADVSTOP)
 * - GAP scanning (AT+GAPSCAN)
 * - Connection management (AT+GAPCONN, AT+GAPDISCONN)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AT_BT_CMDS_H
#define AT_BT_CMDS_H

#include "at_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Get Bluetooth command table
 *
 * @return Pointer to command table
 */
const at_cmd_entry_t *at_bt_cmds_get_table(void);

/**
 * @brief Get Bluetooth command table size
 *
 * @return Number of commands in table
 */
size_t at_bt_cmds_get_count(void);

/**
 * @brief Register Bluetooth commands with AT parser
 *
 * @return 0 on success
 */
int at_bt_cmds_register(void);

/**
 * @brief Process pending BT async events
 *
 * Call periodically to send queued URCs (scan results, etc.)
 */
void at_bt_cmds_process(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_BT_CMDS_H */
