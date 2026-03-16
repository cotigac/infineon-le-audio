/*******************************************************************************
* File Name: isoc_peripheral.c
*
* Description: This file contains functions related to ISOC managements.
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
#include "iso_data_handler.h"
#include "wiced_bt_trace.h"
#include "wiced_bt_types.h"
#include "wiced_timer.h"
#include "wiced_memory.h"
#include "cybsp.h"

/*******************************************************************************
* Defines
*******************************************************************************/
#define READ_PSN_VSC_OPCODE                 (0xFDFA)
#define ISOC_MAX_BURST_COUNT                (6U)

/* sdu interval in micro-second */
#define ISO_SDU_INTERVAL                    (10000U)
#define ISOC_TIMEOUT_IN_MSECONDS            (ISO_SDU_INTERVAL / 1000U)

/* 2 minute keep alive timer to ensure app and controller psn */
#define ISOC_KEEP_ALIVE_TIMEOUT_IN_SECONDS  (120U)
#define ISOC_STATS_TIMEOUT                  (5U)

/* ISOC statistics periodically printed with this flag */
#define ISOC_MONITOR_FOR_DROPPED_SDUs
#define ISOC_ERROR_DROPPED_SDU_VSE_OPCODE   (0x008bU)
#define CONTROLLER_ISO_DATA_PACKET_BUFS     (8U)
#define TIMER_PARAM_VALUE                   (0U)
#define DUMMY_DATA_LENGTH                   (0U)
#define PREFERENCES_INIT_VAL                (0U)
#define TIMER_CB_NO_ARG                     (0U)
#define SEQUENCE_INIT_ZERO                  (0U)
#define DUTY_CYCLE_100                      (1000U)
#define DUTY_CYCLE_50                       (500U)
#define DUTY_CYCLE_0                        (0U)
#define ADD_PSN                             (1U)
#define DELAY_IN_MS                         (50U)
#define CONTROLLER_DELAY                    (0U)
#define CSC_LENGTH                          (0U)
#define NO_CSC                              (0U)
#define SRC_ASC_ID                          (0U)
#define SINK_ASC_ID                         (0U)
#define RESET_RX_COUNT                      (0U)
#define RESET_TX_COUNT                      (0U)
#define NO_SDU                              (0U)
#define DATA_BUFFER_LENGTH_NULLPL           (0U)
#define INITIAL_PACKET                      (0U)
#define PACKET_INCREMENT                    (2U)
#define STATUS_SUCCESS                      (0U)
#define SHIFT_VAL_1                         (8U)
#define SHIFT_VAL_2                         (16U)
#define OFFSET_1                            (0x0FFU)
#define OFFSET_2                            (0x0FFFFU)
#define OFFSET_INDEX_0                      (0U)
#define OFFSET_INDEX_1                      (1U)
#define OFFSET_INDEX_2                      (2U)
#define VSC_PARAM_LEN                       (2U)
#define MEMSET_VAL                          (0U)
#define CONNECTION_HANDLE_INVALID           (0U)
#define ISO_DATA_HEADER_SIZE                (4U)
#define ISO_LOAD_HEADER_SIZE_WITH_TS        (8U)
#define DATA_BUFFER_LENGTH                  (30U)
#define MAX_PAYLOAD                         (100U)
#define MAX_BIS                             (0U)
#define MAX_CIS                             (1U)
#define CHANNEL_COUNT                       (1U)
#define MAX_BUFF_PER_CIS                    (4U)
#define RESET_VAL                           (0U)
#define TIMEOFFSET_INDEX                    (3U)
#define MAX_SDU_SIZE                        (100U)
#define CODE_ID_SIZE                        (5U)
#define CODE_ID_VAL                         (0x3U)
#define CODE_ID_VAL1                        (0x0U)

/*******************************************************************************
* variables
*******************************************************************************/
#pragma pack(1)
typedef struct
{
    uint16_t  connHandle;
    uint16_t  psn;
    uint32_t  timestamp;
    uint16_t  expected_psn;
    uint32_t  expected_timestamp;
} isoc_error_dropped_sdu_t;
#pragma pack()

#pragma pack(1)
typedef struct
{
    uint16_t    cis_conn_handle;
    uint16_t    sequence_num;
    char data_string[DATA_BUFFER_LENGTH];
} iso_rx_data_central_type_t;
#pragma pack()

