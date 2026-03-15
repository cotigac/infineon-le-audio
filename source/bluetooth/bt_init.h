/**
 * @file bt_init.h
 * @brief Bluetooth Stack Initialization API
 *
 * This module handles initialization of the Infineon BTSTACK and
 * HCI transport to the CYW55511 Bluetooth controller.
 *
 * Architecture:
 * - PSoC Edge E81 runs BTSTACK (host stack)
 * - CYW55511 runs BLE controller firmware
 * - Communication via UART HCI transport
 * - Supports LE Audio with HCI ISOC (isochronous channels)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_INIT_H
#define BT_INIT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Bluetooth device address size */
#define BT_ADDR_SIZE            6

/** Maximum device name length */
#define BT_MAX_NAME_LEN         32

/** Default HCI UART baud rate */
#define BT_HCI_DEFAULT_BAUD     3000000

/** Default advertising interval (ms) */
#define BT_DEFAULT_ADV_INTERVAL 100

/** Bluetooth controller types */
typedef enum {
    BT_CONTROLLER_CYW55511 = 0, /**< CYW55511 (Bluetooth 6.0) */
    BT_CONTROLLER_CYW55512,     /**< CYW55512 */
    BT_CONTROLLER_CYW55513,     /**< CYW55513 */
    BT_CONTROLLER_UNKNOWN
} bt_controller_type_t;

/** Bluetooth stack state */
typedef enum {
    BT_STATE_OFF = 0,           /**< Stack not initialized */
    BT_STATE_INITIALIZING,      /**< Stack initializing */
    BT_STATE_READY,             /**< Stack ready, not advertising */
    BT_STATE_ADVERTISING,       /**< Advertising active */
    BT_STATE_CONNECTED,         /**< Connected to peer */
    BT_STATE_STREAMING,         /**< LE Audio streaming active */
    BT_STATE_ERROR              /**< Error state */
} bt_state_t;

/** Bluetooth error codes */
typedef enum {
    BT_OK = 0,
    BT_ERROR_INVALID_PARAM = -1,
    BT_ERROR_NOT_INITIALIZED = -2,
    BT_ERROR_ALREADY_INITIALIZED = -3,
    BT_ERROR_HCI_TRANSPORT = -4,
    BT_ERROR_CONTROLLER_INIT = -5,
    BT_ERROR_FIRMWARE_DOWNLOAD = -6,
    BT_ERROR_TIMEOUT = -7,
    BT_ERROR_NO_MEMORY = -8,
    BT_ERROR_NOT_SUPPORTED = -9,
    BT_ERROR_BUSY = -10
} bt_error_t;

/** LE features to enable */
typedef enum {
    BT_LE_FEATURE_NONE              = 0x0000,
    BT_LE_FEATURE_2M_PHY            = 0x0001,   /**< LE 2M PHY */
    BT_LE_FEATURE_CODED_PHY         = 0x0002,   /**< LE Coded PHY */
    BT_LE_FEATURE_EXT_ADV           = 0x0004,   /**< Extended Advertising */
    BT_LE_FEATURE_PERIODIC_ADV      = 0x0008,   /**< Periodic Advertising */
    BT_LE_FEATURE_ISOC              = 0x0010,   /**< Isochronous Channels */
    BT_LE_FEATURE_CIS               = 0x0020,   /**< Connected Isochronous (unicast) */
    BT_LE_FEATURE_BIS               = 0x0040,   /**< Broadcast Isochronous */
    BT_LE_FEATURE_LE_AUDIO          = 0x007F,   /**< All LE Audio features */
    BT_LE_FEATURE_ALL               = 0xFFFF
} bt_le_features_t;

/** Power management modes */
typedef enum {
    BT_POWER_ACTIVE = 0,        /**< Full power, lowest latency */
    BT_POWER_LOW_LATENCY,       /**< Low power with fast wake */
    BT_POWER_LOW_POWER,         /**< Lower power, slower wake */
    BT_POWER_DEEP_SLEEP         /**< Deep sleep, controller off */
} bt_power_mode_t;

/*******************************************************************************
 * Types
 ******************************************************************************/

/** HCI transport configuration */
typedef struct {
    uint32_t uart_baud;             /**< UART baud rate */
    uint8_t uart_port;              /**< UART port number */
    bool flow_control;              /**< Enable hardware flow control */
    bool download_firmware;         /**< Download firmware on init */
    const char *firmware_path;      /**< Path to firmware file (or NULL) */
} bt_hci_config_t;

