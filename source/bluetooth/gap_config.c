/**
 * @file gap_config.c
 * @brief GAP (Generic Access Profile) Configuration Implementation
 *
 * Provides GAP advertising, scanning, and connection management.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gap_config.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Advertising set state */
typedef enum {
    ADV_SET_STATE_IDLE,
    ADV_SET_STATE_CONFIGURED,
    ADV_SET_STATE_ADVERTISING,
    ADV_SET_STATE_PERIODIC_CONFIGURED,
    ADV_SET_STATE_PERIODIC_ADVERTISING
} adv_set_state_t;

/** Advertising set context */
typedef struct {
    bool in_use;
    uint8_t handle;
    adv_set_state_t state;
    gap_ext_adv_params_t params;
    gap_periodic_adv_params_t periodic_params;
    uint8_t adv_data[GAP_MAX_EXT_ADV_DATA_LEN];
    uint16_t adv_data_len;
    uint8_t scan_rsp_data[GAP_MAX_EXT_ADV_DATA_LEN];
    uint16_t scan_rsp_data_len;
    uint8_t periodic_data[GAP_MAX_PERIODIC_ADV_DATA_LEN];
    uint16_t periodic_data_len;
} adv_set_t;

/** Scan state */
typedef enum {
    SCAN_STATE_IDLE,
    SCAN_STATE_SCANNING,
    SCAN_STATE_EXT_SCANNING
} scan_state_t;

/** Periodic sync context */
typedef struct {
    bool in_use;
    uint16_t sync_handle;
    uint8_t adv_sid;
    gap_address_t address;
} periodic_sync_t;

/** GAP module context */
typedef struct {
    bool initialized;

    /* Device identity */
    char device_name[GAP_MAX_DEVICE_NAME_LEN + 1];
    uint16_t appearance;
    gap_address_t local_address;
    uint8_t random_address[6];
    bool random_address_set;

    /* Legacy advertising */
    gap_adv_params_t legacy_adv_params;
    uint8_t legacy_adv_data[GAP_MAX_ADV_DATA_LEN];
    uint8_t legacy_adv_data_len;
    uint8_t legacy_scan_rsp[GAP_MAX_SCAN_RSP_DATA_LEN];
    uint8_t legacy_scan_rsp_len;
    bool legacy_adv_active;

    /* Extended advertising sets */
    adv_set_t adv_sets[GAP_MAX_ADV_SETS];

    /* Scanning */
    scan_state_t scan_state;
    gap_scan_params_t scan_params;
    gap_ext_scan_params_t ext_scan_params;

    /* Periodic sync */
    periodic_sync_t periodic_syncs[GAP_MAX_ADV_SETS];

    /* Callback */
    gap_callback_t callback;
    void *callback_user_data;
} gap_context_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static gap_context_t gap_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static adv_set_t* find_adv_set(uint8_t handle);
static adv_set_t* alloc_adv_set(uint8_t handle);
static void free_adv_set(adv_set_t *set);
static periodic_sync_t* find_periodic_sync(uint16_t sync_handle);
static periodic_sync_t* alloc_periodic_sync(void);
static void notify_event(gap_event_type_t type, void *data);

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

int gap_init(void)
{
    if (gap_ctx.initialized) {
        return GAP_ERROR_ALREADY_INITIALIZED;
    }

    memset(&gap_ctx, 0, sizeof(gap_ctx));

    /* Set default device name */
    strncpy(gap_ctx.device_name, "LE Audio Device", GAP_MAX_DEVICE_NAME_LEN);

    /* Set default appearance */
    gap_ctx.appearance = GAP_APPEARANCE_GENERIC_AUDIO_SOURCE;

    /* Initialize advertising sets */
    for (int i = 0; i < GAP_MAX_ADV_SETS; i++) {
        gap_ctx.adv_sets[i].in_use = false;
        gap_ctx.adv_sets[i].handle = i;
    }

    /*
     * TODO: Get local address from controller
     *
     * hci_read_bd_addr(&gap_ctx.local_address.addr);
     * gap_ctx.local_address.type = GAP_ADDR_TYPE_PUBLIC;
     */

    gap_ctx.initialized = true;

    return GAP_OK;
}

void gap_deinit(void)
{
    if (!gap_ctx.initialized) {
        return;
    }

    /* Stop all advertising */
    if (gap_ctx.legacy_adv_active) {
        gap_stop_advertising();
    }

    for (int i = 0; i < GAP_MAX_ADV_SETS; i++) {
        if (gap_ctx.adv_sets[i].in_use) {
            if (gap_ctx.adv_sets[i].state == ADV_SET_STATE_PERIODIC_ADVERTISING) {
                gap_stop_periodic_advertising(i);
            }
            if (gap_ctx.adv_sets[i].state == ADV_SET_STATE_ADVERTISING) {
                gap_stop_ext_advertising(i);
            }
        }
    }

    /* Stop scanning */
    if (gap_ctx.scan_state != SCAN_STATE_IDLE) {
        gap_stop_scanning();
    }

    gap_ctx.initialized = false;
}

void gap_register_callback(gap_callback_t callback, void *user_data)
{
    gap_ctx.callback = callback;
    gap_ctx.callback_user_data = user_data;
}