static struct
{
    uint16_t acl_conn_handle;
    uint16_t cis_conn_handle;
    uint16_t max_payload;
    wiced_ble_isoc_cis_established_evt_t  cis_established_data;
    wiced_timer_t isoc_send_data_timer;
    wiced_timer_t isoc_keep_alive_timer;
} isoc = {RESET_VAL};

#pragma pack( push, 1 )
typedef struct {
    uint8_t status;
    uint16_t connHandle;
    uint16_t packetSeqNum;
    uint32_t timeStamp;
    uint8_t  timeOffset[TIMEOFFSET_INDEX];
}tREAD_PSN_EVT;
#pragma pack( pop )

typedef enum
{
    SN_IDLE,
    SN_PENDING,
    SN_VALID,
} sequence_number_state_e;

static uint16_t iso_sdu_count = RESET_VAL;
static uint16_t sequence = RESET_VAL;
static sequence_number_state_e sequence_number_state;
static uint8_t number_of_iso_data_packet_bufs = CONTROLLER_ISO_DATA_PACKET_BUFS;
static uint32_t isoc_rx_count = RESET_VAL;
static uint32_t isoc_tx_count = RESET_VAL;
static uint32_t isoc_tx_fail_count=RESET_VAL;
wiced_timer_t iso_stats_timer;
uint8_t isoc_teardown_pending = RESET_VAL;

/*******************************************************************************
 * Function Declarations
 ******************************************************************************/
static void isoc_send_null_payload(void);
static void isoc_get_psn_start( WICED_TIMER_PARAM_TYPE param );

/*******************************************************************************
* Function Definitions
******************************************************************************/
/*******************************************************************************
* Function Name: app_send_dummy
*******************************************************************************
* Summary:
*  Send test ISO Packet
*
* Parameters:
*  uint16_t handle : Connection handle
*
* Return:
*  None
*
******************************************************************************/
void app_send_dummy(uint16_t handle)
{
    uint8_t* p_buf = iso_dhm_get_data_buffer();
    iso_dhm_send_packet(sequence, handle, WICED_FALSE, p_buf, DUMMY_DATA_LENGTH);
}

/*******************************************************************************
* Function Name: read_psn_cb
*******************************************************************************
* Summary:
*  Callback function of the Read PSN VSC
*
* Parameters:
*  wiced_bt_dev_vendor_specific_command_complete_params_t
*  *p_command_complete_params : Pointer to the PSN complete parameter
*
* Return:
*  None
*
******************************************************************************/
static void read_psn_cb(wiced_bt_dev_vendor_specific_command_complete_params_t 
                *p_command_complete_params)
{
    tREAD_PSN_EVT * evt=(tREAD_PSN_EVT *)p_command_complete_params->p_param_buf;
    int toffset = evt->timeOffset[OFFSET_INDEX_0];

    toffset |= (evt->timeOffset[OFFSET_INDEX_1] & OFFSET_1)<<SHIFT_VAL_1;
    toffset |= (evt->timeOffset[OFFSET_INDEX_2] & OFFSET_1)<< SHIFT_VAL_2;
    printf("[%s] status:%d handle:0x%x psn=%d timestamp:%d"
    " time_offset:%d\n", __FUNCTION__,
    evt->status & OFFSET_1, evt->connHandle& OFFSET_2,
    evt->packetSeqNum& OFFSET_2,
    (int)evt->timeStamp, toffset);

    if(evt->status != STATUS_SUCCESS)
    {
        printf("[%s] status %d", __FUNCTION__, evt->status);
        sequence_number_state = SN_IDLE;
        return;
    }

    if( sequence_number_state != SN_PENDING )
        return;

    /* If initial transmission, no need to increment */
    if( evt->packetSeqNum == INITIAL_PACKET )
        sequence = evt->packetSeqNum;
    else
        sequence = evt->packetSeqNum + PACKET_INCREMENT;

    sequence_number_state = SN_VALID;

    /* Send NULL payload if the idle timer is running */
    if( wiced_is_timer_in_use(&isoc.isoc_keep_alive_timer) )
    {
        isoc_send_null_payload();
    }
}

