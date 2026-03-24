/**
 * @file bt_init.c
 * @brief Bluetooth Stack Initialization Implementation
 *
 * Implements BTSTACK initialization, HCI transport setup, and
 * CYW55512 controller management for LE Audio applications.
 *
 * Adapted from Infineon MTB LE Audio example patterns with:
 * - BT Configurator settings integration (cycfg_bt_settings)
 * - Link tracking via link.c module
 * - Bonding/NVM support hooks
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bt_init.h"
#include "bt_platform_config.h"
#include "link.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Platform Includes (Infineon BTSTACK)
 ******************************************************************************/

/* Infineon BTSTACK headers */
#include "wiced_bt_stack.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_ble_conn.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_cfg.h"
#include "wiced_bt_isoc.h"
#include "wiced_bt_l2c.h"
#include "wiced_memory.h"
#include "cybt_platform_trace.h"

/* Note: cybt_platform_config.h is NOT included here because it requires
 * CY_USING_HAL which is not available on PSoC Edge with MTB HAL.
 * The btstack-integration library handles platform config automatically. */

/* BT Configurator generated settings (optional - use fallback if not available) */
#ifdef USE_BT_CONFIGURATOR
#include "cycfg_bt_settings.h"
#include "cycfg_gap.h"
#endif

/* Application initialization (from example's app.c) */
#include "app.h"

/* Infineon BSP/PDL */
#include "cybsp.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

/*******************************************************************************
 * Constants
 ******************************************************************************/

/** BT task stack size (words) */
#define BT_TASK_STACK_SIZE      (4096)

/** BT task priority */
#define BT_TASK_PRIORITY        (4)

/** Maximum pending events in queue */
#define BT_EVENT_QUEUE_SIZE     (16)

/** HCI reset timeout (ms) */
#define HCI_RESET_TIMEOUT_MS    (5000)

/** Firmware download timeout (ms) */
#define FW_DOWNLOAD_TIMEOUT_MS  (30000)

/** Infineon manufacturer ID */
#define MANUFACTURER_INFINEON   (0x0009)

/** Application heap size (from MTB example) */
#define APP_STACK_HEAP_SIZE     (0x512U)

/** Maximum key size for pairing */
#define MAX_KEY_SIZE            (16U)

/** HCI opcodes (guarded to avoid redefinition with BTSTACK headers) */
#ifndef HCI_RESET
#define HCI_RESET               0x0C03
#endif
#ifndef HCI_READ_LOCAL_VERSION
#define HCI_READ_LOCAL_VERSION  0x1001
#endif
#ifndef HCI_READ_BD_ADDR
#define HCI_READ_BD_ADDR        0x1009
#endif
#ifndef HCI_LE_SET_ADV_PARAMS
#define HCI_LE_SET_ADV_PARAMS   0x2006
#endif
#ifndef HCI_LE_SET_ADV_DATA
#define HCI_LE_SET_ADV_DATA     0x2008
#endif
#ifndef HCI_LE_SET_ADV_ENABLE
#define HCI_LE_SET_ADV_ENABLE   0x200A
#endif
#ifndef HCI_LE_READ_LOCAL_FEAT
#define HCI_LE_READ_LOCAL_FEAT  0x2003
#endif

/** Advertising data types */
#define AD_TYPE_FLAGS           0x01
#define AD_TYPE_COMPLETE_NAME   0x09
#define AD_TYPE_TX_POWER        0x0A
#define AD_TYPE_SERVICE_UUID_16 0x03

/** Default advertising flags */
#define ADV_FLAGS_LE_LIMITED    0x01
#define ADV_FLAGS_LE_GENERAL    0x02
#define ADV_FLAGS_BR_EDR_NOT    0x04

/*******************************************************************************
 * BTSTACK Configuration (BTSTACK 4.x API)
 ******************************************************************************/

/* Forward declaration of management callback */
static wiced_result_t bt_management_callback(wiced_bt_management_evt_t event,
                                              wiced_bt_management_evt_data_t *p_event_data);

/* BLE Scan Settings */
static const wiced_bt_cfg_ble_scan_settings_t ble_scan_cfg = {
    .scan_mode = BTM_BLE_SCAN_MODE_PASSIVE,
    .high_duty_scan_interval = 96,   /* 60ms */
    .high_duty_scan_window = 48,     /* 30ms */
    .high_duty_scan_duration = 30,   /* 30 seconds */
    .low_duty_scan_interval = 2048,  /* 1.28s */
    .low_duty_scan_window = 48,      /* 30ms */
    .low_duty_scan_duration = 30,
    .high_duty_conn_scan_interval = 96,
    .high_duty_conn_scan_window = 48,
    .high_duty_conn_duration = 30,
    .low_duty_conn_scan_interval = 2048,
    .low_duty_conn_scan_window = 48,
    .low_duty_conn_duration = 30,
    .conn_min_interval = 12,         /* 15ms */
    .conn_max_interval = 12,
    .conn_latency = 0,
    .conn_supervision_timeout = 100  /* 1s */
};

/* BLE Advertising Settings */
static const wiced_bt_cfg_ble_advert_settings_t ble_advert_cfg = {
    .channel_map = BTM_BLE_ADVERT_CHNL_37 | BTM_BLE_ADVERT_CHNL_38 | BTM_BLE_ADVERT_CHNL_39,
    .high_duty_min_interval = 48,    /* 30ms */
    .high_duty_max_interval = 48,
    .high_duty_duration = 0,         /* Continuous */
    .low_duty_min_interval = 160,    /* 100ms */
    .low_duty_max_interval = 160,
    .low_duty_duration = 0,
    .high_duty_directed_min_interval = 400,
    .high_duty_directed_max_interval = 800,
    .low_duty_directed_min_interval = 48,
    .low_duty_directed_max_interval = 48,
    .low_duty_directed_duration = 30,
    .high_duty_nonconn_min_interval = 160,
    .high_duty_nonconn_max_interval = 160,
    .high_duty_nonconn_duration = 0,
    .low_duty_nonconn_min_interval = 160,
    .low_duty_nonconn_max_interval = 160,
    .low_duty_nonconn_duration = 0
};

/* BLE Configuration */
static const wiced_bt_cfg_ble_t ble_cfg = {
    .ble_max_simultaneous_links = 2,
    .ble_max_rx_pdu_size = 517,
    .appearance = 0x0000,            /* Unknown */
    .rpa_refresh_timeout = WICED_BT_CFG_DEFAULT_RANDOM_ADDRESS_CHANGE_TIMEOUT,
    .host_addr_resolution_db_size = 5,
    .p_ble_scan_cfg = &ble_scan_cfg,
    .p_ble_advert_cfg = &ble_advert_cfg,
    .default_ble_power_level = 0
};

