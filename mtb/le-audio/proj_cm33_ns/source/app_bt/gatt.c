/*******************************************************************************
* File Name: gatt.c
*
* Description: This file contains the task that handles GATT events.
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

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "app.h"
#include "wiced_memory.h"
#include "cycfg_gap.h"
#include "cycfg_bt_settings.h"

/*******************************************************************************
* Defines
*******************************************************************************/
#define PAIR_LEN_INIT_VAL    (0U)
#define INIT_FLAG_RESET      (0U)
#define INVALID_ATT_HANDLE   (0U)
#define INIT_ATT_HANDLE      (0U)
#define GAP_APPEARANCE_VAL   (2U)
#define NOT_FILLED           (0U)
#define ITERATION_INIT_VAL   (0U)
#define HANDLE_INDEX         (0U)

/*******************************************************************************
* typedef
*******************************************************************************/
typedef void (*pfn_free_buffer_t)(uint8_t *);

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: gatt_free_buffer
********************************************************************************
* Summary:
*  Frees buffer.
*
* Parameters:
*  uint8_t *p_data  : pointer to the start of the buffer to be freed
*
* Return:
*  None
*
*******************************************************************************/
static void gatt_free_buffer(uint8_t *p_data)
{
    wiced_bt_free_buffer(p_data);
}

/*******************************************************************************
* Function Name: gatt_alloc_buffer
********************************************************************************
* Summary:
* Allocate buffer
*
* Parameters:
* uint16_t len : size to be allocated
*
* Return:
* p  : the pointer to the buffer or NULL on failure
*
*******************************************************************************/
static uint8_t * gatt_alloc_buffer(uint16_t len)
{
    uint8_t *p = (uint8_t *)wiced_bt_get_buffer(len);
    return p;
}

/*******************************************************************************
* Function Name: gatt_conn_state_change
********************************************************************************
* Summary:
*  Handles connection state change. This function is called when the
*  link is up or down. It calls link module for the link event.
*
* Parameters:
*  p_status: Pointer to GATT connection status
*
* Return:
*  wiced_bt_gatt_status_t
*******************************************************************************/
static wiced_bt_gatt_status_t gatt_conn_state_change(
       wiced_bt_gatt_connection_status_t * p_status )
{
    if(p_status->connected)
    {
        return link_up( p_status );
    }
    else
    {
        return link_down( p_status );
    }
}