/*******************************************************************************
 * API Functions - Device Identity
 ******************************************************************************/

int gap_set_device_name(const char *name)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (name == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    strncpy(gap_ctx.device_name, name, GAP_MAX_DEVICE_NAME_LEN);
    gap_ctx.device_name[GAP_MAX_DEVICE_NAME_LEN] = '\0';

    /*
     * TODO: Update GATT device name characteristic
     * gatt_update_device_name(gap_ctx.device_name);
     */

    return GAP_OK;
}

int gap_get_device_name(char *name, uint16_t max_len)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (name == NULL || max_len == 0) {
        return GAP_ERROR_INVALID_PARAM;
    }

    strncpy(name, gap_ctx.device_name, max_len - 1);
    name[max_len - 1] = '\0';

    return GAP_OK;
}

int gap_set_appearance(uint16_t appearance)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    gap_ctx.appearance = appearance;

    return GAP_OK;
}

uint16_t gap_get_appearance(void)
{
    return gap_ctx.appearance;
}

int gap_get_local_address(gap_address_t *address)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (address == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    memcpy(address, &gap_ctx.local_address, sizeof(gap_address_t));

    return GAP_OK;
}

int gap_set_random_address(const uint8_t *address)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (address == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    memcpy(gap_ctx.random_address, address, 6);
    gap_ctx.random_address_set = true;

    /*
     * TODO: Set random address via HCI
     * hci_le_set_random_address(address);
     */

    return GAP_OK;
}

/*******************************************************************************
 * API Functions - Legacy Advertising
 ******************************************************************************/

int gap_set_adv_params(const gap_adv_params_t *params)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (gap_ctx.legacy_adv_active) {
        return GAP_ERROR_BUSY;
    }

    memcpy(&gap_ctx.legacy_adv_params, params, sizeof(gap_adv_params_t));

    /*
     * TODO: Set advertising parameters via HCI
     *
     * hci_le_set_advertising_parameters(
     *     params->adv_interval_min,
     *     params->adv_interval_max,
     *     params->adv_type,
     *     params->own_addr_type,
     *     params->peer_addr.type,
     *     params->peer_addr.addr,
     *     params->adv_channel_map,
     *     params->filter_policy
     * );
     */

    return GAP_OK;
}

int gap_set_adv_data(const uint8_t *data, uint8_t len)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL && len > 0) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (len > GAP_MAX_ADV_DATA_LEN) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (len > 0) {
        memcpy(gap_ctx.legacy_adv_data, data, len);
    }
    gap_ctx.legacy_adv_data_len = len;

    /*
     * TODO: Set advertising data via HCI
     * hci_le_set_advertising_data(len, data);
     */

    return GAP_OK;
}

int gap_set_scan_rsp_data(const uint8_t *data, uint8_t len)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL && len > 0) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (len > GAP_MAX_SCAN_RSP_DATA_LEN) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (len > 0) {
        memcpy(gap_ctx.legacy_scan_rsp, data, len);
    }
    gap_ctx.legacy_scan_rsp_len = len;

    /*
     * TODO: Set scan response data via HCI
     * hci_le_set_scan_response_data(len, data);
     */

    return GAP_OK;
}

int gap_start_advertising(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (gap_ctx.legacy_adv_active) {
        return GAP_ERROR_BUSY;
    }

    /*
     * TODO: Enable advertising via HCI
     * hci_le_set_advertise_enable(1);
     */

    gap_ctx.legacy_adv_active = true;

    /* Notify callback */
    uint8_t handle = GAP_ADV_HANDLE_LEGACY;
    notify_event(GAP_EVENT_ADV_STARTED, &handle);

    return GAP_OK;
}

int gap_stop_advertising(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (!gap_ctx.legacy_adv_active) {
        return GAP_OK;
    }

    /*
     * TODO: Disable advertising via HCI
     * hci_le_set_advertise_enable(0);
     */

    gap_ctx.legacy_adv_active = false;

    /* Notify callback */
    uint8_t handle = GAP_ADV_HANDLE_LEGACY;
    notify_event(GAP_EVENT_ADV_STOPPED, &handle);

    return GAP_OK;
}

/*******************************************************************************
 * API Functions - Extended Advertising
 ******************************************************************************/

int gap_create_ext_adv_set(const gap_ext_adv_params_t *params)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    /* Check if handle already exists */
    set = find_adv_set(params->adv_handle);
    if (set != NULL) {
        return GAP_ERROR_BUSY;
    }

    /* Allocate new set */
    set = alloc_adv_set(params->adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NO_RESOURCES;
    }

    memcpy(&set->params, params, sizeof(gap_ext_adv_params_t));
    set->state = ADV_SET_STATE_CONFIGURED;

    /*
     * TODO: Create extended advertising set via HCI
     *
     * hci_le_set_extended_advertising_parameters(
     *     params->adv_handle,
     *     params->adv_event_properties,
     *     params->primary_adv_interval_min,
     *     params->primary_adv_interval_max,
     *     params->primary_adv_channel_map,
     *     params->own_addr_type,
     *     params->peer_addr.type,
     *     params->peer_addr.addr,
     *     params->filter_policy,
     *     params->adv_tx_power,
     *     params->primary_adv_phy,
     *     params->secondary_adv_max_skip,
     *     params->secondary_adv_phy,
     *     params->adv_sid,
     *     params->scan_req_notify_enable
     * );
     */

    return GAP_OK;
}

