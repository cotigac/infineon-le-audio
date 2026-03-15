/**
 * @file gap_config.h
 * @brief GAP (Generic Access Profile) Configuration API
 *
 * This module provides configuration and management of GAP features:
 *
 * - Legacy and Extended Advertising
 * - Periodic Advertising (for Auracast broadcast)
 * - Scanning (legacy and extended)
 * - Connection parameter management
 * - Device identity and appearance
 * - Privacy and bonding settings
 *
 * LE Audio specific features:
 * - Extended advertising for BAP announcements
 * - Periodic advertising for broadcast audio
 * - Scan for broadcast sources (BASS)
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GAP_CONFIG_H
#define GAP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum device name length */
#define GAP_MAX_DEVICE_NAME_LEN         248

/** Maximum advertising data length (legacy) */
#define GAP_MAX_ADV_DATA_LEN            31

/** Maximum extended advertising data length */
#define GAP_MAX_EXT_ADV_DATA_LEN        254

/** Maximum periodic advertising data length */
#define GAP_MAX_PERIODIC_ADV_DATA_LEN   254

/** Maximum scan response data length */
#define GAP_MAX_SCAN_RSP_DATA_LEN       31

/** Maximum number of advertising sets */
#define GAP_MAX_ADV_SETS                4

/** Maximum number of scan filters */
#define GAP_MAX_SCAN_FILTERS            8

/** Advertising handle for legacy advertising */
#define GAP_ADV_HANDLE_LEGACY           0x00

/*******************************************************************************
 * Advertising Types
 ******************************************************************************/

/** Legacy advertising types */
typedef enum {
    GAP_ADV_TYPE_CONNECTABLE_SCANNABLE      = 0x00,
    GAP_ADV_TYPE_CONNECTABLE_HIGH_DUTY      = 0x01,
    GAP_ADV_TYPE_SCANNABLE_NONCONNECTABLE   = 0x02,
    GAP_ADV_TYPE_NONCONNECTABLE             = 0x03,
    GAP_ADV_TYPE_SCAN_RESPONSE              = 0x04
} gap_adv_type_t;

/** Extended advertising types (bitmask) */
typedef enum {
    GAP_EXT_ADV_CONNECTABLE     = 0x01,
    GAP_EXT_ADV_SCANNABLE       = 0x02,
    GAP_EXT_ADV_DIRECTED        = 0x04,
    GAP_EXT_ADV_HIGH_DUTY       = 0x08,
    GAP_EXT_ADV_LEGACY          = 0x10,
    GAP_EXT_ADV_ANONYMOUS       = 0x20,
    GAP_EXT_ADV_INCLUDE_TX_PWR  = 0x40
} gap_ext_adv_type_t;

/** Advertising PHY options */
typedef enum {
    GAP_PHY_1M      = 0x01,
    GAP_PHY_2M      = 0x02,
    GAP_PHY_CODED   = 0x03
} gap_phy_t;

/** Advertising filter policy */
typedef enum {
    GAP_ADV_FILTER_ALLOW_ALL            = 0x00,
    GAP_ADV_FILTER_ALLOW_SCAN_WLIST     = 0x01,
    GAP_ADV_FILTER_ALLOW_CONN_WLIST     = 0x02,
    GAP_ADV_FILTER_ALLOW_ALL_WLIST      = 0x03
} gap_adv_filter_t;

/*******************************************************************************
 * Scanning Types
 ******************************************************************************/

/** Scan types */
typedef enum {
    GAP_SCAN_TYPE_PASSIVE   = 0x00,
    GAP_SCAN_TYPE_ACTIVE    = 0x01
} gap_scan_type_t;

/** Scan filter policy */
typedef enum {
    GAP_SCAN_FILTER_ACCEPT_ALL          = 0x00,
    GAP_SCAN_FILTER_WHITELIST_ONLY      = 0x01,
    GAP_SCAN_FILTER_NOT_RESOLVED        = 0x02,
    GAP_SCAN_FILTER_WHITELIST_RESOLVED  = 0x03
} gap_scan_filter_t;

