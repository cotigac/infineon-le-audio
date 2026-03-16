/******************************************************************************
* File Name:   app.c
*
* Description: This function contains the basic application functions.
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
#include "app.h"
#include "cybt_platform_trace.h"
#include "wiced_bt_trace.h"
#include "wiced_bt_types.h"
#include "cybsp.h"
#include "retarget_io_init.h"

/*******************************************************************************
* Defines
*******************************************************************************/
#define DUTY_CYCLE_100 (1000U)
#define DUTY_CYCLE_50  (500U)
#define DUTY_CYCLE_0   (0U)

/*******************************************************************************
* Variables
*******************************************************************************/
#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

/* PWM_3 Context for SysPm Callback */
mtb_syspm_tcpwm_deepsleep_context_t PWM3dscontext =
{
    .channelNum = CYBSP_PWM_LED_CTRL_3_NUM,
};

/* PWM_2 Context for SysPm Callback */
mtb_syspm_tcpwm_deepsleep_context_t PWM2dscontext =
{
    .channelNum = CYBSP_PWM_LED_CTRL_2_NUM,
};

/* SysPm callback parameter structure for PWM_3 */
static cy_stc_syspm_callback_params_t PWM3DSParams =
{
        .context   = &PWM3dscontext,
        .base      = CYBSP_PWM_LED_CTRL_3_HW
};

/* SysPm callback parameter structure for PWM_2 */
static cy_stc_syspm_callback_params_t PWM2DSParams =
{
        .context   = &PWM2dscontext,
        .base      = CYBSP_PWM_LED_CTRL_2_HW
};

/* SysPm callback structure for PWM_3 */
static cy_stc_syspm_callback_t PWM3DeepSleepCallbackHandler =
{
    .callback           = &mtb_syspm_tcpwm_deepsleep_callback,
    .skipMode           = CY_SYSPM_SKIP_CHECK_FAIL | CY_SYSPM_SKIP_CHECK_READY,
    .type               = CY_SYSPM_DEEPSLEEP,
    .callbackParams     = &PWM3DSParams,
    .prevItm            = NULL,
    .nextItm            = NULL,
    .order              = SYSPM_CALLBACK_ORDER
};

/* SysPm callback structure for PWM_2 */
static cy_stc_syspm_callback_t PWM2DeepSleepCallbackHandler =
{
    .callback           = &mtb_syspm_tcpwm_deepsleep_callback,
    .skipMode           = CY_SYSPM_SKIP_CHECK_FAIL | CY_SYSPM_SKIP_CHECK_READY,
    .type               = CY_SYSPM_DEEPSLEEP,
    .callbackParams     = &PWM2DSParams,
    .prevItm            = NULL,
    .nextItm            = NULL,
    .order              = SYSPM_CALLBACK_ORDER
};

#endif /*CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */

/*******************************************************************************
*  Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: app_gatt_write_handler
********************************************************************************
* Summary:
* This function is called when GATT handle write req event is recieved.
*
* Parameters:
* uint16_t conn_id            -- Connection ID
* wiced_bt_gatt_write_req_t * -- Pointer to gatt_write data
*
* Return:
* wiced_bt_gatt_status_t
*
*******************************************************************************/
wiced_bt_gatt_status_t app_gatt_write_handler(uint16_t conn_id,
                           wiced_bt_gatt_write_req_t * p_wr_data )
{
    wiced_bt_gatt_status_t result = WICED_BT_GATT_ATTRIBUTE_NOT_FOUND;

    if (WICED_BT_GATT_ATTRIBUTE_NOT_FOUND == result)
    {
        result = gatt_write_default_handler(conn_id, p_wr_data);
    }

    return result;
}

/*******************************************************************************
* Function Name: app_gatt_read_req_handler
********************************************************************************
* Summary:
*  This function is called when GATT handle read req event is recieved.
*
* Parameters:
*  uint16_t conn_id                -- Connection ID
*  wiced_bt_gatt_read_t *          -- Pointer to gatt_read data
*  wiced_bt_gatt_opcode_t opcode   -- opcode
*  uint16_t len_requested          -- The requested length
*
* Return:
*  wiced_bt_gatt_status_t
*
*******************************************************************************/
wiced_bt_gatt_status_t app_gatt_read_req_handler( uint16_t conn_id,
                                                  wiced_bt_gatt_read_t *p_req,
                                                  wiced_bt_gatt_opcode_t opcode,
                                                  uint16_t len_requested )
{
    return gatt_read_req_default_handler(conn_id, p_req, opcode,
    len_requested );
}

/*******************************************************************************
* Function Name: app_link_up
********************************************************************************
* Summary:
*  This function is called when link is up
*
* Parameters:
*  wiced_bt_gatt_connection_status_t *p_status: Pointer to GATT connection status
*
* Return:
*  None
*
*******************************************************************************/
void app_link_up(wiced_bt_gatt_connection_status_t * p_status)
{

    printf("%s Link is up, conn_id:%04x type:%d\n",
    link_transport() == BT_TRANSPORT_LE ? "LE" : "BREDR",
    p_status->conn_id, p_status->addr_type);

    /*Turn on LED 1 by setting Duty cycle as 100 when connection is up */
    Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_3_HW,
            CYBSP_PWM_LED_CTRL_3_NUM, DUTY_CYCLE_100);
}

