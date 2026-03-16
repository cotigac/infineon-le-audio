/**
 * @file gatt_db.h
 * @brief GATT Database Configuration API
 *
 * This module manages the GATT database for LE Audio applications.
 * It provides:
 *
 * - Standard services (GAP, GATT, Device Information)
 * - LE Audio services (PACS, ASCS, BASS, CAS)
 * - BLE MIDI service
 * - Dynamic characteristic value management
 * - CCCD (Client Characteristic Configuration Descriptor) handling
 * - Notification/Indication support
 *
 * Service UUIDs:
 * - GAP Service:         0x1800
 * - GATT Service:        0x1801
 * - Device Information:  0x180A
 * - PACS:                0x1850
 * - ASCS:                0x184E
 * - BASS:                0x184F
 * - CAS:                 0x1853
 * - MIDI:                0x03B80E5A-EDE8-4B33-A751-6CE34EC4C700
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GATT_DB_H
#define GATT_DB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Service UUIDs (16-bit)
 ******************************************************************************/

#define GATT_UUID_GAP_SERVICE                   0x1800
#define GATT_UUID_GATT_SERVICE                  0x1801
#define GATT_UUID_DEVICE_INFO_SERVICE           0x180A
#define GATT_UUID_BATTERY_SERVICE               0x180F
#define GATT_UUID_PACS_SERVICE                  0x1850
#define GATT_UUID_ASCS_SERVICE                  0x184E
#define GATT_UUID_BASS_SERVICE                  0x184F
#define GATT_UUID_TMAS_SERVICE                  0x1855  /**< Telephony and Media Audio */
#define GATT_UUID_CAS_SERVICE                   0x1853  /**< Common Audio Service */

/*******************************************************************************
 * Characteristic UUIDs (16-bit)
 ******************************************************************************/

/* GAP Service */
#define GATT_UUID_DEVICE_NAME                   0x2A00
#define GATT_UUID_APPEARANCE                    0x2A01
#define GATT_UUID_PERIPHERAL_PRIVACY_FLAG       0x2A02
#define GATT_UUID_RECONNECTION_ADDRESS          0x2A03
#define GATT_UUID_PERIPHERAL_PREFERRED_CONN     0x2A04

/* GATT Service */
#define GATT_UUID_SERVICE_CHANGED               0x2A05
#define GATT_UUID_CLIENT_SUPPORTED_FEATURES     0x2B29
#define GATT_UUID_DATABASE_HASH                 0x2B2A

/* Device Information Service */
#define GATT_UUID_MANUFACTURER_NAME             0x2A29
#define GATT_UUID_MODEL_NUMBER                  0x2A24
#define GATT_UUID_SERIAL_NUMBER                 0x2A25
#define GATT_UUID_HARDWARE_REVISION             0x2A27
#define GATT_UUID_FIRMWARE_REVISION             0x2A26
#define GATT_UUID_SOFTWARE_REVISION             0x2A28
/* These may be defined in BTSTACK headers, use guards */
#ifndef GATT_UUID_SYSTEM_ID
#define GATT_UUID_SYSTEM_ID                     0x2A23
#endif
#ifndef GATT_UUID_PNP_ID
#define GATT_UUID_PNP_ID                        0x2A50
#endif

/* Battery Service */
#ifndef GATT_UUID_BATTERY_LEVEL
#define GATT_UUID_BATTERY_LEVEL                 0x2A19
#endif

/* PACS */
#define GATT_UUID_SINK_PAC                      0x2BC9
#define GATT_UUID_SINK_AUDIO_LOCATIONS          0x2BCA
#define GATT_UUID_SOURCE_PAC                    0x2BCB
#define GATT_UUID_SOURCE_AUDIO_LOCATIONS        0x2BCC
#define GATT_UUID_AVAILABLE_AUDIO_CONTEXTS      0x2BCD
#define GATT_UUID_SUPPORTED_AUDIO_CONTEXTS      0x2BCE

