/**
 * @file midi_usb.h
 * @brief USB MIDI Class Device API
 *
 * This module implements the USB MIDI 1.0 class specification for
 * sending and receiving MIDI messages over USB.
 *
 * USB MIDI uses the Audio Class with MIDI Streaming subclass:
 * - Interface Class: 0x01 (Audio)
 * - Interface SubClass: 0x03 (MIDI Streaming)
 *
 * The implementation supports USB Full-Speed (12 Mbps) which is
 * sufficient for MIDI's relatively low bandwidth requirements.
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MIDI_USB_H
#define MIDI_USB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** USB MIDI Class Codes */
#define USB_CLASS_AUDIO                 0x01
#define USB_SUBCLASS_MIDISTREAMING      0x03

/** USB MIDI Endpoint packet size (Full-Speed) */
#define MIDI_USB_PACKET_SIZE            64

/** Maximum MIDI cables (virtual ports) */
#define MIDI_USB_MAX_CABLES             1

/** USB MIDI event packet size (always 4 bytes) */
#define MIDI_USB_EVENT_SIZE             4

/** Maximum queued events for TX/RX */
#define MIDI_USB_QUEUE_SIZE             64

/** Invalid cable number */
#define MIDI_USB_INVALID_CABLE          0xFF

/*******************************************************************************
 * USB MIDI Event Packet Format
 ******************************************************************************/

/**
 * USB MIDI Event Packet (4 bytes):
 *
 * Byte 0: Cable Number (4 bits) | Code Index Number (4 bits)
 * Byte 1: MIDI_0 (status byte or data)
 * Byte 2: MIDI_1 (data byte or 0x00)
 * Byte 3: MIDI_2 (data byte or 0x00)
 *
 * Code Index Number (CIN) indicates the message type and
 * number of valid bytes in MIDI_0, MIDI_1, MIDI_2.
 */

/** USB MIDI Code Index Numbers (CIN) */
typedef enum {
    MIDI_CIN_MISC            = 0x0,  /**< Miscellaneous (reserved) */
    MIDI_CIN_CABLE_EVENT     = 0x1,  /**< Cable events (reserved) */
    MIDI_CIN_SYSCOMMON_2     = 0x2,  /**< 2-byte System Common */
    MIDI_CIN_SYSCOMMON_3     = 0x3,  /**< 3-byte System Common */
    MIDI_CIN_SYSEX_START     = 0x4,  /**< SysEx starts or continues */
    MIDI_CIN_SYSCOMMON_1     = 0x5,  /**< 1-byte System Common / SysEx ends with 1 byte */
    MIDI_CIN_SYSEX_END_2     = 0x6,  /**< SysEx ends with 2 bytes */
    MIDI_CIN_SYSEX_END_3     = 0x7,  /**< SysEx ends with 3 bytes */
    MIDI_CIN_NOTE_OFF        = 0x8,  /**< Note Off */
    MIDI_CIN_NOTE_ON         = 0x9,  /**< Note On */
    MIDI_CIN_POLY_KEYPRESS   = 0xA,  /**< Poly Key Pressure */
    MIDI_CIN_CONTROL_CHANGE  = 0xB,  /**< Control Change */
    MIDI_CIN_PROGRAM_CHANGE  = 0xC,  /**< Program Change */
    MIDI_CIN_CHANNEL_PRESS   = 0xD,  /**< Channel Pressure */
    MIDI_CIN_PITCH_BEND      = 0xE,  /**< Pitch Bend */
    MIDI_CIN_SINGLE_BYTE     = 0xF,  /**< Single byte (Real-Time) */
} midi_usb_cin_t;

/*******************************************************************************
 * Types
 ******************************************************************************/

/** USB MIDI event packet */
typedef struct {
    uint8_t cable_cin;      /**< Cable number (high nibble) | CIN (low nibble) */
    uint8_t midi_0;         /**< MIDI byte 0 (status or data) */
    uint8_t midi_1;         /**< MIDI byte 1 (data or 0x00) */
    uint8_t midi_2;         /**< MIDI byte 2 (data or 0x00) */
} midi_usb_event_t;

/** USB MIDI connection state */
typedef enum {
    MIDI_USB_STATE_DETACHED = 0,    /**< USB cable not connected */
    MIDI_USB_STATE_ATTACHED,        /**< USB cable connected, not configured */
    MIDI_USB_STATE_CONFIGURED,      /**< USB configured and ready */
    MIDI_USB_STATE_SUSPENDED        /**< USB suspended */
} midi_usb_state_t;

/** USB MIDI event types for callbacks */
typedef enum {
    MIDI_USB_EVENT_CONNECTED,       /**< USB configured */
    MIDI_USB_EVENT_DISCONNECTED,    /**< USB disconnected */
    MIDI_USB_EVENT_SUSPENDED,       /**< USB suspended */
    MIDI_USB_EVENT_RESUMED,         /**< USB resumed */
    MIDI_USB_EVENT_MESSAGE_RX,      /**< MIDI message received */
    MIDI_USB_EVENT_TX_COMPLETE,     /**< TX transfer complete */
    MIDI_USB_EVENT_ERROR            /**< Error occurred */
} midi_usb_event_type_t;