/*******************************************************************************
* Function Name: start_read_psn_using_vsc
*******************************************************************************
* Summary:
*  Send Vendor Specific command to read PSN value
*
* Parameters:
*  hdl : CIS connection handle
*
* Return:
*  None
*
******************************************************************************/
void start_read_psn_using_vsc(uint16_t hdl)
{
    wiced_bt_dev_vendor_specific_command (READ_PSN_VSC_OPCODE, VSC_PARAM_LEN,
                                          (uint8_t *)&hdl,read_psn_cb);
}

/*******************************************************************************
* Function Name: isoc_send_null_payload
*******************************************************************************
* Summary:
*  Sends data with NULL payload.
******************************************************************************/
static void isoc_send_null_payload(void)
{
    wiced_bool_t result;
    uint8_t* p_buf = NULL;

    if(SN_VALID != sequence_number_state)
        return;

    /* Allocate buffer for ISOC header */
    if(NULL != (p_buf = iso_dhm_get_data_buffer()))
    {
        result = iso_dhm_send_packet(sequence,
        isoc.cis_established_data.cis.cis_conn_handle,
        WICED_FALSE, p_buf, DATA_BUFFER_LENGTH_NULLPL);
        printf("[%s] sent null payload handle %02x result %d\n",
        __FUNCTION__, isoc.cis_established_data.cis.cis_conn_handle,
        result);

        /* Set PSN state back to idle */
        sequence_number_state = SN_IDLE;
    }
}

/*******************************************************************************
* Function Name: isoc_send_data_handler
*******************************************************************************
* Summary:
*  Updates the send buffer and submits data to the controller
*
* Parameters:
*  param: Timer callback argument type
*
* Return:
*  None
******************************************************************************/
static void isoc_send_data_handler( WICED_TIMER_PARAM_TYPE param )
{
    if(sequence_number_state == SN_VALID)
    {

    wiced_bool_t result;
    uint32_t data_length;
    uint8_t* p_buf = NULL;
    uint8_t* p = NULL;
    char *s = "IFX ISOC CIS PERIPHERAL";

    /* Submit data to the controller only if it has bufs available */
    if(number_of_iso_data_packet_bufs)
    {
        if(NULL != (p_buf = iso_dhm_get_data_buffer()))
        {
            p = p_buf ;

            /* Copy the connection handle, sequence number and string to the
             *  pointer p */
            UINT16_TO_STREAM(p, isoc.cis_established_data.cis.cis_conn_handle);
            UINT16_TO_STREAM(p, sequence);
            strcpy((char*)p, s);
            data_length = MAX_SDU_SIZE;

            /* pass data to data handler module */
            result = iso_dhm_send_packet(sequence,
            isoc.cis_established_data.cis.cis_conn_handle, WICED_FALSE,
            p_buf, data_length);

            if(result)
            {
                number_of_iso_data_packet_bufs--;
                isoc_tx_count++;
            }
            else
            {
                isoc_tx_fail_count++;
            }
            printf("[%s] handle:0x%x SN:%d data_length:%d sdu_count:%d"
            " result:%d\n", __FUNCTION__,
            isoc.cis_established_data.cis.cis_conn_handle,
            sequence, (int)data_length, iso_sdu_count, result);
        }
     }

    sequence++;
    iso_sdu_count--;

    if(NO_SDU == iso_sdu_count)
    {
        wiced_stop_timer(&isoc.isoc_send_data_timer);
        sequence_number_state = SN_IDLE;
        /* Start keep alive timer */
        wiced_start_timer(&isoc.isoc_keep_alive_timer,
        ISOC_KEEP_ALIVE_TIMEOUT_IN_SECONDS);
        printf("Started keep alive timer\n");
    }
    }
    else
    {
        wiced_stop_timer(&isoc.isoc_send_data_timer);
                sequence_number_state = SN_IDLE;
        /* Start keep alive timer */
        wiced_start_timer(&isoc.isoc_keep_alive_timer,
                ISOC_KEEP_ALIVE_TIMEOUT_IN_SECONDS);
    }
}

