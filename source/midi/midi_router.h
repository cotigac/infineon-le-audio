/**
 * @file midi_router.h
 * @brief MIDI Router API
 *
 * This module provides MIDI message routing between multiple interfaces:
 * - BLE MIDI (Bluetooth Low Energy)
 * - USB MIDI (USB Full-Speed)
 * - Main Controller (via UART/I2S/SPI)
 *
 * The router supports configurable routing rules, message filtering,
 * channel remapping, and message transformation.
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MIDI_ROUTER_H
#define MIDI_ROUTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** MIDI interface/port identifiers */
typedef enum {
    MIDI_PORT_NONE          = 0x00,     /**< No port (disabled) */
    MIDI_PORT_BLE           = 0x01,     /**< BLE MIDI interface */
    MIDI_PORT_USB           = 0x02,     /**< USB MIDI interface */
    MIDI_PORT_CONTROLLER    = 0x04,     /**< Main controller interface */
    MIDI_PORT_INTERNAL      = 0x08,     /**< Internal (application) */
    MIDI_PORT_ALL           = 0x0F      /**< All ports */
} midi_port_t;

/** MIDI message type filter flags */
typedef enum {
    MIDI_FILTER_NONE            = 0x0000,   /**< No filtering (pass all) */
    MIDI_FILTER_NOTE            = 0x0001,   /**< Note On/Off */
    MIDI_FILTER_AFTERTOUCH      = 0x0002,   /**< Poly/Channel Aftertouch */
    MIDI_FILTER_CONTROL_CHANGE  = 0x0004,   /**< Control Change */
    MIDI_FILTER_PROGRAM_CHANGE  = 0x0008,   /**< Program Change */
    MIDI_FILTER_PITCH_BEND      = 0x0010,   /**< Pitch Bend */
    MIDI_FILTER_SYSEX           = 0x0020,   /**< System Exclusive */
    MIDI_FILTER_CLOCK           = 0x0040,   /**< Timing Clock */
    MIDI_FILTER_TRANSPORT       = 0x0080,   /**< Start/Stop/Continue */
    MIDI_FILTER_ACTIVE_SENSE    = 0x0100,   /**< Active Sensing */
    MIDI_FILTER_REALTIME        = 0x01C0,   /**< All real-time */
    MIDI_FILTER_CHANNEL         = 0x001F,   /**< All channel messages */
    MIDI_FILTER_ALL             = 0x01FF    /**< All messages */
} midi_filter_t;

/** Maximum MIDI message size */
#define MIDI_ROUTER_MAX_MSG_SIZE    256

/** Maximum number of routing rules */
#define MIDI_ROUTER_MAX_RULES       16

/** Maximum message queue size */
#define MIDI_ROUTER_QUEUE_SIZE      64

/** All MIDI channels mask */
#define MIDI_ALL_CHANNELS           0xFFFF

/*******************************************************************************
 * Types
 ******************************************************************************/

/** MIDI message structure */
typedef struct {
    uint8_t data[MIDI_ROUTER_MAX_MSG_SIZE]; /**< Raw MIDI data */
    uint16_t length;                         /**< Message length */
    midi_port_t source;                      /**< Source port */
    uint32_t timestamp;                      /**< Timestamp (ms) */
} midi_router_msg_t;

/** Routing rule structure */
typedef struct {
    midi_port_t source;             /**< Source port(s) - bitmask */
    midi_port_t destination;        /**< Destination port(s) - bitmask */
    uint16_t filter;                /**< Message type filter (midi_filter_t) */
    uint16_t channels;              /**< Channel filter (bit per channel) */
    int8_t channel_offset;          /**< Channel remapping offset (-15 to +15) */
    int8_t transpose;               /**< Note transpose (-127 to +127) */
    bool enabled;                   /**< Rule enabled */
} midi_routing_rule_t;

/** MIDI router configuration */
typedef struct {
    bool enable_ble;                /**< Enable BLE MIDI routing */
    bool enable_usb;                /**< Enable USB MIDI routing */
    bool enable_controller;         /**< Enable controller routing */
    bool soft_thru;                 /**< Enable soft-thru (echo) */
    bool filter_active_sensing;     /**< Filter out Active Sensing */
    bool merge_inputs;              /**< Merge all inputs to all outputs */
    uint16_t controller_baud;       /**< Controller UART baud rate (if applicable) */
} midi_router_config_t;

/** Router statistics */
typedef struct {
    uint32_t messages_routed;       /**< Total messages routed */
    uint32_t messages_filtered;     /**< Messages filtered out */
    uint32_t messages_dropped;      /**< Messages dropped (queue full) */
    uint32_t ble_rx;                /**< Messages received from BLE */
    uint32_t ble_tx;                /**< Messages sent to BLE */
    uint32_t usb_rx;                /**< Messages received from USB */
    uint32_t usb_tx;                /**< Messages sent to USB */
    uint32_t controller_rx;         /**< Messages received from controller */
    uint32_t controller_tx;         /**< Messages sent to controller */
} midi_router_stats_t;

/** MIDI router event types */
typedef enum {
    MIDI_ROUTER_EVENT_MESSAGE,      /**< Message routed */
    MIDI_ROUTER_EVENT_OVERFLOW,     /**< Queue overflow */
    MIDI_ROUTER_EVENT_ERROR         /**< Error occurred */
} midi_router_event_type_t;

/** MIDI router event data */
typedef struct {
    midi_router_event_type_t type;
    union {
        midi_router_msg_t message;  /**< For MESSAGE events */
        int error_code;             /**< For ERROR events */
    } data;
} midi_router_event_t;