/*******************************************************************************
* Function Name : app_bt_gatt_req_read_by_type_handler
********************************************************************************
* Summary:
*  Process read-by-type request from peer device
*
* Parameters:
*  uint16_t conn_id                       : Connection ID
*  wiced_bt_gatt_opcode_t opcode          : LE GATT request type opcode
*  wiced_bt_gatt_read_by_type_t p_read_req: Pointer to read request
*          containing the handle to read
*  uint16_t len_requested                  : Length of data requested
*
* Return:
*  wiced_bt_gatt_status_t                         : LE GATT status
*******************************************************************************/
static wiced_bt_gatt_status_t gatt_req_read_by_type_handler(
                                uint16_t conn_id,
                                wiced_bt_gatt_opcode_t opcode,
                                wiced_bt_gatt_read_by_type_t *p_read_req,
                                uint16_t len_requested )
{
    int         to_copy;
    uint8_t     * copy_from;
    uint16_t    attr_handle = p_read_req->s_handle;
    uint8_t    *p_rsp = wiced_bt_get_buffer(len_requested);
    uint8_t     pair_len = PAIR_LEN_INIT_VAL;
    int used = INIT_FLAG_RESET;

    if (p_rsp == NULL)
    {
        printf("[%s] no memory len_requested: %d!!\n", __FUNCTION__,
        len_requested);
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, attr_handle,
        WICED_BT_GATT_INSUF_RESOURCE);
        return WICED_BT_GATT_INSUF_RESOURCE;
    }

    /* Read by type returns all attributes of the specified type,
       between the start and end handles */
    while (WICED_TRUE)
    {
        attr_handle = wiced_bt_gatt_find_handle_by_type(attr_handle,
        p_read_req->e_handle, &p_read_req->uuid);

        if (INVALID_ATT_HANDLE == attr_handle)
            break;

        switch(attr_handle)
        {
            case HDLC_GAP_DEVICE_NAME_VALUE:
                to_copy = app_gap_device_name_len;
                copy_from = (uint8_t *) app_gap_device_name;
                break;

            case HDLC_GAP_APPEARANCE_VALUE:
                to_copy = GAP_APPEARANCE_VAL;
                copy_from = (uint8_t *)&cy_bt_cfg_ble.appearance;
                break;

            default:
                printf("[%s] found type but no attribute ??\n", __FUNCTION__);
                wiced_bt_gatt_server_send_error_rsp(conn_id, opcode,
                p_read_req->s_handle, WICED_BT_GATT_ERR_UNLIKELY);
                wiced_bt_free_buffer(p_rsp);
                return WICED_BT_GATT_ERR_UNLIKELY;
        }

        int filled = wiced_bt_gatt_put_read_by_type_rsp_in_stream(
        p_rsp + used,len_requested - used,&pair_len,attr_handle,
        to_copy,copy_from);

        if (NOT_FILLED == filled) {
            break;
        }
        used += filled;

        /* Increment starting handle for next search to one past current */
        attr_handle++;
    }

    if (NOT_FILLED == used)
    {
        printf("[%s] attr not found 0x%04x -  0x%04x Type: 0x%04x\n",
        __FUNCTION__, p_read_req->s_handle, p_read_req->e_handle,
        p_read_req->uuid.uu.uuid16);

        wiced_bt_gatt_server_send_error_rsp(conn_id,opcode,p_read_req->s_handle,
        WICED_BT_GATT_INVALID_HANDLE);
        wiced_bt_free_buffer(p_rsp);
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    /* Send the response */
    wiced_bt_gatt_server_send_read_by_type_rsp(conn_id, opcode, pair_len,
    used, p_rsp, (wiced_bt_gatt_app_context_t)wiced_bt_free_buffer);

    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* Function Name: gatt_req_read_multi_handler
********************************************************************************
* Summary:
*  Process read multi request from peer device
*
* parameters:
*  conn id              : Connection ID
*  opcode               : LE GATT request type opcode
*  p_read_req           : Pointer to read request containing the handle
*                        to read
*  len_requested        : length of data requested
*
* Return:
*  wiced_bt_gatt_status_t: See possible status codes in
*                                 wiced_bt_gatt_status_e in wiced_bt_gatt.h
*
*******************************************************************************/
static wiced_bt_gatt_status_t gatt_req_read_multi_handler(
                                uint16_t conn_id,
                                wiced_bt_gatt_opcode_t opcode,
                                wiced_bt_gatt_read_multiple_req_t *p_read_req,
                                uint16_t len_requested )
{
    uint8_t     *p_rsp = wiced_bt_get_buffer(len_requested);
    int         used = INIT_FLAG_RESET;
    int         index;
    uint16_t    handle;
    int         to_copy;
    uint8_t     * copy_from;
    handle = wiced_bt_gatt_get_handle_from_stream(p_read_req->p_handle_stream,
            HANDLE_INDEX);
    if (NULL == p_rsp)
    {
        printf("[%s] no memory len_requested: %d!!\n", __FUNCTION__,
        len_requested);
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, handle,
        WICED_BT_GATT_INSUF_RESOURCE);
        return WICED_BT_GATT_INSUF_RESOURCE;
    }

    /* Read by type returns all attributes of the specified type,
       between the start and end handles */
    for (index = ITERATION_INIT_VAL; index < p_read_req->num_handles; index++)
    {
        handle=wiced_bt_gatt_get_handle_from_stream(p_read_req
                ->p_handle_stream, index);
        switch(handle)
        {
            case HDLC_GAP_DEVICE_NAME_VALUE:
                to_copy = app_gap_device_name_len;
                copy_from = (uint8_t *) app_gap_device_name;
                break;

            case HDLC_GAP_APPEARANCE_VALUE:
                to_copy = GAP_APPEARANCE_VAL;
                copy_from = 
                (uint8_t *) &cy_bt_cfg_ble.appearance;
                break;

            default:
                printf("[%s] no handle 0x%04x\n", __FUNCTION__, handle);
                wiced_bt_gatt_server_send_error_rsp(conn_id, opcode,
                *p_read_req->p_handle_stream, WICED_BT_GATT_ERR_UNLIKELY);
                wiced_bt_free_buffer(p_rsp);
                return WICED_BT_GATT_ERR_UNLIKELY;
        }
        int filled = wiced_bt_gatt_put_read_multi_rsp_in_stream(opcode,
                p_rsp + used, len_requested - used,handle,to_copy,copy_from);
        if (!filled) {
            break;
        }
        used += filled;
    }
    if (NOT_FILLED == used)
    {
        printf("[%s] no attr found", __FUNCTION__);
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode,
        *p_read_req->p_handle_stream, WICED_BT_GATT_INVALID_HANDLE);
        wiced_bt_free_buffer(p_rsp);
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    /* Send the response */
    wiced_bt_gatt_server_send_read_multiple_rsp(conn_id, opcode, used, p_rsp,
   (wiced_bt_gatt_app_context_t)wiced_bt_free_buffer);
    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* Function Name: gatt_req_mtu_handler
********************************************************************************
* Summary:
*  Process the request for mtu.
*
* Parameters:
*  conn_id: Connection ID of the GATT connection
*  mtu: mtu value
*
* Return:
*  wiced_result_t
*******************************************************************************/
static wiced_result_t gatt_req_mtu_handler( uint16_t conn_id, uint16_t mtu )
{
    printf("req_mtu: %d\n", mtu);
    wiced_bt_gatt_server_send_mtu_rsp(conn_id, mtu, cfg_mtu());
    return (wiced_result_t) WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* Function Name: gatt_req_conf_handler
********************************************************************************
* Summary:
*  Process indication confirm. The indication must be confirmed before
*  another indication can be sent.
*
* Parameters:
*  conn_id : Connection ID of the GATT connection
*  handle  : Connection handle value
*
* Return:
*  wiced_bt_gatt_status_t : GATT status
*******************************************************************************/
static wiced_bt_gatt_status_t gatt_req_conf_handler(uint16_t conn_id,
                                                    uint16_t handle)
{
    printf("gatt_req_conf_handler, conn 0x%04x hdl 0x%04x\n",
    conn_id, handle );
    link_set_indication_pending(false);
    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
* Function Name: app_bt_gatt_req_cb
********************************************************************************
* Summary:
* This function handles GATT server events from the BT stack.
*
* Parameters:
* p_req                 : Pointer to LE GATT connection status
*
* Return:
* wiced_bt_gatt_status_t: See possible status codes in
*                         wiced_bt_gatt_status_e in wiced_bt_gatt.h
*
*******************************************************************************/
static wiced_bt_gatt_status_t gatt_req_cb(wiced_bt_gatt_attribute_request_t
                                          *p_req )
{
    wiced_bt_gatt_status_t result  = WICED_BT_GATT_SUCCESS;
    printf("GATT request conn_id:0x%04x opcode:%d\n", p_req->conn_id,
            p_req->opcode );
    switch ( p_req->opcode )
    {
        case GATT_REQ_READ:
        case GATT_REQ_READ_BLOB:
            result = app_gatt_read_req_handler( p_req->conn_id,
            &p_req->data.read_req, p_req->opcode,
            p_req->len_requested);
            break;

        case GATT_REQ_READ_BY_TYPE:
            result = gatt_req_read_by_type_handler(p_req->conn_id,
            p_req->opcode,
            &p_req->data.read_by_type,
            p_req->len_requested);
            break;

        case GATT_REQ_READ_MULTI:
        case GATT_REQ_READ_MULTI_VAR_LENGTH:
            printf( "req_read_multi_handler\n" );
            result = gatt_req_read_multi_handler(p_req->conn_id,
            p_req->opcode,
            &p_req->data.read_multiple_req,
            p_req->len_requested);
            break;

        case GATT_REQ_WRITE:
        case GATT_CMD_WRITE:
        case GATT_CMD_SIGNED_WRITE:
            result = app_gatt_write_handler(p_req->conn_id,
                                            &p_req->data.write_req);
            if (result == WICED_BT_GATT_SUCCESS)
            {
                wiced_bt_gatt_server_send_write_rsp(p_req->conn_id,
                p_req->opcode,
                p_req->data.write_req.handle);
            }
            else
            {
                wiced_bt_gatt_server_send_error_rsp(p_req->conn_id,
                p_req->opcode,
                p_req->data.write_req.handle, result);
            }
            break;

        case GATT_REQ_EXECUTE_WRITE:
            result = WICED_SUCCESS;
            wiced_bt_gatt_server_send_execute_write_rsp(p_req->conn_id,
            p_req->opcode);
            break;

        case GATT_REQ_MTU:
            result = gatt_req_mtu_handler(p_req->conn_id,
            p_req->data.remote_mtu);
            break;

        case GATT_HANDLE_VALUE_CONF:
            result = gatt_req_conf_handler(p_req->conn_id,
            p_req->data.confirm.handle );
            break;

        case GATT_HANDLE_VALUE_NOTIF:
            break;

        default:
            printf("Unhandled GATT request x%x\n", p_req->opcode);
            break;
    }

    return result;
}

/*******************************************************************************
* Function Name: app_bt_gatt_event_callback
********************************************************************************
* Summary:
* This function handles GATT events from the BT stack.
*
* Parameters:
* wiced_bt_gatt_evt_t event                : LE GATT event code of one
*                                            byte length
* wiced_bt_gatt_event_data_t *p_data       : Pointer to LE GATT event
*                                            structures
*
* Return:
* wiced_bt_gatt_status_t                   : See possible status
*                                           codes in wiced_bt_gatt_status_e in
*                                           wiced_bt_gatt.h
*
*******************************************************************************/
static wiced_bt_gatt_status_t gatt_callback(wiced_bt_gatt_evt_t event,
                                            wiced_bt_gatt_event_data_t * p_data)
{
    wiced_bt_gatt_status_t result = WICED_BT_GATT_SUCCESS;
    switch(event)
    {
        case GATT_CONNECTION_STATUS_EVT:
            printf("GATT_CONNECTION_STATUS_EVT\n");
            result = gatt_conn_state_change(&p_data->connection_status);
            break;

        case GATT_OPERATION_CPLT_EVT:
            printf("GATT_OPERATION_CPLT_EVT\n");
            break;

        case GATT_DISCOVERY_CPLT_EVT:
            printf("GATT_DISCOVERY_CPLT_EVT\n");
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT:
            printf("GATT_ATTRIBUTE_REQUEST_EVT\n");
            result = gatt_req_cb(&p_data->attribute_request);
            break;

        case GATT_CONGESTION_EVT:
            printf("GATT_CONGESTION_EVT:%d\n",
                            p_data->congestion.congested);
            break;

        case GATT_GET_RESPONSE_BUFFER_EVT:
            p_data->buffer_request.buffer.p_app_rsp_buffer = 
            gatt_alloc_buffer (p_data->buffer_request.len_requested);
            p_data->buffer_request.buffer.p_app_ctxt=(void *) gatt_free_buffer;
            result = WICED_BT_GATT_SUCCESS;
            break;

        case GATT_APP_BUFFER_TRANSMITTED_EVT:
            {
                pfn_free_buffer_t pfn_free = 
                (pfn_free_buffer_t)p_data->buffer_xmitted.p_app_ctxt;

                /* If the buffer is dynamic, the context will point to 
                   a function to free it. */
                if (pfn_free)
                {
                    pfn_free(p_data->buffer_xmitted.p_app_data);
                }
                result = WICED_BT_GATT_SUCCESS;
            }
            break;

        default:
            printf("gatts_callback: unhandled event!!!:0x%x\n", event);
            break;
    }

    return result;
}

/*******************************************************************************
* Function Name: gatt_read_req_default_handler
********************************************************************************
* Summary:
*  Default handler to process read request or command from peer device.
*  The event calls application gatt_read_req_handler first. When it is not
*  handled in application, this default handler is called.
*
* Parameters:
*  uint16_t conn_id
*  wiced_bt_gatt_read_t *p_req
*  wiced_bt_gatt_opcode_t opcode
*  uint16_t len_requested
*
* Return:
*  wiced_bt_gatt_status_t
*******************************************************************************/
wiced_bt_gatt_status_t gatt_read_req_default_handler(uint16_t conn_id,
                           wiced_bt_gatt_read_t *p_req,
                           wiced_bt_gatt_opcode_t opcode,
                           uint16_t len_requested)
{
    const gatt_db_lookup_table_t * p_attribute;
    wiced_bt_gatt_status_t result = WICED_BT_GATT_ERROR;
    uint8_t * from;

    /* In case if p_req is NULL, we don't want to crash system calling
       wiced_bt_gatt_server_send_error_rsp() for p_req->handle */
    uint16_t handle = INIT_ATT_HANDLE;
    int to_copy = len_requested;
    if(p_req)
    {
        handle = p_req->handle;
        printf("read_attrib - conn:0x%04x hdl:0x%04x ofst:%d len:%d\n",
        conn_id, handle, p_req->offset, len_requested );
        result = WICED_BT_GATT_INVALID_HANDLE;
        p_attribute = wiced_bt_util_get_attribute(app_gatt_db_ext_attr_tbl,
                handle);
        if(p_attribute)
        {
            from = p_attribute->p_data + p_req->offset;
            result = WICED_BT_GATT_INVALID_OFFSET;
            if (p_req->offset < p_attribute->max_len)
            {
                result = WICED_BT_GATT_SUCCESS;
                /* copy len is over limit */
                if (to_copy > (p_attribute->max_len - p_req->offset))
                {
                    to_copy = p_attribute->max_len - p_req->offset;
                    printf("read length:%d\n", to_copy );
                }
            }
        }
    }

    if (result == WICED_BT_GATT_SUCCESS)
    {
        wiced_bt_gatt_server_send_read_handle_rsp(conn_id, opcode, to_copy,
        from, NULL);
    }
    else
    {
        printf("read failed, reason %d", result );
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, handle, result);
    }

    return result;
}

/*******************************************************************************
* Function Name: gatt_write_default_handler
********************************************************************************
* Summary:
*  Default handler to process write request or command from peer device.
*  The event calls application gatt_write_handler first. When it is not
*  handled in application, this default handler is called.
*
* Parameters:
*  uint16_t conn_id
*  wiced_bt_gatt_write_req_t * p_wr_data
*
* Return:
*  wiced_bt_gatt_status_t
*******************************************************************************/
wiced_bt_gatt_status_t gatt_write_default_handler(uint16_t conn_id,
                           wiced_bt_gatt_write_req_t * p_wr_data )
{
    if(link_conn_id() != conn_id)
    {
        printf("gatt: write handle to an invalid conn_id:%04x\n",conn_id);
        return WICED_BT_GATT_ERROR;
    }
    else
    {
        const gatt_db_lookup_table_t * p_attribute =
        wiced_bt_util_get_attribute(app_gatt_db_ext_attr_tbl,
        p_wr_data->handle);
        wiced_bt_gatt_status_t result = WICED_BT_GATT_SUCCESS;

        printf("write_attrib - conn:0x%04x hdl:0x%04x off:%d len:%d",
            conn_id, p_wr_data->handle, p_wr_data->offset, p_wr_data->val_len );

        if(p_attribute)
        {
            if(p_wr_data->offset > p_attribute->max_len)
            {
                printf("Invalid offset, max_len=%d, ofst:%d\n",
                p_attribute->max_len, p_wr_data->offset);
                result = WICED_BT_GATT_INVALID_OFFSET;
            }
            else if((p_wr_data->val_len + p_wr_data->offset) >
                     p_attribute->max_len)
            {
                printf("Invalid len\n");
                result = WICED_BT_GATT_INVALID_ATTR_LEN;
            }
            else
            {
                printf("write_attrib - success\n");

                /* Copy the data */
                memcpy(p_attribute->p_data + p_wr_data->offset, p_wr_data->p_val,
                        p_wr_data->val_len);
            }
        }
        else
        {
            result = WICED_BT_GATT_INVALID_HANDLE;
        }

        return result;
    }
}

/*******************************************************************************
* Function Name: gatt_initialize
********************************************************************************
* Summary:
*  Initialize gatt database.
*  The advertisement data CY_BT_ADV_PACKET_DATA_SIZ and cy_bt_adv_packet_data
*  are generated by BT Configurator in cycfg_gap.h/c.
*  The gatt database gatt_database, and gatt_database_len are generated by
*  BT Configurator in cycfg_gatt_db.h/c.
*
* Parameters:
*  None
*
* Return:
*  wiced_bt_gatt_status_t
*******************************************************************************/
wiced_bt_gatt_status_t gatt_initialize(void)
{
    printf("[%s]\n", __FUNCTION__);
    wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_DATA_SIZE,
    cy_bt_adv_packet_data);

    /* Register with stack to receive GATT callback */
    wiced_bt_gatt_register( gatt_callback );
    return wiced_bt_gatt_db_init( gatt_database, gatt_database_len, NULL );
}

/*******************************************************************************
* Function Name: wiced_bt_util_get_attribute
********************************************************************************
* Summary:
*  Get attribute data from the look up table.
*
* Parameters:
*  gatt_db_lookup_table_t * p_attribute : Attribute look up table
*  uint16_t handle                      : The handle of the attribute.
*
* Return:
*  A pointer points to the attribute data. It returns NULL if the handle is
*  invalid.
*******************************************************************************/
const gatt_db_lookup_table_t * wiced_bt_util_get_attribute
     (gatt_db_lookup_table_t * p_attribute, uint16_t handle)
{
    uint16_t limit = app_gatt_db_ext_attr_tbl_size;

    while(limit--)
    {
        if(p_attribute->handle == handle)
        {
            return p_attribute;
        }
        p_attribute++;
    }
    
    printf("Requested attribute 0x%04x not found!!!", handle);
    return NULL;
}

/* end of file */