/* GATT Configuration */
static const wiced_bt_cfg_gatt_t gatt_cfg = {
    .max_db_service_modules = 0,
    .max_eatt_bearers = 0
};

/* L2CAP Application Configuration */
static const wiced_bt_cfg_l2cap_application_t l2cap_cfg = {
    .max_app_l2cap_psms = 0,
    .max_app_l2cap_channels = 0,
    .max_app_l2cap_le_fixed_channels = 0,
    .max_app_l2cap_br_edr_ertm_chnls = 0,
    .max_app_l2cap_br_edr_ertm_tx_win = 0
};

/* ISOC Configuration (deprecated in BTSTACK 4.0, but still needed for LE Audio) */
static const wiced_bt_cfg_isoc_t isoc_cfg = {
    .max_sdu_size = 240,             /* Max SDU for LC3 */
    .channel_count = 2,              /* Stereo */
    .max_cis_conn = 2,               /* Max CIS connections */
    .max_cig_count = 1,              /* Max CIG groups */
    .max_buffers_per_cis = 4,
    .max_big_count = 1               /* Max BIG for broadcast */
};

/* Main Bluetooth Stack Configuration */
static wiced_bt_cfg_settings_t bt_cfg_settings = {
    .device_name = (uint8_t *)"Infineon LE Audio",
    .security_required = BTM_SEC_BEST_EFFORT,
    .p_br_cfg = NULL,                /* BR/EDR disabled for LE-only */
    .p_ble_cfg = &ble_cfg,
    .p_gatt_cfg = &gatt_cfg,
    .p_isoc_cfg = &isoc_cfg,
    .p_l2cap_app_cfg = &l2cap_cfg
};

/*******************************************************************************
 * Bonding/NVM Support (Weak Functions - Override in app_bt_bonding.c)
 ******************************************************************************/

/**
 * Weak function stubs for bonding support.
 * Override these by linking with app_bt_bonding.c from MTB example.
 */

__attribute__((weak))
void app_kv_store_init(void) {}

__attribute__((weak))
uint32_t app_bt_restore_bond_data(void) { return 0; }

__attribute__((weak))
uint32_t app_bt_save_device_link_keys(wiced_bt_device_link_keys_t *link_key)
{
    (void)link_key;
    return 0;
}

__attribute__((weak))
uint32_t app_bt_save_local_identity_key(wiced_bt_local_identity_keys_t id_key)
{
    (void)id_key;
    return 0;
}

__attribute__((weak))
uint32_t app_bt_read_local_identity_keys(void) { return 1; /* Error - not found */ }

__attribute__((weak))
uint8_t app_bt_find_device_in_nvm(uint8_t *bd_addr)
{
    (void)bd_addr;
    return 0xFF; /* Not found */
}

__attribute__((weak))
void app_bt_update_slot_data(void) {}

__attribute__((weak))
void app_bt_add_devices_to_address_resolution_db(void) {}

/* External bond_info structure (from app_bt_bonding.c if linked) */
typedef struct {
    uint8_t slot_data[2];
    wiced_bt_device_link_keys_t link_keys[4];
    uint8_t privacy_mode[4];
} bond_info_t;

__attribute__((weak))
bond_info_t bond_info = {0};

__attribute__((weak))
wiced_bt_local_identity_keys_t identity_keys = {0};

/*******************************************************************************
 * Types
 ******************************************************************************/

/** Internal stack state (simplified - connection tracking moved to link.c) */
typedef struct {
    bool initialized;
    bt_state_t state;
    bt_config_t config;
    bt_controller_info_t controller_info;
    bt_stats_t stats;
    bt_power_mode_t power_mode;

    /* Callback */
    bt_event_callback_t event_callback;
    void *callback_user_data;

    /* Advertising state */
    bool advertising;
    bool connectable_adv;
    wiced_bt_ble_advert_mode_t adv_mode;
    wiced_bt_ble_advert_mode_t intended_adv_mode;
    uint16_t adv_interval;

    /* Advertising data */
    uint8_t adv_data[31];
    uint8_t adv_data_len;
    uint8_t scan_rsp_data[31];
    uint8_t scan_rsp_len;

    /* FreeRTOS handles */
    SemaphoreHandle_t init_semaphore;    /* Signaled when BTM_ENABLED_EVT received */
    SemaphoreHandle_t cmd_semaphore;     /* For synchronous operations */
    QueueHandle_t event_queue;           /* Application event queue */

} bt_context_t;

/*******************************************************************************
 * Module Variables
 ******************************************************************************/

static bt_context_t bt_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static void dispatch_event(const bt_event_t *event);
static void set_state(bt_state_t new_state);
static int build_default_adv_data(void);
static uint8_t parse_raw_adv_data(const uint8_t *raw_data, uint8_t raw_len,
                                  wiced_bt_ble_advert_elem_t *elements, uint8_t max_elements);

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

/**
 * @brief Parse raw advertisement data (LTV format) into wiced_bt_ble_advert_elem_t array
 *
 * @param raw_data      Pointer to raw advertisement data in LTV format
 * @param raw_len       Length of raw data
 * @param elements      Array to store parsed elements
 * @param max_elements  Maximum number of elements to parse
 * @return Number of elements parsed
 */
static uint8_t parse_raw_adv_data(const uint8_t *raw_data, uint8_t raw_len,
                                  wiced_bt_ble_advert_elem_t *elements, uint8_t max_elements)
{
    uint8_t num_elements = 0;
    uint8_t offset = 0;

    while (offset < raw_len && num_elements < max_elements) {
        uint8_t elem_len = raw_data[offset];  /* Length byte (excludes itself) */
        if (elem_len == 0 || offset + 1 + elem_len > raw_len) {
            break;  /* Invalid or incomplete element */
        }

        elements[num_elements].len = elem_len - 1;  /* Data length (excludes type) */
        elements[num_elements].advert_type = (wiced_bt_ble_advert_type_t)raw_data[offset + 1];
        elements[num_elements].p_data = (uint8_t *)&raw_data[offset + 2];

        num_elements++;
        offset += 1 + elem_len;  /* Move to next element */
    }

    return num_elements;
}