int gap_remove_ext_adv_set(uint8_t adv_handle)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    set = find_adv_set(adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    if (set->state == ADV_SET_STATE_ADVERTISING ||
        set->state == ADV_SET_STATE_PERIODIC_ADVERTISING) {
        return GAP_ERROR_BUSY;
    }

    /*
     * TODO: Remove extended advertising set via HCI
     * hci_le_remove_advertising_set(adv_handle);
     */

    free_adv_set(set);

    return GAP_OK;
}

int gap_set_ext_adv_params(const gap_ext_adv_params_t *params)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    set = find_adv_set(params->adv_handle);
    if (set == NULL) {
        /* Create new set */
        return gap_create_ext_adv_set(params);
    }

    if (set->state == ADV_SET_STATE_ADVERTISING) {
        return GAP_ERROR_BUSY;
    }

    memcpy(&set->params, params, sizeof(gap_ext_adv_params_t));

    /*
     * TODO: Update extended advertising parameters via HCI
     */

    return GAP_OK;
}

int gap_set_ext_adv_data(uint8_t adv_handle, const uint8_t *data, uint16_t len)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL && len > 0) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (len > GAP_MAX_EXT_ADV_DATA_LEN) {
        return GAP_ERROR_INVALID_PARAM;
    }

    set = find_adv_set(adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    if (len > 0) {
        memcpy(set->adv_data, data, len);
    }
    set->adv_data_len = len;

    /*
     * TODO: Set extended advertising data via HCI
     *
     * For data > 251 bytes, fragment across multiple HCI commands:
     * hci_le_set_extended_advertising_data(
     *     adv_handle,
     *     operation,  // 0=intermediate, 1=first, 2=last, 3=complete
     *     fragment_preference,
     *     fragment_data,
     *     fragment_len
     * );
     */

    return GAP_OK;
}

int gap_set_ext_scan_rsp_data(uint8_t adv_handle, const uint8_t *data, uint16_t len)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL && len > 0) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (len > GAP_MAX_EXT_ADV_DATA_LEN) {
        return GAP_ERROR_INVALID_PARAM;
    }

    set = find_adv_set(adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    if (len > 0) {
        memcpy(set->scan_rsp_data, data, len);
    }
    set->scan_rsp_data_len = len;

    /*
     * TODO: Set extended scan response data via HCI
     * hci_le_set_extended_scan_response_data(...);
     */

    return GAP_OK;
}

int gap_start_ext_advertising(uint8_t adv_handle, uint16_t duration, uint8_t max_events)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    set = find_adv_set(adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    if (set->state == ADV_SET_STATE_ADVERTISING) {
        return GAP_ERROR_BUSY;
    }

    /*
     * TODO: Enable extended advertising via HCI
     *
     * hci_le_set_extended_advertising_enable(
     *     1,  // enable
     *     1,  // num_sets
     *     &adv_handle,
     *     &duration,
     *     &max_events
     * );
     */

    (void)duration;
    (void)max_events;

    set->state = ADV_SET_STATE_ADVERTISING;

    notify_event(GAP_EVENT_ADV_STARTED, &adv_handle);

    return GAP_OK;
}

int gap_stop_ext_advertising(uint8_t adv_handle)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    set = find_adv_set(adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    if (set->state != ADV_SET_STATE_ADVERTISING) {
        return GAP_OK;
    }

    /*
     * TODO: Disable extended advertising via HCI
     *
     * uint16_t zero_duration = 0;
     * uint8_t zero_events = 0;
     * hci_le_set_extended_advertising_enable(
     *     0,  // disable
     *     1,
     *     &adv_handle,
     *     &zero_duration,
     *     &zero_events
     * );
     */

    set->state = ADV_SET_STATE_CONFIGURED;

    notify_event(GAP_EVENT_ADV_STOPPED, &adv_handle);

    return GAP_OK;
}

/*******************************************************************************
 * API Functions - Periodic Advertising (for Auracast)
 ******************************************************************************/

int gap_set_periodic_adv_params(const gap_periodic_adv_params_t *params)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    set = find_adv_set(params->adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    memcpy(&set->periodic_params, params, sizeof(gap_periodic_adv_params_t));

    /*
     * TODO: Set periodic advertising parameters via HCI
     *
     * hci_le_set_periodic_advertising_parameters(
     *     params->adv_handle,
     *     params->periodic_adv_interval_min,
     *     params->periodic_adv_interval_max,
     *     params->periodic_adv_properties
     * );
     */

    if (set->state == ADV_SET_STATE_CONFIGURED) {
        set->state = ADV_SET_STATE_PERIODIC_CONFIGURED;
    }

    return GAP_OK;
}

