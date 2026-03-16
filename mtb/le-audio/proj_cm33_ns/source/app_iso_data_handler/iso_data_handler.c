/*******************************************************************************
* File Name: iso_data_handler.c
*
* Description: This file contains the functions related to ISO data handling.
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
* INCLUDES
*******************************************************************************/
#include "iso_data_handler.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "wiced_bt_isoc.h"
#include "wiced_bt_trace.h"
#include "wiced_memory.h"
#include "cybt_platform_interface.h"

/*******************************************************************************
* DEFINES
*******************************************************************************/
#define ISO_DATA_HEADER_SIZE                  (4U)
#define ISO_LOAD_HEADER_SIZE_WITH_TS          (8U)
#define ISO_LOAD_HEADER_SIZE_WITHOUT_TS       (4U)
#define ISO_PKT_PB_FLAG_MASK                  (3U)
#define ISO_PKT_PB_FLAG_OFFSET                (12U)
#define ISO_PKT_PB_FLAG_FIRST_FRAGMENT        (0U)
#define ISO_PKT_PB_FLAG_CONTINUATION_FRAGMENT (1U)
#define ISO_PKT_PB_FLAG_COMPLETE              (2U)
#define ISO_PKT_PB_FLAG_LAST_FRAGMENT         (3U)
#define ISO_PKT_TS_FLAG_MASK                  (1U)
#define ISO_PKT_TS_FLAG_OFFSET                (14U)
#define ISO_PKT_RESERVED_FLAG_MASK            (1U)
#define ISO_PKT_RESERVED_FLAG_OFFSET          (15U)
#define ISO_PKT_DATA_LOAD_LENGTH_MASK         (0x3FFFU)
#define ISO_PKT_SDU_LENGTH_MASK               (0x0FFFU)
#define MAX_BUFFER_SIZE                       (550U)
#define INIT_VAL                              (0U)
#define INIT_HANDLE                           (0U)

/*******************************************************************************
* Variables
*******************************************************************************/
wiced_bt_buffer_t *g_cis_iso_pool = NULL;
static iso_dhm_num_complete_evt_cb_t g_num_complete_cb;
static iso_dhm_rx_evt_cb_t g_rx_data_cb;
uint16_t data_load_length = INIT_VAL;

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: iso_dhm_process_rx_data
********************************************************************************
* Summary:
*  Processes the Rx Data
*
* Parameters:
*  uint8_t *p_data: Pointer to Rx data buffer
*  uint32_t length: length of Rx data buffer
*
* Return:
*  None
*******************************************************************************/
void iso_dhm_process_rx_data(uint8_t *p_data, uint32_t length)
{
    uint16_t handle_and_flags = INIT_VAL;
    uint16_t ts_flag = INIT_VAL;
    uint16_t pb_flag = INIT_VAL;
    uint16_t psn = INIT_VAL;
    uint16_t sdu_len = INIT_VAL;
    uint32_t ts = INIT_VAL;

    if (!length)
    {
        WICED_BT_TRACE("dhm rx data len = 0 ");
        return;
    }

    STREAM_TO_UINT16(handle_and_flags, p_data);
    STREAM_TO_UINT16(data_load_length, p_data);
    pb_flag = (handle_and_flags & (ISO_PKT_PB_FLAG_MASK <<
            ISO_PKT_PB_FLAG_OFFSET)) >> ISO_PKT_PB_FLAG_OFFSET;
    ts_flag = (handle_and_flags & (ISO_PKT_TS_FLAG_MASK <<
            ISO_PKT_TS_FLAG_OFFSET)) >> ISO_PKT_TS_FLAG_OFFSET;

    handle_and_flags &= ~(ISO_PKT_PB_FLAG_MASK << ISO_PKT_PB_FLAG_OFFSET);
    handle_and_flags &= ~(ISO_PKT_TS_FLAG_MASK << ISO_PKT_TS_FLAG_OFFSET);
    handle_and_flags &= ~(ISO_PKT_RESERVED_FLAG_MASK <<
            ISO_PKT_RESERVED_FLAG_OFFSET);
    if (ts_flag) 
    {
        STREAM_TO_UINT32(ts, p_data);
    }

    STREAM_TO_UINT16(psn, p_data);
    STREAM_TO_UINT16(sdu_len, p_data);
    data_load_length &= ISO_PKT_DATA_LOAD_LENGTH_MASK;
    sdu_len &= ISO_PKT_SDU_LENGTH_MASK;
    (void)ts;
    (void)pb_flag;
    (void)psn;

    if (!sdu_len)
    {
        return;
    }

    if (g_rx_data_cb)
    {
        g_rx_data_cb(handle_and_flags, p_data, sdu_len);
    }
}