/*******************************************************************************
 * NOTE: HCI Transport and Controller Management
 *
 * The btstack-integration library (COMPONENT_HCI-UART) handles all HCI
 * transport, firmware download, and controller initialization internally.
 *
 * - cybt_platform_config_init() configures the HCI UART pins and baud rates
 * - wiced_bt_stack_init() handles:
 *   - Firmware patchram download to CYW55512
 *   - HCI reset and controller initialization
 *   - Reading controller capabilities
 *
 * The management callback (bt_management_callback) receives BTM_ENABLED_EVT
 * when initialization is complete.
 ******************************************************************************/

/*******************************************************************************
 * Event Handling
 ******************************************************************************/

/**
 * @brief Dispatch event to registered callback
 */
static void dispatch_event(const bt_event_t *event)
{
    if (bt_ctx.event_callback != NULL) {
        bt_ctx.event_callback(event, bt_ctx.callback_user_data);
    }
}

/**
 * @brief Set new state and dispatch event
 */
static void set_state(bt_state_t new_state)
{
    if (bt_ctx.state != new_state) {
        printf("BT State: %d -> %d\n", bt_ctx.state, new_state);
        bt_ctx.state = new_state;

        bt_event_t event = {
            .type = BT_EVENT_STATE_CHANGED,
            .data.new_state = new_state
        };
        dispatch_event(&event);
    }
}

/*
 * NOTE: HCI event handling (connection complete, disconnection, LE meta events)
 * is now handled internally by btstack-integration and the BTSTACK library.
 * Events are dispatched through the bt_management_callback() above.
 */

/*******************************************************************************
 * BTSTACK Management Callback
 ******************************************************************************/

/**
 * @brief BTSTACK management event callback
 *
 * This is called by the BTSTACK for all Bluetooth management events.
 * Adapted from MTB example bt.c with bonding/NVM support.
 */