/** Scan filter duplicate mode */
typedef enum {
    GAP_SCAN_DUP_FILTER_DISABLED    = 0x00,
    GAP_SCAN_DUP_FILTER_ENABLED     = 0x01,
    GAP_SCAN_DUP_FILTER_RESET       = 0x02
} gap_scan_dup_filter_t;

/*******************************************************************************
 * Address Types
 ******************************************************************************/

typedef enum {
    GAP_ADDR_TYPE_PUBLIC            = 0x00,
    GAP_ADDR_TYPE_RANDOM            = 0x01,
    GAP_ADDR_TYPE_PUBLIC_IDENTITY   = 0x02,
    GAP_ADDR_TYPE_RANDOM_IDENTITY   = 0x03
} gap_addr_type_t;

/*******************************************************************************
 * Appearance Values (Bluetooth SIG Assigned Numbers)
 ******************************************************************************/

typedef enum {
    GAP_APPEARANCE_UNKNOWN                  = 0x0000,
    GAP_APPEARANCE_GENERIC_PHONE            = 0x0040,
    GAP_APPEARANCE_GENERIC_COMPUTER         = 0x0080,
    GAP_APPEARANCE_GENERIC_WATCH            = 0x00C0,
    GAP_APPEARANCE_GENERIC_AUDIO_SINK       = 0x0841,
    GAP_APPEARANCE_GENERIC_AUDIO_SOURCE     = 0x0842,
    GAP_APPEARANCE_GENERIC_HEARING_AID      = 0x0A40,
    GAP_APPEARANCE_HEADSET                  = 0x0941,
    GAP_APPEARANCE_HEADPHONES               = 0x0942,
    GAP_APPEARANCE_SPEAKER                  = 0x0943,
    GAP_APPEARANCE_SOUNDBAR                 = 0x0944,
    GAP_APPEARANCE_GENERIC_MEDIA_PLAYER     = 0x0980,
    GAP_APPEARANCE_GENERIC_MICROPHONE       = 0x09C0,
    GAP_APPEARANCE_GENERIC_BROADCAST_DEVICE = 0x0A00,
    GAP_APPEARANCE_BROADCAST_AUDIO_SOURCE   = 0x0A01,
    GAP_APPEARANCE_BROADCAST_AUDIO_SINK     = 0x0A02,
    GAP_APPEARANCE_EARBUD                   = 0x0941,
    GAP_APPEARANCE_MUSICAL_INSTRUMENT       = 0x0980
} gap_appearance_t;

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    GAP_OK = 0,
    GAP_ERROR_INVALID_PARAM = -1,
    GAP_ERROR_NOT_INITIALIZED = -2,
    GAP_ERROR_ALREADY_INITIALIZED = -3,
    GAP_ERROR_NO_RESOURCES = -4,
    GAP_ERROR_NOT_FOUND = -5,
    GAP_ERROR_BUSY = -6,
    GAP_ERROR_INVALID_STATE = -7,
    GAP_ERROR_HCI_ERROR = -8,
    GAP_ERROR_TIMEOUT = -9
} gap_error_t;

/*******************************************************************************
 * AD Types (Advertising Data Types)
 ******************************************************************************/

/** Common AD types from Bluetooth SIG */
#define GAP_AD_TYPE_FLAGS                       0x01
#define GAP_AD_TYPE_INCOMPLETE_16BIT_UUIDS      0x02
#define GAP_AD_TYPE_COMPLETE_16BIT_UUIDS        0x03
#define GAP_AD_TYPE_INCOMPLETE_32BIT_UUIDS      0x04
#define GAP_AD_TYPE_COMPLETE_32BIT_UUIDS        0x05
#define GAP_AD_TYPE_INCOMPLETE_128BIT_UUIDS     0x06
#define GAP_AD_TYPE_COMPLETE_128BIT_UUIDS       0x07
#define GAP_AD_TYPE_SHORTENED_LOCAL_NAME        0x08
#define GAP_AD_TYPE_COMPLETE_LOCAL_NAME         0x09
#define GAP_AD_TYPE_TX_POWER_LEVEL              0x0A
#define GAP_AD_TYPE_CLASS_OF_DEVICE             0x0D
#define GAP_AD_TYPE_APPEARANCE                  0x19
#define GAP_AD_TYPE_ADVERTISING_INTERVAL        0x1A
#define GAP_AD_TYPE_SERVICE_DATA_16BIT          0x16
#define GAP_AD_TYPE_SERVICE_DATA_32BIT          0x20
#define GAP_AD_TYPE_SERVICE_DATA_128BIT         0x21
#define GAP_AD_TYPE_MANUFACTURER_DATA           0xFF