int gap_set_periodic_adv_data(uint8_t adv_handle, const uint8_t *data, uint16_t len)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL && len > 0) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (len > GAP_MAX_PERIODIC_ADV_DATA_LEN) {
        return GAP_ERROR_INVALID_PARAM;
    }

    set = find_adv_set(adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    if (len > 0) {
        memcpy(set->periodic_data, data, len);
    }
    set->periodic_data_len = len;

    /*
     * TODO: Set periodic advertising data via HCI
     *
     * For Auracast, this contains the BASE (Broadcast Audio Source Endpoint)
     * structure with codec configuration, BIS info, etc.
     *
     * hci_le_set_periodic_advertising_data(
     *     adv_handle,
     *     operation,
     *     data,
     *     len
     * );
     */

    return GAP_OK;
}

int gap_start_periodic_advertising(uint8_t adv_handle)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    set = find_adv_set(adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    if (set->state == ADV_SET_STATE_PERIODIC_ADVERTISING) {
        return GAP_ERROR_BUSY;
    }

    /* Extended advertising must be running first */
    if (set->state != ADV_SET_STATE_ADVERTISING &&
        set->state != ADV_SET_STATE_PERIODIC_CONFIGURED) {
        return GAP_ERROR_INVALID_STATE;
    }

    /*
     * TODO: Enable periodic advertising via HCI
     *
     * hci_le_set_periodic_advertising_enable(1, adv_handle);
     */

    set->state = ADV_SET_STATE_PERIODIC_ADVERTISING;

    return GAP_OK;
}

int gap_stop_periodic_advertising(uint8_t adv_handle)
{
    adv_set_t *set;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    set = find_adv_set(adv_handle);
    if (set == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    if (set->state != ADV_SET_STATE_PERIODIC_ADVERTISING) {
        return GAP_OK;
    }

    /*
     * TODO: Disable periodic advertising via HCI
     * hci_le_set_periodic_advertising_enable(0, adv_handle);
     */

    set->state = ADV_SET_STATE_ADVERTISING;

    return GAP_OK;
}

/*******************************************************************************
 * API Functions - Periodic Advertising Sync
 ******************************************************************************/

int gap_periodic_adv_create_sync(uint8_t adv_sid, const gap_address_t *address,
                                  uint16_t skip, uint16_t sync_timeout)
{
    periodic_sync_t *sync;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (address == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    sync = alloc_periodic_sync();
    if (sync == NULL) {
        return GAP_ERROR_NO_RESOURCES;
    }

    sync->adv_sid = adv_sid;
    memcpy(&sync->address, address, sizeof(gap_address_t));

    /*
     * TODO: Create periodic advertising sync via HCI
     *
     * hci_le_periodic_advertising_create_sync(
     *     0,  // options
     *     adv_sid,
     *     address->type,
     *     address->addr,
     *     skip,
     *     sync_timeout,
     *     0   // sync_cte_type
     * );
     */

    (void)skip;
    (void)sync_timeout;

    return GAP_OK;
}

int gap_periodic_adv_cancel_sync(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    /*
     * TODO: Cancel pending sync via HCI
     * hci_le_periodic_advertising_create_sync_cancel();
     */

    return GAP_OK;
}

int gap_periodic_adv_terminate_sync(uint16_t sync_handle)
{
    periodic_sync_t *sync;

    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    sync = find_periodic_sync(sync_handle);
    if (sync == NULL) {
        return GAP_ERROR_NOT_FOUND;
    }

    /*
     * TODO: Terminate sync via HCI
     * hci_le_periodic_advertising_terminate_sync(sync_handle);
     */

    sync->in_use = false;

    return GAP_OK;
}

/*******************************************************************************
 * API Functions - Scanning
 ******************************************************************************/

int gap_set_scan_params(const gap_scan_params_t *params)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (gap_ctx.scan_state != SCAN_STATE_IDLE) {
        return GAP_ERROR_BUSY;
    }

    memcpy(&gap_ctx.scan_params, params, sizeof(gap_scan_params_t));

    /*
     * TODO: Set scan parameters via HCI
     *
     * hci_le_set_scan_parameters(
     *     params->scan_type,
     *     params->scan_interval,
     *     params->scan_window,
     *     params->own_addr_type,
     *     params->filter_policy
     * );
     */

    return GAP_OK;
}

int gap_start_scanning(gap_scan_dup_filter_t filter_duplicates)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (gap_ctx.scan_state != SCAN_STATE_IDLE) {
        return GAP_ERROR_BUSY;
    }

    /*
     * TODO: Enable scanning via HCI
     * hci_le_set_scan_enable(1, filter_duplicates);
     */

    (void)filter_duplicates;

    gap_ctx.scan_state = SCAN_STATE_SCANNING;

    notify_event(GAP_EVENT_SCAN_STARTED, NULL);

    return GAP_OK;
}

int gap_stop_scanning(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (gap_ctx.scan_state == SCAN_STATE_IDLE) {
        return GAP_OK;
    }

    /*
     * TODO: Disable scanning via HCI
     * hci_le_set_scan_enable(0, 0);
     */

    gap_ctx.scan_state = SCAN_STATE_IDLE;

    notify_event(GAP_EVENT_SCAN_STOPPED, NULL);

    return GAP_OK;
}

