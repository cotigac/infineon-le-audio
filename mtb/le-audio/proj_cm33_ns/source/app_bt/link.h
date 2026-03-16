/*******************************************************************************
* File Name: link.h
*
* Description: This file is the public interface of link.c
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
#ifndef LINK_H__
#define LINK_H__

/*******************************************************************************
* INCLUDES
******************************************************************************/
#include "wiced_bt_gatt.h"

/*******************************************************************************
 * DEFINES
 ******************************************************************************/
#define BT_TRANSPORT_NONE                      (0U)
#define NON_ISOC_ACL_CONN_INTERVAL             (6U)
#define NON_ISOC_ACL_LINK_SUPERVISION_TIMEOUT  (200U)     /* 2 sec timeout */
#define ISOC_ACL_LINK_SUPERVISION_TIMEOUT      (1500U)     /* 15 sec timeout */

/*******************************************************************************
* FUNCTION DECLARATIONS
*******************************************************************************/
uint8_t link_transport(void);
uint16_t link_conn_id(void);
uint16_t link_acl_conn_handle(void);
uint16_t link_first_acl_conn_handle(void);
void link_set_encrypted(wiced_bool_t set);
wiced_bool_t link_is_encrypted(void);
void link_set_parameter_updated(wiced_bool_t set);
wiced_bool_t link_is_parameter_updated(void);
void link_set_indication_pending(wiced_bool_t set);
wiced_bool_t link_is_indication_pending(void);
void link_set_bonded(wiced_bool_t set);
wiced_bool_t link_is_bonded(void);
wiced_bt_gatt_status_t link_up(
        wiced_bt_gatt_connection_status_t * p_status );
wiced_bt_gatt_status_t link_down(const
wiced_bt_gatt_connection_status_t * p_status );
wiced_bool_t link_is_connected(void);
wiced_bt_gatt_connection_status_t * link_connection_status(void);
wiced_bool_t link_set_acl_conn_interval(uint16_t interval);

#endif // LINK_H__

/* end of file */