/** LE Audio specific AD types */
#define GAP_AD_TYPE_BROADCAST_NAME              0x30
#define GAP_AD_TYPE_RSI                         0x2E    /**< Resolvable Set Identifier */

/** AD Flags */
#define GAP_AD_FLAG_LIMITED_DISCOVERABLE        0x01
#define GAP_AD_FLAG_GENERAL_DISCOVERABLE        0x02
#define GAP_AD_FLAG_BR_EDR_NOT_SUPPORTED        0x04
#define GAP_AD_FLAG_LE_BR_EDR_CONTROLLER        0x08
#define GAP_AD_FLAG_LE_BR_EDR_HOST              0x10

/*******************************************************************************
 * Configuration Structures
 ******************************************************************************/

/** Bluetooth device address */
typedef struct {
    uint8_t addr[6];
    gap_addr_type_t type;
} gap_address_t;

/** Legacy advertising parameters */
typedef struct {
    uint16_t adv_interval_min;      /**< Min interval (0.625ms units) */
    uint16_t adv_interval_max;      /**< Max interval (0.625ms units) */
    gap_adv_type_t adv_type;        /**< Advertising type */
    gap_addr_type_t own_addr_type;  /**< Own address type */
    gap_address_t peer_addr;        /**< Peer address (for directed) */
    uint8_t adv_channel_map;        /**< Channel map (bit 0=37, 1=38, 2=39) */
    gap_adv_filter_t filter_policy; /**< Filter policy */
} gap_adv_params_t;

/** Extended advertising parameters */
typedef struct {
    uint8_t adv_handle;             /**< Advertising set handle */
    uint16_t adv_event_properties;  /**< Event properties (gap_ext_adv_type_t) */
    uint32_t primary_adv_interval_min;  /**< Min interval (0.625ms units) */
    uint32_t primary_adv_interval_max;  /**< Max interval (0.625ms units) */
    uint8_t primary_adv_channel_map;    /**< Channel map */
    gap_addr_type_t own_addr_type;  /**< Own address type */
    gap_address_t peer_addr;        /**< Peer address */
    gap_adv_filter_t filter_policy; /**< Filter policy */
    int8_t adv_tx_power;            /**< TX power (-127 to +20 dBm) */
    gap_phy_t primary_adv_phy;      /**< Primary PHY */
    uint8_t secondary_adv_max_skip; /**< Secondary max skip */
    gap_phy_t secondary_adv_phy;    /**< Secondary PHY */
    uint8_t adv_sid;                /**< Advertising SID */
    bool scan_req_notify_enable;    /**< Enable scan request notification */
} gap_ext_adv_params_t;

/** Periodic advertising parameters */
typedef struct {
    uint8_t adv_handle;             /**< Associated ext adv handle */
    uint16_t periodic_adv_interval_min;  /**< Min interval (1.25ms units) */
    uint16_t periodic_adv_interval_max;  /**< Max interval (1.25ms units) */
    uint16_t periodic_adv_properties;    /**< Properties (include TX power) */
} gap_periodic_adv_params_t;

/** Scan parameters */
typedef struct {
    gap_scan_type_t scan_type;      /**< Passive or active */
    uint16_t scan_interval;         /**< Scan interval (0.625ms units) */
    uint16_t scan_window;           /**< Scan window (0.625ms units) */
    gap_addr_type_t own_addr_type;  /**< Own address type */
    gap_scan_filter_t filter_policy; /**< Filter policy */
} gap_scan_params_t;