/* ASCS */
#define GATT_UUID_ASE_SINK                      0x2BC4
#define GATT_UUID_ASE_SOURCE                    0x2BC5
#define GATT_UUID_ASE_CONTROL_POINT             0x2BC6

/* BASS */
#define GATT_UUID_BROADCAST_AUDIO_SCAN_CP       0x2BC7
#define GATT_UUID_BROADCAST_RECEIVE_STATE       0x2BC8

/* CAS */
#define GATT_UUID_CSIS_MEMBER_LOCK              0x2B86
#define GATT_UUID_CSIS_SIRK                     0x2B84
#define GATT_UUID_CSIS_SIZE                     0x2B85
#define GATT_UUID_CSIS_RANK                     0x2B87

/*******************************************************************************
 * Descriptor UUIDs
 ******************************************************************************/

#define GATT_UUID_CHAR_EXTENDED_PROPERTIES      0x2900
#define GATT_UUID_CHAR_USER_DESCRIPTION         0x2901
#define GATT_UUID_CLIENT_CHAR_CONFIG            0x2902  /**< CCCD */
#define GATT_UUID_SERVER_CHAR_CONFIG            0x2903
#define GATT_UUID_CHAR_FORMAT                   0x2904
#define GATT_UUID_CHAR_AGGREGATE_FORMAT         0x2905

/*******************************************************************************
 * BLE MIDI Service (128-bit UUIDs)
 ******************************************************************************/

/** MIDI Service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 */
#define GATT_UUID_MIDI_SERVICE \
    { 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, \
      0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03 }

/** MIDI I/O Characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3 */
#define GATT_UUID_MIDI_IO \
    { 0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, \
      0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77 }

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum number of services */
#define GATT_DB_MAX_SERVICES                    16

/** Maximum number of characteristics */
#define GATT_DB_MAX_CHARACTERISTICS             64

/** Maximum number of descriptors */
#define GATT_DB_MAX_DESCRIPTORS                 32

/** Maximum number of CCCD entries */
#define GATT_DB_MAX_CCCD                        32

/** Maximum characteristic value length */
#define GATT_DB_MAX_VALUE_LEN                   512

/** Maximum device name length */
#define GATT_DB_MAX_DEVICE_NAME_LEN             248

/** Maximum number of ASEs */
#define GATT_DB_MAX_ASE_COUNT                   4

/** Maximum number of broadcast receive states */
#define GATT_DB_MAX_BROADCAST_RECEIVE_STATES    2

/*******************************************************************************
 * CCCD Values
 ******************************************************************************/

#define GATT_CCCD_NONE                          0x0000
#define GATT_CCCD_NOTIFICATION                  0x0001
#define GATT_CCCD_INDICATION                    0x0002

/*******************************************************************************
 * Characteristic Properties
 ******************************************************************************/

#define GATT_PROP_BROADCAST                     0x01
#define GATT_PROP_READ                          0x02
#define GATT_PROP_WRITE_NO_RSP                  0x04
#define GATT_PROP_WRITE                         0x08
#define GATT_PROP_NOTIFY                        0x10
#define GATT_PROP_INDICATE                      0x20
#define GATT_PROP_AUTH_WRITE                    0x40
#define GATT_PROP_EXTENDED                      0x80

/*******************************************************************************
 * Permission Flags
 ******************************************************************************/

