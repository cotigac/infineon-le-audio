/*******************************************************************************
* File Name:   app_bt_bonding.c
*
* Description: This is the source code for bonding implementation using kv-store
*              library.
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

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "app_bt_bonding.h"
#include "wiced_bt_stack.h"
#include "cybsp.h"
#include <FreeRTOS.h>
#include <task.h>
#include "cycfg_bt_settings.h"
#include "mtb_kvstore.h"
#include "app_bt_utils.h"
#include <inttypes.h>
#include "retarget_io_init.h"


/*******************************************************************************
* Macros
*******************************************************************************/
#define NUM_KEYS                (8U)
#define MIN_PROGRAM_SIZE        (0x200U)
#define RESET_VAL               (0U)
#define MEMCPY_SUCCESS          (0U)
#define NVM_REGION_ZERO         (0U)
#define MULTIPLICATION_FACTOR   (2U)
#define INDEX_OFFSET            (1U)
#define RRAM_NVM_DATA_NS_OFFSET (CYMEM_CM33_0_user_nvm_OFFSET)

/*******************************************************************************
* Variable Definitions
*******************************************************************************/
mtb_kvstore_t kvstore_obj;
bond_info_t bond_info;
wiced_bt_local_identity_keys_t identity_keys;
uint16_t peer_cccd_data[BOND_INDEX_MAX];
static mtb_block_storage_t* bsd;
static mtb_block_storage_t nvm_bsd;
static mtb_hal_nvm_t nvm_obj;
static mtb_hal_nvm_region_info_t region_info;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static void app_kvstore_bd_init(void);
static void get_kvstore_init_params(uint32_t *length, uint32_t *start_addr);
static wiced_result_t app_bt_delete_device_info(uint8_t index);
static cy_rslt_t app_bt_update_bond_data(void);

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: get_kvstore_init_params
********************************************************************************
* Summary:
*  This function is used to intialize the kv-store related parameters such as
*  program size, area size, start address and length.
*
* Parameters:
*  uint32_t *start_addr: Start Address of the data region in NVM.
*  uint32_t *length    : Length of the data to be stored in the NVM.
*
* Return:
*  None
*******************************************************************************/
static void get_kvstore_init_params(uint32_t *length, uint32_t *start_addr)
{
    uint32_t program_size = bsd->get_program_size(bsd->context,
            region_info.start_address);

    program_size = (program_size > MIN_PROGRAM_SIZE) ? program_size :
            MIN_PROGRAM_SIZE;

    uint32_t area_size = (NUM_KEYS * program_size * MULTIPLICATION_FACTOR);

    *start_addr = region_info.start_address + RRAM_NVM_DATA_NS_OFFSET;
    *length = area_size;
}

/*******************************************************************************
* Function Name: app_kv_store_init
********************************************************************************
* Summary:
*  This function initializes the block device and kv-store library.
*******************************************************************************/
void app_kv_store_init(void)
{
   cy_rslt_t rslt;
   uint32_t  start_addr, length;

   app_kvstore_bd_init();
   get_kvstore_init_params(&length, &start_addr);

   /*Initialize kv-store library*/
   rslt = mtb_kvstore_init(&kvstore_obj, start_addr, length, bsd, NULL, NULL);

   /*Check if the kv-store initialization was successful*/
   if (CY_RSLT_SUCCESS != rslt)
   {
       printf("Failed to initialize KV-Store \n");
       handle_app_error();
   }
}