/** Extended scan parameters */
typedef struct {
    gap_addr_type_t own_addr_type;
    gap_scan_filter_t filter_policy;
    uint8_t scanning_phys;          /**< PHY bitmask (1M, Coded) */
    /* Per-PHY parameters */
    struct {
        gap_scan_type_t scan_type;
        uint16_t scan_interval;
        uint16_t scan_window;
    } phy_params[2];                /**< [0]=1M, [1]=Coded */
} gap_ext_scan_params_t;

/** Connection parameters */
typedef struct {
    uint16_t conn_interval_min;     /**< Min interval (1.25ms units) */
    uint16_t conn_interval_max;     /**< Max interval (1.25ms units) */
    uint16_t conn_latency;          /**< Slave latency */
    uint16_t supervision_timeout;   /**< Supervision timeout (10ms units) */
    uint16_t min_ce_length;         /**< Min CE length (0.625ms units) */
    uint16_t max_ce_length;         /**< Max CE length (0.625ms units) */
} gap_conn_params_t;

/*******************************************************************************
 * Advertising Data Builder
 ******************************************************************************/

/** Advertising data builder context */
typedef struct {
    uint8_t data[GAP_MAX_EXT_ADV_DATA_LEN];
    uint16_t length;
    uint16_t max_length;
} gap_adv_data_builder_t;

/*******************************************************************************
 * Event Types
 ******************************************************************************/

typedef enum {
    GAP_EVENT_ADV_STARTED,
    GAP_EVENT_ADV_STOPPED,
    GAP_EVENT_ADV_SET_TERMINATED,
    GAP_EVENT_SCAN_STARTED,
    GAP_EVENT_SCAN_STOPPED,
    GAP_EVENT_SCAN_RESULT,
    GAP_EVENT_EXT_SCAN_RESULT,
    GAP_EVENT_PERIODIC_ADV_SYNC,
    GAP_EVENT_PERIODIC_ADV_REPORT,
    GAP_EVENT_PERIODIC_ADV_SYNC_LOST,
    GAP_EVENT_CONNECTION_COMPLETE,
    GAP_EVENT_DISCONNECTION,
    GAP_EVENT_CONN_PARAM_UPDATE,
    GAP_EVENT_ERROR
} gap_event_type_t;

/** Scan result data */
typedef struct {
    gap_address_t address;
    int8_t rssi;
    uint8_t adv_type;
    uint8_t data[GAP_MAX_ADV_DATA_LEN];
    uint8_t data_len;
} gap_scan_result_t;

/** Extended scan result data */
typedef struct {
    gap_address_t address;
    int8_t rssi;
    int8_t tx_power;
    uint8_t adv_type;
    uint8_t primary_phy;
    uint8_t secondary_phy;
    uint8_t adv_sid;
    uint16_t periodic_adv_interval;
    uint8_t data[GAP_MAX_EXT_ADV_DATA_LEN];
    uint16_t data_len;
} gap_ext_scan_result_t;

/** Periodic advertising sync info */
typedef struct {
    uint16_t sync_handle;
    uint8_t adv_sid;
    gap_address_t address;
    uint8_t adv_phy;
    uint16_t periodic_adv_interval;
    uint8_t adv_clock_accuracy;
} gap_periodic_sync_t;

/** Periodic advertising report */
typedef struct {
    uint16_t sync_handle;
    int8_t tx_power;
    int8_t rssi;
    uint8_t data_status;
    uint8_t data[GAP_MAX_PERIODIC_ADV_DATA_LEN];
    uint16_t data_len;
} gap_periodic_report_t;

/** GAP event data */
typedef struct {
    gap_event_type_t type;
    union {
        uint8_t adv_handle;
        gap_scan_result_t scan_result;
        gap_ext_scan_result_t ext_scan_result;
        gap_periodic_sync_t periodic_sync;
        gap_periodic_report_t periodic_report;
        struct {
            uint16_t conn_handle;
            gap_address_t peer_addr;
            gap_conn_params_t params;
        } connection;
        struct {
            uint16_t conn_handle;
            uint8_t reason;
        } disconnection;
        int error_code;
    } data;
} gap_event_t;

/** GAP event callback */
typedef void (*gap_callback_t)(const gap_event_t *event, void *user_data);

/*******************************************************************************
 * Default Configurations
 ******************************************************************************/

