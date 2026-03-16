/**
 * @file link.c
 * @brief Link/Connection Management Implementation
 *
 * Implements connection state tracking for BLE links.
 * Adapted from Infineon MTB LE Audio example patterns.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "link.h"
#include "wiced_bt_l2c.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Constants
 ******************************************************************************/

/** Connection interval threshold for ISOC link supervision timeout */
#define ISOC_INTERVAL_THRESHOLD         (10U)

/** Supervision timeout for non-ISOC ACL links (in 10ms units) */
#define NON_ISOC_SUPERVISION_TIMEOUT    (100U)

/** Supervision timeout for ISOC ACL links (in 10ms units) */
#define ISOC_SUPERVISION_TIMEOUT        (200U)

/*******************************************************************************
 * Module State
 ******************************************************************************/

static struct {
    /** Connection state for each possible link */
    link_state_t conn[LINK_MAX_CONNECTIONS];

    /** Pointer to the currently active link (most recent activity) */
    link_state_t *active;

} link_ctx;

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief Find link state by connection ID
 */
static link_state_t *find_by_conn_id(uint16_t conn_id)
{
    for (uint8_t i = 0; i < LINK_MAX_CONNECTIONS; i++) {
        if (link_ctx.conn[i].connection_status.conn_id == conn_id) {
            link_ctx.active = &link_ctx.conn[i];
            return link_ctx.active;
        }
    }
    return NULL;
}

/**
 * @brief Find an empty slot for a new connection
 */
static link_state_t *find_empty_slot(void)
{
    for (uint8_t i = 0; i < LINK_MAX_CONNECTIONS; i++) {
        if (link_ctx.conn[i].connection_status.conn_id == LINK_INVALID_CONN_ID) {
            return &link_ctx.conn[i];
        }
    }
    return NULL;
}

/**
 * @brief Find the first connected link (for fallback after disconnect)
 */
static link_state_t *find_first_connected(void)
{
    for (uint8_t i = 0; i < LINK_MAX_CONNECTIONS; i++) {
        if (link_ctx.conn[i].connection_status.conn_id != LINK_INVALID_CONN_ID) {
            return &link_ctx.conn[i];
        }
    }
    return NULL;
}

/*******************************************************************************
 * Public Functions - Connection Events
 ******************************************************************************/

wiced_bt_gatt_status_t link_up(wiced_bt_gatt_connection_status_t *p_status)
{
    link_state_t *conn;

    if (p_status == NULL) {
        return WICED_BT_GATT_ERROR;
    }

    printf("Link: UP conn_id=0x%04x addr=%02X:%02X:%02X:%02X:%02X:%02X\n",
           p_status->conn_id,
           p_status->bd_addr[0], p_status->bd_addr[1],
           p_status->bd_addr[2], p_status->bd_addr[3],
           p_status->bd_addr[4], p_status->bd_addr[5]);

    /* Check if this connection ID already exists (shouldn't happen) */
    conn = find_by_conn_id(p_status->conn_id);
    if (conn != NULL) {
        printf("Link: Warning - conn_id 0x%04x already exists\n", p_status->conn_id);
        /* Update existing entry */
    } else {
        /* Find empty slot */
        conn = find_empty_slot();
        if (conn == NULL) {
            printf("Link: Error - max connections (%d) reached\n", LINK_MAX_CONNECTIONS);
            return WICED_BT_GATT_NO_RESOURCES;
        }
    }

    /* Initialize the link state */
    memset(conn, 0, sizeof(link_state_t));
    memcpy(&conn->connection_status, p_status, sizeof(wiced_bt_gatt_connection_status_t));
    memcpy(conn->bd_addr, p_status->bd_addr, BD_ADDR_LEN);
    conn->transport = BT_TRANSPORT_LE;

    /* Get the ACL connection handle */
    conn->acl_conn_handle = wiced_bt_dev_get_acl_conn_handle(conn->bd_addr, BT_TRANSPORT_LE);

    /* Enable connection parameter updates */
    wiced_bt_l2cap_enable_update_ble_conn_params(conn->bd_addr, true);

    /* Set as active link */
    link_ctx.active = conn;

    printf("Link: Connection established, acl_handle=0x%04x\n", conn->acl_conn_handle);

    return WICED_BT_GATT_SUCCESS;
}