/*******************************************************************************
* Function Name: isoc_stop
*******************************************************************************
* Summary:
*  Called upon disconnection or failure to establish CIS.
******************************************************************************/
static void isoc_stop(void)
{
    wiced_stop_timer(&isoc.isoc_send_data_timer);
    printf("[%s] enabled HCI trace\n", __FUNCTION__);
    wiced_bt_dev_update_debug_trace_mode(TRUE);
    wiced_bt_dev_update_hci_trace_mode(TRUE);

    /* Set duty cycle of LED2 to zero to turn it off*/
    Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_2_HW,
            CYBSP_PWM_LED_CTRL_2_NUM,DUTY_CYCLE_0);

    /*Reset ISOC Rx and Tx counts*/
    isoc_rx_count = RESET_RX_COUNT;
    isoc_tx_count = RESET_TX_COUNT;
    wiced_stop_timer(&iso_stats_timer);
}

/******************************************************************************
* Function Name: isoc_stats_timeout
******************************************************************************
* Summary:
*  Prints isoc stats upon timeout
*
* Parameters:
*  param: Timer callback argument type
*
* Return:
*  None
******************************************************************************/
static void isoc_stats_timeout( WICED_TIMER_PARAM_TYPE param )
{
    printf("[ISOC STATS] isoc_rx_count:%d  isoc_tx_count:%d "
            "isoc_tx_fail_count:%d\n", (int)isoc_rx_count,
            (int)isoc_tx_count, (int)isoc_tx_fail_count);
}