/** Default legacy advertising parameters (connectable, 100ms interval) */
#define GAP_ADV_PARAMS_DEFAULT { \
    .adv_interval_min = 160,  /* 100ms */ \
    .adv_interval_max = 160, \
    .adv_type = GAP_ADV_TYPE_CONNECTABLE_SCANNABLE, \
    .own_addr_type = GAP_ADDR_TYPE_PUBLIC, \
    .peer_addr = {{0}, GAP_ADDR_TYPE_PUBLIC}, \
    .adv_channel_map = 0x07, \
    .filter_policy = GAP_ADV_FILTER_ALLOW_ALL \
}

/** Extended advertising for LE Audio (non-connectable, 100ms) */
#define GAP_EXT_ADV_PARAMS_LE_AUDIO { \
    .adv_handle = 1, \
    .adv_event_properties = 0,  /* Non-connectable, non-scannable */ \
    .primary_adv_interval_min = 160, \
    .primary_adv_interval_max = 160, \
    .primary_adv_channel_map = 0x07, \
    .own_addr_type = GAP_ADDR_TYPE_PUBLIC, \
    .peer_addr = {{0}, GAP_ADDR_TYPE_PUBLIC}, \
    .filter_policy = GAP_ADV_FILTER_ALLOW_ALL, \
    .adv_tx_power = 0, \
    .primary_adv_phy = GAP_PHY_1M, \
    .secondary_adv_max_skip = 0, \
    .secondary_adv_phy = GAP_PHY_2M, \
    .adv_sid = 0, \
    .scan_req_notify_enable = false \
}

/** Periodic advertising for Auracast (100ms interval) */
#define GAP_PERIODIC_ADV_PARAMS_AURACAST { \
    .adv_handle = 1, \
    .periodic_adv_interval_min = 80,  /* 100ms */ \
    .periodic_adv_interval_max = 80, \
    .periodic_adv_properties = 0 \
}

/** Default scan parameters (active, 100ms interval, 50ms window) */
#define GAP_SCAN_PARAMS_DEFAULT { \
    .scan_type = GAP_SCAN_TYPE_ACTIVE, \
    .scan_interval = 160,  /* 100ms */ \
    .scan_window = 80,     /* 50ms */ \
    .own_addr_type = GAP_ADDR_TYPE_PUBLIC, \
    .filter_policy = GAP_SCAN_FILTER_ACCEPT_ALL \
}

/** Default connection parameters (30ms interval, 4s timeout) */
#define GAP_CONN_PARAMS_DEFAULT { \
    .conn_interval_min = 24,   /* 30ms */ \
    .conn_interval_max = 40,   /* 50ms */ \
    .conn_latency = 0, \
    .supervision_timeout = 400,  /* 4s */ \
    .min_ce_length = 0, \
    .max_ce_length = 0 \
}

/** LE Audio optimized connection parameters (low latency) */
#define GAP_CONN_PARAMS_LE_AUDIO { \
    .conn_interval_min = 8,    /* 10ms */ \
    .conn_interval_max = 16,   /* 20ms */ \
    .conn_latency = 0, \
    .supervision_timeout = 200,  /* 2s */ \
    .min_ce_length = 0, \
    .max_ce_length = 0 \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize GAP module
 *
 * @return GAP_OK on success
 */
int gap_init(void);

/**
 * @brief Deinitialize GAP module
 */
void gap_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Event callback function
 * @param user_data User context
 */
void gap_register_callback(gap_callback_t callback, void *user_data);

/*******************************************************************************
 * API Functions - Device Identity
 ******************************************************************************/

/**
 * @brief Set device name
 *
 * @param name Device name (null-terminated)
 * @return GAP_OK on success
 */
int gap_set_device_name(const char *name);

/**
 * @brief Get device name
 *
 * @param name Output buffer
 * @param max_len Buffer size
 * @return GAP_OK on success
 */
int gap_get_device_name(char *name, uint16_t max_len);

/**
 * @brief Set device appearance
 *
 * @param appearance Appearance value
 * @return GAP_OK on success
 */
int gap_set_appearance(uint16_t appearance);