/** Bluetooth configuration */
typedef struct {
    char device_name[BT_MAX_NAME_LEN];  /**< Device name for advertising */
    uint8_t device_addr[BT_ADDR_SIZE];  /**< BLE address (or all zeros for default) */
    bool use_random_addr;               /**< Use random static address */
    uint16_t le_features;               /**< LE features to enable (bt_le_features_t) */
    bt_hci_config_t hci;                /**< HCI transport config */
} bt_config_t;

/** Bluetooth event types */
typedef enum {
    BT_EVENT_STATE_CHANGED,         /**< Stack state changed */
    BT_EVENT_INITIALIZED,           /**< Stack initialization complete */
    BT_EVENT_CONNECTION_UP,         /**< BLE connection established */
    BT_EVENT_CONNECTION_DOWN,       /**< BLE connection terminated */
    BT_EVENT_PAIRING_REQUEST,       /**< Pairing request from peer */
    BT_EVENT_PAIRING_COMPLETE,      /**< Pairing completed */
    BT_EVENT_MTU_CHANGED,           /**< ATT MTU changed */
    BT_EVENT_PHY_UPDATED,           /**< PHY updated */
    BT_EVENT_ISOC_ESTABLISHED,      /**< Isochronous channel established */
    BT_EVENT_ISOC_TERMINATED,       /**< Isochronous channel terminated */
    BT_EVENT_ERROR                  /**< Error occurred */
} bt_event_type_t;

/** Connection parameters */
typedef struct {
    uint16_t conn_handle;           /**< Connection handle */
    uint16_t conn_interval;         /**< Connection interval (1.25ms units) */
    uint16_t conn_latency;          /**< Peripheral latency */
    uint16_t supervision_timeout;   /**< Supervision timeout (10ms units) */
    uint8_t peer_addr[BT_ADDR_SIZE];/**< Peer device address */
    uint8_t peer_addr_type;         /**< Peer address type */
} bt_connection_info_t;

/** Isochronous channel info */
typedef struct {
    uint16_t cis_handle;            /**< CIS/BIS handle */
    uint16_t cig_id;                /**< CIG ID (for unicast) */
    uint16_t big_handle;            /**< BIG handle (for broadcast) */
    uint32_t sdu_interval;          /**< SDU interval in microseconds */
    uint16_t max_sdu;               /**< Maximum SDU size */
    uint8_t phy;                    /**< PHY used */
    uint8_t framing;                /**< Framing mode */
    bool is_broadcast;              /**< true for BIS, false for CIS */
} bt_isoc_info_t;

/** Bluetooth event data */
typedef struct {
    bt_event_type_t type;
    union {
        bt_state_t new_state;               /**< For STATE_CHANGED */
        bt_connection_info_t connection;    /**< For CONNECTION_UP */
        uint16_t conn_handle;               /**< For CONNECTION_DOWN */
        uint16_t mtu;                       /**< For MTU_CHANGED */
        bt_isoc_info_t isoc;                /**< For ISOC events */
        int error_code;                     /**< For ERROR */
    } data;
} bt_event_t;

/** Bluetooth event callback */
typedef void (*bt_event_callback_t)(const bt_event_t *event, void *user_data);

/** Controller information (read after init) */
typedef struct {
    bt_controller_type_t type;      /**< Controller type */
    uint8_t hci_version;            /**< HCI version */
    uint16_t hci_revision;          /**< HCI revision */
    uint8_t lmp_version;            /**< LMP version */
    uint16_t manufacturer;          /**< Manufacturer ID */
    uint16_t lmp_subversion;        /**< LMP subversion */
    uint8_t bd_addr[BT_ADDR_SIZE];  /**< Controller BD_ADDR */
    char fw_version[32];            /**< Firmware version string */
    bool le_audio_supported;        /**< LE Audio support detected */
    bool isoc_supported;            /**< ISOC support detected */
    uint8_t max_cig;                /**< Max CIG supported */
    uint8_t max_cis_per_cig;        /**< Max CIS per CIG */
    uint8_t max_big;                /**< Max BIG supported */
    uint8_t max_bis_per_big;        /**< Max BIS per BIG */
} bt_controller_info_t;