/*******************************************************************************
* Function Name: iso_dhm_process_num_complete_pkts
********************************************************************************
* Summary:
*  Processes the completed packets and returns TRUE/FALSE
*
* Parameters:
*  uint8_t *p_buf : Pointer to data buffer
*
* Return:
*  wiced_bool_t : TRUE/FALSE
*******************************************************************************/
wiced_bool_t iso_dhm_process_num_completed_pkts(uint8_t *p_buf)
{
    uint8_t num_handles, index;
    uint16_t handle;
    uint16_t num_sent;
    wiced_bool_t complete = WICED_TRUE;
    STREAM_TO_UINT8(num_handles, p_buf);

    for (index = INIT_HANDLE; index < num_handles; index++)
    {
        STREAM_TO_UINT16(handle, p_buf);
        STREAM_TO_UINT16(num_sent, p_buf);

        /* validate handle */
        if (wiced_ble_isoc_is_cis_connected_with_conn_hdl(handle) ||
                wiced_ble_isoc_is_bis_created(handle))
        {
            /* callback to app to send more packets */
            if (g_num_complete_cb) 
            {
                    g_num_complete_cb(handle, num_sent);
            }
        }
        else 
        {
            complete = WICED_FALSE;
        }
    }

    return complete;
}
/*******************************************************************************
* Function Name: iso_dhm_init
********************************************************************************
* Summary:
*  Sets the ISO DHM register callback
*
* Parameters:
*  wiced_bt_cfg_isoc_t *p_isoc_cfg
*  iso_dhm_num_complete_evt_cb_t num_complete_cb
*  iso_dhm_rx_evt_cb_t rx_data_cb
*
* Return:
*  None
*******************************************************************************/
void iso_dhm_init(uint32_t max_sdu_size,
        uint32_t channel_count,
        uint32_t max_buffers_per_cis,
          iso_dhm_num_complete_evt_cb_t num_complete_cb,
          iso_dhm_rx_evt_cb_t rx_data_cb)
{
    wiced_ble_isoc_register_data_cb(iso_dhm_process_rx_data,
            iso_dhm_process_num_completed_pkts);
    int buff_size =
            (max_sdu_size * channel_count) + ISO_LOAD_HEADER_SIZE_WITH_TS +
            ISO_DATA_HEADER_SIZE;

    /* Allocate only once, allowing multiple calls to update callbacks */
    if (!g_cis_iso_pool)
    {
        g_cis_iso_pool = wiced_bt_create_pool("ISO SDU", buff_size, 
                             max_buffers_per_cis, NULL);
    }

    printf("[%s] g_cis_iso_pool 0x%x size %d count %d\n",
    __FUNCTION__,(int)g_cis_iso_pool,
    buff_size, (uint8_t)max_buffers_per_cis);
    g_num_complete_cb = num_complete_cb;
    g_rx_data_cb = rx_data_cb;
}

/*******************************************************************************
* Function Name: iso_dhm_get_data_buffer
********************************************************************************
* Summary:
* Returns the pointer to the data buffer plus the header size
*
* Parameters:
* None
*
* Return:
* Pointer to data buffer
*******************************************************************************/
uint8_t *iso_dhm_get_data_buffer(void)
{
    uint8_t *p_buf = wiced_bt_get_buffer_from_pool(g_cis_iso_pool);

    if (NULL != p_buf)
    {
        return p_buf + ISO_LOAD_HEADER_SIZE_WITH_TS + ISO_DATA_HEADER_SIZE;
    }
    else
    {
        return NULL;
    }
}