#define GATT_PERM_READ                          0x01
#define GATT_PERM_READ_ENCRYPTED                0x02
#define GATT_PERM_READ_AUTHENTICATED            0x04
#define GATT_PERM_WRITE                         0x10
#define GATT_PERM_WRITE_ENCRYPTED               0x20
#define GATT_PERM_WRITE_AUTHENTICATED           0x40

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    GATT_DB_OK = 0,
    GATT_DB_ERROR_INVALID_PARAM = -1,
    GATT_DB_ERROR_NOT_INITIALIZED = -2,
    GATT_DB_ERROR_ALREADY_INITIALIZED = -3,
    GATT_DB_ERROR_NO_RESOURCES = -4,
    GATT_DB_ERROR_NOT_FOUND = -5,
    GATT_DB_ERROR_INVALID_HANDLE = -6,
    GATT_DB_ERROR_INVALID_OFFSET = -7,
    GATT_DB_ERROR_INSUFFICIENT_RESOURCES = -8,
    GATT_DB_ERROR_WRITE_NOT_PERMITTED = -9,
    GATT_DB_ERROR_READ_NOT_PERMITTED = -10
} gatt_db_error_t;

/*******************************************************************************
 * ATT Error Codes (for responses)
 ******************************************************************************/

#define ATT_ERROR_SUCCESS                       0x00
#define ATT_ERROR_INVALID_HANDLE                0x01
#define ATT_ERROR_READ_NOT_PERMITTED            0x02
#define ATT_ERROR_WRITE_NOT_PERMITTED           0x03
#define ATT_ERROR_INVALID_PDU                   0x04
#define ATT_ERROR_INSUFFICIENT_AUTHENTICATION  0x05
#define ATT_ERROR_REQUEST_NOT_SUPPORTED         0x06
#define ATT_ERROR_INVALID_OFFSET                0x07
#define ATT_ERROR_INSUFFICIENT_AUTHORIZATION    0x08
#define ATT_ERROR_PREPARE_QUEUE_FULL            0x09
#define ATT_ERROR_ATTRIBUTE_NOT_FOUND           0x0A
#define ATT_ERROR_ATTRIBUTE_NOT_LONG            0x0B
#define ATT_ERROR_INSUFFICIENT_ENCRYPTION_SIZE  0x0C
#define ATT_ERROR_INVALID_ATTRIBUTE_LENGTH      0x0D
#define ATT_ERROR_UNLIKELY_ERROR                0x0E
#define ATT_ERROR_INSUFFICIENT_ENCRYPTION       0x0F
#define ATT_ERROR_UNSUPPORTED_GROUP_TYPE        0x10
#define ATT_ERROR_INSUFFICIENT_RESOURCES        0x11

/* Application-specific error codes (0x80-0xFF) */
#define ATT_ERROR_CCC_IMPROPER_CONFIG           0xFD
#define ATT_ERROR_PROCEDURE_IN_PROGRESS         0xFE
#define ATT_ERROR_VALUE_OUT_OF_RANGE            0xFF

/*******************************************************************************
 * Types
 ******************************************************************************/

/** Handle type */
typedef uint16_t gatt_handle_t;

/** Invalid handle value */
#define GATT_INVALID_HANDLE                     0x0000

/** UUID type */
typedef struct {
    uint8_t type;           /**< 0 = 16-bit, 1 = 128-bit */
    union {
        uint16_t uuid16;
        uint8_t uuid128[16];
    } value;
} gatt_uuid_t;

/** Characteristic read callback */
typedef uint8_t (*gatt_read_callback_t)(uint16_t conn_handle,
                                         gatt_handle_t attr_handle,
                                         uint8_t *data, uint16_t *len,
                                         uint16_t offset, void *user_data);

/** Characteristic write callback */
typedef uint8_t (*gatt_write_callback_t)(uint16_t conn_handle,
                                          gatt_handle_t attr_handle,
                                          const uint8_t *data, uint16_t len,
                                          uint16_t offset, void *user_data);

/** CCCD write callback */
typedef void (*gatt_cccd_callback_t)(uint16_t conn_handle,
                                      gatt_handle_t attr_handle,
                                      uint16_t cccd_value, void *user_data);

/*******************************************************************************
 * Service Definition Structures
 ******************************************************************************/