/** MIDI router callback function type */
typedef void (*midi_router_callback_t)(const midi_router_event_t *event, void *user_data);

/** Message transform callback - allows custom processing */
typedef bool (*midi_transform_callback_t)(midi_router_msg_t *msg, void *user_data);

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default router configuration */
#define MIDI_ROUTER_CONFIG_DEFAULT {    \
    .enable_ble = true,                 \
    .enable_usb = true,                 \
    .enable_controller = true,          \
    .soft_thru = false,                 \
    .filter_active_sensing = true,      \
    .merge_inputs = true,               \
    .controller_baud = 31250            \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize the MIDI router
 *
 * @param config Pointer to configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int midi_router_init(const midi_router_config_t *config);

/**
 * @brief Deinitialize the MIDI router
 */
void midi_router_deinit(void);

/**
 * @brief Register event callback for routed messages
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void midi_router_register_callback(midi_router_callback_t callback, void *user_data);

/**
 * @brief Register message transform callback
 *
 * Called before routing to allow custom message processing.
 * Return false from callback to drop the message.
 *
 * @param callback Transform function
 * @param user_data User data passed to callback
 */
void midi_router_register_transform(midi_transform_callback_t callback, void *user_data);

/**
 * @brief Process pending MIDI messages
 *
 * Should be called periodically from the MIDI task to process
 * queued messages and perform routing.
 */
void midi_router_process(void);

/**
 * @brief Route a MIDI message
 *
 * @param msg Pointer to message to route
 * @return 0 on success, negative error code on failure
 */
int midi_router_send(const midi_router_msg_t *msg);

/**
 * @brief Send raw MIDI data from a specific port
 *
 * @param source Source port identifier
 * @param data MIDI data bytes
 * @param length Number of bytes
 * @return 0 on success, negative error code on failure
 */
int midi_router_send_from(midi_port_t source, const uint8_t *data, uint16_t length);

/*******************************************************************************
 * Routing Rules API
 ******************************************************************************/

/**
 * @brief Add a routing rule
 *
 * @param rule Pointer to routing rule
 * @return Rule index on success, negative error code on failure
 */
int midi_router_add_rule(const midi_routing_rule_t *rule);

/**
 * @brief Remove a routing rule
 *
 * @param index Rule index to remove
 * @return 0 on success, negative error code on failure
 */
int midi_router_remove_rule(int index);

/**
 * @brief Enable or disable a routing rule
 *
 * @param index Rule index
 * @param enabled true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int midi_router_enable_rule(int index, bool enabled);

/**
 * @brief Clear all routing rules
 */
void midi_router_clear_rules(void);

/**
 * @brief Get a routing rule
 *
 * @param index Rule index
 * @param rule Pointer to store rule
 * @return 0 on success, negative error code on failure
 */
int midi_router_get_rule(int index, midi_routing_rule_t *rule);

/**
 * @brief Set default routing (merge all inputs to all outputs)
 */
void midi_router_set_default_routing(void);

/*******************************************************************************
 * Port Control API
 ******************************************************************************/

/**
 * @brief Enable or disable a port
 *
 * @param port Port identifier
 * @param enabled true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int midi_router_set_port_enabled(midi_port_t port, bool enabled);

/**
 * @brief Check if a port is enabled
 *
 * @param port Port identifier
 * @return true if port is enabled
 */
bool midi_router_is_port_enabled(midi_port_t port);

/**
 * @brief Set soft-thru mode
 *
 * When enabled, messages are echoed back to their source port.
 *
 * @param enabled true to enable soft-thru
 */
void midi_router_set_soft_thru(bool enabled);

/*******************************************************************************
 * Convenience Functions
 ******************************************************************************/

/**
 * @brief Send a Note On message
 *
 * @param destination Destination port(s)
 * @param channel MIDI channel (0-15)
 * @param note Note number (0-127)
 * @param velocity Velocity (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_router_send_note_on(midi_port_t destination, uint8_t channel,
                             uint8_t note, uint8_t velocity);

/**
 * @brief Send a Note Off message
 *
 * @param destination Destination port(s)
 * @param channel MIDI channel (0-15)
 * @param note Note number (0-127)
 * @param velocity Release velocity (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_router_send_note_off(midi_port_t destination, uint8_t channel,
                              uint8_t note, uint8_t velocity);

/**
 * @brief Send a Control Change message
 *
 * @param destination Destination port(s)
 * @param channel MIDI channel (0-15)
 * @param controller Controller number (0-127)
 * @param value Controller value (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_router_send_control_change(midi_port_t destination, uint8_t channel,
                                    uint8_t controller, uint8_t value);

/**
 * @brief Send a Program Change message
 *
 * @param destination Destination port(s)
 * @param channel MIDI channel (0-15)
 * @param program Program number (0-127)
 * @return 0 on success, negative error code on failure
 */
int midi_router_send_program_change(midi_port_t destination, uint8_t channel,
                                    uint8_t program);

/**
 * @brief Send All Notes Off to all ports
 *
 * @return 0 on success, negative error code on failure
 */
int midi_router_panic(void);

/*******************************************************************************
 * Statistics API
 ******************************************************************************/

/**
 * @brief Get router statistics
 *
 * @param stats Pointer to statistics structure to fill
 */
void midi_router_get_stats(midi_router_stats_t *stats);

/**
 * @brief Reset router statistics
 */
void midi_router_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_ROUTER_H */