/*******************************************************************************
* Function Name: isoc_management_cback
********************************************************************************
* Summary:
*  This is the callback function for ISOC Management.
*
* Parameters:
*  wiced_bt_isoc_event_t event              : ISOC event code
*  wiced_bt_isoc_event_data_t *p_event_data : Pointer to ISOC event data
*
* Return:
*  None
*******************************************************************************/
static void isoc_management_cback(wiced_ble_isoc_event_t event,
                                  wiced_ble_isoc_event_data_t *p_event_data)
{
    wiced_result_t result;
    printf("[%s] %d\n", __FUNCTION__, event);
    switch (event)
    {
        case WICED_BLE_ISOC_SET_CIG_CMD_COMPLETE_EVT:
            printf("WICED_BLE_ISOC_SET_CIG_CMD_COMPLETE\n");
            break;

        case WICED_BLE_ISOC_CIS_REQUEST_EVT:
            printf("WICED_BLE_ISOC_CIS_REQUEST\n");
            isoc.acl_conn_handle = p_event_data->cis_request.acl_conn_handle;
            isoc.cis_conn_handle = p_event_data->cis_request.cis_conn_handle;
            result = wiced_ble_isoc_peripheral_accept_cis(&p_event_data
                    ->cis_request);
            printf("[%s] accept cis %d\n", __FUNCTION__, result);
            break;

        case WICED_BLE_ISOC_CIS_ESTABLISHED_EVT:
        {
            wiced_ble_isoc_cis_established_evt_t *p_est = &p_event_data
                    ->cis_established_data;

            printf("WICED_BLE_ISOC_CIS_ESTABLISHED\n");
            if(WICED_BT_SUCCESS == p_est->status)
            {
                wiced_ble_isoc_setup_data_path_info_t iso_audio_param_data;
                uint8_t codec_id[CODE_ID_SIZE] = {CODE_ID_VAL, CODE_ID_VAL1,
                        CODE_ID_VAL1, CODE_ID_VAL1, CODE_ID_VAL1};

                memcpy(&isoc.cis_established_data, p_est,
                        sizeof(wiced_ble_isoc_cis_established_evt_t));
                printf("[%s] CIS established %d %d %d \n", __FUNCTION__,
                        p_est->cis.cig_id,
                        p_est->cis.cis_id,
                        p_est->cis.cis_conn_handle);

                iso_audio_param_data.isoc_conn_hdl = p_est->cis.cis_conn_handle;
                iso_audio_param_data.p_app_ctx = (void *)WICED_BLE_ISOC_DPD_INPUT;
                iso_audio_param_data.data_path_dir = WICED_BLE_ISOC_DPD_INPUT;
                iso_audio_param_data.data_path_id = WICED_BLE_ISOC_DPID_HCI;
                iso_audio_param_data.p_csc = NULL;
                iso_audio_param_data.csc_length = RESET_VAL;
                memcpy(iso_audio_param_data.codec_id, codec_id,
                        sizeof(iso_audio_param_data.codec_id));
                result = wiced_ble_isoc_setup_data_path( &iso_audio_param_data);
                printf("[%s] setup_data_path INPUT %d\n", __FUNCTION__, result);

                iso_audio_param_data.isoc_conn_hdl = p_est->cis.cis_conn_handle;
                iso_audio_param_data.p_app_ctx = (void *)WICED_BLE_ISOC_DPD_OUTPUT;
                iso_audio_param_data.data_path_dir = WICED_BLE_ISOC_DPD_OUTPUT;
                result = wiced_ble_isoc_setup_data_path( &iso_audio_param_data);
                printf("[%s] setup_data_path OUTPUT %d\n", __FUNCTION__, result);
            }
            else
            {
                printf("[%s] CIS establishment failure status: %d\n",
                               __FUNCTION__,
                               p_event_data->cis_established_data.status);

                memset(&isoc.cis_established_data, RESET_VAL,
                        sizeof(wiced_ble_isoc_cis_established_evt_t));
                isoc_stop();
            }
        }break;

        case WICED_BLE_ISOC_CIS_DISCONNECTED_EVT:
        {
            wiced_result_t result = WICED_SUCCESS;
            wiced_bool_t result_dp = TRUE;
            wiced_ble_isoc_cis_disconnect_evt_t *p_dis = &p_event_data
                    ->cis_disconnect;
            printf("WICED_BLE_ISOC_CIS_DISCONNECTED\n");
            isoc_stop();
            printf("[%s] CIS Disconnected cig: %d cis: %d %d %d reason:%d\n",
                           __FUNCTION__,p_dis->cis.cig_id,
                           p_dis->cis.cis_id,p_dis->cis.cis_conn_handle,
                           isoc.cis_established_data.cis.cis_conn_handle,
                           p_dis->reason);
            memset(&isoc.cis_established_data, RESET_VAL,
                    sizeof(wiced_ble_isoc_cis_established_evt_t));
            if (wiced_ble_isoc_is_cis_connected_with_conn_hdl(p_dis
                    ->cis.cis_conn_handle))
            {
                result_dp = wiced_ble_isoc_remove_data_path( p_dis
                    ->cis.cis_conn_handle, WICED_BLE_ISOC_DPD_INPUT_BIT);
                printf("[%s] remove DP, result: %d\n", __FUNCTION__, result_dp);
            }

            printf("[%s] remove cig, result: %d\n", __FUNCTION__, result);
        }break;

        case WICED_BLE_ISOC_DATA_PATH_SETUP_EVT:
            printf("WICED_BLE_ISOC_DATA_PATH_SETUP\n");
            if(WICED_BT_SUCCESS != p_event_data->datapath.status)
            {
                printf("[%s] Datapath setup failure, status: %d\n",
                    __FUNCTION__, p_event_data->datapath.status);
                return;
            }

            if(isoc.cis_established_data.cis.cis_conn_handle
               != p_event_data->datapath.conn_hdl)
            {
                printf("[%s] Connection Handle mismatch in Datapath"
                               " Status \n",__FUNCTION__);
                return;
            }

            if (p_event_data->datapath.p_app_ctx ==
                    (void *)WICED_BLE_ISOC_DPD_OUTPUT)
            {
                isoc_start();
                app_send_dummy(p_event_data->datapath.conn_hdl);
            }

            break;

        case WICED_BLE_ISOC_DATA_PATH_REMOVED_EVT:
            printf("WICED_BLE_ISOC_DATA_PATH_REMOVED\n");
            if(WICED_BT_SUCCESS != p_event_data->datapath.status)
            {
                printf("[%s] Datapath remove failure \n", __FUNCTION__);
                return;
            }

            printf("[%s] Datapath removed, Disconnect CIS \n", __FUNCTION__);
            result = wiced_ble_isoc_disconnect_cis(p_event_data
                    ->datapath.conn_hdl);
            printf("[%s] disconnect cis on DP removed %d\n",
                    __FUNCTION__,result);
            break;

        default:
            printf("[%s] Unhandled event %d\n", __FUNCTION__, event);
            break;
        }

        CY_UNUSED_PARAMETER(result);
}

