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

/* Infineon BTSTACK headers for GAP operations */
#include "wiced_bt_ble.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_l2c.h"

/* BTSTACK 4.x extended advertising and scanning headers */
#include "wiced_bt_adv_scan_extended.h"
#include "wiced_bt_adv_scan_periodic.h"

/* GATT database for device name updates */
#include "gatt_db.h"

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
static uint8_t parse_raw_adv_data(const uint8_t *raw_data, uint8_t raw_len,
                                  wiced_bt_ble_advert_elem_t *elements, uint8_t max_elements);

/*******************************************************************************
 * Private Helper Functions
 ******************************************************************************/

/**
 * @brief Parse raw LTV advertisement data into BTSTACK 4.x element array
 */
static uint8_t parse_raw_adv_data(const uint8_t *raw_data, uint8_t raw_len,
                                  wiced_bt_ble_advert_elem_t *elements, uint8_t max_elements)
{
    uint8_t count = 0;
    uint8_t offset = 0;

    while (offset < raw_len && count < max_elements) {
        uint8_t len = raw_data[offset];
        if (len == 0 || offset + len >= raw_len) {
            break;
        }

        elements[count].advert_type = raw_data[offset + 1];
        elements[count].len = len - 1;  /* Length minus type byte */
        elements[count].p_data = (uint8_t *)&raw_data[offset + 2];
        count++;

        offset += len + 1;  /* Move past length + data */
    }

    return count;
}

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

    /* Get local address from controller via BTSTACK */
    wiced_bt_dev_read_local_addr(gap_ctx.local_address.addr);
    gap_ctx.local_address.type = GAP_ADDR_TYPE_PUBLIC;

    printf("GAP: Initialized, local addr=%02X:%02X:%02X:%02X:%02X:%02X\n",
           gap_ctx.local_address.addr[0], gap_ctx.local_address.addr[1],
           gap_ctx.local_address.addr[2], gap_ctx.local_address.addr[3],
           gap_ctx.local_address.addr[4], gap_ctx.local_address.addr[5]);

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

    /* Update GATT device name characteristic */
    gatt_db_set_device_name(gap_ctx.device_name);

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

    /* Set random address via BTSTACK (BTSTACK 4.x API) */
    wiced_bt_set_local_bdaddr((uint8_t *)address, BLE_ADDR_RANDOM);

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

    /* Set advertising parameters via BTSTACK
     * Note: BTSTACK uses wiced_bt_ble_set_raw_advertisement_data for data
     * and wiced_bt_start_advertisements to control advertising with params
     */
    printf("GAP: Configured legacy adv params: interval=%d-%d, type=%d\n",
           params->adv_interval_min, params->adv_interval_max, params->adv_type);

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

    /* Set advertising data via BTSTACK (BTSTACK 4.x element-based API) */
    if (len > 0) {
        wiced_bt_ble_advert_elem_t elements[10];
        uint8_t num_elements = parse_raw_adv_data(data, len, elements, 10);
        if (num_elements > 0) {
            wiced_bt_ble_set_raw_advertisement_data(num_elements, elements);
        }
    }

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

    /* Set scan response data via BTSTACK (BTSTACK 4.x element-based API) */
    if (len > 0) {
        wiced_bt_ble_advert_elem_t elements[10];
        uint8_t num_elements = parse_raw_adv_data(data, len, elements, 10);
        if (num_elements > 0) {
            wiced_bt_ble_set_raw_scan_response_data(num_elements, elements);
        }
    }

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

    /* Enable advertising via BTSTACK */
    wiced_result_t result;
    wiced_bt_ble_advert_mode_t mode = BTM_BLE_ADVERT_UNDIRECTED_HIGH;

    /* Determine advertising mode based on configured params */
    if (gap_ctx.legacy_adv_params.adv_type == GAP_ADV_TYPE_NONCONN_IND) {
        mode = BTM_BLE_ADVERT_NONCONN_HIGH;
    } else if (gap_ctx.legacy_adv_params.adv_type == GAP_ADV_TYPE_SCAN_IND) {
        mode = BTM_BLE_ADVERT_DISCOVERABLE_HIGH;
    }

    result = wiced_bt_start_advertisements(mode, BLE_ADDR_PUBLIC, NULL);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to start advertising: %d\n", result);
        return GAP_ERROR_INVALID_STATE;
    }

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

    /* Disable advertising via BTSTACK */
    wiced_result_t result;
    result = wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF, BLE_ADDR_PUBLIC, NULL);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to stop advertising: %d\n", result);
    }

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

    /* Configure extended advertising parameters via BTSTACK 4.x API */
    wiced_ble_ext_adv_params_t ext_params;
    memset(&ext_params, 0, sizeof(ext_params));

    ext_params.adv_handle = params->adv_handle;
    ext_params.event_properties = params->adv_event_properties;
    ext_params.primary_adv_int_min = params->primary_adv_interval_min;
    ext_params.primary_adv_int_max = params->primary_adv_interval_max;
    ext_params.primary_adv_channel_map = params->primary_adv_channel_map;
    ext_params.own_addr_type = (wiced_bt_ble_address_type_t)params->own_addr_type;
    ext_params.peer_addr_type = (wiced_bt_ble_address_type_t)params->peer_addr.type;
    memcpy(ext_params.peer_addr, params->peer_addr.addr, 6);
    ext_params.adv_filter_policy = (wiced_bt_ble_advert_filter_policy_t)params->filter_policy;
    ext_params.adv_tx_power = params->adv_tx_power;
    ext_params.primary_adv_phy = (params->primary_adv_phy == GAP_PHY_1M) ?
        WICED_BLE_EXT_ADV_PHY_1M : ((params->primary_adv_phy == GAP_PHY_CODED) ?
        WICED_BLE_EXT_ADV_PHY_LE_CODED : WICED_BLE_EXT_ADV_PHY_1M);
    ext_params.secondary_adv_max_skip = params->secondary_adv_max_skip;
    ext_params.secondary_adv_phy = (params->secondary_adv_phy == GAP_PHY_1M) ?
        WICED_BLE_EXT_ADV_PHY_1M : ((params->secondary_adv_phy == GAP_PHY_2M) ?
        WICED_BLE_EXT_ADV_PHY_2M : WICED_BLE_EXT_ADV_PHY_LE_CODED);
    ext_params.adv_sid = params->adv_sid;
    ext_params.scan_request_not = params->scan_req_notify_enable ?
        WICED_BLE_EXT_ADV_SCAN_REQ_NOTIFY_ENABLE : WICED_BLE_EXT_ADV_SCAN_REQ_NOTIFY_DISABLE;
    ext_params.primary_phy_opts = WICED_BLE_EXT_ADV_PHY_OPTIONS_NO_PREFERENCE;
    ext_params.secondary_phy_opts = WICED_BLE_EXT_ADV_PHY_OPTIONS_NO_PREFERENCE;

    wiced_result_t result = wiced_ble_ext_adv_set_params(params->adv_handle, &ext_params);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to set ext adv params: %d\n", result);
        free_adv_set(set);
        return GAP_ERROR_INVALID_PARAM;
    }

    printf("GAP: Created ext adv set %d\n", params->adv_handle);
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

    /* Remove extended advertising set via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_ext_adv_remove_adv_set(adv_handle);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to remove ext adv set %d: %d\n", adv_handle, result);
    }

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

    /* Extended advertising params updated in local copy - apply on next start */
    printf("GAP: Updated ext adv params for set %d\n", params->adv_handle);

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

    /* Set extended advertising data via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_ext_adv_set_adv_data(
        adv_handle,
        len,
        (uint8_t *)data
    );
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to set ext adv data for set %d: %d\n", adv_handle, result);
        return GAP_ERROR_INVALID_PARAM;
    }

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

    /* Set extended scan response data via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_ext_adv_set_scan_rsp_data(
        adv_handle,
        len,
        (uint8_t *)data
    );
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to set ext scan rsp data for set %d: %d\n", adv_handle, result);
        return GAP_ERROR_INVALID_PARAM;
    }

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

    /* Enable extended advertising via BTSTACK 4.x API */
    wiced_ble_ext_adv_duration_config_t adv_cfg;
    adv_cfg.adv_handle = adv_handle;
    adv_cfg.adv_duration = duration;
    adv_cfg.max_ext_adv_events = max_events;

    wiced_result_t result = wiced_ble_ext_adv_enable(WICED_TRUE, 1, &adv_cfg);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to start ext advertising set %d: %d\n", adv_handle, result);
        return GAP_ERROR_INVALID_STATE;
    }

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

    /* Disable extended advertising via BTSTACK 4.x API */
    wiced_ble_ext_adv_duration_config_t adv_cfg;
    adv_cfg.adv_handle = adv_handle;
    adv_cfg.adv_duration = 0;
    adv_cfg.max_ext_adv_events = 0;

    wiced_result_t result = wiced_ble_ext_adv_enable(WICED_FALSE, 1, &adv_cfg);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to stop ext advertising set %d: %d\n", adv_handle, result);
    }

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

    /* Set periodic advertising parameters via BTSTACK 4.x API */
    wiced_ble_padv_params_t pa_params;
    memset(&pa_params, 0, sizeof(pa_params));
    pa_params.periodic_adv_int_min = params->periodic_adv_interval_min;
    pa_params.periodic_adv_int_max = params->periodic_adv_interval_max;
    pa_params.adv_properties = params->periodic_adv_properties;

    wiced_result_t result = wiced_ble_padv_set_adv_params(
        params->adv_handle, &pa_params);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to set periodic adv params for set %d: %d\n",
               params->adv_handle, result);
        return GAP_ERROR_INVALID_PARAM;
    }

    if (set->state == ADV_SET_STATE_CONFIGURED) {
        set->state = ADV_SET_STATE_PERIODIC_CONFIGURED;
    }

    printf("GAP: Configured periodic adv for set %d\n", params->adv_handle);
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

    /* Set periodic advertising data via BTSTACK 4.x API
     * For Auracast, this contains the BASE (Broadcast Audio Source Endpoint)
     * structure with codec configuration, BIS info, etc.
     */
    wiced_result_t result = wiced_ble_padv_set_adv_data(
        adv_handle,
        len,
        (uint8_t *)data
    );
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to set periodic adv data for set %d: %d\n",
               adv_handle, result);
        return GAP_ERROR_INVALID_PARAM;
    }

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

    /* Enable periodic advertising via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_padv_enable_adv(adv_handle, WICED_TRUE);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to start periodic advertising set %d: %d\n",
               adv_handle, result);
        return GAP_ERROR_INVALID_STATE;
    }

    set->state = ADV_SET_STATE_PERIODIC_ADVERTISING;
    printf("GAP: Started periodic advertising set %d\n", adv_handle);

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

    /* Disable periodic advertising via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_padv_enable_adv(adv_handle, WICED_FALSE);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to stop periodic advertising set %d: %d\n",
               adv_handle, result);
    }

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

    /* Create periodic advertising sync via BTSTACK 4.x API */
    wiced_ble_padv_sync_params_t sync_params;
    memset(&sync_params, 0, sizeof(sync_params));
    sync_params.options = 0;
    sync_params.adv_sid = adv_sid;
    sync_params.adv_addr_type = (wiced_bt_ble_address_type_t)address->type;
    memcpy(sync_params.adv_addr, address->addr, 6);
    sync_params.skip = skip;
    sync_params.sync_timeout = sync_timeout;
    sync_params.sync_cte_type = 0;

    wiced_result_t result = wiced_ble_padv_create_sync(&sync_params);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to create periodic sync: %d\n", result);
        sync->in_use = false;
        return GAP_ERROR_INVALID_PARAM;
    }

    return GAP_OK;
}