/** Characteristic definition */
typedef struct {
    gatt_uuid_t uuid;               /**< Characteristic UUID */
    uint8_t properties;             /**< Characteristic properties */
    uint8_t permissions;            /**< Value permissions */
    gatt_read_callback_t read_cb;   /**< Read callback */
    gatt_write_callback_t write_cb; /**< Write callback */
    void *user_data;                /**< User context */
    uint16_t max_len;               /**< Maximum value length */
    bool has_cccd;                  /**< Include CCCD descriptor */
} gatt_char_def_t;

/** Service definition */
typedef struct {
    gatt_uuid_t uuid;               /**< Service UUID */
    bool is_primary;                /**< Primary or secondary service */
    const gatt_char_def_t *chars;   /**< Array of characteristics */
    uint8_t num_chars;              /**< Number of characteristics */
} gatt_service_def_t;

/*******************************************************************************
 * Attribute Handle Mapping
 ******************************************************************************/

/** Handle structure for PACS */
typedef struct {
    gatt_handle_t service;
    gatt_handle_t sink_pac;
    gatt_handle_t sink_pac_cccd;
    gatt_handle_t sink_audio_locations;
    gatt_handle_t sink_audio_locations_cccd;
    gatt_handle_t source_pac;
    gatt_handle_t source_pac_cccd;
    gatt_handle_t source_audio_locations;
    gatt_handle_t source_audio_locations_cccd;
    gatt_handle_t available_contexts;
    gatt_handle_t available_contexts_cccd;
    gatt_handle_t supported_contexts;
} gatt_pacs_handles_t;

/** Handle structure for ASCS */
typedef struct {
    gatt_handle_t service;
    gatt_handle_t ase_sink[GATT_DB_MAX_ASE_COUNT];
    gatt_handle_t ase_sink_cccd[GATT_DB_MAX_ASE_COUNT];
    gatt_handle_t ase_source[GATT_DB_MAX_ASE_COUNT];
    gatt_handle_t ase_source_cccd[GATT_DB_MAX_ASE_COUNT];
    gatt_handle_t ase_control_point;
    gatt_handle_t ase_control_point_cccd;
    uint8_t num_sink_ases;
    uint8_t num_source_ases;
} gatt_ascs_handles_t;

/** Handle structure for BASS */
typedef struct {
    gatt_handle_t service;
    gatt_handle_t broadcast_scan_cp;
    gatt_handle_t broadcast_scan_cp_cccd;
    gatt_handle_t broadcast_receive_state[GATT_DB_MAX_BROADCAST_RECEIVE_STATES];
    gatt_handle_t broadcast_receive_state_cccd[GATT_DB_MAX_BROADCAST_RECEIVE_STATES];
    uint8_t num_receive_states;
} gatt_bass_handles_t;

/** Handle structure for MIDI */
typedef struct {
    gatt_handle_t service;
    gatt_handle_t midi_io;
    gatt_handle_t midi_io_cccd;
} gatt_midi_handles_t;

/** Handle structure for Device Information */
typedef struct {
    gatt_handle_t service;
    gatt_handle_t manufacturer_name;
    gatt_handle_t model_number;
    gatt_handle_t serial_number;
    gatt_handle_t firmware_revision;
    gatt_handle_t hardware_revision;
    gatt_handle_t software_revision;
    gatt_handle_t pnp_id;
} gatt_device_info_handles_t;

/** All service handles */
typedef struct {
    gatt_pacs_handles_t pacs;
    gatt_ascs_handles_t ascs;
    gatt_bass_handles_t bass;
    gatt_midi_handles_t midi;
    gatt_device_info_handles_t device_info;
} gatt_db_handles_t;

/*******************************************************************************
 * Device Information Configuration
 ******************************************************************************/

typedef struct {
    const char *manufacturer_name;
    const char *model_number;
    const char *serial_number;
    const char *firmware_revision;
    const char *hardware_revision;
    const char *software_revision;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t product_version;
    uint8_t vendor_id_source;       /**< 1=Bluetooth SIG, 2=USB-IF */
} gatt_device_info_t;