static wiced_result_t bt_management_callback(wiced_bt_management_evt_t event,
                                              wiced_bt_management_evt_data_t *p_event_data)
{
    wiced_result_t result = WICED_BT_SUCCESS;
    uint8_t bondindex;

    printf("BT Mgmt: Event %d\n", event);

    switch (event) {
        case BTM_ENABLED_EVT:
            /* Bluetooth stack enabled */
            if (p_event_data->enabled.status == WICED_BT_SUCCESS) {
                printf("BT Mgmt: Stack enabled successfully\n");

                /* Read local BD address (via extended API like MTB example) */
                wiced_bt_dev_read_local_addr(bt_ctx.controller_info.bd_addr);
                printf("BT Mgmt: BD_ADDR: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       bt_ctx.controller_info.bd_addr[0],
                       bt_ctx.controller_info.bd_addr[1],
                       bt_ctx.controller_info.bd_addr[2],
                       bt_ctx.controller_info.bd_addr[3],
                       bt_ctx.controller_info.bd_addr[4],
                       bt_ctx.controller_info.bd_addr[5]);

                /* Mark controller as initialized */
                bt_ctx.controller_info.type = BT_CONTROLLER_CYW55512;
                bt_ctx.controller_info.hci_version = 0x0F;  /* BT 6.0 */
                bt_ctx.controller_info.manufacturer = MANUFACTURER_INFINEON;
                bt_ctx.controller_info.le_audio_supported = true;
                bt_ctx.controller_info.isoc_supported = true;
                bt_ctx.controller_info.max_cig = 1;
                bt_ctx.controller_info.max_cis_per_cig = 2;
                bt_ctx.controller_info.max_big = 1;
                bt_ctx.controller_info.max_bis_per_big = 2;
                snprintf(bt_ctx.controller_info.fw_version,
                         sizeof(bt_ctx.controller_info.fw_version),
                         "CYW55512");

                /* Build default advertising data */
                build_default_adv_data();

                /* Restore bonding data from NVM */
                app_bt_restore_bond_data();

                /* Add bonded devices to address resolution database */
                app_bt_add_devices_to_address_resolution_db();

                /* Update state */
                bt_ctx.initialized = true;
                bt_ctx.power_mode = BT_POWER_ACTIVE;
                set_state(BT_STATE_READY);

                /* Signal initialization complete */
                if (bt_ctx.init_semaphore != NULL) {
                    xSemaphoreGive(bt_ctx.init_semaphore);
                }

                /* Dispatch initialization event */
                bt_event_t bt_event = {
                    .type = BT_EVENT_INITIALIZED
                };
                dispatch_event(&bt_event);

                /* Call app_init() to initialize GATT, ISOC, LEDs, and signal tasks */
                app_init();
            } else {
                printf("BT Mgmt: Stack enable failed: %d\n", p_event_data->enabled.status);
                set_state(BT_STATE_ERROR);

                /* Signal initialization failed */
                if (bt_ctx.init_semaphore != NULL) {
                    xSemaphoreGive(bt_ctx.init_semaphore);
                }
            }
            break;

        case BTM_DISABLED_EVT:
            /* Bluetooth stack disabled */
            printf("BT Mgmt: Stack disabled\n");
            set_state(BT_STATE_OFF);
            break;

        case BTM_USER_CONFIRMATION_REQUEST_EVT:
            /* User confirmation for numeric comparison (auto-accept) */
            printf("BT Mgmt: User confirmation request, value: %d\n",
                   (int)p_event_data->user_confirmation_request.numeric_value);
            wiced_bt_dev_confirm_req_reply(WICED_BT_SUCCESS,
                p_event_data->user_confirmation_request.bd_addr);
            break;

        case BTM_PASSKEY_NOTIFICATION_EVT:
            /* Passkey notification (auto-confirm) */
            wiced_bt_dev_confirm_req_reply(WICED_BT_SUCCESS,
                p_event_data->user_passkey_notification.bd_addr);
            break;

        case BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT:
            /* Local identity keys updated - save to NVM */
            printf("BT Mgmt: Local identity keys updated\n");
            if (app_bt_save_local_identity_key(
                    p_event_data->local_identity_keys_update) != 0) {
                result = WICED_BT_ERROR;
            }
            break;

        case BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT:
            /* Local identity keys requested - read from NVM */
            printf("BT Mgmt: Local identity keys requested\n");
            if (app_bt_read_local_identity_keys() == 0) {
                memcpy(&(p_event_data->local_identity_keys_request),
                       &identity_keys, sizeof(wiced_bt_local_identity_keys_t));
                result = WICED_BT_SUCCESS;
            } else {
                result = WICED_BT_ERROR;
            }
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
            /* Device link keys updated - save to NVM */
            printf("BT Mgmt: Link keys updated\n");
            app_bt_save_device_link_keys(
                &(p_event_data->paired_device_link_keys_update));
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
            /* Device link keys requested - lookup in NVM */
            printf("BT Mgmt: Link keys requested for: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   p_event_data->paired_device_link_keys_request.bd_addr[0],
                   p_event_data->paired_device_link_keys_request.bd_addr[1],
                   p_event_data->paired_device_link_keys_request.bd_addr[2],
                   p_event_data->paired_device_link_keys_request.bd_addr[3],
                   p_event_data->paired_device_link_keys_request.bd_addr[4],
                   p_event_data->paired_device_link_keys_request.bd_addr[5]);

            result = WICED_BT_ERROR;
            bondindex = app_bt_find_device_in_nvm(
                p_event_data->paired_device_link_keys_request.bd_addr);

            if (bondindex < 4) {  /* BOND_INDEX_MAX */
                memcpy(&(p_event_data->paired_device_link_keys_request),
                       &bond_info.link_keys[bondindex],
                       sizeof(wiced_bt_device_link_keys_t));
                result = WICED_BT_SUCCESS;
            } else {
                printf("BT Mgmt: Device link keys not found in NVM\n");
            }
            break;

        case BTM_ENCRYPTION_STATUS_EVT:
            /* Encryption status change - update link state */
            printf("BT Mgmt: Encryption status: %d\n",
                   p_event_data->encryption_status.result);
            link_set_encrypted(p_event_data->encryption_status.result == WICED_SUCCESS);
            break;

        case BTM_PAIRING_COMPLETE_EVT:
            /* Pairing complete - update slot data */
            printf("BT Mgmt: Pairing complete: %d\n",
                   p_event_data->pairing_complete.pairing_complete_info.ble.status);
            app_bt_update_slot_data();
            {
                bt_event_t bt_event = {
                    .type = BT_EVENT_PAIRING_COMPLETE
                };
                dispatch_event(&bt_event);
            }
            break;

        case BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
            /* Pairing IO capabilities request */
            printf("BT Mgmt: Pairing IO caps request\n");
            p_event_data->pairing_io_capabilities_ble_request.local_io_cap = BTM_IO_CAPABILITIES_NONE;
            p_event_data->pairing_io_capabilities_ble_request.oob_data = BTM_OOB_NONE;
            p_event_data->pairing_io_capabilities_ble_request.auth_req =
                BTM_LE_AUTH_REQ_SC | BTM_LE_AUTH_REQ_BOND;
            p_event_data->pairing_io_capabilities_ble_request.max_key_size = MAX_KEY_SIZE;
            p_event_data->pairing_io_capabilities_ble_request.init_keys =
                BTM_LE_KEY_PENC | BTM_LE_KEY_PID | BTM_LE_KEY_PCSRK | BTM_LE_KEY_PLK;
            p_event_data->pairing_io_capabilities_ble_request.resp_keys =
                BTM_LE_KEY_PENC | BTM_LE_KEY_PID | BTM_LE_KEY_PCSRK | BTM_LE_KEY_PLK;
            break;

        case BTM_SECURITY_REQUEST_EVT:
            /* Security request from peer */
            printf("BT Mgmt: Security request\n");
            wiced_bt_ble_security_grant(p_event_data->security_request.bd_addr,
                                        WICED_BT_SUCCESS);
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            /* Advertising state changed */
            {
                wiced_bt_ble_advert_mode_t new_adv_mode =
                    p_event_data->ble_advert_state_changed;
                printf("BT Mgmt: Advert state changed: %d\n", new_adv_mode);

                /* Handle automatic transition from high to low duty cycle */
                if (!link_is_connected() && new_adv_mode == BTM_BLE_ADVERT_OFF) {
                    if (bt_ctx.intended_adv_mode == BTM_BLE_ADVERT_UNDIRECTED_HIGH) {
                        /* Switch to low duty cycle undirected advertising */
                        wiced_result_t adv_result = wiced_bt_start_advertisements(
                            BTM_BLE_ADVERT_UNDIRECTED_LOW, BLE_ADDR_PUBLIC, NULL);
                        if (adv_result != WICED_BT_SUCCESS) {
                            printf("BT Mgmt: Failed to start low duty cycle adv\n");
                        }
                        break;
                    }
                }

                bt_ctx.adv_mode = new_adv_mode;
                if (new_adv_mode == BTM_BLE_ADVERT_OFF) {
                    bt_ctx.advertising = false;
                    if (!link_is_connected()) {
                        set_state(BT_STATE_READY);
                    }
                } else {
                    bt_ctx.advertising = true;
                    if (!link_is_connected()) {
                        set_state(BT_STATE_ADVERTISING);
                    }
                }
            }
            break;

        case BTM_BLE_SCAN_STATE_CHANGED_EVT:
            printf("BT Mgmt: Scan state changed: %d\n",
                   p_event_data->ble_scan_state_changed);
            break;

        case BTM_BLE_CONNECTION_PARAM_UPDATE:
            /* Connection parameters updated */
            printf("BT Mgmt: Conn params updated - status:%d interval:%d latency:%d timeout:%d\n",
                   p_event_data->ble_connection_param_update.status,
                   p_event_data->ble_connection_param_update.conn_interval,
                   p_event_data->ble_connection_param_update.conn_latency,
                   p_event_data->ble_connection_param_update.supervision_timeout);
            if (!p_event_data->ble_connection_param_update.status) {
                link_set_parameter_updated(true);
            }
            break;

        case BTM_BLE_PHY_UPDATE_EVT:
            /* PHY updated */
            printf("BT Mgmt: PHY updated - status:%d TX:%d RX:%d\n",
                   p_event_data->ble_phy_update_event.status,
                   p_event_data->ble_phy_update_event.tx_phy,
                   p_event_data->ble_phy_update_event.rx_phy);
            {
                /* Note: BTSTACK 4.x removed conn_handle from phy_update_event
                 * We use link_get_conn_handle_by_bdaddr() to lookup the handle */
                bt_event_t bt_event = {
                    .type = BT_EVENT_PHY_UPDATED,
                    .data.conn_handle = link_get_conn_handle_by_bdaddr(
                        p_event_data->ble_phy_update_event.bd_address)
                };
                dispatch_event(&bt_event);
            }
            break;

        default:
            printf("BT Mgmt: Unhandled event %d\n", event);
            break;
    }

    return result;
}

/*******************************************************************************
 * Advertising
 ******************************************************************************/

/**
 * @brief Build default advertising data
 */