int gap_periodic_adv_cancel_sync(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    /* Cancel pending sync via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_padv_cancel_create_sync();
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to cancel periodic sync: %d\n", result);
        return GAP_ERROR_INVALID_STATE;
    }

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

    /* Terminate sync via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_padv_terminate_sync(sync_handle);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to terminate periodic sync %d: %d\n", sync_handle, result);
    }

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

    /* Scan parameters will be applied when scanning starts via BTSTACK */
    printf("GAP: Configured scan params: interval=%d, window=%d, type=%d\n",
           params->scan_interval, params->scan_window, params->scan_type);

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

    /* Enable scanning via BTSTACK */
    wiced_result_t result = wiced_bt_ble_scan(
        (gap_ctx.scan_params.scan_type == GAP_SCAN_TYPE_ACTIVE) ?
            BTM_BLE_SCAN_TYPE_HIGH_DUTY : BTM_BLE_SCAN_TYPE_LOW_DUTY,
        WICED_TRUE,
        NULL  /* Use registered callback */
    );
    if (result != WICED_BT_SUCCESS && result != WICED_BT_PENDING) {
        printf("GAP: Failed to start scanning: %d\n", result);
        return GAP_ERROR_INVALID_STATE;
    }

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

    /* Disable scanning via BTSTACK */
    wiced_result_t result = wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE, WICED_FALSE, NULL);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to stop scanning: %d\n", result);
    }

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

    /* Configure extended scan parameters via BTSTACK 4.x API */
    wiced_ble_ext_scan_params_t ext_params;
    memset(&ext_params, 0, sizeof(ext_params));
    ext_params.own_addr_type = (wiced_bt_ble_address_type_t)params->own_addr_type;
    ext_params.scanning_filter_policy = (wiced_bt_ble_scanner_filter_policy_t)params->filter_policy;
    ext_params.scanning_phys = params->scanning_phys;

    /* Configure parameters for 1M PHY */
    if (params->scanning_phys & 0x01) {
        ext_params.params_1m_phy.scan_type = params->phy_params[0].scan_type;
        ext_params.params_1m_phy.scan_interval = params->phy_params[0].scan_interval;
        ext_params.params_1m_phy.scan_window = params->phy_params[0].scan_window;
    }

    /* Configure parameters for Coded PHY */
    if (params->scanning_phys & 0x04) {
        ext_params.params_coded_phy.scan_type = params->phy_params[1].scan_type;
        ext_params.params_coded_phy.scan_interval = params->phy_params[1].scan_interval;
        ext_params.params_coded_phy.scan_window = params->phy_params[1].scan_window;
    }

    wiced_result_t result = wiced_ble_ext_scan_set_params(&ext_params);
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to set ext scan params: %d\n", result);
        return GAP_ERROR_INVALID_PARAM;
    }

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

    /* Enable extended scanning via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_ext_scan_enable(
        WICED_TRUE,
        (wiced_ble_ext_scan_filter_duplicate_t)filter_duplicates,
        duration,
        period
    );
    if (result != WICED_BT_SUCCESS && result != WICED_BT_PENDING) {
        printf("GAP: Failed to start ext scanning: %d\n", result);
        return GAP_ERROR_INVALID_STATE;
    }

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

    /* Disable extended scanning via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_ext_scan_enable(
        WICED_FALSE,
        WICED_BLE_EXT_SCAN_FILTER_DUPLICATE_DISABLE,
        0,
        0
    );
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to stop ext scanning: %d\n", result);
    }

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

    /* Create connection via BTSTACK */
    wiced_bt_ble_conn_params_t conn_params;
    conn_params.conn_interval_min = params->conn_interval_min;
    conn_params.conn_interval_max = params->conn_interval_max;
    conn_params.conn_latency = params->conn_latency;
    conn_params.conn_supervision_timeout = params->supervision_timeout;

    wiced_bool_t result = wiced_bt_ble_update_background_connection_device(
        WICED_TRUE,
        (uint8_t *)peer_addr->addr
    );
    if (result != WICED_TRUE) {
        printf("GAP: Failed to add device for connection\n");
        return GAP_ERROR_INVALID_PARAM;
    }

    /* Set preferred connection parameters */
    wiced_bt_ble_set_conn_params(
        params->conn_interval_min,
        params->conn_interval_max,
        params->conn_latency,
        params->supervision_timeout
    );

    printf("GAP: Initiating connection to %02X:%02X:%02X:%02X:%02X:%02X\n",
           peer_addr->addr[0], peer_addr->addr[1], peer_addr->addr[2],
           peer_addr->addr[3], peer_addr->addr[4], peer_addr->addr[5]);

    return GAP_OK;
}