/**
 * @brief Get device appearance
 *
 * @return Appearance value
 */
uint16_t gap_get_appearance(void);

/**
 * @brief Get local Bluetooth address
 *
 * @param address Output address
 * @return GAP_OK on success
 */
int gap_get_local_address(gap_address_t *address);

/**
 * @brief Set random address
 *
 * @param address Random address to set
 * @return GAP_OK on success
 */
int gap_set_random_address(const uint8_t *address);

/*******************************************************************************
 * API Functions - Legacy Advertising
 ******************************************************************************/

/**
 * @brief Set legacy advertising parameters
 *
 * @param params Advertising parameters
 * @return GAP_OK on success
 */
int gap_set_adv_params(const gap_adv_params_t *params);

/**
 * @brief Set legacy advertising data
 *
 * @param data Advertising data
 * @param len Data length (max 31)
 * @return GAP_OK on success
 */
int gap_set_adv_data(const uint8_t *data, uint8_t len);

/**
 * @brief Set scan response data
 *
 * @param data Scan response data
 * @param len Data length (max 31)
 * @return GAP_OK on success
 */
int gap_set_scan_rsp_data(const uint8_t *data, uint8_t len);

/**
 * @brief Start legacy advertising
 *
 * @return GAP_OK on success
 */
int gap_start_advertising(void);

/**
 * @brief Stop legacy advertising
 *
 * @return GAP_OK on success
 */
int gap_stop_advertising(void);

/*******************************************************************************
 * API Functions - Extended Advertising
 ******************************************************************************/

/**
 * @brief Create extended advertising set
 *
 * @param params Extended advertising parameters
 * @return GAP_OK on success
 */
int gap_create_ext_adv_set(const gap_ext_adv_params_t *params);

/**
 * @brief Remove extended advertising set
 *
 * @param adv_handle Advertising handle
 * @return GAP_OK on success
 */
int gap_remove_ext_adv_set(uint8_t adv_handle);

/**
 * @brief Set extended advertising parameters
 *
 * @param params Extended advertising parameters
 * @return GAP_OK on success
 */
int gap_set_ext_adv_params(const gap_ext_adv_params_t *params);

/**
 * @brief Set extended advertising data
 *
 * @param adv_handle Advertising handle
 * @param data Advertising data
 * @param len Data length
 * @return GAP_OK on success
 */
int gap_set_ext_adv_data(uint8_t adv_handle, const uint8_t *data, uint16_t len);

/**
 * @brief Set extended scan response data
 *
 * @param adv_handle Advertising handle
 * @param data Scan response data
 * @param len Data length
 * @return GAP_OK on success
 */
int gap_set_ext_scan_rsp_data(uint8_t adv_handle, const uint8_t *data, uint16_t len);

/**
 * @brief Start extended advertising
 *
 * @param adv_handle Advertising handle
 * @param duration Duration in 10ms units (0 = until stopped)
 * @param max_events Max advertising events (0 = no limit)
 * @return GAP_OK on success
 */
int gap_start_ext_advertising(uint8_t adv_handle, uint16_t duration, uint8_t max_events);

/**
 * @brief Stop extended advertising
 *
 * @param adv_handle Advertising handle
 * @return GAP_OK on success
 */
int gap_stop_ext_advertising(uint8_t adv_handle);

/*******************************************************************************
 * API Functions - Periodic Advertising (for Auracast)
 ******************************************************************************/

/**
 * @brief Set periodic advertising parameters
 *
 * @param params Periodic advertising parameters
 * @return GAP_OK on success
 */
int gap_set_periodic_adv_params(const gap_periodic_adv_params_t *params);

/**
 * @brief Set periodic advertising data
 *
 * @param adv_handle Advertising handle
 * @param data Periodic advertising data (BASE for Auracast)
 * @param len Data length
 * @return GAP_OK on success
 */
int gap_set_periodic_adv_data(uint8_t adv_handle, const uint8_t *data, uint16_t len);

/**
 * @brief Start periodic advertising
 *
 * @param adv_handle Advertising handle
 * @return GAP_OK on success
 */