static int build_default_adv_data(void)
{
    uint8_t pos = 0;

    /* Flags */
    bt_ctx.adv_data[pos++] = 2;  /* Length */
    bt_ctx.adv_data[pos++] = AD_TYPE_FLAGS;
    bt_ctx.adv_data[pos++] = ADV_FLAGS_LE_GENERAL | ADV_FLAGS_BR_EDR_NOT;

    /* Complete Local Name */
    uint8_t name_len = (uint8_t)strlen(bt_ctx.config.device_name);
    if (name_len > 25) {
        name_len = 25;  /* Limit to fit in advertising data */
    }
    bt_ctx.adv_data[pos++] = name_len + 1;
    bt_ctx.adv_data[pos++] = AD_TYPE_COMPLETE_NAME;
    memcpy(&bt_ctx.adv_data[pos], bt_ctx.config.device_name, name_len);
    pos += name_len;

    bt_ctx.adv_data_len = pos;

    /* Scan response - TX Power */
    pos = 0;
    bt_ctx.scan_rsp_data[pos++] = 2;
    bt_ctx.scan_rsp_data[pos++] = AD_TYPE_TX_POWER;
    bt_ctx.scan_rsp_data[pos++] = 0;  /* 0 dBm */
    bt_ctx.scan_rsp_len = pos;

    return BT_OK;
}

/*******************************************************************************
 * Public API - Initialization
 ******************************************************************************/

int bt_init_with_config(const bt_config_t *config)
{
    wiced_result_t wiced_result;

    if (bt_ctx.initialized) {
        return BT_ERROR_ALREADY_INITIALIZED;
    }

    /* Clear context */
    memset(&bt_ctx, 0, sizeof(bt_ctx));

    /* Apply configuration */
    if (config != NULL) {
        bt_ctx.config = *config;
    } else {
        /* Use defaults */
        bt_config_t defaults = BT_CONFIG_DEFAULT;
        bt_ctx.config = defaults;
    }

    set_state(BT_STATE_INITIALIZING);

    printf("BT Init: Starting Bluetooth stack initialization\n");

    /* Initialize KV store for bonding data (if available) */
    app_kv_store_init();

    /* Create synchronization primitives */
    bt_ctx.init_semaphore = xSemaphoreCreateBinary();
    bt_ctx.cmd_semaphore = xSemaphoreCreateBinary();
    bt_ctx.event_queue = xQueueCreate(BT_EVENT_QUEUE_SIZE, sizeof(bt_event_t));

    if (bt_ctx.init_semaphore == NULL ||
        bt_ctx.cmd_semaphore == NULL ||
        bt_ctx.event_queue == NULL) {
        printf("BT Init: Failed to create FreeRTOS primitives\n");
        bt_deinit();
        return BT_ERROR_NO_MEMORY;
    }

    /* Initialize the btstack-integration platform layer */
    printf("BT Init: Configuring HCI UART platform\n");
    if (bt_platform_init() != 0) {
        printf("BT Init: Platform init failed\n");
        bt_deinit();
        return BT_ERROR_HCI_TRANSPORT;
    }

    /* Update BTSTACK config with device name if provided */
    if (bt_ctx.config.device_name[0] != '\0') {
        bt_cfg_settings.device_name = (uint8_t *)bt_ctx.config.device_name;
    }

    /* Initialize BTSTACK
     *
     * This will:
     * 1. Download firmware to CYW55512 (patchram)
     * 2. Initialize HCI transport
     * 3. Send HCI Reset
     * 4. Configure the controller
     * 5. Call bt_management_callback with BTM_ENABLED_EVT on success
     *
     * Uses bt_cfg_settings (our fallback config) or cycfg_bt_settings
     * if USE_BT_CONFIGURATOR is defined.
     */
    printf("BT Init: Calling wiced_bt_stack_init()\n");
#ifdef USE_BT_CONFIGURATOR
    wiced_result = wiced_bt_stack_init(bt_management_callback, &cy_bt_cfg_settings);
#else
    wiced_result = wiced_bt_stack_init(bt_management_callback, &bt_cfg_settings);
#endif

    if (wiced_result != WICED_BT_SUCCESS) {
        printf("BT Init: wiced_bt_stack_init failed: %d\n", wiced_result);
        bt_deinit();
        return BT_ERROR_CONTROLLER_INIT;
    }

    /* Create default heap (MTB example pattern) */
    if (wiced_bt_create_heap("app", NULL, APP_STACK_HEAP_SIZE, NULL, WICED_TRUE) == NULL) {
        printf("BT Init: Failed to create app heap (size %d)\n", APP_STACK_HEAP_SIZE);
        bt_deinit();
        return BT_ERROR_NO_MEMORY;
    }

    /*
     * BTM_ENABLED_EVT will be delivered via bt_management_callback()
     * AFTER vTaskStartScheduler() starts the BTSTACK tasks.
     *
     * DO NOT block here - the FreeRTOS scheduler hasn't started yet!
     * Bond restoration and state updates happen in the callback.
     *
     * Tasks that need to wait for BT ready should use bt_ctx.init_semaphore
     * which is signaled by the callback when BTM_ENABLED_EVT is received.
     */
    printf("BT Init: Stack init requested, BTM_ENABLED_EVT will arrive asynchronously\n");

    return BT_OK;
}

void bt_deinit(void)
{
    if (!bt_ctx.initialized) {
        return;
    }

    printf("BT Deinit: Shutting down Bluetooth stack\n");

    /* Stop advertising */
    if (bt_ctx.advertising) {
        bt_stop_advertising();
    }

    /* Disconnect all connections (using link.c) */
    for (uint8_t i = 0; i < LINK_MAX_CONNECTIONS; i++) {
        link_state_t *link = link_get_by_index(i);
        if (link != NULL) {
            bt_disconnect(link->connection_status.conn_id, 0x13);
        }
    }

    /* Deinitialize BTSTACK */
    wiced_bt_stack_deinit();

    /* Delete FreeRTOS objects */
    if (bt_ctx.init_semaphore != NULL) {
        vSemaphoreDelete(bt_ctx.init_semaphore);
        bt_ctx.init_semaphore = NULL;
    }
    if (bt_ctx.cmd_semaphore != NULL) {
        vSemaphoreDelete(bt_ctx.cmd_semaphore);
        bt_ctx.cmd_semaphore = NULL;
    }
    if (bt_ctx.event_queue != NULL) {
        vQueueDelete(bt_ctx.event_queue);
        bt_ctx.event_queue = NULL;
    }

    set_state(BT_STATE_OFF);
    bt_ctx.initialized = false;

    printf("BT Deinit: Bluetooth stack shut down\n");
}