/** Bluetooth statistics */
typedef struct {
    uint32_t tx_packets;            /**< Packets transmitted */
    uint32_t rx_packets;            /**< Packets received */
    uint32_t tx_bytes;              /**< Bytes transmitted */
    uint32_t rx_bytes;              /**< Bytes received */
    uint32_t tx_errors;             /**< Transmission errors */
    uint32_t rx_errors;             /**< Reception errors */
    uint32_t connections;           /**< Total connections made */
    uint32_t disconnections;        /**< Total disconnections */
    uint32_t isoc_tx_packets;       /**< ISOC packets transmitted */
    uint32_t isoc_rx_packets;       /**< ISOC packets received */
    uint32_t isoc_missed;           /**< ISOC packets missed */
} bt_stats_t;

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default HCI configuration for CYW55511 */
#define BT_HCI_CONFIG_DEFAULT {             \
    .uart_baud = 3000000,                   \
    .uart_port = 0,                         \
    .flow_control = true,                   \
    .download_firmware = true,              \
    .firmware_path = NULL                   \
}

/** Default Bluetooth configuration */
#define BT_CONFIG_DEFAULT {                 \
    .device_name = "Infineon LE Audio",     \
    .device_addr = {0},                     \
    .use_random_addr = true,                \
    .le_features = BT_LE_FEATURE_LE_AUDIO,  \
    .hci = BT_HCI_CONFIG_DEFAULT            \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize the Bluetooth stack
 *
 * Initializes BTSTACK, HCI transport, and the CYW55511 controller.
 * Downloads firmware if configured and enables requested LE features.
 *
 * @param config Pointer to configuration (NULL for defaults)
 * @return BT_OK on success, negative error code on failure
 */
int bt_init(const bt_config_t *config);

/**
 * @brief Deinitialize the Bluetooth stack
 *
 * Stops all activity, closes connections, and shuts down the stack.
 */
void bt_deinit(void);

/**
 * @brief Check if Bluetooth stack is initialized
 *
 * @return true if initialized and ready
 */
