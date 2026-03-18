/**
 * @file at_system_cmds.h
 * @brief System AT Command Handlers
 *
 * System commands for device information, configuration,
 * and general control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AT_SYSTEM_CMDS_H
#define AT_SYSTEM_CMDS_H

#include "at_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Get system command table
 *
 * @return Pointer to command table
 */
const at_cmd_entry_t *at_system_cmds_get_table(void);

/**
 * @brief Get system command table size
 *
 * @return Number of commands in table
 */
size_t at_system_cmds_get_count(void);

/**
 * @brief Register system commands with AT parser
 *
 * @return 0 on success
 */
int at_system_cmds_register(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_SYSTEM_CMDS_H */
