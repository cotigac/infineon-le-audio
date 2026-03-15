/**
 * @file midi_ble_service.h
 * @brief BLE MIDI Service API
 *
 * This module implements the MIDI over Bluetooth Low Energy (BLE-MIDI)
 * specification for sending and receiving MIDI messages over BLE.
 *
 * The service uses the Apple-defined MIDI over BLE protocol which has
 * become the de-facto standard adopted by iOS, Android, macOS, Windows,
 * and Linux.
 *
 * Service UUID:        03B80E5A-EDE8-4B33-A751-6CE34EC4C700
 * Characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MIDI_BLE_SERVICE_H
#define MIDI_BLE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** BLE MIDI Service UUID (128-bit) */
#define MIDI_BLE_SERVICE_UUID           {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, \
                                         0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, \
                                         0x5A, 0x0E, 0xB8, 0x03}

/** BLE MIDI Characteristic UUID (128-bit) */
#define MIDI_BLE_CHAR_UUID              {0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, \
                                         0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, \
                                         0xDB, 0xE5, 0x72, 0x77}

/** Maximum BLE MIDI packet size (MTU - 3) */
#define MIDI_BLE_MAX_PACKET_SIZE        (512 - 3)

/** Minimum BLE MIDI packet size */
#define MIDI_BLE_MIN_PACKET_SIZE        3

/** Maximum MIDI message size (SysEx can be large) */
#define MIDI_BLE_MAX_MESSAGE_SIZE       256

/** Connection handle for no connection */
#define MIDI_BLE_INVALID_CONN_HANDLE    0xFFFF

/*******************************************************************************
 * MIDI Message Types
 ******************************************************************************/

/** MIDI status byte masks */
#define MIDI_STATUS_MASK                0x80
#define MIDI_CHANNEL_MASK               0x0F
#define MIDI_TYPE_MASK                  0xF0

/** MIDI Channel Voice Messages */
typedef enum {
    MIDI_MSG_NOTE_OFF           = 0x80,  /**< Note Off */
    MIDI_MSG_NOTE_ON            = 0x90,  /**< Note On */
    MIDI_MSG_POLY_AFTERTOUCH    = 0xA0,  /**< Polyphonic Key Pressure */
    MIDI_MSG_CONTROL_CHANGE     = 0xB0,  /**< Control Change */
    MIDI_MSG_PROGRAM_CHANGE     = 0xC0,  /**< Program Change */
    MIDI_MSG_CHANNEL_AFTERTOUCH = 0xD0,  /**< Channel Pressure */
    MIDI_MSG_PITCH_BEND         = 0xE0,  /**< Pitch Bend Change */
} midi_channel_msg_t;

/** MIDI System Messages */
typedef enum {
    MIDI_MSG_SYSEX_START        = 0xF0,  /**< System Exclusive Start */
    MIDI_MSG_TIME_CODE          = 0xF1,  /**< MIDI Time Code Quarter Frame */
    MIDI_MSG_SONG_POSITION      = 0xF2,  /**< Song Position Pointer */
    MIDI_MSG_SONG_SELECT        = 0xF3,  /**< Song Select */
    MIDI_MSG_TUNE_REQUEST       = 0xF6,  /**< Tune Request */
    MIDI_MSG_SYSEX_END          = 0xF7,  /**< End of System Exclusive */
    MIDI_MSG_TIMING_CLOCK       = 0xF8,  /**< Timing Clock */
    MIDI_MSG_START              = 0xFA,  /**< Start */
    MIDI_MSG_CONTINUE           = 0xFB,  /**< Continue */
    MIDI_MSG_STOP               = 0xFC,  /**< Stop */
    MIDI_MSG_ACTIVE_SENSING     = 0xFE,  /**< Active Sensing */
    MIDI_MSG_SYSTEM_RESET       = 0xFF,  /**< System Reset */
} midi_system_msg_t;

/*******************************************************************************
 * Types
 ******************************************************************************/

/** MIDI message structure */
typedef struct {
    uint8_t data[MIDI_BLE_MAX_MESSAGE_SIZE];  /**< Raw MIDI data */
    uint16_t length;                           /**< Message length */
    uint16_t timestamp;                        /**< 13-bit BLE-MIDI timestamp */
} midi_message_t;

/** BLE MIDI connection state */
typedef enum {
    MIDI_BLE_STATE_DISCONNECTED = 0,
    MIDI_BLE_STATE_CONNECTED,
    MIDI_BLE_STATE_SUBSCRIBED
} midi_ble_state_t;