/*******************************************************************************
 * Database Configuration
 ******************************************************************************/

typedef struct {
    const char *device_name;
    uint16_t appearance;
    gatt_device_info_t device_info;

    /* LE Audio configuration */
    bool enable_pacs;
    bool enable_ascs;
    bool enable_bass;
    uint8_t num_sink_ases;
    uint8_t num_source_ases;
    uint8_t num_broadcast_receive_states;

    /* MIDI configuration */
    bool enable_midi;

    /* Callbacks */
    gatt_cccd_callback_t cccd_callback;
    void *user_data;
} gatt_db_config_t;

/*******************************************************************************
 * Event Types
 ******************************************************************************/

typedef enum {
    GATT_DB_EVENT_CONNECTED,
    GATT_DB_EVENT_DISCONNECTED,
    GATT_DB_EVENT_MTU_CHANGED,
    GATT_DB_EVENT_CCCD_WRITTEN,
    GATT_DB_EVENT_VALUE_CHANGED
} gatt_db_event_type_t;

typedef struct {
    gatt_db_event_type_t type;
    uint16_t conn_handle;
    union {
        uint16_t mtu;
        struct {
            gatt_handle_t attr_handle;
            uint16_t cccd_value;
        } cccd;
        struct {
            gatt_handle_t attr_handle;
            const uint8_t *data;
            uint16_t len;
        } value;
    } data;
} gatt_db_event_t;

typedef void (*gatt_db_callback_t)(const gatt_db_event_t *event, void *user_data);

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

#define GATT_DB_CONFIG_DEFAULT { \
    .device_name = "LE Audio Device", \
    .appearance = 0x0842, /* Generic Audio Source */ \
    .device_info = { \
        .manufacturer_name = "Infineon", \
        .model_number = "PSoC-E81", \
        .firmware_revision = "1.0.0", \
        .vendor_id_source = 1, \
        .vendor_id = 0x0009, /* Infineon */ \
        .product_id = 0x0001, \
        .product_version = 0x0100 \
    }, \
    .enable_pacs = true, \
    .enable_ascs = true, \
    .enable_bass = false, \
    .num_sink_ases = 2, \
    .num_source_ases = 2, \
    .num_broadcast_receive_states = 0, \
    .enable_midi = true, \
    .cccd_callback = NULL, \
    .user_data = NULL \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize GATT database
 *
 * @param config Database configuration
 * @return GATT_DB_OK on success
 */
int gatt_db_init(const gatt_db_config_t *config);

/**
 * @brief Deinitialize GATT database
 */
void gatt_db_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Event callback
 * @param user_data User context
 */
void gatt_db_register_callback(gatt_db_callback_t callback, void *user_data);

/**
 * @brief Get all service handles
 *
 * @return Pointer to handle structure
 */
const gatt_db_handles_t* gatt_db_get_handles(void);

/*******************************************************************************
 * API Functions - Service Registration
 ******************************************************************************/

/**
 * @brief Add a custom service
 *
 * @param service Service definition
 * @param start_handle Output: service start handle
 * @return GATT_DB_OK on success
 */
int gatt_db_add_service(const gatt_service_def_t *service,
                         gatt_handle_t *start_handle);

/**
 * @brief Register PACS callbacks
 *
 * @param read_cb Read callback for PAC characteristics
 * @param user_data User context
 * @return GATT_DB_OK on success
 */
int gatt_db_register_pacs_callbacks(gatt_read_callback_t read_cb, void *user_data);

/**
 * @brief Register ASCS callbacks
 *
 * @param read_cb Read callback for ASE characteristics
 * @param write_cb Write callback for control point
 * @param user_data User context
 * @return GATT_DB_OK on success
 */
int gatt_db_register_ascs_callbacks(gatt_read_callback_t read_cb,
                                     gatt_write_callback_t write_cb,
                                     void *user_data);