/*******************************************************************************
* Function Name: app_bt_restore_bond_data
********************************************************************************
* Summary:
*  This function restores the bond information from the NVM.
*
* Parameters:
*  None
*
* Return:  
*  cy_rslt_t: CY_RSLT_SUCCESS if the restoration was successful,
*             an error code otherwise.
*
*******************************************************************************/
cy_rslt_t app_bt_restore_bond_data(void)
{
    /* Read and restore contents of NVM */
    uint32_t data_size = sizeof(bond_info);
    cy_rslt_t rslt = mtb_kvstore_read(&kvstore_obj, "bond_data",
                                     (uint8_t *)&bond_info, &data_size);
    if (CY_RSLT_SUCCESS != rslt)
    {
        printf("Bond data not present in the NVM!\n");
    }

    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_update_bond_data
********************************************************************************
* Summary:
*  This function updates the bond information in the NVM.
*
* Parameters:
*  None
*
* Return:
* cy_rslt_t: CY_RSLT_SUCCESS if the update was successful,
*            an error code otherwise.
*
*******************************************************************************/
static cy_rslt_t app_bt_update_bond_data(void)
{
    cy_rslt_t rslt = CY_RSLT_TYPE_ERROR;
    rslt = mtb_kvstore_write(&kvstore_obj, "bond_data",(uint8_t *)&bond_info,
            sizeof(bond_info));
    if (CY_RSLT_SUCCESS != rslt)
    {
        printf("NVM Write Error,Error code: %" PRIu32 "\n", rslt);
    }

    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_delete_bond_info
********************************************************************************
* Summary:
*  This deletes the bond information from the NVM.
*
* Parameters:
*  None
*
* Return:
* cy_rslt_t: CY_RSLT_SUCCESS if the deletion was successful,
*            an error code otherwise.
*
*******************************************************************************/
cy_rslt_t app_bt_delete_bond_info(void)
{
    cy_rslt_t rslt = CY_RSLT_SUCCESS;
    wiced_result_t result;
    for (uint8_t i = RESET_VAL; i < bond_info.slot_data[NUM_BONDED]; i++)
    {
        result = app_bt_delete_device_info(i);
        if (WICED_BT_SUCCESS == result)
        {
            /*Update the slot data*/
            bond_info.slot_data[NUM_BONDED] = RESET_VAL;
            bond_info.slot_data[NEXT_FREE_INDEX] = RESET_VAL;

            /*Update bond information*/
            rslt = app_bt_update_bond_data();
        }
        else
        {
            rslt = CY_RSLT_TYPE_ERROR;
        }
    }

    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_delete_device_info
********************************************************************************
* Summary:
*  This function deletes the bond information of the device from the NVM
*  and address resolution database.
*
* Parameters: link keys index
*
* Return:
*  wiced_result_t: WICED_BT_SUCCESS if the deletion was successful,
*                  an error code otherwise.
*
*******************************************************************************/
static wiced_result_t app_bt_delete_device_info(uint8_t index)
{
    wiced_result_t result = WICED_BT_SUCCESS;

    /* Remove from the bonded device list */
    result = wiced_bt_dev_delete_bonded_device(bond_info.link_keys[index]
                                              .bd_addr);
    if(WICED_BT_SUCCESS == result)
    {
        /* Remove device from address resolution database */
        result = wiced_bt_dev_remove_device_from_address_resolution_db
                    (&(bond_info.link_keys[index]));
        if (WICED_BT_SUCCESS == result)
        {
            /* Remove bonding information in NVM */
            peer_cccd_data[index]=RESET_VAL;
            bond_info.privacy_mode[index]=RESET_VAL;
            memset(&bond_info.link_keys[index], RESET_VAL,
                    sizeof(wiced_bt_device_link_keys_t));
        }
        else
        {
            printf("Unable to remove device from address resolution db\n");
        }
    }
    else
    {
        printf("Unable to delete bond information from the NVM\n");
    }

    return result;
}

/*******************************************************************************
* Function Name: app_bt_update_slot_data
********************************************************************************
* Summary:
* This function updates the slot data in the NVM.
*
* Parameters:
*  None
*
* Return:
* cy_rslt_t: CY_RSLT_SUCCESS if the update was successful,
*            an error code otherwise.
*******************************************************************************/
cy_rslt_t app_bt_update_slot_data(void)
{
    cy_rslt_t rslt = CY_RSLT_TYPE_ERROR;

    /* Increment number of bonded devices and next free slot and save them in
     * the NVM */
    if (BOND_INDEX_MAX > bond_info.slot_data[NUM_BONDED])
    {
        /* Increment only if the bonded devices are less than BOND_INDEX_MAX */
        bond_info.slot_data[NUM_BONDED]++;
    }

    /* Update Next Slot to be used for next incoming Device */
    bond_info.slot_data[NEXT_FREE_INDEX] =
            (bond_info.slot_data[NEXT_FREE_INDEX]+INDEX_OFFSET) % BOND_INDEX_MAX;
    rslt = app_bt_update_bond_data();
    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_save_device_link_keys
********************************************************************************
* Summary:
*  This function saves peer device link keys to the NVM
*
* Parameters:
*  link_key: Link keys of the peer device.
*
* Return:
*  cy_rslt_t: CY_RSLT_SUCCESS if the save was successful,
*             an error code otherwise.
*
*******************************************************************************/
cy_rslt_t app_bt_save_device_link_keys(wiced_bt_device_link_keys_t *link_key)
{
    cy_rslt_t rslt = CY_RSLT_TYPE_ERROR;
    uint8_t index;

    /* Check if there is an entry of keys for the peer BDA in NVM */
    index = app_bt_find_device_in_nvm(link_key->bd_addr);
    if(BOND_INDEX_MAX != index)
    {
        memcpy(&bond_info.link_keys[index],
               (uint8_t *)(link_key), sizeof(wiced_bt_device_link_keys_t));

        rslt = mtb_kvstore_write(&kvstore_obj, "bond_data",
                (uint8_t *)&bond_info, sizeof(bond_info));
        if (CY_RSLT_SUCCESS != rslt)
        {
            printf("NVM Write Error,Error code: %" PRIu32 "\n", rslt );
        }
    }

    /* If there is no entry of keys in NVM, create a fresh entry in next
     * free slot */
    else
    {
        memcpy(&bond_info.link_keys[bond_info.slot_data[NEXT_FREE_INDEX]],
               (uint8_t *)(link_key), sizeof(wiced_bt_device_link_keys_t));
        rslt = mtb_kvstore_write(&kvstore_obj, "bond_data",
                (uint8_t *)&bond_info, sizeof(bond_info));
        if (CY_RSLT_SUCCESS != rslt)
        {
            printf("NVM Write Error,Error code: %" PRIu32 "\n", rslt );
        }
    }

    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_save_local_identity_key
********************************************************************************
* Summary:
*  This function saves local device identity keys to the NVM
*
* Parameters:
*  id_key: Local identity keys to store in the NVM.
*
* Return:
*  cy_rslt_t: CY_RSLT_SUCCESS if the save was successful,
*             an error code otherwise.
*
*******************************************************************************/
cy_rslt_t app_bt_save_local_identity_key(wiced_bt_local_identity_keys_t id_key)
{
    cy_rslt_t rslt;
    memcpy(&identity_keys, (uint8_t *)&(id_key),
            sizeof(wiced_bt_local_identity_keys_t));
    rslt = mtb_kvstore_write(&kvstore_obj, "local_irk",
            (uint8_t *)&identity_keys, sizeof(wiced_bt_local_identity_keys_t));
    if (CY_RSLT_SUCCESS == rslt)
    {
        printf("Local identity Keys saved to NVM \n");
    }
    else
    {
        printf("NVM Write Error,Error code: %" PRIu32 "\n", rslt );
    }

    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_read_local_identity_keys
********************************************************************************
* Summary:
*  This function reads local device identity keys from the NVM
*
* Parameters:
*  None
*
* Return:
*  cy_rslt_t: CY_RSLT_SUCCESS if the read was successful,
*              an error code otherwise.
*
*******************************************************************************/
cy_rslt_t app_bt_read_local_identity_keys(void)
{
    uint32_t data_size = sizeof(identity_keys);
    cy_rslt_t rslt = mtb_kvstore_read(&kvstore_obj, "local_irk", NULL,
            &data_size);
    if (CY_RSLT_SUCCESS != rslt)
    {
        printf("New Keys need to be generated! \n");
    }
    else
    {
        printf("Identity keys are available in the database.\n");
        rslt = mtb_kvstore_read(&kvstore_obj, "local_irk",
                (uint8_t *)&identity_keys, &data_size);
        printf("Local identity keys read from NVM: \n");
    }

    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_update_cccd
********************************************************************************
* Summary:
*  This function updates the CCCD data in the NVM.
*
* Parameters:
*  cccd : cccd value to be updated in NVM.
*  index: Index of the device in the NVM.
*
* Return:
*  cy_rslt_t: CY_RSLT_SUCCESS if the update was successful,
*             an error code otherwise.
*******************************************************************************/
cy_rslt_t app_bt_update_cccd(uint16_t cccd, uint8_t index)
{
    cy_rslt_t rslt = CY_RSLT_TYPE_ERROR;

    peer_cccd_data[index]= cccd;
    printf("Updating CCCD Value to: %d \n",cccd);
    rslt = mtb_kvstore_write(&kvstore_obj, "cccd_data",
          (uint8_t *)&peer_cccd_data, sizeof(peer_cccd_data));
    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_restore_cccd
********************************************************************************
* Summary:
*  This function restores the cccd from the NVM.
*
* Parameters:
*  None
*
* Return:
*  cy_rslt_t: CY_RSLT_SUCCESS if the update was successful,
*              an error code otherwise.
*
*******************************************************************************/
cy_rslt_t app_bt_restore_cccd(void)
{
    cy_rslt_t rslt = CY_RSLT_TYPE_ERROR;
    uint32_t data_size = sizeof(peer_cccd_data);

    rslt = mtb_kvstore_read(&kvstore_obj, "cccd_data",(uint8_t *)peer_cccd_data,
            &data_size);
    return rslt;
}

/*******************************************************************************
* Function Name: app_bt_find_device_in_nvm
********************************************************************************
* Summary:
*  This function searches provided bd_addr in bonded devices list.
*
* Parameters:
*  *bd_addr: pointer to the address of the device to be searched.
*
* Return:
*  uint8_t: Index of the device in the bond data stored in the NVM if found,
*           else returns  BOND_INDEX_MAX to indicate the device was not found.
*
*******************************************************************************/
uint8_t app_bt_find_device_in_nvm(uint8_t *bd_addr)
{
    /*Return out of range value if device is not found*/
    uint8_t index =  BOND_INDEX_MAX;

    for (uint8_t count = RESET_VAL; count < bond_info.slot_data[NUM_BONDED];
            count++)
    {
        if (MEMCPY_SUCCESS == memcmp(&(bond_info.link_keys[count].bd_addr),
                bd_addr,sizeof(wiced_bt_device_address_t)))
        {
            printf("Found device in the NVM!\n");
            index = count;
            break; /* Exit the loop since we found what we want */
        }
    }

    return index;
}

/*******************************************************************************
* Function Name: app_bt_add_devices_to_address_resolution_db
********************************************************************************
* Summary:
*  This function adds the bonded devices to address resolution database.
*******************************************************************************/
void app_bt_add_devices_to_address_resolution_db(void)
{
    wiced_result_t result;

    /* Copy in the keys and add them to the address resolution database */
    for (uint8_t i = RESET_VAL; (i < bond_info.slot_data[NUM_BONDED]) &&
    (i < BOND_INDEX_MAX); i++)
    {
        /* Add device to address resolution database */
        result = wiced_bt_dev_add_device_to_address_resolution_db
                (&bond_info.link_keys[i]);
                
        if (WICED_BT_SUCCESS == result)
        {
            printf("Device added to address resolution database: ");
            print_bd_address((uint8_t *)&bond_info.link_keys[i].bd_addr);
        }
    }
}

/*******************************************************************************
* Function Name: print_bond_data
********************************************************************************
* Summary:
*  This function prints the bond data stored in the NVM.
*******************************************************************************/
void print_bond_data(void)
{
    for (uint8_t i = RESET_VAL; i < bond_info.slot_data[NUM_BONDED]; i++)
    {
        printf("Slot: %d",i+1);
        printf("Device Bluetooth Address: ");
        print_bd_address(bond_info.link_keys[i].bd_addr);
        printf("Device Keys: \n");
        print_array(&(bond_info.link_keys[i].key_data),
                sizeof(wiced_bt_device_sec_keys_t));
        printf("\n");
    }
}

/*******************************************************************************
* Function Name: app_kvstore_bd_init
********************************************************************************
* Summary:
*  This function initializes the underlying block device.
*******************************************************************************/
static void app_kvstore_bd_init(void)
{
    mtb_hal_nvm_info_t nvm_info;
    mtb_hal_nvm_setup(&nvm_obj,NULL);
    mtb_hal_nvm_get_info(&nvm_obj, &nvm_info);
    mtb_block_storage_create_hal_nvm(&nvm_bsd, &nvm_obj);
    bsd = &nvm_bsd;
    region_info = nvm_info.regions[NVM_REGION_ZERO];
}

/* END OF FILE [] */