/** BLE MIDI event types */
typedef enum {
    MIDI_BLE_EVENT_CONNECTED,       /**< Client connected */
    MIDI_BLE_EVENT_DISCONNECTED,    /**< Client disconnected */
    MIDI_BLE_EVENT_SUBSCRIBED,      /**< Client subscribed to notifications */
    MIDI_BLE_EVENT_UNSUBSCRIBED,    /**< Client unsubscribed */
    MIDI_BLE_EVENT_MESSAGE_RX,      /**< MIDI message received */
    MIDI_BLE_EVENT_TX_COMPLETE,     /**< MIDI message sent */
    MIDI_BLE_EVENT_ERROR            /**< Error occurred */
} midi_ble_event_type_t;

/** BLE MIDI event data */
typedef struct {
    midi_ble_event_type_t type;
    uint16_t conn_handle;
    union {
        midi_message_t message;     /**< For MESSAGE_RX events */
        int error_code;             /**< For ERROR events */
    } data;
} midi_ble_event_t;

/** BLE MIDI event callback function type */
typedef void (*midi_ble_callback_t)(const midi_ble_event_t *event, void *user_data);

/** BLE MIDI configuration */
typedef struct {
    const char *device_name;        /**< Device name for advertising */
    uint16_t mtu;                   /**< Maximum transmission unit */
    bool auto_advertise;            /**< Start advertising on init */
} midi_ble_config_t;

/** BLE MIDI statistics */
typedef struct {
    uint32_t messages_sent;         /**< Total messages sent */
    uint32_t messages_received;     /**< Total messages received */
    uint32_t bytes_sent;            /**< Total bytes sent */
    uint32_t bytes_received;        /**< Total bytes received */
    uint32_t tx_errors;             /**< Transmission errors */
    uint32_t rx_errors;             /**< Reception/parsing errors */
} midi_ble_stats_t;

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default BLE MIDI configuration */
#define MIDI_BLE_CONFIG_DEFAULT {           \
    .device_name = "Infineon MIDI",         \
    .mtu = 247,                             \
    .auto_advertise = true                  \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize the BLE MIDI service
 *
 * @param config Pointer to configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int midi_ble_init(const midi_ble_config_t *config);

/**
 * @brief Deinitialize the BLE MIDI service
 */
void midi_ble_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void midi_ble_register_callback(midi_ble_callback_t callback, void *user_data);

/**
 * @brief Start BLE advertising
 *
 * @return 0 on success, negative error code on failure
 */
int midi_ble_start_advertising(void);

/**
 * @brief Stop BLE advertising
 *
 * @return 0 on success, negative error code on failure
 */
int midi_ble_stop_advertising(void);

/**
 * @brief Send a MIDI message
 *
 * @param message Pointer to MIDI message to send
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send(const midi_message_t *message);

/**
 * @brief Send raw MIDI data
 *
 * @param data MIDI data bytes
 * @param length Number of bytes
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_raw(const uint8_t *data, uint16_t length);

/**
 * @brief Send a Note On message
 *
 * @param channel MIDI channel (0-15)
 * @param note Note number (0-127)
 * @param velocity Velocity (0-127, 0 = note off)
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity);

/**
 * @brief Send a Note Off message
 *
 * @param channel MIDI channel (0-15)
 * @param note Note number (0-127)
 * @param velocity Release velocity (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_note_off(uint8_t channel, uint8_t note, uint8_t velocity);

/**
 * @brief Send a Control Change message
 *
 * @param channel MIDI channel (0-15)
 * @param controller Controller number (0-127)
 * @param value Controller value (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_control_change(uint8_t channel, uint8_t controller, uint8_t value);

/**
 * @brief Send a Program Change message
 *
 * @param channel MIDI channel (0-15)
 * @param program Program number (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_program_change(uint8_t channel, uint8_t program);

/**
 * @brief Send a Pitch Bend message
 *
 * @param channel MIDI channel (0-15)
 * @param value Pitch bend value (0-16383, 8192 = center)
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_pitch_bend(uint8_t channel, uint16_t value);

/**
 * @brief Get current connection state
 *
 * @return Current BLE MIDI state
 */
midi_ble_state_t midi_ble_get_state(void);

/**
 * @brief Check if a client is connected
 *
 * @return true if a client is connected
 */
bool midi_ble_is_connected(void);

/**
 * @brief Check if a client has subscribed to notifications
 *
 * @return true if notifications are enabled
 */
bool midi_ble_is_subscribed(void);

/**
 * @brief Get current connection handle
 *
 * @return Connection handle or MIDI_BLE_INVALID_CONN_HANDLE
 */
uint16_t midi_ble_get_conn_handle(void);

/**
 * @brief Get BLE MIDI statistics
 *
 * @param stats Pointer to statistics structure to fill
 */
void midi_ble_get_stats(midi_ble_stats_t *stats);

/**
 * @brief Reset BLE MIDI statistics
 */
void midi_ble_reset_stats(void);

/**
 * @brief Disconnect current client
 *
 * @return 0 on success, negative error code on failure
 */
int midi_ble_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_BLE_SERVICE_H */