/**
 * @brief Register BASS callbacks
 *
 * @param read_cb Read callback
 * @param write_cb Write callback for scan control point
 * @param user_data User context
 * @return GATT_DB_OK on success
 */
int gatt_db_register_bass_callbacks(gatt_read_callback_t read_cb,
                                     gatt_write_callback_t write_cb,
                                     void *user_data);

/**
 * @brief Register MIDI callbacks
 *
 * @param write_cb Write callback for MIDI input
 * @param user_data User context
 * @return GATT_DB_OK on success
 */
int gatt_db_register_midi_callbacks(gatt_write_callback_t write_cb, void *user_data);

/*******************************************************************************
 * API Functions - Attribute Operations
 ******************************************************************************/

/**
 * @brief Read attribute value
 *
 * @param attr_handle Attribute handle
 * @param data Output buffer
 * @param len Output: data length
 * @param max_len Buffer size
 * @return GATT_DB_OK on success
 */
int gatt_db_read_value(gatt_handle_t attr_handle, uint8_t *data,
                        uint16_t *len, uint16_t max_len);

/**
 * @brief Write attribute value
 *
 * @param attr_handle Attribute handle
 * @param data Data to write
 * @param len Data length
 * @return GATT_DB_OK on success
 */
int gatt_db_write_value(gatt_handle_t attr_handle, const uint8_t *data,
                         uint16_t len);

/**
 * @brief Set characteristic value (for server-initiated updates)
 *
 * @param attr_handle Characteristic value handle
 * @param data New value
 * @param len Value length
 * @return GATT_DB_OK on success
 */
int gatt_db_set_value(gatt_handle_t attr_handle, const uint8_t *data,
                       uint16_t len);

/*******************************************************************************
 * API Functions - Notifications/Indications
 ******************************************************************************/

/**
 * @brief Send notification
 *
 * @param conn_handle Connection handle
 * @param attr_handle Characteristic value handle
 * @param data Data to send
 * @param len Data length
 * @return GATT_DB_OK on success
 */
int gatt_db_send_notification(uint16_t conn_handle, gatt_handle_t attr_handle,
                               const uint8_t *data, uint16_t len);

/**
 * @brief Send indication
 *
 * @param conn_handle Connection handle
 * @param attr_handle Characteristic value handle
 * @param data Data to send
 * @param len Data length
 * @return GATT_DB_OK on success
 */
int gatt_db_send_indication(uint16_t conn_handle, gatt_handle_t attr_handle,
                             const uint8_t *data, uint16_t len);

/**
 * @brief Check if notifications are enabled for a characteristic
 *
 * @param conn_handle Connection handle
 * @param cccd_handle CCCD handle
 * @return true if notifications enabled
 */
bool gatt_db_notifications_enabled(uint16_t conn_handle, gatt_handle_t cccd_handle);

/**
 * @brief Check if indications are enabled for a characteristic
 *
 * @param conn_handle Connection handle
 * @param cccd_handle CCCD handle
 * @return true if indications enabled
 */
bool gatt_db_indications_enabled(uint16_t conn_handle, gatt_handle_t cccd_handle);

/*******************************************************************************
 * API Functions - CCCD Management
 ******************************************************************************/

/**
 * @brief Get CCCD value for a connection
 *
 * @param conn_handle Connection handle
 * @param cccd_handle CCCD handle
 * @return CCCD value (notifications/indications flags)
 */
uint16_t gatt_db_get_cccd(uint16_t conn_handle, gatt_handle_t cccd_handle);

/**
 * @brief Set CCCD value (internal use, typically from bonding restore)
 *
 * @param conn_handle Connection handle
 * @param cccd_handle CCCD handle
 * @param value CCCD value
 * @return GATT_DB_OK on success
 */
int gatt_db_set_cccd(uint16_t conn_handle, gatt_handle_t cccd_handle,
                      uint16_t value);

/**
 * @brief Clear all CCCDs for a connection (on disconnect)
 *
 * @param conn_handle Connection handle
 */
