/*******************************************************************************
* File Name:   util_functions.h
*
* Description: This file consists of the utility functions that will help
*              debugging and developing the applications easier with much
*              more meaningful information.
*
* Related Document: See Readme.md
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
#ifndef APP_BT_UTILS_H__
#define APP_BT_UTILS_H__

/*******************************************************************************
* Includes
*******************************************************************************/
#include "wiced_bt_dev.h"
#include "wiced_bt_gatt.h"
#include <stdio.h>

/*******************************************************************************
* Constants
*******************************************************************************/
#define CASE_RETURN_STR(const)          case const: return #const;

/*******************************************************************************
* Function Declarations
*******************************************************************************/
void print_bd_address(wiced_bt_device_address_t bdadr);
void print_array(void * to_print, uint16_t len);
const char *get_btm_event_name(wiced_bt_management_evt_t event);
const char *get_bt_advert_mode_name(wiced_bt_ble_advert_mode_t mode);
const char *get_bt_gatt_disconn_reason_name(wiced_bt_gatt_disconn_reason_t reason);
const char *get_bt_gatt_status_name(wiced_bt_gatt_status_t gatt_status);
const char *get_bt_smp_status_name(wiced_bt_smp_status_t status);

#endif      /*APP_BT_UTILS_H__ */