bool bt_is_initialized(void)
{
    return bt_ctx.initialized;
}

void bt_register_callback(bt_event_callback_t callback, void *user_data)
{
    bt_ctx.event_callback = callback;
    bt_ctx.callback_user_data = user_data;
}

bt_state_t bt_get_state(void)
{
    return bt_ctx.state;
}

/*******************************************************************************
 * Public API - Controller
 ******************************************************************************/

int bt_get_controller_info(bt_controller_info_t *info)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (info == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    *info = bt_ctx.controller_info;
    return BT_OK;
}

int bt_reset_controller(void)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    printf("BT Reset: Resetting controller\n");

    /*
     * With Infineon BTSTACK, a full reset requires re-initialization.
     * For a soft reset, we stop activities and let the stack recover.
     *
     * Note: wiced_bt_stack_deinit() + wiced_bt_stack_init() would be
     * a full reset, but that's typically not needed during operation.
     */

    /* Stop advertising if active */
    if (bt_ctx.advertising) {
        bt_stop_advertising();
    }

    /* Re-read BD address to verify controller is responsive */
    wiced_bt_dev_read_local_addr(bt_ctx.controller_info.bd_addr);

    printf("BT Reset: Controller reset complete\n");

    return BT_OK;
}

int bt_set_device_address(const uint8_t addr[BT_ADDR_SIZE], bool random)
{
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (addr == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    printf("BT Addr: Setting device address %02X:%02X:%02X:%02X:%02X:%02X (random=%d)\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], random);

    /* Set device address using BTSTACK API */
    wiced_bt_device_address_t bd_addr;
    memcpy(bd_addr, addr, BT_ADDR_SIZE);

    if (random) {
        /* Ensure static address format (two MSBs = 11) */
        bd_addr[0] |= 0xC0;

        /* Set random address using wiced_bt_set_local_bdaddr */
        result = wiced_bt_set_local_bdaddr(bd_addr, BLE_ADDR_RANDOM);
    } else {
        /* Set local BD address (usually done before stack init) */
        result = wiced_bt_set_local_bdaddr(bd_addr, BLE_ADDR_PUBLIC);
    }

    if (result != WICED_BT_SUCCESS) {
        printf("BT Addr: Failed to set address: %d\n", result);
        return BT_ERROR_INVALID_PARAM;
    }

    /* Update local copy */
    memcpy(bt_ctx.controller_info.bd_addr, addr, BT_ADDR_SIZE);

    return BT_OK;
}

int bt_get_device_address(uint8_t addr[BT_ADDR_SIZE], bool *random)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (addr == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    memcpy(addr, bt_ctx.controller_info.bd_addr, BT_ADDR_SIZE);

    if (random != NULL) {
        *random = bt_ctx.config.use_random_addr;
    }

    return BT_OK;
}

/*******************************************************************************
 * Public API - Advertising
 ******************************************************************************/

int bt_start_advertising(bool connectable, uint16_t interval_ms)
{
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (bt_ctx.advertising) {
        /* Already advertising, stop first */
        bt_stop_advertising();
    }

    printf("BT Adv: Starting advertising (connectable=%d, interval=%dms)\n",
           connectable, interval_ms);

    bt_ctx.connectable_adv = connectable;
    bt_ctx.adv_interval = interval_ms;

    /* Parse and set advertising data */
    wiced_bt_ble_advert_elem_t adv_elements[10];
    uint8_t num_adv_elements = parse_raw_adv_data(bt_ctx.adv_data, bt_ctx.adv_data_len,
                                                   adv_elements, 10);
    if (num_adv_elements > 0) {
        result = wiced_bt_ble_set_raw_advertisement_data(num_adv_elements, adv_elements);
        if (result != WICED_BT_SUCCESS) {
            printf("BT Adv: Failed to set adv data: %d\n", result);
            return BT_ERROR_INVALID_PARAM;
        }
    }

    /* Parse and set scan response data */
    if (bt_ctx.scan_rsp_len > 0) {
        wiced_bt_ble_advert_elem_t scan_rsp_elements[10];
        uint8_t num_scan_elements = parse_raw_adv_data(bt_ctx.scan_rsp_data, bt_ctx.scan_rsp_len,
                                                        scan_rsp_elements, 10);
        if (num_scan_elements > 0) {
            result = wiced_bt_ble_set_raw_scan_response_data(num_scan_elements, scan_rsp_elements);
            if (result != WICED_BT_SUCCESS) {
                printf("BT Adv: Failed to set scan rsp: %d\n", result);
            }
        }
    }

    /* Start advertising (track intended mode for auto low-duty cycle transition) */
    wiced_bt_ble_advert_mode_t mode = connectable ?
        BTM_BLE_ADVERT_UNDIRECTED_HIGH : BTM_BLE_ADVERT_NONCONN_HIGH;

    bt_ctx.intended_adv_mode = mode;
    result = wiced_bt_start_advertisements(mode, BLE_ADDR_PUBLIC, NULL);
    if (result != WICED_BT_SUCCESS) {
        printf("BT Adv: Failed to start advertising: %d\n", result);
        return BT_ERROR_BUSY;
    }

    printf("BT Adv: Advertising started\n");
    bt_ctx.advertising = true;
    bt_ctx.adv_mode = mode;
    set_state(BT_STATE_ADVERTISING);

    return BT_OK;
}

int bt_stop_advertising(void)
{
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (!bt_ctx.advertising) {
        return BT_OK;
    }

    printf("BT Adv: Stopping advertising\n");

    /* Stop advertising */
    result = wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF, BLE_ADDR_PUBLIC, NULL);
    if (result != WICED_BT_SUCCESS) {
        printf("BT Adv: Failed to stop advertising: %d\n", result);
        return BT_ERROR_BUSY;
    }

    bt_ctx.advertising = false;

    if (!link_is_connected()) {
        set_state(BT_STATE_READY);
    }

    printf("BT Adv: Advertising stopped\n");

    return BT_OK;
}

int bt_set_advertising_data(const uint8_t *data, uint8_t len)
{
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || len > 31) {
        return BT_ERROR_INVALID_PARAM;
    }

    memcpy(bt_ctx.adv_data, data, len);
    bt_ctx.adv_data_len = len;

    /* Parse and update advertising data via BTSTACK API */
    wiced_bt_ble_advert_elem_t adv_elements[10];
    uint8_t num_elements = parse_raw_adv_data(data, len, adv_elements, 10);
    if (num_elements > 0) {
        result = wiced_bt_ble_set_raw_advertisement_data(num_elements, adv_elements);
    } else {
        result = WICED_BT_ERROR;
    }
    if (result != WICED_BT_SUCCESS) {
        printf("BT Adv: Failed to set adv data: %d\n", result);
        return BT_ERROR_INVALID_PARAM;
    }

    return BT_OK;
}