void gatt_db_clear_cccd(uint16_t conn_handle);

/*******************************************************************************
 * API Functions - Device Information
 ******************************************************************************/

/**
 * @brief Update device name
 *
 * @param name New device name
 * @return GATT_DB_OK on success
 */
int gatt_db_set_device_name(const char *name);

/**
 * @brief Update appearance
 *
 * @param appearance New appearance value
 * @return GATT_DB_OK on success
 */
int gatt_db_set_appearance(uint16_t appearance);

/*******************************************************************************
 * API Functions - MIDI Service
 ******************************************************************************/

/**
 * @brief Send MIDI data via notification
 *
 * @param conn_handle Connection handle
 * @param data MIDI packet data
 * @param len Data length
 * @return GATT_DB_OK on success
 */
int gatt_db_midi_send(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/*******************************************************************************
 * API Functions - LE Audio Services
 ******************************************************************************/

/**
 * @brief Notify PACS Sink PAC change
 *
 * @param conn_handle Connection handle (0 for all connections)
 * @param data PAC data
 * @param len Data length
 * @return GATT_DB_OK on success
 */
int gatt_db_notify_sink_pac(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/**
 * @brief Notify PACS Source PAC change
 *
 * @param conn_handle Connection handle (0 for all connections)
 * @param data PAC data
 * @param len Data length
 * @return GATT_DB_OK on success
 */
int gatt_db_notify_source_pac(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/**
 * @brief Notify PACS Available Audio Contexts change
 *
 * @param conn_handle Connection handle (0 for all connections)
 * @param sink_contexts Available sink contexts
 * @param source_contexts Available source contexts
 * @return GATT_DB_OK on success
 */
int gatt_db_notify_available_contexts(uint16_t conn_handle,
                                       uint16_t sink_contexts,
                                       uint16_t source_contexts);

/**
 * @brief Notify ASE state change
 *
 * @param conn_handle Connection handle
 * @param ase_id ASE ID
 * @param is_sink true for sink ASE, false for source
 * @param data ASE state data
 * @param len Data length
 * @return GATT_DB_OK on success
 */
int gatt_db_notify_ase_state(uint16_t conn_handle, uint8_t ase_id,
                              bool is_sink, const uint8_t *data, uint16_t len);

/**
 * @brief Notify ASE Control Point response
 *
 * @param conn_handle Connection handle
 * @param data Response data
 * @param len Data length
 * @return GATT_DB_OK on success
 */
int gatt_db_notify_ase_cp(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/*******************************************************************************
 * API Functions - Connection Events
 ******************************************************************************/

/**
 * @brief Handle new connection
 *
 * @param conn_handle Connection handle
 */
void gatt_db_on_connect(uint16_t conn_handle);

/**
 * @brief Handle disconnection
 *
 * @param conn_handle Connection handle
 */
void gatt_db_on_disconnect(uint16_t conn_handle);

/**
 * @brief Handle MTU exchange
 *
 * @param conn_handle Connection handle
 * @param mtu Negotiated MTU
 */
void gatt_db_on_mtu_changed(uint16_t conn_handle, uint16_t mtu);

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * @brief Create 16-bit UUID structure
 *
 * @param uuid16 16-bit UUID value
 * @return UUID structure
 */
gatt_uuid_t gatt_uuid16(uint16_t uuid16);

/**
 * @brief Create 128-bit UUID structure
 *
 * @param uuid128 128-bit UUID (little-endian)
 * @return UUID structure
 */
gatt_uuid_t gatt_uuid128(const uint8_t *uuid128);

/**
 * @brief Compare two UUIDs
 *
 * @param a First UUID
 * @param b Second UUID
 * @return true if equal
 */
bool gatt_uuid_equal(const gatt_uuid_t *a, const gatt_uuid_t *b);

#ifdef __cplusplus
}
#endif

#endif /* GATT_DB_H */