/*******************************************************************************
* Function Name: rx_handler
********************************************************************************
* Summary:
*  Handles received ISOC data
*
* Parameters:
*  uint16_t cis_handle : CIS Handle
*  uint8_t *p_data     : Pointer to the received data
*  uint32_t length     : Length of the received data
*
* Return:
*  None
*******************************************************************************/
static void rx_handler(uint16_t cis_handle, uint8_t *p_data, uint32_t length)
{
    iso_rx_data_central_type_t* p_rx_data = (
    iso_rx_data_central_type_t*) p_data;

    if (length >= sizeof(iso_rx_data_central_type_t))
    {

        printf("[rx_data] cis_id:0x%x SN:%d data:%s length:%d\n",
        p_rx_data->cis_conn_handle, p_rx_data->sequence_num,
        p_rx_data->data_string,(uint8_t)length);

        /* Increment rx count */
        isoc_rx_count++;

        /* Blink LED1 to show the reception of data */
        Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_3_HW,
                CYBSP_PWM_LED_CTRL_3_NUM,DUTY_CYCLE_0);
        Cy_SysLib_Delay(DELAY_IN_MS);
        Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_3_HW,
                CYBSP_PWM_LED_CTRL_3_NUM,DUTY_CYCLE_100);
    }
}

/*******************************************************************************
* Function Name: isoc_get_psn_start
********************************************************************************
* Summary:
*  Returns the PSN start value for the current transmission packet.
*
* Parameters:
*  param: Timer callback argument type
*
* Return:
*  None
*******************************************************************************/
static void isoc_get_psn_start(WICED_TIMER_PARAM_TYPE param)
{
    if(sequence_number_state == SN_IDLE &&
    isoc.cis_established_data.cis.cis_conn_handle)
    {
        sequence_number_state = SN_PENDING;
        printf("[%s] sending HCI_BLE_ISOC_READ_TX_SYNC for handle %02x\n",
        __FUNCTION__, isoc.cis_established_data.cis.cis_conn_handle);

        /* Start to read PSN using VSC OPCODE */
        start_read_psn_using_vsc(isoc.cis_established_data.cis.cis_conn_handle);
    }
}

/*******************************************************************************
* Function Name: isoc_send_data_num_complete_packets_evt
********************************************************************************
* Summary:
*  Handle Number of Complete Packets event from controller
*
* Parameters:
*  uint16_t cis handle: CIS Handle
*  uint16_t num_sent  : number of data sent
*
* Return:
*  None
*******************************************************************************/
static void isoc_send_data_num_complete_packets_evt(uint16_t cis_handle,
                                                    uint16_t num_sent)
{
    number_of_iso_data_packet_bufs += num_sent;
    wiced_start_timer(&isoc.isoc_keep_alive_timer,
    ISOC_KEEP_ALIVE_TIMEOUT_IN_SECONDS);
}

/*******************************************************************************
* Function Name: isoc_vse_cback
********************************************************************************
* Summary:
*  VSE callback used to monitor for dropped sdu error events from
*  the controller
*
* Parameters:
*  uint8_t len: dropped SDU length
*  uint8_t *p : Opcode
*
* Return:
*  None
*
*******************************************************************************/
static void isoc_vse_cback(uint8_t len, uint8_t *p)
{
    uint16_t opcode;
    isoc_error_dropped_sdu_t* p_isoc_error_dropped_sdu_vse;
    STREAM_TO_UINT16(opcode, p);

    if(opcode == ISOC_ERROR_DROPPED_SDU_VSE_OPCODE)
    {
        p_isoc_error_dropped_sdu_vse = (isoc_error_dropped_sdu_t*) p;
        /* set sequence to next expected PSN */
        sequence = p_isoc_error_dropped_sdu_vse->expected_psn + ADD_PSN;
        if (wiced_is_timer_in_use(&isoc.isoc_keep_alive_timer))
        {
            /* idle packet is failed. so send it again */
            app_send_dummy(p_isoc_error_dropped_sdu_vse->connHandle);
        }else
        {
            isoc_tx_count--;
        }
    }
}

/*******************************************************************************
* Function Name: isoc_cis_connected
********************************************************************************
* Summary:
*  Returns TRUE if CIS connected, else FALSE
*
* Parameters:
*  None
*
* Return:
*  wiced_bool_t : TRUE/FALSE
*******************************************************************************/
wiced_bool_t isoc_cis_connected(void)
{
    return isoc.cis_established_data.cis.cis_conn_handle !=
            CONNECTION_HANDLE_INVALID;
}