/** USB MIDI callback event data */
typedef struct {
    midi_usb_event_type_t type;
    union {
        midi_usb_event_t event;     /**< For MESSAGE_RX */
        int error_code;             /**< For ERROR */
    } data;
} midi_usb_callback_event_t;

/** USB MIDI callback function type */
typedef void (*midi_usb_callback_t)(const midi_usb_callback_event_t *event, void *user_data);

/** USB MIDI configuration */
typedef struct {
    const char *product_name;       /**< USB product string */
    const char *manufacturer;       /**< USB manufacturer string */
    const char *serial_number;      /**< USB serial number (NULL for auto) */
    uint16_t vendor_id;             /**< USB Vendor ID */
    uint16_t product_id;            /**< USB Product ID */
    uint8_t num_cables;             /**< Number of MIDI cables (virtual ports) */
} midi_usb_config_t;

/** USB MIDI statistics */
typedef struct {
    uint32_t messages_sent;         /**< Total messages sent */
    uint32_t messages_received;     /**< Total messages received */
    uint32_t packets_sent;          /**< Total USB packets sent */
    uint32_t packets_received;      /**< Total USB packets received */
    uint32_t tx_errors;             /**< Transmission errors */
    uint32_t rx_errors;             /**< Reception errors */
    uint32_t buffer_overflows;      /**< Buffer overflow count */
} midi_usb_stats_t;

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default USB MIDI configuration */
#define MIDI_USB_CONFIG_DEFAULT {               \
    .product_name = "Infineon MIDI Device",     \
    .manufacturer = "Infineon Technologies",    \
    .serial_number = NULL,                      \
    .vendor_id = 0x04B4,                        \
    .product_id = 0x0001,                       \
    .num_cables = 1                             \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize USB MIDI device
 *
 * @param config Pointer to configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int midi_usb_init(const midi_usb_config_t *config);

/**
 * @brief Deinitialize USB MIDI device
 */
void midi_usb_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void midi_usb_register_callback(midi_usb_callback_t callback, void *user_data);

/**
 * @brief Process USB events (call from USB task)
 *
 * This should be called periodically to process USB events
 * and handle incoming MIDI data.
 */
void midi_usb_process(void);

/**
 * @brief Send a USB MIDI event
 *
 * @param event Pointer to USB MIDI event packet
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_event(const midi_usb_event_t *event);

/**
 * @brief Send raw MIDI data
 *
 * Automatically converts raw MIDI data to USB MIDI event format.
 *
 * @param cable Cable number (0 to num_cables-1)
 * @param data MIDI data bytes
 * @param length Number of bytes
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_raw(uint8_t cable, const uint8_t *data, uint16_t length);

/**
 * @brief Send a Note On message
 *
 * @param cable Cable number
 * @param channel MIDI channel (0-15)
 * @param note Note number (0-127)
 * @param velocity Velocity (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_note_on(uint8_t cable, uint8_t channel, uint8_t note, uint8_t velocity);

/**
 * @brief Send a Note Off message
 *
 * @param cable Cable number
 * @param channel MIDI channel (0-15)
 * @param note Note number (0-127)
 * @param velocity Release velocity (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_note_off(uint8_t cable, uint8_t channel, uint8_t note, uint8_t velocity);

/**
 * @brief Send a Control Change message
 *
 * @param cable Cable number
 * @param channel MIDI channel (0-15)
 * @param controller Controller number (0-127)
 * @param value Controller value (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_control_change(uint8_t cable, uint8_t channel, uint8_t controller, uint8_t value);

/**
 * @brief Send a Program Change message
 *
 * @param cable Cable number
 * @param channel MIDI channel (0-15)
 * @param program Program number (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_program_change(uint8_t cable, uint8_t channel, uint8_t program);

/**
 * @brief Send a Pitch Bend message
 *
 * @param cable Cable number
 * @param channel MIDI channel (0-15)
 * @param value Pitch bend value (0-16383, 8192 = center)
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_pitch_bend(uint8_t cable, uint8_t channel, uint16_t value);

/**
 * @brief Send a SysEx message
 *
 * @param cable Cable number
 * @param data SysEx data (including F0 and F7)
 * @param length Data length
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_sysex(uint8_t cable, const uint8_t *data, uint16_t length);

/**
 * @brief Send a real-time message (clock, start, stop, etc.)
 *
 * @param cable Cable number
 * @param status Real-time status byte (0xF8-0xFF)
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_realtime(uint8_t cable, uint8_t status);

/**
 * @brief Get current USB state
 *
 * @return Current USB MIDI state
 */
midi_usb_state_t midi_usb_get_state(void);

/**
 * @brief Check if USB is connected and configured
 *
 * @return true if ready to send/receive MIDI
 */
bool midi_usb_is_ready(void);

/**
 * @brief Get USB MIDI statistics
 *
 * @param stats Pointer to statistics structure to fill
 */
void midi_usb_get_stats(midi_usb_stats_t *stats);

/**
 * @brief Reset USB MIDI statistics
 */
void midi_usb_reset_stats(void);

/**
 * @brief Flush TX buffer
 *
 * Forces any queued MIDI events to be sent immediately.
 *
 * @return 0 on success, negative error code on failure
 */
int midi_usb_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_USB_H */