int gap_start_periodic_advertising(uint8_t adv_handle);

/**
 * @brief Stop periodic advertising
 *
 * @param adv_handle Advertising handle
 * @return GAP_OK on success
 */
int gap_stop_periodic_advertising(uint8_t adv_handle);

/*******************************************************************************
 * API Functions - Periodic Advertising Sync (for receiving broadcasts)
 ******************************************************************************/

/**
 * @brief Create sync to periodic advertising
 *
 * @param adv_sid Advertising SID
 * @param address Advertiser address
 * @param skip Number of periodic events to skip
 * @param sync_timeout Sync timeout (10ms units)
 * @return GAP_OK on success
 */
int gap_periodic_adv_create_sync(uint8_t adv_sid, const gap_address_t *address,
                                  uint16_t skip, uint16_t sync_timeout);

/**
 * @brief Cancel pending periodic advertising sync
 *
 * @return GAP_OK on success
 */
int gap_periodic_adv_cancel_sync(void);

/**
 * @brief Terminate periodic advertising sync
 *
 * @param sync_handle Sync handle
 * @return GAP_OK on success
 */
int gap_periodic_adv_terminate_sync(uint16_t sync_handle);

/*******************************************************************************
 * API Functions - Scanning
 ******************************************************************************/

/**
 * @brief Set scan parameters
 *
 * @param params Scan parameters
 * @return GAP_OK on success
 */
int gap_set_scan_params(const gap_scan_params_t *params);

/**
 * @brief Start scanning
 *
 * @param filter_duplicates Filter duplicate advertisements
 * @return GAP_OK on success
 */
int gap_start_scanning(gap_scan_dup_filter_t filter_duplicates);

/**
 * @brief Stop scanning
 *
 * @return GAP_OK on success
 */
int gap_stop_scanning(void);

/**
 * @brief Set extended scan parameters
 *
 * @param params Extended scan parameters
 * @return GAP_OK on success
 */
int gap_set_ext_scan_params(const gap_ext_scan_params_t *params);

/**
 * @brief Start extended scanning
 *
 * @param filter_duplicates Filter duplicate mode
 * @param duration Scan duration in 10ms units (0 = until stopped)
 * @param period Scan period in 1.28s units (0 = continuous)
 * @return GAP_OK on success
 */
int gap_start_ext_scanning(gap_scan_dup_filter_t filter_duplicates,
                            uint16_t duration, uint16_t period);

/**
 * @brief Stop extended scanning
 *
 * @return GAP_OK on success
 */
int gap_stop_ext_scanning(void);

/*******************************************************************************
 * API Functions - Connection Management
 ******************************************************************************/

/**
 * @brief Create connection to peer device
 *
 * @param peer_addr Peer address
 * @param params Connection parameters
 * @return GAP_OK on success
 */
int gap_connect(const gap_address_t *peer_addr, const gap_conn_params_t *params);

/**
 * @brief Cancel pending connection
 *
 * @return GAP_OK on success
 */
int gap_cancel_connect(void);

/**
 * @brief Disconnect from peer
 *
 * @param conn_handle Connection handle
 * @param reason Disconnect reason
 * @return GAP_OK on success
 */
int gap_disconnect(uint16_t conn_handle, uint8_t reason);

/**
 * @brief Update connection parameters
 *
 * @param conn_handle Connection handle
 * @param params New connection parameters
 * @return GAP_OK on success
 */
int gap_update_conn_params(uint16_t conn_handle, const gap_conn_params_t *params);

/**
 * @brief Request connection parameter update (peripheral role)
 *
 * @param conn_handle Connection handle
 * @param params Requested parameters
 * @return GAP_OK on success
 */
int gap_request_conn_param_update(uint16_t conn_handle, const gap_conn_params_t *params);

/*******************************************************************************
 * API Functions - Advertising Data Builder
 ******************************************************************************/

/**
 * @brief Initialize advertising data builder
 *
 * @param builder Builder context
 * @param max_length Maximum data length
 */
void gap_adv_data_builder_init(gap_adv_data_builder_t *builder, uint16_t max_length);

/**
 * @brief Clear advertising data builder
 *
 * @param builder Builder context
 */