int gap_set_ext_scan_params(const gap_ext_scan_params_t *params)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (gap_ctx.scan_state != SCAN_STATE_IDLE) {
        return GAP_ERROR_BUSY;
    }

    memcpy(&gap_ctx.ext_scan_params, params, sizeof(gap_ext_scan_params_t));

    /*
     * TODO: Set extended scan parameters via HCI
     *
     * hci_le_set_extended_scan_parameters(
     *     params->own_addr_type,
     *     params->filter_policy,
     *     params->scanning_phys,
     *     ... per-PHY parameters
     * );
     */

    return GAP_OK;
}

int gap_start_ext_scanning(gap_scan_dup_filter_t filter_duplicates,
                            uint16_t duration, uint16_t period)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (gap_ctx.scan_state != SCAN_STATE_IDLE) {
        return GAP_ERROR_BUSY;
    }

    /*
     * TODO: Enable extended scanning via HCI
     *
     * hci_le_set_extended_scan_enable(
     *     1,  // enable
     *     filter_duplicates,
     *     duration,
     *     period
     * );
     */

    (void)filter_duplicates;
    (void)duration;
    (void)period;

    gap_ctx.scan_state = SCAN_STATE_EXT_SCANNING;

    notify_event(GAP_EVENT_SCAN_STARTED, NULL);

    return GAP_OK;
}

int gap_stop_ext_scanning(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (gap_ctx.scan_state != SCAN_STATE_EXT_SCANNING) {
        return GAP_OK;
    }

    /*
     * TODO: Disable extended scanning via HCI
     * hci_le_set_extended_scan_enable(0, 0, 0, 0);
     */

    gap_ctx.scan_state = SCAN_STATE_IDLE;

    notify_event(GAP_EVENT_SCAN_STOPPED, NULL);

    return GAP_OK;
}

/*******************************************************************************
 * API Functions - Connection Management
 ******************************************************************************/

int gap_connect(const gap_address_t *peer_addr, const gap_conn_params_t *params)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (peer_addr == NULL || params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    /*
     * TODO: Create connection via HCI
     *
     * hci_le_create_connection(
     *     scan_interval,
     *     scan_window,
     *     initiator_filter_policy,
     *     peer_addr->type,
     *     peer_addr->addr,
     *     own_addr_type,
     *     params->conn_interval_min,
     *     params->conn_interval_max,
     *     params->conn_latency,
     *     params->supervision_timeout,
     *     params->min_ce_length,
     *     params->max_ce_length
     * );
     */

    return GAP_OK;
}

int gap_cancel_connect(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    /*
     * TODO: Cancel connection via HCI
     * hci_le_create_connection_cancel();
     */

    return GAP_OK;
}

int gap_disconnect(uint16_t conn_handle, uint8_t reason)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    /*
     * TODO: Disconnect via HCI
     * hci_disconnect(conn_handle, reason);
     */

    (void)conn_handle;
    (void)reason;

    return GAP_OK;
}

int gap_update_conn_params(uint16_t conn_handle, const gap_conn_params_t *params)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    /*
     * TODO: Update connection parameters via HCI
     *
     * hci_le_connection_update(
     *     conn_handle,
     *     params->conn_interval_min,
     *     params->conn_interval_max,
     *     params->conn_latency,
     *     params->supervision_timeout,
     *     params->min_ce_length,
     *     params->max_ce_length
     * );
     */

    return GAP_OK;
}

int gap_request_conn_param_update(uint16_t conn_handle, const gap_conn_params_t *params)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (params == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    /*
     * TODO: Request connection parameter update via L2CAP
     * This is used in peripheral role when we can't directly update params.
     *
     * l2cap_connection_parameter_update_request(
     *     conn_handle,
     *     params->conn_interval_min,
     *     params->conn_interval_max,
     *     params->conn_latency,
     *     params->supervision_timeout
     * );
     */

    return GAP_OK;
}

/*******************************************************************************
 * API Functions - Advertising Data Builder
 ******************************************************************************/

void gap_adv_data_builder_init(gap_adv_data_builder_t *builder, uint16_t max_length)
{
    if (builder == NULL) {
        return;
    }

    memset(builder, 0, sizeof(gap_adv_data_builder_t));
    builder->max_length = (max_length > GAP_MAX_EXT_ADV_DATA_LEN) ?
                          GAP_MAX_EXT_ADV_DATA_LEN : max_length;
}

void gap_adv_data_builder_clear(gap_adv_data_builder_t *builder)
{
    if (builder == NULL) {
        return;
    }

    builder->length = 0;
    memset(builder->data, 0, sizeof(builder->data));
}