/*******************************************************************************
* Function Name: isoc_send_data
********************************************************************************
* Summary:
*  Called once the ISOC data patch has been established.
*
* Parameters:
*  None
*
* Return:
*  None
*******************************************************************************/
void isoc_start(void)
{   
    /* Set Duty cycle 100% to turn on LED 1 and LED 2*/
    Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_2_HW,
            CYBSP_PWM_LED_CTRL_2_NUM,DUTY_CYCLE_100);
    Cy_TCPWM_PWM_SetCompare0(CYBSP_PWM_LED_CTRL_3_HW,
            CYBSP_PWM_LED_CTRL_3_NUM,DUTY_CYCLE_100);

    /* Initialize sequence number as zero */
    sequence = SEQUENCE_INIT_ZERO;

    /*Start ISOC stats timeout*/
    wiced_start_timer(&iso_stats_timer, ISOC_STATS_TIMEOUT);
}

/*******************************************************************************
* Function Name: isoc_send_data
********************************************************************************
* Summary:
*  Called when configured for burst tx mode when button is pressed.
*  Number of packets sent defined by ISOC_MAX_BURST_COUNT.
*******************************************************************************/
void isoc_send_data(void)
{
    /* start to burst out data */
    iso_sdu_count += ISOC_MAX_BURST_COUNT;

    /* stop keep alive timer if it is running */
    if (wiced_is_timer_in_use(&isoc.isoc_keep_alive_timer))
    {
        wiced_stop_timer(&isoc.isoc_keep_alive_timer);
    }

    /* Get PSN start value for current transmission packet */
    isoc_get_psn_start(TIMER_PARAM_VALUE);

    /* start to burst out data */
    if (!wiced_is_timer_in_use(&isoc.isoc_send_data_timer))
    {
        wiced_start_timer(&isoc.isoc_send_data_timer, ISOC_TIMEOUT_IN_MSECONDS);
    }
}

/*******************************************************************************
* Function Name: isoc_init
********************************************************************************
* Summary:
*  Registers ISOC callbacks.
*  Sets Phy preferences to ISOC.
*******************************************************************************/
void isoc_init(void)
{
    wiced_result_t status;
    wiced_bt_ble_phy_preferences_t phy_preferences = {{PREFERENCES_INIT_VAL}};
    printf("[%s]\n", __FUNCTION__);
    isoc.max_payload = MAX_PAYLOAD;
    wiced_ble_isoc_cfg_t cfg = {
        .max_bis = MAX_BIS,
        .max_cis = MAX_CIS
    };

    /* Register ISOC management callback */
    wiced_ble_isoc_init(&cfg, isoc_management_cback);

    /* Init ISOC data handler module and register ISOC receive data handler */
    iso_dhm_init(ISO_SDU_SIZE,CHANNEL_COUNT,MAX_BUFF_PER_CIS,
            isoc_send_data_num_complete_packets_evt, rx_handler);

    /* Set to 2M phy */
    phy_preferences.rx_phys = WICED_BLE_ISOC_LE_2M_PHY;
    phy_preferences.tx_phys = WICED_BLE_ISOC_LE_2M_PHY;
    status = wiced_bt_ble_set_default_phy(&phy_preferences);
    printf("[%s] Set default phy status %d\n", __FUNCTION__, status);

    sequence_number_state = SN_IDLE;

    /* Init send data timer */
    wiced_init_timer(&isoc.isoc_send_data_timer, isoc_send_data_handler,
            TIMER_CB_NO_ARG,
    WICED_MILLI_SECONDS_PERIODIC_TIMER);

    /* Init keep alive timer */
    wiced_init_timer(&isoc.isoc_keep_alive_timer, isoc_get_psn_start,
            TIMER_CB_NO_ARG,
    WICED_SECONDS_PERIODIC_TIMER);

    /* Init stats timer */
    wiced_init_timer(&iso_stats_timer, isoc_stats_timeout, TIMER_CB_NO_ARG,
    WICED_SECONDS_PERIODIC_TIMER);

    /* Register VSC callback to return the PSN value */
    wiced_bt_dev_register_vse_callback(isoc_vse_cback);
    CY_UNUSED_PARAMETER(status);
}


/* end of file */