wiced_bt_gatt_status_t link_down(const wiced_bt_gatt_connection_status_t *p_status)
{
    link_state_t *conn;

    if (p_status == NULL) {
        return WICED_BT_GATT_ERROR;
    }

    printf("Link: DOWN conn_id=0x%04x reason=0x%02x\n",
           p_status->conn_id, p_status->reason);

    /* Find the connection */
    conn = find_by_conn_id(p_status->conn_id);
    if (conn == NULL) {
        printf("Link: Warning - conn_id 0x%04x not found\n", p_status->conn_id);
        return WICED_BT_GATT_DATABASE_OUT_OF_SYNC;
    }

    /* Clear the link state */
    memset(conn, 0, sizeof(link_state_t));

    /* Clear active pointer if this was the active link */
    if (link_ctx.active == conn) {
        link_ctx.active = NULL;
    }

    /* Find another connected link to make active */
    link_ctx.active = find_first_connected();

    if (link_ctx.active != NULL) {
        printf("Link: Switched active to conn_id=0x%04x\n",
               link_ctx.active->connection_status.conn_id);
    } else {
        printf("Link: No active connections remaining\n");
    }

    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
 * Public Functions - Connection Status
 ******************************************************************************/

bool link_is_connected(void)
{
    return (link_ctx.active != NULL);
}

uint16_t link_conn_id(void)
{
    if (link_ctx.active != NULL) {
        return link_ctx.active->connection_status.conn_id;
    }
    return LINK_INVALID_CONN_ID;
}

uint16_t link_acl_conn_handle(void)
{
    if (link_ctx.active != NULL) {
        return link_ctx.active->acl_conn_handle;
    }
    return LINK_INVALID_ACL_HANDLE;
}

uint16_t link_first_acl_conn_handle(void)
{
    if (link_ctx.conn[0].connection_status.conn_id != LINK_INVALID_CONN_ID) {
        return wiced_bt_dev_get_acl_conn_handle(
            link_ctx.conn[0].bd_addr, BT_TRANSPORT_LE);
    }
    return LINK_INVALID_ACL_HANDLE;
}

uint8_t link_transport(void)
{
    if (link_ctx.active != NULL) {
        return link_ctx.active->transport;
    }
    return BT_TRANSPORT_NONE;
}

wiced_bt_gatt_connection_status_t *link_connection_status(void)
{
    if (link_ctx.active != NULL) {
        return &link_ctx.active->connection_status;
    }
    return NULL;
}

link_state_t *link_get_by_conn_id(uint16_t conn_id)
{
    return find_by_conn_id(conn_id);
}

uint8_t link_get_connection_count(void)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < LINK_MAX_CONNECTIONS; i++) {
        if (link_ctx.conn[i].connection_status.conn_id != LINK_INVALID_CONN_ID) {
            count++;
        }
    }
    return count;
}

link_state_t *link_get_by_index(uint8_t index)
{
    if (index >= LINK_MAX_CONNECTIONS) {
        return NULL;
    }
    if (link_ctx.conn[index].connection_status.conn_id == LINK_INVALID_CONN_ID) {
        return NULL;
    }
    return &link_ctx.conn[index];
}

/*******************************************************************************
 * Public Functions - Link State Flags
 ******************************************************************************/

void link_set_encrypted(bool encrypted)
{
    if (link_ctx.active != NULL) {
        link_ctx.active->encrypted = encrypted ? 1 : 0;
        printf("Link: Encryption %s\n", encrypted ? "enabled" : "disabled");
    }
}

bool link_is_encrypted(void)
{
    if (link_ctx.active != NULL) {
        return link_ctx.active->encrypted != 0;
    }
    return false;
}

void link_set_parameter_updated(bool updated)
{
    if (link_ctx.active != NULL) {
        link_ctx.active->parameter_updated = updated ? 1 : 0;
    }
}

bool link_is_parameter_updated(void)
{
    if (link_ctx.active != NULL) {
        return link_ctx.active->parameter_updated != 0;
    }
    return false;
}

void link_set_indication_pending(bool pending)
{
    if (link_ctx.active != NULL) {
        link_ctx.active->indication_pending = pending ? 1 : 0;
    }
}

bool link_is_indication_pending(void)
{
    if (link_ctx.active != NULL) {
        return link_ctx.active->indication_pending != 0;
    }
    return false;
}

void link_set_bonded(bool bonded)
{
    if (link_ctx.active != NULL) {
        link_ctx.active->bonded = bonded ? 1 : 0;
        printf("Link: Bonding %s\n", bonded ? "established" : "cleared");
    }
}

bool link_is_bonded(void)
{
    if (link_ctx.active != NULL) {
        return link_ctx.active->bonded != 0;
    }
    return false;
}

/*******************************************************************************
 * Public Functions - Connection Parameters
 ******************************************************************************/

bool link_set_acl_conn_interval(uint16_t interval)
{
    wiced_bt_ble_conn_params_t current_params;

    if (link_ctx.active == NULL) {
        return false;
    }

    /* Get current connection parameters */
    wiced_bt_ble_get_connection_parameters(link_ctx.active->bd_addr, &current_params);

    /* Check if update is needed */
    if (current_params.conn_interval == interval) {
        printf("Link: Connection interval already at %d\n", interval);
        return false;
    }

    /* Calculate supervision timeout based on interval */
    uint16_t supervision_timeout = (interval < ISOC_INTERVAL_THRESHOLD) ?
        NON_ISOC_SUPERVISION_TIMEOUT : ISOC_SUPERVISION_TIMEOUT;

    /* Prepare connection parameter update request */
    wiced_bt_ble_pref_conn_params_t new_params = {
        .conn_interval_min = interval,
        .conn_interval_max = interval,
        .conn_latency = 0,
        .conn_supervision_timeout = supervision_timeout
    };

    printf("Link: Requesting interval change from %d to %d\n",
           current_params.conn_interval, interval);

    /* Request connection parameter update */
    wiced_bt_l2cap_update_ble_conn_params(link_ctx.active->bd_addr, &new_params);

    return true;
}

uint16_t link_get_conn_handle_by_bdaddr(const wiced_bt_device_address_t bd_addr)
{
    for (uint8_t i = 0; i < LINK_MAX_CONNECTIONS; i++) {
        link_state_t *link = &link_ctx.conn[i];
        if (link->connection_status.conn_id != LINK_INVALID_CONN_ID &&
            memcmp(link->bd_addr, bd_addr, sizeof(wiced_bt_device_address_t)) == 0) {
            return link->connection_status.conn_id;
        }
    }
    return LINK_INVALID_CONN_ID;
}