int gap_adv_data_add_flags(gap_adv_data_builder_t *builder, uint8_t flags)
{
    if (builder == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    /* Flags: len=2, type=0x01, value=flags */
    if (builder->length + 3 > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = 2;  /* Length */
    builder->data[builder->length++] = GAP_AD_TYPE_FLAGS;
    builder->data[builder->length++] = flags;

    return GAP_OK;
}

int gap_adv_data_add_name(gap_adv_data_builder_t *builder, const char *name)
{
    uint8_t name_len;

    if (builder == NULL || name == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    name_len = (uint8_t)strlen(name);
    if (builder->length + 2 + name_len > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = name_len + 1;  /* Length */
    builder->data[builder->length++] = GAP_AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&builder->data[builder->length], name, name_len);
    builder->length += name_len;

    return GAP_OK;
}

int gap_adv_data_add_short_name(gap_adv_data_builder_t *builder, const char *name)
{
    uint8_t name_len;

    if (builder == NULL || name == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    name_len = (uint8_t)strlen(name);
    if (builder->length + 2 + name_len > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = name_len + 1;
    builder->data[builder->length++] = GAP_AD_TYPE_SHORTENED_LOCAL_NAME;
    memcpy(&builder->data[builder->length], name, name_len);
    builder->length += name_len;

    return GAP_OK;
}

int gap_adv_data_add_uuid16(gap_adv_data_builder_t *builder, const uint16_t *uuids,
                             uint8_t count, bool complete)
{
    uint8_t data_len;

    if (builder == NULL || uuids == NULL || count == 0) {
        return GAP_ERROR_INVALID_PARAM;
    }

    data_len = count * 2;
    if (builder->length + 2 + data_len > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = data_len + 1;
    builder->data[builder->length++] = complete ?
        GAP_AD_TYPE_COMPLETE_16BIT_UUIDS : GAP_AD_TYPE_INCOMPLETE_16BIT_UUIDS;

    for (int i = 0; i < count; i++) {
        builder->data[builder->length++] = uuids[i] & 0xFF;
        builder->data[builder->length++] = (uuids[i] >> 8) & 0xFF;
    }

    return GAP_OK;
}

int gap_adv_data_add_service_data_16(gap_adv_data_builder_t *builder, uint16_t uuid,
                                      const uint8_t *data, uint8_t len)
{
    if (builder == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (builder->length + 4 + len > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = len + 3;  /* Length: type + UUID + data */
    builder->data[builder->length++] = GAP_AD_TYPE_SERVICE_DATA_16BIT;
    builder->data[builder->length++] = uuid & 0xFF;
    builder->data[builder->length++] = (uuid >> 8) & 0xFF;

    if (data != NULL && len > 0) {
        memcpy(&builder->data[builder->length], data, len);
        builder->length += len;
    }

    return GAP_OK;
}

int gap_adv_data_add_appearance(gap_adv_data_builder_t *builder, uint16_t appearance)
{
    if (builder == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (builder->length + 4 > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = 3;
    builder->data[builder->length++] = GAP_AD_TYPE_APPEARANCE;
    builder->data[builder->length++] = appearance & 0xFF;
    builder->data[builder->length++] = (appearance >> 8) & 0xFF;

    return GAP_OK;
}

int gap_adv_data_add_tx_power(gap_adv_data_builder_t *builder, int8_t tx_power)
{
    if (builder == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (builder->length + 3 > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = 2;
    builder->data[builder->length++] = GAP_AD_TYPE_TX_POWER_LEVEL;
    builder->data[builder->length++] = (uint8_t)tx_power;

    return GAP_OK;
}

int gap_adv_data_add_manufacturer_data(gap_adv_data_builder_t *builder,
                                        uint16_t company_id,
                                        const uint8_t *data, uint8_t len)
{
    if (builder == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (builder->length + 4 + len > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = len + 3;
    builder->data[builder->length++] = GAP_AD_TYPE_MANUFACTURER_DATA;
    builder->data[builder->length++] = company_id & 0xFF;
    builder->data[builder->length++] = (company_id >> 8) & 0xFF;

    if (data != NULL && len > 0) {
        memcpy(&builder->data[builder->length], data, len);
        builder->length += len;
    }

    return GAP_OK;
}

int gap_adv_data_add_broadcast_name(gap_adv_data_builder_t *builder, const char *name)
{
    uint8_t name_len;

    if (builder == NULL || name == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    name_len = (uint8_t)strlen(name);
    if (builder->length + 2 + name_len > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = name_len + 1;
    builder->data[builder->length++] = GAP_AD_TYPE_BROADCAST_NAME;
    memcpy(&builder->data[builder->length], name, name_len);
    builder->length += name_len;

    return GAP_OK;
}

int gap_adv_data_add_raw(gap_adv_data_builder_t *builder, uint8_t ad_type,
                          const uint8_t *data, uint8_t len)
{
    if (builder == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    if (builder->length + 2 + len > builder->max_length) {
        return GAP_ERROR_NO_RESOURCES;
    }

    builder->data[builder->length++] = len + 1;
    builder->data[builder->length++] = ad_type;

    if (data != NULL && len > 0) {
        memcpy(&builder->data[builder->length], data, len);
        builder->length += len;
    }

    return GAP_OK;
}

void gap_adv_data_get(const gap_adv_data_builder_t *builder,
                       const uint8_t **data, uint16_t *len)
{
    if (builder == NULL || data == NULL || len == NULL) {
        return;
    }

    *data = builder->data;
    *len = builder->length;
}

/*******************************************************************************
 * API Functions - Whitelist Management
 ******************************************************************************/

int gap_whitelist_clear(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    /*
     * TODO: Clear whitelist via HCI
     * hci_le_clear_white_list();
     */

    return GAP_OK;
}

int gap_whitelist_add(const gap_address_t *address)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (address == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    /*
     * TODO: Add to whitelist via HCI
     * hci_le_add_device_to_white_list(address->type, address->addr);
     */

    return GAP_OK;
}

int gap_whitelist_remove(const gap_address_t *address)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    if (address == NULL) {
        return GAP_ERROR_INVALID_PARAM;
    }

    /*
     * TODO: Remove from whitelist via HCI
     * hci_le_remove_device_from_white_list(address->type, address->addr);
     */

    return GAP_OK;
}

int gap_whitelist_get_size(void)
{
    /*
     * TODO: Read whitelist size from controller
     * return hci_le_read_white_list_size();
     */

    return 8;  /* Typical default */
}

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

static adv_set_t* find_adv_set(uint8_t handle)
{
    for (int i = 0; i < GAP_MAX_ADV_SETS; i++) {
        if (gap_ctx.adv_sets[i].in_use && gap_ctx.adv_sets[i].handle == handle) {
            return &gap_ctx.adv_sets[i];
        }
    }
    return NULL;
}

static adv_set_t* alloc_adv_set(uint8_t handle)
{
    for (int i = 0; i < GAP_MAX_ADV_SETS; i++) {
        if (!gap_ctx.adv_sets[i].in_use) {
            memset(&gap_ctx.adv_sets[i], 0, sizeof(adv_set_t));
            gap_ctx.adv_sets[i].in_use = true;
            gap_ctx.adv_sets[i].handle = handle;
            gap_ctx.adv_sets[i].state = ADV_SET_STATE_IDLE;
            return &gap_ctx.adv_sets[i];
        }
    }
    return NULL;
}

static void free_adv_set(adv_set_t *set)
{
    if (set != NULL) {
        set->in_use = false;
        set->state = ADV_SET_STATE_IDLE;
    }
}

static periodic_sync_t* find_periodic_sync(uint16_t sync_handle)
{
    for (int i = 0; i < GAP_MAX_ADV_SETS; i++) {
        if (gap_ctx.periodic_syncs[i].in_use &&
            gap_ctx.periodic_syncs[i].sync_handle == sync_handle) {
            return &gap_ctx.periodic_syncs[i];
        }
    }
    return NULL;
}

static periodic_sync_t* alloc_periodic_sync(void)
{
    for (int i = 0; i < GAP_MAX_ADV_SETS; i++) {
        if (!gap_ctx.periodic_syncs[i].in_use) {
            memset(&gap_ctx.periodic_syncs[i], 0, sizeof(periodic_sync_t));
            gap_ctx.periodic_syncs[i].in_use = true;
            return &gap_ctx.periodic_syncs[i];
        }
    }
    return NULL;
}

static void notify_event(gap_event_type_t type, void *data)
{
    if (gap_ctx.callback == NULL) {
        return;
    }

    gap_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;

    switch (type) {
        case GAP_EVENT_ADV_STARTED:
        case GAP_EVENT_ADV_STOPPED:
        case GAP_EVENT_ADV_SET_TERMINATED:
            if (data != NULL) {
                event.data.adv_handle = *(uint8_t*)data;
            }
            break;

        case GAP_EVENT_SCAN_RESULT:
            if (data != NULL) {
                memcpy(&event.data.scan_result, data, sizeof(gap_scan_result_t));
            }
            break;

        case GAP_EVENT_EXT_SCAN_RESULT:
            if (data != NULL) {
                memcpy(&event.data.ext_scan_result, data, sizeof(gap_ext_scan_result_t));
            }
            break;

        case GAP_EVENT_PERIODIC_ADV_SYNC:
            if (data != NULL) {
                memcpy(&event.data.periodic_sync, data, sizeof(gap_periodic_sync_t));
            }
            break;

        case GAP_EVENT_PERIODIC_ADV_REPORT:
            if (data != NULL) {
                memcpy(&event.data.periodic_report, data, sizeof(gap_periodic_report_t));
            }
            break;

        default:
            break;
    }

    gap_ctx.callback(&event, gap_ctx.callback_user_data);
}

/*******************************************************************************
 * HCI Event Handlers (called from BT stack)
 ******************************************************************************/

/**
 * @brief Handle advertising report from HCI
 */
void gap_on_advertising_report(uint8_t adv_type, gap_addr_type_t addr_type,
                                const uint8_t *addr, int8_t rssi,
                                const uint8_t *data, uint8_t data_len)
{
    gap_scan_result_t result;

    memset(&result, 0, sizeof(result));
    result.adv_type = adv_type;
    result.address.type = addr_type;
    memcpy(result.address.addr, addr, 6);
    result.rssi = rssi;
    result.data_len = (data_len > GAP_MAX_ADV_DATA_LEN) ? GAP_MAX_ADV_DATA_LEN : data_len;
    memcpy(result.data, data, result.data_len);

    notify_event(GAP_EVENT_SCAN_RESULT, &result);
}

/**
 * @brief Handle extended advertising report from HCI
 */
void gap_on_ext_advertising_report(uint16_t event_type, gap_addr_type_t addr_type,
                                    const uint8_t *addr, uint8_t primary_phy,
                                    uint8_t secondary_phy, uint8_t adv_sid,
                                    int8_t tx_power, int8_t rssi,
                                    uint16_t periodic_adv_interval,
                                    const uint8_t *data, uint16_t data_len)
{
    gap_ext_scan_result_t result;

    memset(&result, 0, sizeof(result));
    result.adv_type = event_type & 0xFF;
    result.address.type = addr_type;
    memcpy(result.address.addr, addr, 6);
    result.primary_phy = primary_phy;
    result.secondary_phy = secondary_phy;
    result.adv_sid = adv_sid;
    result.tx_power = tx_power;
    result.rssi = rssi;
    result.periodic_adv_interval = periodic_adv_interval;
    result.data_len = (data_len > GAP_MAX_EXT_ADV_DATA_LEN) ?
                      GAP_MAX_EXT_ADV_DATA_LEN : data_len;
    memcpy(result.data, data, result.data_len);

    notify_event(GAP_EVENT_EXT_SCAN_RESULT, &result);
}

/**
 * @brief Handle periodic advertising sync established
 */
void gap_on_periodic_sync_established(uint8_t status, uint16_t sync_handle,
                                       uint8_t adv_sid, gap_addr_type_t addr_type,
                                       const uint8_t *addr, uint8_t adv_phy,
                                       uint16_t periodic_adv_interval,
                                       uint8_t adv_clock_accuracy)
{
    if (status != 0) {
        int error = status;
        notify_event(GAP_EVENT_ERROR, &error);
        return;
    }

    /* Update sync context */
    for (int i = 0; i < GAP_MAX_ADV_SETS; i++) {
        if (gap_ctx.periodic_syncs[i].in_use &&
            gap_ctx.periodic_syncs[i].adv_sid == adv_sid) {
            gap_ctx.periodic_syncs[i].sync_handle = sync_handle;
            break;
        }
    }

    gap_periodic_sync_t sync;
    sync.sync_handle = sync_handle;
    sync.adv_sid = adv_sid;
    sync.address.type = addr_type;
    memcpy(sync.address.addr, addr, 6);
    sync.adv_phy = adv_phy;
    sync.periodic_adv_interval = periodic_adv_interval;
    sync.adv_clock_accuracy = adv_clock_accuracy;

    notify_event(GAP_EVENT_PERIODIC_ADV_SYNC, &sync);
}

/**
 * @brief Handle periodic advertising report
 */
void gap_on_periodic_adv_report(uint16_t sync_handle, int8_t tx_power,
                                 int8_t rssi, uint8_t data_status,
                                 const uint8_t *data, uint16_t data_len)
{
    gap_periodic_report_t report;

    report.sync_handle = sync_handle;
    report.tx_power = tx_power;
    report.rssi = rssi;
    report.data_status = data_status;
    report.data_len = (data_len > GAP_MAX_PERIODIC_ADV_DATA_LEN) ?
                      GAP_MAX_PERIODIC_ADV_DATA_LEN : data_len;
    memcpy(report.data, data, report.data_len);

    notify_event(GAP_EVENT_PERIODIC_ADV_REPORT, &report);
}

/**
 * @brief Handle periodic advertising sync lost
 */
void gap_on_periodic_sync_lost(uint16_t sync_handle)
{
    periodic_sync_t *sync = find_periodic_sync(sync_handle);
    if (sync != NULL) {
        sync->in_use = false;
    }

    notify_event(GAP_EVENT_PERIODIC_ADV_SYNC_LOST, &sync_handle);
}

/**
 * @brief Handle connection complete
 */
void gap_on_connection_complete(uint8_t status, uint16_t conn_handle,
                                 uint8_t role, gap_addr_type_t peer_addr_type,
                                 const uint8_t *peer_addr,
                                 uint16_t conn_interval, uint16_t conn_latency,
                                 uint16_t supervision_timeout)
{
    gap_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = GAP_EVENT_CONNECTION_COMPLETE;
    event.data.connection.conn_handle = conn_handle;
    event.data.connection.peer_addr.type = peer_addr_type;
    memcpy(event.data.connection.peer_addr.addr, peer_addr, 6);
    event.data.connection.params.conn_interval_min = conn_interval;
    event.data.connection.params.conn_interval_max = conn_interval;
    event.data.connection.params.conn_latency = conn_latency;
    event.data.connection.params.supervision_timeout = supervision_timeout;

    (void)status;
    (void)role;

    if (gap_ctx.callback != NULL) {
        gap_ctx.callback(&event, gap_ctx.callback_user_data);
    }
}

/**
 * @brief Handle disconnection complete
 */
void gap_on_disconnection_complete(uint16_t conn_handle, uint8_t reason)
{
    gap_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = GAP_EVENT_DISCONNECTION;
    event.data.disconnection.conn_handle = conn_handle;
    event.data.disconnection.reason = reason;

    if (gap_ctx.callback != NULL) {
        gap_ctx.callback(&event, gap_ctx.callback_user_data);
    }
}