/*******************************************************************************
* Function Name: iso_dhm_free_data_buffer
********************************************************************************
* Summary:
*  Frees the ISO data buffer memory
*
* Parameters:
*  uint8_t *p_buf : Pointer to the data buffer
*
*
* Return:
*  None
*******************************************************************************/
void iso_dhm_free_data_buffer(uint8_t *p_buf)
{
    wiced_bt_free_buffer(p_buf - (ISO_LOAD_HEADER_SIZE_WITH_TS +
            ISO_DATA_HEADER_SIZE));
}

/*******************************************************************************
* Function Name: isoc_dhm_send_packet
********************************************************************************
* Summary:
*  Sends the ISO packet to lower stack and returns TRUE if it is successful
*
* Parameters:
*  uint16_t psn
*  uint16_t conn_handle
*  uint8_t ts_flag
*  uint8_t *p_data_buf
*  uint32_t data_buf_len
*
* Return:
*  wiced_bool_t : TRUE/FALSE
*******************************************************************************/
wiced_bool_t iso_dhm_send_packet(uint16_t psn,
                         uint16_t conn_handle,
                         uint8_t ts_flag,
                         uint8_t *p_data_buf,
                         uint32_t data_buf_len)
{
    uint8_t *p = NULL;
    uint8_t *p_iso_sdu = NULL;
    uint16_t handle_and_flags = conn_handle;
    uint16_t data_load_length = INIT_VAL;
    wiced_bool_t result = WICED_FALSE;

    if (data_buf_len > MAX_BUFFER_SIZE)
    {
    WICED_BT_TRACE_CRIT("Received packet larger than the ISO SDU len supported");
    return WICED_FALSE;
    }

    handle_and_flags |= (ISO_PKT_PB_FLAG_COMPLETE << ISO_PKT_PB_FLAG_OFFSET);
    handle_and_flags |= (ts_flag << ISO_PKT_TS_FLAG_OFFSET);

    if (ts_flag)
    {
        /* timestamp supported, header size is 4 + 8 */
        p_iso_sdu = p = p_data_buf - (ISO_LOAD_HEADER_SIZE_WITH_TS +
                ISO_DATA_HEADER_SIZE);
        data_load_length = data_buf_len + ISO_LOAD_HEADER_SIZE_WITH_TS;
    }
    else
    {
        /* timestamp not supported, header size is 4 + 4 */
        p_iso_sdu = p = p_data_buf - (ISO_LOAD_HEADER_SIZE_WITHOUT_TS +
                ISO_DATA_HEADER_SIZE);
        data_load_length = data_buf_len + ISO_LOAD_HEADER_SIZE_WITHOUT_TS;
    }

    data_load_length &= ISO_PKT_DATA_LOAD_LENGTH_MASK;
    data_buf_len &= ISO_PKT_SDU_LENGTH_MASK;
    UINT16_TO_STREAM(p, handle_and_flags);
    UINT16_TO_STREAM(p, data_load_length);
    UINT16_TO_STREAM(p, psn);
    UINT16_TO_STREAM(p, data_buf_len);
    result = wiced_ble_isoc_write_data_to_lower(p_iso_sdu, data_load_length +
            ISO_DATA_HEADER_SIZE);
    iso_dhm_free_data_buffer(p_data_buf);
    return result;
}

/*******************************************************************************
* Function Name: isoc_dhm_get_header_size
********************************************************************************
* Summary:
*  Returns the ISO DHM header size
*
* Parameters:
*  None
*
* Return:
*  uint32_t header size
*******************************************************************************/
uint32_t iso_dhm_get_header_size(void)
{
    return ISO_LOAD_HEADER_SIZE_WITH_TS + ISO_DATA_HEADER_SIZE;
}

/* end of file */