bool bt_is_initialized(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void bt_register_callback(bt_event_callback_t callback, void *user_data);

/**
 * @brief Get current Bluetooth state
 *
 * @return Current state
 */
bt_state_t bt_get_state(void);

/*******************************************************************************
 * API Functions - Controller
 ******************************************************************************/

/**
 * @brief Get controller information
 *
 * @param info Pointer to structure to fill
 * @return BT_OK on success, negative error code on failure
 */
int bt_get_controller_info(bt_controller_info_t *info);

/**
 * @brief Reset the controller
 *
 * Performs HCI reset and reinitializes the controller.
 *
 * @return BT_OK on success, negative error code on failure
 */
int bt_reset_controller(void);

/**
 * @brief Set device address
 *
 * @param addr 6-byte device address
 * @param random true for random static address
 * @return BT_OK on success, negative error code on failure
 */
int bt_set_device_address(const uint8_t addr[BT_ADDR_SIZE], bool random);

/**
 * @brief Get device address
 *
 * @param addr Buffer for 6-byte address
 * @param random Output: true if random address
 * @return BT_OK on success, negative error code on failure
 */
int bt_get_device_address(uint8_t addr[BT_ADDR_SIZE], bool *random);

/*******************************************************************************
 * API Functions - Advertising
 ******************************************************************************/

/**
 * @brief Start advertising
 *
 * @param connectable true for connectable advertising
 * @param interval_ms Advertising interval in milliseconds
 * @return BT_OK on success, negative error code on failure
 */
int bt_start_advertising(bool connectable, uint16_t interval_ms);

/**
 * @brief Stop advertising
 *
 * @return BT_OK on success, negative error code on failure
 */
int bt_stop_advertising(void);

/**
 * @brief Set advertising data
 *
 * @param data Advertising data (raw AD structures)
 * @param len Data length
 * @return BT_OK on success, negative error code on failure
 */
int bt_set_advertising_data(const uint8_t *data, uint8_t len);

/**
 * @brief Set scan response data
 *
 * @param data Scan response data (raw AD structures)
 * @param len Data length
 * @return BT_OK on success, negative error code on failure
 */
int bt_set_scan_response_data(const uint8_t *data, uint8_t len);

/**
 * @brief Set device name in advertising
 *
 * @param name Null-terminated device name
 * @return BT_OK on success, negative error code on failure
 */
int bt_set_device_name(const char *name);

/*******************************************************************************
 * API Functions - Connection Management
 ******************************************************************************/

/**
 * @brief Disconnect from peer
 *
 * @param conn_handle Connection handle
 * @param reason HCI disconnect reason (0x13 = remote user terminated)
 * @return BT_OK on success, negative error code on failure
 */
int bt_disconnect(uint16_t conn_handle, uint8_t reason);

/**
 * @brief Update connection parameters
 *
 * @param conn_handle Connection handle
 * @param interval_min Min connection interval (1.25ms units)
 * @param interval_max Max connection interval (1.25ms units)
 * @param latency Peripheral latency
 * @param timeout Supervision timeout (10ms units)
 * @return BT_OK on success, negative error code on failure
 */
int bt_update_connection_params(uint16_t conn_handle,
                                uint16_t interval_min,
                                uint16_t interval_max,
                                uint16_t latency,
                                uint16_t timeout);

/**
 * @brief Get connection info
 *
 * @param conn_handle Connection handle
 * @param info Pointer to structure to fill
 * @return BT_OK on success, negative error code on failure
 */
int bt_get_connection_info(uint16_t conn_handle, bt_connection_info_t *info);

/**
 * @brief Set preferred PHY
 *
 * @param conn_handle Connection handle
 * @param tx_phy Preferred TX PHY (1=1M, 2=2M, 4=Coded)
 * @param rx_phy Preferred RX PHY
 * @return BT_OK on success, negative error code on failure
 */
int bt_set_phy(uint16_t conn_handle, uint8_t tx_phy, uint8_t rx_phy);

/*******************************************************************************
 * API Functions - Power Management
 ******************************************************************************/

/**
 * @brief Set power mode
 *
 * @param mode Power mode
 * @return BT_OK on success, negative error code on failure
 */
int bt_set_power_mode(bt_power_mode_t mode);

/**
 * @brief Get current power mode
 *
 * @return Current power mode
 */
bt_power_mode_t bt_get_power_mode(void);

/**
 * @brief Set TX power level
 *
 * @param power_dbm TX power in dBm (-20 to +10 typical)
 * @return BT_OK on success, negative error code on failure
 */
int bt_set_tx_power(int8_t power_dbm);

/*******************************************************************************
 * API Functions - Statistics
 ******************************************************************************/

/**
 * @brief Get Bluetooth statistics
 *
 * @param stats Pointer to structure to fill
 */
void bt_get_stats(bt_stats_t *stats);

/**
 * @brief Reset Bluetooth statistics
 */
void bt_reset_stats(void);

/*******************************************************************************
 * API Functions - ISOC Support (for LE Audio)
 ******************************************************************************/

/**
 * @brief Check if ISOC (isochronous channels) is supported
 *
 * @return true if CIS/BIS supported
 */
bool bt_isoc_is_supported(void);

/**
 * @brief Get ISOC capabilities
 *
 * @param max_cig Output: max CIG count
 * @param max_cis Output: max CIS per CIG
 * @param max_big Output: max BIG count
 * @param max_bis Output: max BIS per BIG
 * @return BT_OK on success, negative error code on failure
 */
int bt_isoc_get_capabilities(uint8_t *max_cig, uint8_t *max_cis,
                             uint8_t *max_big, uint8_t *max_bis);

/*******************************************************************************
 * API Functions - FreeRTOS Task
 ******************************************************************************/

/**
 * @brief Process Bluetooth stack events
 *
 * Non-blocking function to process pending HCI events and callbacks.
 * Call this periodically from a FreeRTOS task.
 */
void bt_process(void);

/**
 * @brief Bluetooth stack task function
 *
 * Main task that processes HCI events and runs the BTSTACK event loop.
 * Create this as a FreeRTOS task during system initialization.
 *
 * @param pvParameters Task parameters (unused)
 */
void bt_task(void *pvParameters);

/**
 * @brief Get required stack size for BT task
 *
 * @return Stack size in words
 */
uint32_t bt_get_task_stack_size(void);

/**
 * @brief Get recommended task priority
 *
 * @return FreeRTOS task priority
 */
uint32_t bt_get_task_priority(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_INIT_H */
