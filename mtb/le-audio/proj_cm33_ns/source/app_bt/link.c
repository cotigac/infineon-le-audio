/*******************************************************************************
* File Name: link.c
*
* Description: This file contains the functions for link management
*
* Related Document: See README.md
*
********************************************************************************
* (c) 2023-2025, Infineon Technologies AG, or an affiliate of Infineon Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is owned by
* Infineon Technologies AG or one of its affiliates ("Infineon") and is protected
* by and subject to worldwide patent protection, worldwide copyright laws, and
* international treaty provisions. Therefore, you may use this Software only as
* provided in the license agreement accompanying the software package from which
* you obtained this Software. If no license agreement applies, then any use,
* reproduction, modification, translation, or compilation of this Software is
* prohibited without the express written permission of Infineon.
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING,
* BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF THIRD-PARTY RIGHTS AND
* IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A SPECIFIC USE/PURPOSE OR
* MERCHANTABILITY. Infineon reserves the right to make changes to the Software
* without notice. You are responsible for properly designing, programming, and
* testing the functionality and safety of your intended application of the
* Software, as well as complying with any legal requirements related to its
* use. Infineon does not guarantee that the Software will be free from intrusion,
* data theft or loss, or other breaches ("Security Breaches"), and Infineon
* shall have no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any application
* where a failure of the Product or any consequences of the use thereof can
* reasonably be expected to result in personal injury.
*******************************************************************************/
#include "app.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_l2c.h"

/*******************************************************************************
*  Defines
*******************************************************************************/
#define MAX_CONN                (1U)
#define INVALID_CONN_ID         (0U)
#define INVALID_CONN_HANDLE     (0U)
#define INVALID_ACL_CONN_HANDLE (0U)
#define LATENCY                 (0U)
#define ISOC_ACL_LINK_INT       (10U)
#define MEMSET_VAL              (0U)
#define CONN_INDEX              (0U)
#define CONN_INDEX_INITIAL      (0U)
#define RESET_VAL               (0U)
#define MAX_INTERVAL            (10U)

/*******************************************************************************
*  typedef
*******************************************************************************/
typedef struct
{
    wiced_bt_gatt_connection_status_t   connection_status;
    uint16_t                            acl_conn_handle;
    /* Remote device address (from connection_status) */
    wiced_bt_device_address_t           bd_addr;
    /* 0:no link (BT_TRANSPORT_NONE), 1:Classic (BT_TRANSPORT_BR_EDR) */
    /*2:LE (BT_TRANSPORT_LE) */
    uint8_t                             transport:2;
    /* encrypted:1, not encrypted:0 */
    uint8_t                             encrypted:1;
    /* 1:connection parameter update, 0: no connection parameter update
     * since connection */
    uint8_t                             link_parameter_updated:1;
    /* 1:waiting for indication confirm, 0:no indication pending */
    uint8_t                             indicate_pending:1;
    /* 1:bonded, 0:not bonded */
    uint8_t                             bonded:1;
} link_state_t;

/*******************************************************************************
*  local variables
*******************************************************************************/
static struct
{
    link_state_t   conn[MAX_CONN];
    link_state_t * active;

} link;

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: link_get_state
********************************************************************************
* Summary:
*  Returns link state pointer for the given connection id.
*
* Parameters:
*  uint16_t conn_id : Connection ID
*
* Return:
*  link_state_t : Link state
*******************************************************************************/
static link_state_t * link_get_state(uint16_t conn_id)
{
    /* find the connection id */
    for (uint8_t idx = CONN_INDEX; idx < MAX_CONN; idx++)
    {
        if (link.conn[idx].connection_status.conn_id == conn_id)
        {
            link.active = &link.conn[idx];
            return link.active;
        }
    }

    return NULL;
}

/******************************************************************************
* Function Name: link_transport
******************************************************************************
* Summary:
*  Returns the link transport.(BT_TRANSPORT_NONE, BT_TRANSPORT_BR_EDR or
*  BT_TRANSPORT_LE)
*
* Parameters:
*  None
*
* Return:
*  uint8_t : Link transport
******************************************************************************/
uint8_t link_transport(void)
{
    return link.active ? link.active->transport : BT_TRANSPORT_NONE;
}

/*******************************************************************************
* Function Name: link_conn_id
********************************************************************************
* Summary:
*  Return last active link conn_id. If no link, it returns 0.
*
* Parameters:
*  None
*
* Return:
*  uint16_t :Last active connection id
******************************************************************************/
uint16_t link_conn_id(void)
{
    return link.active ? link.active->connection_status.conn_id : INVALID_CONN_ID;
}