void gap_adv_data_builder_clear(gap_adv_data_builder_t *builder);

/**
 * @brief Add flags to advertising data
 *
 * @param builder Builder context
 * @param flags AD flags
 * @return GAP_OK on success
 */
int gap_adv_data_add_flags(gap_adv_data_builder_t *builder, uint8_t flags);

/**
 * @brief Add complete local name
 *
 * @param builder Builder context
 * @param name Device name
 * @return GAP_OK on success
 */
int gap_adv_data_add_name(gap_adv_data_builder_t *builder, const char *name);

/**
 * @brief Add shortened local name
 *
 * @param builder Builder context
 * @param name Shortened name
 * @return GAP_OK on success
 */
int gap_adv_data_add_short_name(gap_adv_data_builder_t *builder, const char *name);

/**
 * @brief Add 16-bit service UUIDs
 *
 * @param builder Builder context
 * @param uuids Array of 16-bit UUIDs
 * @param count Number of UUIDs
 * @param complete true for complete list
 * @return GAP_OK on success
 */
int gap_adv_data_add_uuid16(gap_adv_data_builder_t *builder, const uint16_t *uuids,
                             uint8_t count, bool complete);

/**
 * @brief Add service data (16-bit UUID)
 *
 * @param builder Builder context
 * @param uuid 16-bit service UUID
 * @param data Service data
 * @param len Data length
 * @return GAP_OK on success
 */
int gap_adv_data_add_service_data_16(gap_adv_data_builder_t *builder, uint16_t uuid,
                                      const uint8_t *data, uint8_t len);

/**
 * @brief Add appearance
 *
 * @param builder Builder context
 * @param appearance Appearance value
 * @return GAP_OK on success
 */
int gap_adv_data_add_appearance(gap_adv_data_builder_t *builder, uint16_t appearance);

/**
 * @brief Add TX power level
 *
 * @param builder Builder context
 * @param tx_power TX power in dBm
 * @return GAP_OK on success
 */
int gap_adv_data_add_tx_power(gap_adv_data_builder_t *builder, int8_t tx_power);

/**
 * @brief Add manufacturer specific data
 *
 * @param builder Builder context
 * @param company_id Company identifier
 * @param data Manufacturer data
 * @param len Data length
 * @return GAP_OK on success
 */
int gap_adv_data_add_manufacturer_data(gap_adv_data_builder_t *builder,
                                        uint16_t company_id,
                                        const uint8_t *data, uint8_t len);

/**
 * @brief Add broadcast name (for Auracast)
 *
 * @param builder Builder context
 * @param name Broadcast name
 * @return GAP_OK on success
 */
int gap_adv_data_add_broadcast_name(gap_adv_data_builder_t *builder, const char *name);

/**
 * @brief Add raw AD structure
 *
 * @param builder Builder context
 * @param ad_type AD type
 * @param data AD data
 * @param len Data length
 * @return GAP_OK on success
 */
int gap_adv_data_add_raw(gap_adv_data_builder_t *builder, uint8_t ad_type,
                          const uint8_t *data, uint8_t len);

/**
 * @brief Get built advertising data
 *
 * @param builder Builder context
 * @param data Output data pointer
 * @param len Output data length
 */
void gap_adv_data_get(const gap_adv_data_builder_t *builder,
                       const uint8_t **data, uint16_t *len);

/*******************************************************************************
 * API Functions - Whitelist Management
 ******************************************************************************/

/**
 * @brief Clear whitelist
 *
 * @return GAP_OK on success
 */
int gap_whitelist_clear(void);

/**
 * @brief Add device to whitelist
 *
 * @param address Device address
 * @return GAP_OK on success
 */
int gap_whitelist_add(const gap_address_t *address);

/**
 * @brief Remove device from whitelist
 *
 * @param address Device address
 * @return GAP_OK on success
 */
int gap_whitelist_remove(const gap_address_t *address);

/**
 * @brief Get whitelist size
 *
 * @return Number of entries in whitelist
 */
int gap_whitelist_get_size(void);

#ifdef __cplusplus
}
#endif

#endif /* GAP_CONFIG_H */
