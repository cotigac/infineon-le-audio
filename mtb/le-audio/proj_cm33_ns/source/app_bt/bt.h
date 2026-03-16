/*******************************************************************************
* File Name: bt.h
*
* Description: This is the public interface of bt.c
*
* Related Document: See README.md
*
********************************************************************************
* (c) 2024-2025, Infineon Technologies AG, or an affiliate of Infineon Technologies AG. All rights reserved.
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
#ifndef APP_BT_H_
#define APP_BT_H_

/*******************************************************************************
* Includes
*******************************************************************************/
#include "wiced_bt_dev.h"
#include "wiced_bt_gatt.h"
#include <stdio.h>
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_cfg.h"

/*******************************************************************************
* Defines
*******************************************************************************/
#define bt_stop_advertisement() \
        bt_start_advertisements(BTM_BLE_ADVERT_OFF, 0, NULL)
#define bt_disconnect() wiced_bt_gatt_disconnect( link_conn_id() )
#define bt_get_advertising_mode() wiced_bt_ble_get_current_advert_mode()
#define bt_is_advertising() bt_get_advertising_mode()

/*******************************************************************************
* Function Declarations
*******************************************************************************/
wiced_bt_dev_local_addr_ext_t * dev_info(void);
wiced_result_t bt_init(void);
void bt_enter_pairing(void);
wiced_result_t bt_start_advertisements(wiced_bt_ble_advert_mode_t advert_mode,
               wiced_bt_ble_address_type_t directed_advertisement_bdaddr_type,
               wiced_bt_device_address_ptr_t directed_advertisement_bdaddr_ptr);

#endif // APP_BT_H_

/* end of file */