/*******************************************************************************
* Function Name: link_acl_conn_handle
********************************************************************************
* Summary:
*    Return last active ACL connection handle. If no link, it returns 0.
* Parameters:
*  None
*
* Return:
*  uint16_t :last active ACL connection handle
*******************************************************************************/
uint16_t link_acl_conn_handle(void)
{
    return link.active ? link.active->acl_conn_handle : INVALID_CONN_HANDLE;
}

/*******************************************************************************
* Function Name: link_first_acl_conn_handle
********************************************************************************
* Summary:
*  Return first active ACL connection handle. If no link, it returns 0.
*
* Parameters:
*  None
*
* Return:
*  uint16_t :first active ACL connection handle
*******************************************************************************/
uint16_t link_first_acl_conn_handle(void)
{
    return ((link.conn[CONN_INDEX].connection_status.conn_id) ?
            wiced_bt_dev_get_acl_conn_handle(link.conn[CONN_INDEX].bd_addr,
            BT_TRANSPORT_LE) : INVALID_ACL_CONN_HANDLE);
}


/*******************************************************************************
* Function Name: link_set_encrypted
********************************************************************************
* Summary:
*  Set the connection link encrypted state
*
* Parameters:
*  wiced_bool_t set : encrypted /not encrypted
*
* Return:
*  None
*******************************************************************************/
void link_set_encrypted(wiced_bool_t set)
{
    if (link.active)
    {
        link.active->encrypted = set;
    }
}

/*******************************************************************************
* Function Name: link_is_encrypted
********************************************************************************
* Summary:
*    Return TRUE if link is encrypted
*
* Parameters:
*  None
*
* Return:
*  wiced_bool_t : TRUE/FALSE
*******************************************************************************/
wiced_bool_t link_is_encrypted(void)
{
    if (link.active)
    {
        return link.active->encrypted;
    }

    return FALSE;
}

/*******************************************************************************
* Function Name: link_set_parameter_updated
********************************************************************************
* Summary:
*  Set the connection link parameter updated state
*
* Parameters:
*  wiced_bool_t set : is parameter updated ? (TRUE/FALSE)
*
* Return:
*  None
*******************************************************************************/
void link_set_parameter_updated(wiced_bool_t set)
{
    if (link.active)
    {
        link.active->link_parameter_updated = set;
    }
}

/*******************************************************************************
* Function Name: link_is_parameter_updated
********************************************************************************
* Summary:
*  Return TUE if the link parameter is updated
*
* Parameters:
*  None
*
* Return:
*  wiced_bool_t : TRUE/FALSE
*******************************************************************************/
wiced_bool_t link_is_parameter_updated(void)
{
    if (link.active)
    {
        return link.active->link_parameter_updated;
    }

    return FALSE;
}

/*******************************************************************************
* Function Name: link_set_indication_pending
********************************************************************************
* Summary:
*  Set the indication flag is pending for this link
*
* Parameters:
*  wiced_bool_t set : is indication pending ? (TRUE/FALSE)
*
* Return:
*  None
*******************************************************************************/
void link_set_indication_pending(wiced_bool_t set)
{
    if (link.active)
    {
        link.active->indicate_pending = set;
    }
}

/*******************************************************************************
* Function Name: link_is_indication_pending
********************************************************************************
* Summary:
*  Return TRUE if device is waiting for host indication confirmation
*
* Parameters:
*  None
*
* Return:
*  wiced_bool_t : TRUE/FALSE
*******************************************************************************/
wiced_bool_t link_is_indication_pending(void)
{
    if (link.active)
    {
        return link.active->indicate_pending;
    }

    return FALSE;
}

/*******************************************************************************
* Function Name: link_set_bonded
********************************************************************************
* Summary:
*  Set the connection bonded state
*
* Parameters:
*  wiced_bool_t set: Bonded state TRUE/FALSE
*
* Return:
*  None
*******************************************************************************/
void link_set_bonded(wiced_bool_t set)
{
    if (link.active)
    {
        link.active->bonded = set;
    }
}

/*******************************************************************************
* Function Name: link_is_bonded
********************************************************************************
* Summary:
*  Return TRUE if the link is bonded
*
* Parameters:
*  none
*
* Return:
*  bonded state
*
*******************************************************************************/
wiced_bool_t link_is_bonded(void)
{
    if (link.active)
    {
        return link.active->bonded;
    }

    return FALSE;
}

