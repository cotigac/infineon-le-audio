/*******************************************************************************
* File Name:   bt.c
*
* Description: This file consists of the BT initialization function and bluetooth
*              management callback function that processes the bluetooth events
*              from the stack.
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

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "wiced_bt_stack.h"
#include "wiced_memory.h"
#include "cy_retarget_io.h"
#include "cycfg_bt_settings.h"
#include "cycfg_gap.h"
#include "source/app.h"
#include "app_bt_utils.h"
#include "mtb_kvstore.h"
#include "app_bt_bonding.h"
#include "retarget_io_init.h"

/*******************************************************************************
* Defines
*******************************************************************************/
#define APP_STACK_HEAP_SIZE  (0x512U)
#define ASSERT_VALUE         (0U)
#define BOND_INDEX_RESET     (0U)
#define DEV_ADDR_0           (0x12U)
#define DEV_ADDR_1           (0x34U)
#define DEV_ADDR_2           (0x56U)
#define DEV_ADDR_3           (0x78U)
#define DEV_ADDR_4           (0x90U)
#define DEV_ADDR_5           (0x00U)
#define RESET_VAL            (0U)
#define MAX_KEY_SIZE         (16U)
#define VS_ID_LOCAL_IDENTITY "local_identity"

/*******************************************************************************
* Structures
*******************************************************************************/
static struct {
    wiced_bt_dev_local_addr_ext_t   dev;
    wiced_bt_ble_advert_mode_t      adv_mode, intended_adv_mode;
    uint8_t                         adv_bdAddr[BD_ADDR_LEN];
} bt = {{RESET_VAL}};

/*******************************************************************************
* Variables
*******************************************************************************/
wiced_bt_device_address_t dev_addr = {DEV_ADDR_0, DEV_ADDR_1, DEV_ADDR_2,
                                      DEV_ADDR_3, DEV_ADDR_4, DEV_ADDR_5};
