/******************************************************************************
* File Name:   button.c
*
* Description: This file consists of the functions that are necessary for
*              user button use cases.
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
#include "button.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cycfg_pins.h"
#include "cyabs_rtos.h"
#include "wiced_bt_trace.h"
#include "wiced_bt_types.h"
#include "wiced_data_types.h"
#include "inttypes.h"
#include "cybsp_types.h"
#include "retarget_io_init.h"
#include "app_bt_bonding.h"

/*******************************************************************************
* DEFINES
*******************************************************************************/
#define BTN_IRQ_MASK                    (1U)
#define ISR_PRIORITY                    (7U)
#define BUTTON_TASK_PRIORITY            (2U)
#define BUTTON_TASK_STACK_SIZE          (256U)
#define MAX_READ_RETRY                  (2U)
#define SHIFT_VALUE                     (1U)
#define UL_BITS_TO_CLEAR_ON_ENTRY       (0U)
#define UL_BITS_TO_CLEAR_ON_EXIT        (0xffffffff)
#define NOTIFY_VAL                      (1U)
#define BUTTON_SCAN_DELAY_MSEC          (10U)
#define SHORT_PRESS_DELAY_MSEC          (50U)
#define LONG_PRESS_DELAY_MSEC           (3000U)
#define INIT_VALUE                      (0U)

/*******************************************************************************
* Global Variables
*******************************************************************************/
TaskHandle_t button_task_handle;

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: button_task
********************************************************************************
* Summary:
*  Task that takes care of button activities such as sending isoc data
*  and start advertising.
*******************************************************************************/
void button_task(void *args)
{
    uint32_t ulNotifiedValue;
    TickType_t last_button_press_time = INIT_VALUE;
    TickType_t long_press_start_time = INIT_VALUE;
    BaseType_t long_press_detected = pdFALSE;

    while(1)
    {
        xTaskNotifyWait(UL_BITS_TO_CLEAR_ON_ENTRY, UL_BITS_TO_CLEAR_ON_EXIT,
                &ulNotifiedValue, portMAX_DELAY);
        last_button_press_time = xTaskGetTickCount();
        long_press_start_time = xTaskGetTickCount();
        long_press_detected = pdFALSE;

        /* Condition for short press and long press of User Button 1*/
        while (Cy_GPIO_Read(CYBSP_SW2_PORT, CYBSP_SW2_PIN) == CYBSP_BTN_PRESSED)
        {
            if(!long_press_detected &&
                  ((xTaskGetTickCount() - long_press_start_time) >=
                  pdMS_TO_TICKS(LONG_PRESS_DELAY_MSEC)))
            {
                long_press_detected = pdTRUE;
                printf("USER BUTTON 1 long press is detected!\r\n");

                /* Reset Kv-store library, this will clear the NVM */
                cy_rslt_t cy_result = mtb_kvstore_reset(&kvstore_obj);
                if(CY_RSLT_SUCCESS == cy_result)
                {
                    printf("Successfully reset kv-store library, "
                           "please reset the device to generate new "
                           "keys!\n");
                }
                else
                {
                    printf("failed to reset kv-store libray\n");
                }

                handle_app_error();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_SCAN_DELAY_MSEC));

        if (!long_press_detected &&
                    ((xTaskGetTickCount() - last_button_press_time) >=
                      pdMS_TO_TICKS(SHORT_PRESS_DELAY_MSEC)))
        {
            if (link_is_connected())
            {
                if (isoc_cis_connected())
                {
                    /*Start Sending ISOC data if CIS connected*/
                    isoc_send_data();
                }
            }
            else
            {
                /*Start advertising if CIS is not connected*/
                bt_enter_pairing();
            }
        }
    }
}

/*******************************************************************************
* Function Name: button_interrupt_handler
********************************************************************************
* Summary:
*  Button state change interrupt handler
*
* Parameters:
*  void *handler_arg : Pointer to handle
*  cyhal_gpio_event_t event : GPIO interrupt event type
*
* Return:
*  None
*******************************************************************************/
static void button_interrupt_handler(void )
{
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;

    /* Check if the interrupt is from USER BUTTON 1 */
    if(Cy_GPIO_GetInterruptStatusMasked(CYBSP_USER_BTN_PORT,
                CYBSP_USER_BTN_PIN))
    {
        /* Clear the interrupt */
        Cy_GPIO_ClearInterrupt (CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN);
        NVIC_ClearPendingIRQ(CYBSP_USER_BTN_IRQ);
        xTaskNotifyFromISR(button_task_handle,NOTIFY_VAL, eSetBits,
                &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    /* CYBSP_USER_BTN1 (SW2) and CYBSP_USER_BTN2 (SW4) share the same port and
     * hence they share the same NVIC IRQ line. Since both the buttons are
     * configured for falling edge interrupt in the BSP, pressing any button
     * will trigger the execution of this ISR. Therefore, we must clear the
     * interrupt flag of the user button (CYBSP_USER_BTN2) to avoid issues in
     * case if user presses BTN2 by mistake.
     */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
}

/*******************************************************************************
* Function Name: button_lib_init
********************************************************************************
* Summary:
*  Initialize button functions. This is the first function to be called before
*  other button functions can be used.
*
* Parameters:
*  None
*
* Return:
*  wiced_bool_t
*******************************************************************************/
 bool button_lib_init(void)
 {
     BaseType_t rtos_result;

     /* Interrupt config structure */
     cy_stc_sysint_t intrCfg =
     {
         .intrSrc =CYBSP_USER_BTN1_IRQ,
          .intrPriority = GPIO_INTERRUPT_PRIORITY
     };

     /* CYBSP_USER_BTN1 (SW2) and CYBSP_USER_BTN2 (SW4) share the same port and
      * hence they share the same NVIC IRQ line. Since both are configured in the
      * BSP via the Device Configurator, the interrupt flags for both the buttons
      * are set right after they get initialized through the call to cybsp_init().
      * The flags must be cleared before initializing the interrupt, otherwise
      * the interrupt line will be constantly asserted. 
      */
     Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
     Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
     NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
     NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);

     /* Initialize the interrupt and register interrupt callback */
     cy_en_sysint_status_t btn_interrupt_init_status =
             Cy_SysInt_Init(&intrCfg, &button_interrupt_handler);
     if(CY_SYSINT_SUCCESS != btn_interrupt_init_status)
     {
         handle_app_error();
     }

     /* Enable the interrupt in the NVIC */
     NVIC_EnableIRQ(intrCfg.intrSrc);

     /* Initialize button task */
     rtos_result= xTaskCreate(button_task,
                                    "Button Task",
                                    BUTTON_TASK_STACK_SIZE,
                                    NULL,
                                    BUTTON_TASK_PRIORITY,
                                    &button_task_handle);

     if( pdPASS != rtos_result)
     {
        printf("Failed to create Button task! \n");
        return FALSE;
     }
     else
     {
        return TRUE;
     }
 }

 /* end of file */