/*******************************************************************************
* Function Name: link_up
********************************************************************************
* Summary:
*  This function should be called when link is established
*
* Parameters:
*  wiced_bt_gatt_connection_status_t *p_status : Pointer to GATT connection status EVT
*
* Return:
*  wiced_bt_gatt_status : GATT status
*******************************************************************************/
wiced_bt_gatt_status_t link_up( wiced_bt_gatt_connection_status_t * p_status )
{
    link_state_t * new_conn = NULL;

    for (uint8_t idx=CONN_INDEX_INITIAL; idx < MAX_CONN; idx++)
    {
        if ((link.conn[idx].connection_status.conn_id == p_status->conn_id)
            || !link.conn[idx].connection_status.conn_id)
        {
            if (link.conn[idx].connection_status.conn_id)
            {
                printf("** Warning: connection id %04x is already up",
                p_status->conn_id);
            }
            new_conn = &link.conn[idx];
            break;
        }
    }
    /* If we cannot find an empty slot, we have reached the max connection we
       can support */
    if (NULL == new_conn)
    {
        printf("Max link %d reached",  MAX_CONN);
        return WICED_BT_GATT_NO_RESOURCES;
    }

    link.active = new_conn;

    /* Copy the connection info */
    memcpy(&new_conn->connection_status, p_status,
           sizeof(wiced_bt_gatt_connection_status_t));
    memcpy(new_conn->bd_addr, new_conn->connection_status.bd_addr, BD_ADDR_LEN);
    new_conn->transport = BT_TRANSPORT_LE;
    new_conn->acl_conn_handle = wiced_bt_dev_get_acl_conn_handle(
    new_conn->bd_addr, BT_TRANSPORT_LE);

    wiced_bt_l2cap_enable_update_ble_conn_params (new_conn->bd_addr, true);
    /* notify application the link is up */
    app_link_up(p_status);
    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* Function Name: link_down
********************************************************************************
* Summary:
*  This function should be called when link is down
*
* Parameters:
*  wiced_bt_gatt_connection_status_t *p_status : Pointer to GATT connection
*  status EVT
*
* Return:
*  wiced_bt_gatt_status : GATT status
*******************************************************************************/
wiced_bt_gatt_status_t link_down(const wiced_bt_gatt_connection_status_t *p_status )
{
    link_state_t * conn = link_get_state(p_status->conn_id);

    if (NULL == conn)
    {
        /* link down with invalid conn_id should not happen */
        printf("Invalid conn_id for link down event %d",p_status->conn_id);
        return WICED_BT_GATT_DATABASE_OUT_OF_SYNC;
    }

    memset(conn, MEMSET_VAL, sizeof(link_state_t));
    link.active = NULL;

    /* if we still have link is connected, we change active link to the 
       connected link */
    for (int idx=CONN_INDEX_INITIAL; idx < MAX_CONN; idx++)
    {
        if (link.conn[idx].connection_status.conn_id)
        {
            link.active = &link.conn[idx];
            break;
        }
    }

    /* notify application the link is down */
    app_link_down(p_status);
    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* Function Name: link_is_connected
********************************************************************************
* Summary:
*  Return TRUE if the link is connected
*
* Parameters:
*  None
*
* Return:
*  wiced_bool_t : TRUE/FALSE
*******************************************************************************/
wiced_bool_t link_is_connected(void)
{
    return (wiced_bool_t) link.active;
}

/*******************************************************************************
* Function Name: link_connection_status
********************************************************************************
* Summary:
*  Returns current connection status data
*
* Parameters:
*  None
*
* Return:
*  wiced_bt_gatt_connection_status_t : current connection status data
*******************************************************************************/
wiced_bt_gatt_connection_status_t * link_connection_status(void)
{
    return link.active ? &link.active->connection_status : NULL;
}

/*******************************************************************************
* Function Name: bt_set_acl_conn_interval
********************************************************************************
* Summary:
*  Change Set connection interval
*
* Parameters:
*  uint16_t interval : connection interval
*
* Return:
*  wiced_bool_t : TRUE/FALSE
*******************************************************************************/
wiced_bool_t link_set_acl_conn_interval(uint16_t interval)
{
    wiced_bt_ble_conn_params_t conn_parameters;

    if (link.active)
    {
        wiced_bt_ble_get_connection_parameters(link.active
                ->bd_addr,&conn_parameters);

        if (conn_parameters.conn_interval != interval)
        {
            uint16_t sup_timeout = interval < MAX_INTERVAL ?
                    NON_ISOC_ACL_LINK_SUPERVISION_TIMEOUT :
                    ISOC_ACL_LINK_SUPERVISION_TIMEOUT;
            wiced_bt_ble_pref_conn_params_t conn_params = {
                .conn_interval_min = interval,
                .conn_interval_max = interval,
                .conn_supervision_timeout = sup_timeout
            };

            printf("Set connection interval from %d to %d",
            conn_parameters.conn_interval, interval);
            wiced_bt_l2cap_update_ble_conn_params(link.active->bd_addr,
                    &conn_params);
            return TRUE;
        }
    }
    
    return FALSE;
}

/* end of file */