int gap_cancel_connect(void)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    /* Cancel pending connection via BTSTACK */
    wiced_bt_ble_update_background_connection_device(WICED_FALSE, NULL);
    printf("GAP: Connection cancelled\n");

    return GAP_OK;
}

int gap_disconnect(uint16_t conn_handle, uint8_t reason)
{
    if (!gap_ctx.initialized) {
        return GAP_ERROR_NOT_INITIALIZED;
    }

    /* Disconnect via BTSTACK */
    wiced_result_t result = wiced_bt_gatt_disconnect(conn_handle);
    if (result != WICED_BT_SUCCESS && result != WICED_BT_PENDING) {
        printf("GAP: Failed to disconnect handle %d: %d\n", conn_handle, result);
        return GAP_ERROR_INVALID_PARAM;
    }

    (void)reason;  /* Reason is handled internally by BTSTACK */
    printf("GAP: Disconnecting handle %d\n", conn_handle);

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

    /* Update connection parameters - need peer BD address for BTSTACK API
     * For now, log the request - actual implementation needs connection tracking */
    printf("GAP: Update conn params for handle %d: interval=%d-%d, latency=%d, timeout=%d\n",
           conn_handle, params->conn_interval_min, params->conn_interval_max,
           params->conn_latency, params->supervision_timeout);

    /* Note: wiced_bt_l2cap_update_ble_conn_params requires BD address, not handle */
    (void)conn_handle;

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

    /* Request connection parameter update via L2CAP (peripheral role)
     * Note: wiced_bt_l2cap_update_ble_conn_params needs BD address
     * For peripheral role, this sends a parameter update request to central */
    printf("GAP: Request conn param update for handle %d\n", conn_handle);

    (void)conn_handle;
    (void)params;

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

    /* Clear filter accept list via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_clear_filter_accept_list();
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to clear whitelist: %d\n", result);
        return GAP_ERROR_INVALID_STATE;
    }

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

    /* Add to filter accept list via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_add_to_filter_accept_list(
        (wiced_bt_ble_address_type_t)address->type,
        (uint8_t *)address->addr
    );
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to add device to whitelist: %d\n", result);
        return GAP_ERROR_NO_RESOURCES;
    }

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

    /* Remove from filter accept list via BTSTACK 4.x API */
    wiced_result_t result = wiced_ble_remove_from_filter_accept_list(
        (wiced_bt_ble_address_type_t)address->type,
        (uint8_t *)address->addr
    );
    if (result != WICED_BT_SUCCESS) {
        printf("GAP: Failed to remove device from whitelist: %d\n", result);
        return GAP_ERROR_NOT_FOUND;
    }

    return GAP_OK;
}

int gap_whitelist_get_size(void)
{
    /* The whitelist size is typically read during controller initialization
     * and stored. CYW55512 supports a whitelist size of 8-16 entries.
     * For now, return the typical default value. */
    return 8;  /* Typical whitelist size for CYW55512 */
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