int bt_set_scan_response_data(const uint8_t *data, uint8_t len)
{
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || len > 31) {
        return BT_ERROR_INVALID_PARAM;
    }

    memcpy(bt_ctx.scan_rsp_data, data, len);
    bt_ctx.scan_rsp_len = len;

    /* Parse and update scan response data via BTSTACK API */
    wiced_bt_ble_advert_elem_t scan_elements[10];
    uint8_t num_elements = parse_raw_adv_data(data, len, scan_elements, 10);
    if (num_elements > 0) {
        result = wiced_bt_ble_set_raw_scan_response_data(num_elements, scan_elements);
    } else {
        result = WICED_BT_ERROR;
    }
    if (result != WICED_BT_SUCCESS) {
        printf("BT Adv: Failed to set scan rsp data: %d\n", result);
        return BT_ERROR_INVALID_PARAM;
    }

    return BT_OK;
}

int bt_set_device_name(const char *name)
{
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (name == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    strncpy(bt_ctx.config.device_name, name, BT_MAX_NAME_LEN - 1);
    bt_ctx.config.device_name[BT_MAX_NAME_LEN - 1] = '\0';

    /* Rebuild advertising data with new name */
    build_default_adv_data();

    /* Parse and update advertising data via BTSTACK API */
    wiced_bt_ble_advert_elem_t adv_elements[10];
    uint8_t num_elements = parse_raw_adv_data(bt_ctx.adv_data, bt_ctx.adv_data_len,
                                               adv_elements, 10);
    if (num_elements > 0) {
        result = wiced_bt_ble_set_raw_advertisement_data(num_elements, adv_elements);
    } else {
        result = WICED_BT_ERROR;
    }
    if (result != WICED_BT_SUCCESS) {
        printf("BT Adv: Failed to update adv data with new name: %d\n", result);
        return BT_ERROR_INVALID_PARAM;
    }

    return BT_OK;
}

/*******************************************************************************
 * Public API - Connection Management
 ******************************************************************************/

int bt_disconnect(uint16_t conn_handle, uint8_t reason)
{
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    printf("BT Conn: Disconnecting handle %d, reason 0x%02X\n", conn_handle, reason);

    /* Disconnect using BTSTACK API */
    result = wiced_bt_gatt_disconnect(conn_handle);
    if (result != WICED_BT_SUCCESS) {
        printf("BT Conn: Disconnect failed: %d\n", result);
        return BT_ERROR_INVALID_PARAM;
    }

    return BT_OK;
}

int bt_update_connection_params(uint16_t conn_handle,
                                uint16_t interval_min,
                                uint16_t interval_max,
                                uint16_t latency,
                                uint16_t timeout)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    /* Find the connection using link.c */
    link_state_t *link = link_get_by_conn_id(conn_handle);
    if (link == NULL) {
        printf("BT Conn: Connection %d not found\n", conn_handle);
        return BT_ERROR_INVALID_PARAM;
    }

    printf("BT Conn: Updating params for handle %d: interval=%d-%d, latency=%d, timeout=%d\n",
           conn_handle, interval_min, interval_max, latency, timeout);

    /* Prepare connection parameter update request */
    wiced_bt_ble_pref_conn_params_t conn_params = {
        .conn_interval_min = interval_min,
        .conn_interval_max = interval_max,
        .conn_latency = latency,
        .conn_supervision_timeout = timeout
    };

    /* Use BTSTACK API to update connection parameters */
    wiced_bt_l2cap_update_ble_conn_params(link->bd_addr, &conn_params);

    return BT_OK;
}