cy_rslt_t rslt;
uint8_t  bondindex = RESET_VAL;

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: app_bt_management_callback
********************************************************************************
* Summary:
* This is a Bluetooth stack event handler function to receive
*  management events from the LE stack and process as per the application.
*
* Parameters:
* wiced_bt_management_evt_t event             : LE event code of
*                                               one byte length
* wiced_bt_management_evt_data_t *p_event_data: Pointer to LE
*                                               management event structures
*
* Return:
* wiced_result_t                          : Error code from
*                                           WICED_RESULT_LIST or BT_RESULT_LIST
*
*******************************************************************************/
static wiced_result_t app_bt_management(wiced_bt_management_evt_t event,
                          wiced_bt_management_evt_data_t *p_event_data)
{
    wiced_result_t result = WICED_BT_SUCCESS;
    wiced_bt_dev_encryption_status_t  *p_encryption_status;

    printf( "BT event: %d, %s\n", event, get_btm_event_name(event) );

    switch( event )
    {
        /* Bluetooth  stack enabled */
        case BTM_ENABLED_EVT:

            if ( p_event_data->enabled.status == WICED_BT_SUCCESS )
            {
                printf("BTM initialized\n");
                wiced_bt_set_local_bdaddr (dev_addr, BLE_ADDR_PUBLIC);

                /* read extended device info */
                wiced_bt_dev_read_local_addr_ext(&bt.dev);
                printf("Local Bluetooth Address: ");
                print_bd_address(dev_info()->local_addr);

                /* This function initializes the GATT DB, ISOC and
                 * initializes the TCPWM for USER LEDs.
                 */
                app_init();
            }
            else
            {
                printf("** BT Enable failed, status:%d\n",
                p_event_data->enabled.status);
                handle_app_error();
            }

            break;

        case BTM_USER_CONFIRMATION_REQUEST_EVT:
            printf("BTM_USER_CONFIRMATION_REQUEST_EVT: Numeric_value:"
            "%d \n", (int)p_event_data->user_confirmation_request.numeric_value);
            wiced_bt_dev_confirm_req_reply( WICED_BT_SUCCESS,
            p_event_data->user_confirmation_request.bd_addr);
            break;

        case BTM_PASSKEY_NOTIFICATION_EVT:
            wiced_bt_dev_confirm_req_reply(WICED_BT_SUCCESS,
            p_event_data->user_passkey_notification.bd_addr);
            break;

        case BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT:
            printf("BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT\n");
            rslt = app_bt_save_local_identity_key(p_event_data
                    ->local_identity_keys_update);
            if (CY_RSLT_SUCCESS != rslt)
            {
                result = WICED_BT_ERROR;
            }

            break;

        case  BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT:
            printf("BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT\n");
            /* Read Local Identity Resolution Keys if present in NVM*/
            rslt = app_bt_read_local_identity_keys();
            if(CY_RSLT_SUCCESS == rslt)
            {
                memcpy(&(p_event_data->local_identity_keys_request),
                        &(identity_keys),sizeof(wiced_bt_local_identity_keys_t));
                print_array(&identity_keys,
                        sizeof(wiced_bt_local_identity_keys_t));
                result = WICED_BT_SUCCESS;
            }
            else
            {
               result = WICED_BT_ERROR;
            }

            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
            app_bt_save_device_link_keys(&(p_event_data
                    ->paired_device_link_keys_update));
            break;

        case  BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
            printf("BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT\n");
            print_bd_address((uint8_t *)(p_event_data
                    ->paired_device_link_keys_request.bd_addr));

            result = WICED_BT_ERROR;
            bondindex = app_bt_find_device_in_nvm(p_event_data
                    ->paired_device_link_keys_request.bd_addr);

            if(BOND_INDEX_MAX > bondindex)
            {
                memcpy(&(p_event_data->paired_device_link_keys_request),
                       &bond_info.link_keys[bondindex],
                       sizeof(wiced_bt_device_link_keys_t));
                result = WICED_BT_SUCCESS;
            }
            else
            {
                printf("Device Link Keys not found in the database! \n");
                bondindex = BOND_INDEX_RESET;
            }

            break;

        case BTM_ENCRYPTION_STATUS_EVT:
            p_encryption_status = &p_event_data->encryption_status;
            (void)p_encryption_status;

            link_set_encrypted(p_event_data->encryption_status.result
                    == WICED_SUCCESS);
            break;

        case BTM_PAIRING_COMPLETE_EVT:
            app_bt_update_slot_data();
            break;

        case BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
            printf("BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT\n");
            p_event_data->pairing_io_capabilities_ble_request.local_io_cap = 
            BTM_IO_CAPABILITIES_NONE;
            p_event_data->pairing_io_capabilities_ble_request.oob_data = 
            BTM_OOB_NONE;
            p_event_data->pairing_io_capabilities_ble_request.auth_req =
            BTM_LE_AUTH_REQ_SC | BTM_LE_AUTH_REQ_BOND;  /* LE sec bonding */
            p_event_data->pairing_io_capabilities_ble_request.max_key_size
            = MAX_KEY_SIZE;
            p_event_data->pairing_io_capabilities_ble_request.init_keys =
            BTM_LE_KEY_PENC|BTM_LE_KEY_PID|BTM_LE_KEY_PCSRK|BTM_LE_KEY_PLK;
            p_event_data->pairing_io_capabilities_ble_request.resp_keys =
            BTM_LE_KEY_PENC|BTM_LE_KEY_PID|BTM_LE_KEY_PCSRK|BTM_LE_KEY_PLK;
            break;

        case BTM_SECURITY_REQUEST_EVT:
            printf("BTM_SECURITY_REQUEST_EVT\n");
             /* Use the default security */
            wiced_bt_ble_security_grant(p_event_data->security_request.bd_addr,
                                        WICED_BT_SUCCESS);
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            {
                wiced_bt_ble_advert_mode_t new_adv_mode = p_event_data
                        ->ble_advert_state_changed;

                if (!link_is_connected() && BTM_BLE_ADVERT_OFF== new_adv_mode)
                {
                    /* If the adv is off and previous state was     *
                     * BTM_BLE_ADVERT_UNDIRECTED_HIGH, we switch to *
                     * BTM_BLE_ADVERT_UNDIRECTED_LOW                */
                     if (BTM_BLE_ADVERT_UNDIRECTED_HIGH == bt.intended_adv_mode)
                    {
                        /* start high duty cycle directed advertising. */
                        if (bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_LOW,
                                BLE_ADDR_PUBLIC, NULL))
                        {
                            printf("Failed to start low duty cycle "
                                    "undirected advertising!!!\n");
                        }

                        break;
                    }
                }
                app_adv_state_changed(bt.adv_mode, new_adv_mode);
                bt.adv_mode = new_adv_mode;
            }

            break;

        case BTM_BLE_SCAN_STATE_CHANGED_EVT:
            printf("Scan State Change: %d\n",p_event_data
                    ->ble_scan_state_changed );
            break;

        case BTM_BLE_CONNECTION_PARAM_UPDATE:
            printf("BTM_BLE_CONNECTION_PARAM_UPDATE status:%d interval:"
            "%d latency:%d timeout:%d\n",
            p_event_data->ble_connection_param_update.status,
            p_event_data->ble_connection_param_update.conn_interval,
            p_event_data->ble_connection_param_update.conn_latency,
            p_event_data->ble_connection_param_update.supervision_timeout);

            if (!p_event_data->ble_connection_param_update.status)
            {
                link_set_parameter_updated(TRUE);
            }

            break;

        case BTM_BLE_PHY_UPDATE_EVT:
            printf("PHY update: status:%d Tx:%d Rx:%d BDA: ",
            p_event_data->ble_phy_update_event.status,
            p_event_data->ble_phy_update_event.tx_phy,
            p_event_data->ble_phy_update_event.rx_phy);
            print_bd_address(p_event_data->ble_phy_update_event.bd_address);
            break;

        default:
            break;
    }

    return result;
}