/*******************************************************************************
* Function Name: app_link_down
********************************************************************************
* Summary:
*  This function is called when link is down
*
* Parameters:
*  wiced_bt_gatt_connection_status_t *p_status: Pointer to GATT connection status
*
* Return:
*  None
*
*******************************************************************************/
void app_link_down(const wiced_bt_gatt_connection_status_t * p_status)
{

    uint16_t conn_id = link_conn_id();
    printf("Link down, id:0x%04x reason: %d\n",  p_status->conn_id,
    p_status->reason );

    /* if no more link, turn off both LEDs by setting duty cycle 0 */
    if (!conn_id)
    {
        Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_3_HW,
                CYBSP_PWM_LED_CTRL_3_NUM, DUTY_CYCLE_0);
        Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_2_HW,
                CYBSP_PWM_LED_CTRL_2_NUM, DUTY_CYCLE_0);
    }
    else
    {
        p_status = link_connection_status();
        if (p_status)
        {
            printf("conn_id:%04x peer_addr:%s type:%d now is active",
            p_status->conn_id, p_status->bd_addr,
            p_status->addr_type);
        }
    }
}

/*******************************************************************************
* Function Name: app_adv_state_changed
********************************************************************************
* Summary:
*  This function is called when advertisment state is changed
*
* Parameters:
*  wiced_bt_ble_advert_mode_t old_adv
*  wiced_bt_ble_advert_mode_t adv
*
* Return:
*  none
*
*******************************************************************************/
void app_adv_state_changed(wiced_bt_ble_advert_mode_t old_adv,
                           wiced_bt_ble_advert_mode_t adv)
{
    if (BTM_BLE_ADVERT_OFF == adv)
    {
        printf("Advertisement Stopped\n");
        /* Set duty cycle 0 to turn off LED 1 */
        Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_3_HW,
                CYBSP_PWM_LED_CTRL_3_NUM, DUTY_CYCLE_0);
    }
    else
    {
        if (BTM_BLE_ADVERT_OFF == old_adv)
        {
           printf("Advertisement %d started\n", adv);
           /* Set duty cycle 50 to blink LED 2 */
           Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_2_HW,
                   CYBSP_PWM_LED_CTRL_2_NUM, DUTY_CYCLE_50);
        }
        else
        {
            printf("Advertisement State Change: %d -> %d", old_adv, adv);
        }
    }
}

/*******************************************************************************
* Function Name: app_init
********************************************************************************
* Summary:
* This function is called after BT has been enabled. It initializes the GATT DB,
* ISOC communication and TCPWM for USER LEDs.
*
* Parameters:
* None
*
* Return:
* Wiced_result_t : Returns WICED_BT_SUCCESS if all initializations are
*                  successful
*******************************************************************************/
wiced_result_t app_init(void)
{
    cy_rslt_t result = CY_TCPWM_SUCCESS;

    /*Initialize the GATT DB and related GATT operations */
    gatt_initialize();

    /*Initialize the ISOC connection */
    isoc_init();

    /*Initialize TCPWM for LED1 */
    result=Cy_TCPWM_PWM_Init(CYBSP_PWM_LED_CTRL_3_HW,
            CYBSP_PWM_LED_CTRL_3_NUM, &CYBSP_PWM_LED_CTRL_3_config);
    if(CY_TCPWM_SUCCESS != result)
    {
        printf("Failed to initialize PWM for LED1.\r\n");
        handle_app_error();
    }

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

    /* SysPm callback registration for PWM3 */
    Cy_SysPm_RegisterCallback(&PWM3DeepSleepCallbackHandler);

#endif

    /* Enable the TCPWM block for LED1 */
    Cy_TCPWM_PWM_Enable(CYBSP_PWM_LED_CTRL_3_HW,
            CYBSP_PWM_LED_CTRL_3_NUM);
    /* Start the PWM for LED1 */
    Cy_TCPWM_TriggerStart_Single(CYBSP_PWM_LED_CTRL_3_HW,
            CYBSP_PWM_LED_CTRL_3_NUM);

    /* Initialize TCPWM for LED2 */
    result=Cy_TCPWM_PWM_Init(CYBSP_PWM_LED_CTRL_2_HW,
            CYBSP_PWM_LED_CTRL_2_NUM, &CYBSP_PWM_LED_CTRL_2_config);
    if(CY_TCPWM_SUCCESS != result)
    {
        printf("Failed to initialize PWM for LED2.\r\n");
        handle_app_error();
    }
    
#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

    /* SysPm callback registration for PWM2 */
    Cy_SysPm_RegisterCallback(&PWM2DeepSleepCallbackHandler);

#endif

    /* Enable the TCPWM block for LED2 */
    Cy_TCPWM_PWM_Enable(CYBSP_PWM_LED_CTRL_2_HW,
            CYBSP_PWM_LED_CTRL_2_NUM);
    /* Start the PWM for LED2 */
    Cy_TCPWM_TriggerStart_Single(CYBSP_PWM_LED_CTRL_2_HW,
            CYBSP_PWM_LED_CTRL_2_NUM);

    /* Allow peer to pair */
    wiced_bt_set_pairable_mode(WICED_TRUE, WICED_TRUE);
    return WICED_BT_SUCCESS;
}

/*******************************************************************************
* Function Name: application_start()
********************************************************************************
* Summary:
*  Entry point to the application. Set device configuration and start Bluetooth
*  stack initialization.  The actual application initialization (app_init) will
*  be called when stack reports that Bluetooth device is ready.
*******************************************************************************/
void application_start(void)
{
    /* Clear UART Terminal Window*/
    printf("\x1b[2J\x1b[;H");

    /*Display header*/
    printf("***************** "
    "PSOC Edge MCU: Bluetooth LE ISOC Peripheral "
    "*****************\n\n");

    /* Initialize the BT stack*/
    bt_init();
}

/* end of file */
