/**
 * @file link.h
 * @brief Link/Connection Management Module
 *
 * Provides connection state tracking for BLE links, including
 * encryption status, bonding state, and connection parameters.
 * Adapted from Infineon MTB LE Audio example patterns.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LINK_H
#define LINK_H

#include <stdint.h>
#include <stdbool.h>
#include "wiced_bt_gatt.h"
#include "wiced_bt_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Constants
 ******************************************************************************/

/** Maximum concurrent connections supported */
#define LINK_MAX_CONNECTIONS    (2U)

/** Invalid connection ID */
#define LINK_INVALID_CONN_ID    (0U)

/** Invalid ACL connection handle */
#define LINK_INVALID_ACL_HANDLE (0U)

/*******************************************************************************
 * Types
 ******************************************************************************/

/**
 * @brief Link state information for a single connection
 */
typedef struct {
    /** GATT connection status from stack */
    wiced_bt_gatt_connection_status_t connection_status;

    /** ACL connection handle */
    uint16_t acl_conn_handle;

    /** Remote device address */
    wiced_bt_device_address_t bd_addr;

    /** Transport type (BT_TRANSPORT_NONE, BT_TRANSPORT_BR_EDR, BT_TRANSPORT_LE) */
    uint8_t transport : 2;

    /** Link encrypted flag */
    uint8_t encrypted : 1;

    /** Connection parameters updated since connection */
    uint8_t parameter_updated : 1;

    /** Indication pending (waiting for confirmation) */
    uint8_t indication_pending : 1;

    /** Link is bonded */
    uint8_t bonded : 1;

    /** Reserved bits */
    uint8_t reserved : 2;

} link_state_t;

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

/**
 * @brief Handle link up event (connection established)
 *
 * Should be called from GATT connection status callback when connected=true.
 *
 * @param p_status Pointer to GATT connection status from stack
 * @return WICED_BT_GATT_SUCCESS on success, error code otherwise
 */
wiced_bt_gatt_status_t link_up(wiced_bt_gatt_connection_status_t *p_status);

/**
 * @brief Handle link down event (disconnection)
 *
 * Should be called from GATT connection status callback when connected=false.
 *
 * @param p_status Pointer to GATT connection status from stack
 * @return WICED_BT_GATT_SUCCESS on success, error code otherwise
 */
wiced_bt_gatt_status_t link_down(const wiced_bt_gatt_connection_status_t *p_status);

/**
 * @brief Check if any link is currently connected
 *
 * @return true if at least one link is connected
 */
bool link_is_connected(void);

/**
 * @brief Get the active link's connection ID
 *
 * @return Connection ID of the active link, or LINK_INVALID_CONN_ID if none
 */
uint16_t link_conn_id(void);

/**
 * @brief Get the active link's ACL connection handle
 *
 * @return ACL connection handle, or LINK_INVALID_ACL_HANDLE if none
 */
uint16_t link_acl_conn_handle(void);

/**
 * @brief Get the first active ACL connection handle
 *
 * @return First ACL connection handle, or LINK_INVALID_ACL_HANDLE if none
 */
uint16_t link_first_acl_conn_handle(void);

/**
 * @brief Get the transport type of the active link
 *
 * @return BT_TRANSPORT_NONE, BT_TRANSPORT_BR_EDR, or BT_TRANSPORT_LE
 */
uint8_t link_transport(void);

/**
 * @brief Set the encrypted state of the active link
 *
 * @param encrypted true if link is encrypted
 */
void link_set_encrypted(bool encrypted);

/**
 * @brief Check if the active link is encrypted
 *
 * @return true if link is encrypted
 */
bool link_is_encrypted(void);

/**
 * @brief Set the parameter updated state
 *
 * @param updated true if connection parameters have been updated
 */
void link_set_parameter_updated(bool updated);

/**
 * @brief Check if connection parameters have been updated
 *
 * @return true if parameters have been updated since connection
 */
bool link_is_parameter_updated(void);

/**
 * @brief Set indication pending state
 *
 * @param pending true if waiting for indication confirmation
 */
void link_set_indication_pending(bool pending);

/**
 * @brief Check if an indication is pending
 *
 * @return true if waiting for indication confirmation
 */
bool link_is_indication_pending(void);

/**
 * @brief Set the bonded state of the active link
 *
 * @param bonded true if link is bonded
 */
void link_set_bonded(bool bonded);

/**
 * @brief Check if the active link is bonded
 *
 * @return true if link is bonded
 */
bool link_is_bonded(void);

/**
 * @brief Get the connection status structure of the active link
 *
 * @return Pointer to connection status, or NULL if no active link
 */
wiced_bt_gatt_connection_status_t *link_connection_status(void);

/**
 * @brief Get the link state for a specific connection ID
 *
 * @param conn_id Connection ID to look up
 * @return Pointer to link state, or NULL if not found
 */
link_state_t *link_get_by_conn_id(uint16_t conn_id);

/**
 * @brief Set ACL connection interval for the active link
 *
 * @param interval Connection interval in 1.25ms units
 * @return true if request was sent, false otherwise
 */
bool link_set_acl_conn_interval(uint16_t interval);

/**
 * @brief Get the number of active connections
 *
 * @return Number of active connections (0 to LINK_MAX_CONNECTIONS)
 */
uint8_t link_get_connection_count(void);

/**
 * @brief Get link state by index
 *
 * @param index Connection index (0 to LINK_MAX_CONNECTIONS-1)
 * @return Pointer to link state, or NULL if index invalid or not connected
 */
link_state_t *link_get_by_index(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* LINK_H */