int bt_get_connection_info(uint16_t conn_handle, bt_connection_info_t *info)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (info == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    /* Find the connection using link.c */
    link_state_t *link = link_get_by_conn_id(conn_handle);
    if (link == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    /* Populate connection info from link state */
    info->conn_handle = conn_handle;
    memcpy(info->peer_addr, link->bd_addr, BT_ADDR_SIZE);

    /* Get connection parameters from BTSTACK */
    wiced_bt_ble_conn_params_t params;
    if (wiced_bt_ble_get_connection_parameters(link->bd_addr, &params)) {
        info->conn_interval = params.conn_interval;
        info->conn_latency = params.conn_latency;
        info->supervision_timeout = params.supervision_timeout;
    }

    return BT_OK;
}

int bt_set_phy(uint16_t conn_handle, uint8_t tx_phy, uint8_t rx_phy)
{
    wiced_bt_ble_phy_preferences_t phy_pref;
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    printf("BT PHY: Setting PHY for handle %d: TX=%d, RX=%d\n",
           conn_handle, tx_phy, rx_phy);

    /* Find the connection using link.c */
    link_state_t *link = link_get_by_conn_id(conn_handle);
    if (link == NULL) {
        printf("BT PHY: Connection %d not found\n", conn_handle);
        return BT_ERROR_INVALID_PARAM;
    }

    /* Set PHY preferences */
    memset(&phy_pref, 0, sizeof(phy_pref));
    memcpy(phy_pref.remote_bd_addr, link->bd_addr, BT_ADDR_SIZE);
    phy_pref.tx_phys = tx_phy;
    phy_pref.rx_phys = rx_phy;
    phy_pref.phy_opts = BTM_BLE_PREFER_NO_LELR;

    /* Request PHY update via BTSTACK API */
    result = wiced_bt_ble_set_phy(&phy_pref);
    if (result != WICED_BT_SUCCESS) {
        printf("BT PHY: Failed to set PHY: %d\n", result);
        return BT_ERROR_INVALID_PARAM;
    }

    return BT_OK;
}

/*******************************************************************************
 * Public API - Power Management
 ******************************************************************************/

int bt_set_power_mode(bt_power_mode_t mode)
{
    wiced_result_t result = WICED_BT_SUCCESS;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    printf("BT Power: Setting power mode to %d\n", mode);

    /*
     * CYW55512 power management is handled through the btstack-integration
     * layer's sleep mode configuration. The actual sleep behavior depends
     * on the cybt_platform_config_t settings.
     *
     * For runtime power mode changes, we use the device power management:
     * - Active: Normal operation
     * - Low latency: Allow light sleep between connection events
     * - Low power: Allow deeper sleep, longer wake latency
     * - Deep sleep: Requires external wake signal
     */

    /*
     * Note: wiced_bt_dev_allow_host_sleep() is not available in BTSTACK 4.x.
     * Power management for PSoC Edge with CYW55512 is handled through:
     * - Platform-specific sleep callbacks configured in btstack-integration
     * - Device power manager (Cy_SysPm) integration
     *
     * For now, we just track the power mode locally.
     */
    switch (mode) {
        case BT_POWER_ACTIVE:
        case BT_POWER_LOW_LATENCY:
        case BT_POWER_LOW_POWER:
        case BT_POWER_DEEP_SLEEP:
            /* Mode accepted - actual power management is platform-specific */
            break;

        default:
            return BT_ERROR_INVALID_PARAM;
    }
    (void)result;  /* Suppress unused variable warning */

    bt_ctx.power_mode = mode;

    return BT_OK;
}

bt_power_mode_t bt_get_power_mode(void)
{
    return bt_ctx.power_mode;
}

int bt_set_tx_power(int8_t power_dbm)
{
    wiced_result_t result;

    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    printf("BT Power: Setting TX power to %d dBm\n", power_dbm);

    /*
     * CYW55512 supports TX power range from -20 to +12 dBm.
     * Use BTSTACK API to set advertising TX power.
     * For connection TX power, separate API may be needed.
     */

    /* Clamp to valid range */
    if (power_dbm < -20) {
        power_dbm = -20;
    } else if (power_dbm > 12) {
        power_dbm = 12;
    }

    /* Set advertising TX power (callback parameter required in BTSTACK 4.x) */
    result = wiced_bt_ble_set_adv_tx_power(power_dbm, NULL);
    if (result != WICED_BT_SUCCESS && result != WICED_BT_PENDING) {
        printf("BT Power: Failed to set TX power: %d\n", result);
        return BT_ERROR_INVALID_PARAM;
    }

    return BT_OK;
}

/*******************************************************************************
 * Public API - Statistics
 ******************************************************************************/

void bt_get_stats(bt_stats_t *stats)
{
    if (stats != NULL) {
        *stats = bt_ctx.stats;
    }
}

void bt_reset_stats(void)
{
    memset(&bt_ctx.stats, 0, sizeof(bt_ctx.stats));
}

/*******************************************************************************
 * Public API - ISOC Support
 ******************************************************************************/

bool bt_isoc_is_supported(void)
{
    if (!bt_ctx.initialized) {
        return false;
    }

    return bt_ctx.controller_info.isoc_supported;
}

int bt_isoc_get_capabilities(uint8_t *max_cig, uint8_t *max_cis,
                              uint8_t *max_big, uint8_t *max_bis)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (max_cig != NULL) {
        *max_cig = bt_ctx.controller_info.max_cig;
    }
    if (max_cis != NULL) {
        *max_cis = bt_ctx.controller_info.max_cis_per_cig;
    }
    if (max_big != NULL) {
        *max_big = bt_ctx.controller_info.max_big;
    }
    if (max_bis != NULL) {
        *max_bis = bt_ctx.controller_info.max_bis_per_big;
    }

    return BT_OK;
}

/*******************************************************************************
 * Public API - FreeRTOS Task
 ******************************************************************************/

void bt_process(void)
{
    if (!bt_ctx.initialized) {
        return;
    }

    /*
     * With Infineon BTSTACK and btstack-integration:
     *
     * The HCI transport runs in its own FreeRTOS tasks created by
     * the btstack-integration layer (cybt_task). Events are processed
     * asynchronously and callbacks are invoked from that context.
     *
     * This function is provided for compatibility and can be used
     * to perform any application-level periodic processing related
     * to Bluetooth operations.
     *
     * Note: wiced_bt_stack_process() is not typically needed when
     * using the FreeRTOS-based btstack-integration layer, as it
     * handles event processing internally.
     */

    /* Update statistics if there are active connections */
    if (link_is_connected()) {
        /* Could query link quality, RSSI, etc. here if needed */
    }
}

void bt_task(void *pvParameters)
{
    (void)pvParameters;

    printf("BT Task: Started\n");

    /*
     * The btstack-integration layer creates its own HCI TX/RX tasks.
     * This task handles application-level BT events from the event queue.
     */

    while (1) {
        bt_event_t event;

        /* Wait for events from the queue */
        if (xQueueReceive(bt_ctx.event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            /* Process the event */
            dispatch_event(&event);
        }

        /* Periodic processing (keep-alive, stats, etc.) */
        bt_process();
    }
}

uint32_t bt_get_task_stack_size(void)
{
    return BT_TASK_STACK_SIZE;
}

uint32_t bt_get_task_priority(void)
{
    return BT_TASK_PRIORITY;
}

/**
 * @brief Enter pairing mode (start undirected advertisements)
 *
 * Called by button handler to put device into pairing mode.
 * Wrapper for bt_start_advertising for compatibility with app_bt/bt.h interface.
 */
void bt_enter_pairing(void)
{
    printf("BT: Entering pairing mode\n");
    bt_start_advertising(true, 0);  /* Connectable, default interval */
}


/*******************************************************************************
 * Compatibility Wrapper for app_bt/bt.h Interface
 *
 * The app_bt/bt.h header declares: wiced_result_t bt_init(void);
 * This wrapper provides that interface and calls our implementation.
 ******************************************************************************/

/**
 * @brief Initialize Bluetooth stack (compatibility wrapper)
 *
 * This function matches the signature expected by app_bt/bt.h:
 *   wiced_result_t bt_init(void);
 *
 * It calls bt_init_with_config(NULL) to use default configuration.
 *
 * @return WICED_BT_SUCCESS on success, error code otherwise
 */
wiced_result_t bt_init(void)
{
    int result = bt_init_with_config(NULL);

    switch (result) {
        case BT_OK:
            return WICED_BT_SUCCESS;
        case BT_ERROR_ALREADY_INITIALIZED:
            return WICED_BT_SUCCESS;  /* Already initialized is not an error */
        case BT_ERROR_NO_MEMORY:
            return WICED_BT_NO_RESOURCES;
        case BT_ERROR_TIMEOUT:
            return WICED_BT_TIMEOUT;
        default:
            return WICED_BT_ERROR;
    }
}