/*******************************************************************************
* Function Name: dev_info
********************************************************************************
* Summary:
*  This function returns the device information data structure.
*
* Parameters:
*  None
*
* Return:
*  wiced_bt_dev_local_addr_ext_t
*******************************************************************************/
wiced_bt_dev_local_addr_ext_t * dev_info(void)
{
    return &bt.dev;
}

/*******************************************************************************
* Function Name: bt_enter_pairing
********************************************************************************
* Summary:
*  Starts undirected advertisements
*
* Parameters:
*  None
*
* Return:
*  None
*******************************************************************************/
void bt_enter_pairing(void)
{
    bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, BLE_ADDR_PUBLIC,
            NULL);
}

/*******************************************************************************
* Function Name: bt_init
********************************************************************************
* Summary:
*  This is one of the first function related to BLE to be called upon device
*  reset. It initialize BT Stack. When BT stack is up, it will call application
*  to continue system initialization.
*
* Parameters:
*  None
*
* Return:
*  wiced_result_t
*******************************************************************************/
wiced_result_t bt_init(void)
{
    wiced_result_t result = wiced_bt_stack_init(app_bt_management,
            &cy_bt_cfg_settings);
    if (WICED_BT_SUCCESS == result)
    {
        /* Create default heap */
        if (NULL == wiced_bt_create_heap("app", NULL, APP_STACK_HEAP_SIZE, NULL,
                WICED_TRUE))
        {
            printf("create default heap error: size %d\n",
            APP_STACK_HEAP_SIZE);
            result = WICED_BT_NO_RESOURCES;
        }
    }
    
    return result;
}

/*******************************************************************************
* Function Name: bt_start_advertisements
********************************************************************************
* Summary:
*  Saves the current advertisement mode and calls wiced_bt_start_advertisements
*
* Parameters:
*  advert_mode                         : Advertisement mode
*  directed_advertisement_bdaddr_type  : If using directed advertisements
*  directed_advertisement_bdaddr_ptr   : Directed advertisement address
*                                     (NULL if not using directed advertisement)
*
* Return:
*  wiced_result_t : Error code from WICED_RESULT_LIST or BT_RESULT_LIST
*
*******************************************************************************/
wiced_result_t bt_start_advertisements(wiced_bt_ble_advert_mode_t advert_mode,
               wiced_bt_ble_address_type_t directed_advertisement_bdaddr_type,
               wiced_bt_device_address_ptr_t directed_advertisement_bdaddr_ptr)
{
    bt.intended_adv_mode = advert_mode;
    return wiced_bt_start_advertisements(advert_mode,
           directed_advertisement_bdaddr_type,
           directed_advertisement_bdaddr_ptr);
}

/* end of file */
