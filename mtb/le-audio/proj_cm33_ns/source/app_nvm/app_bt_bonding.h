/*******************************************************************************
* File Name:   app_bt_bonding.h
*
* Description: This is the header file for the bonding related defines for
*              Hello Sensor Example for ModusToolbox.
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
#ifndef __APP_BT_BONDING_H_
#define __APP_BT_BONDING_H_

/*******************************************************************************
*Header Files
*******************************************************************************/
#include <FreeRTOS.h>
#include <task.h>
#include "cycfg_bt_settings.h"
#include "mtb_kvstore.h"


/*******************************************************************************
* Macro Definitions
*******************************************************************************/
/* Max number of bonded devices */
#define  BOND_INDEX_MAX                      (4U)

/*******************************************************************************
*Structures and Enumerations
*******************************************************************************/
/* enum for bond_info_t structure */
enum bond_info_e
{
    NUM_BONDED ,
    NEXT_FREE_INDEX
};

/* Structure to store info that goes into nvm (non-volatile memory) - it holds
 * the number of bonded devices, remote keys and local keys */
typedef struct
{
    uint8_t slot_data[2];   /* Refer  bond_info_e */
    wiced_bt_device_link_keys_t link_keys[BOND_INDEX_MAX];
    wiced_bt_ble_privacy_mode_t privacy_mode[BOND_INDEX_MAX];
}bond_info_t;

/*******************************************************************************
* Variable Definitions
*******************************************************************************/
/* Object for kvstore library */
extern mtb_kvstore_t  kvstore_obj;

/* Variable to store pairing key information */
extern bond_info_t   bond_info;

/* Variable to store CCCD values of peer device */
extern uint16_t   peer_cccd_data[BOND_INDEX_MAX];

/* Variable to store identity keys of our device */
extern wiced_bt_local_identity_keys_t identity_keys;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
void app_kv_store_init(void);
cy_rslt_t app_bt_restore_bond_data(void);
cy_rslt_t app_bt_delete_bond_info(void);
cy_rslt_t app_bt_update_slot_data(void);
cy_rslt_t app_bt_save_device_link_keys(wiced_bt_device_link_keys_t *link_key);
cy_rslt_t app_bt_save_local_identity_key(wiced_bt_local_identity_keys_t id_key);
cy_rslt_t app_bt_read_local_identity_keys(void);
cy_rslt_t app_bt_update_cccd(uint16_t cccd, uint8_t index);
cy_rslt_t app_bt_restore_cccd(void);
uint8_t app_bt_find_device_in_nvm(uint8_t *bd_addr);
void app_bt_add_devices_to_address_resolution_db(void);
void print_bond_data(void);

#endif // __APP_BT_BONDING_H_

/* [] END OF FILE */
