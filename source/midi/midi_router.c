/**
 * @file midi_router.c
 * @brief MIDI Router Implementation
 *
 * This module routes MIDI messages between multiple interfaces:
 * - BLE MIDI
 * - USB MIDI
 * - Main Controller (UART)
 * - Internal application
 *
 * Features:
 * - Configurable routing rules
 * - Message filtering by type and channel
 * - Channel remapping
 * - Note transpose
 * - Soft-thru (local echo)
 * - Message transformation callbacks
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "midi_router.h"
#include "midi_ble_service.h"
#include "midi_usb.h"

#include <stdlib.h>
#include <string.h>

/* TODO: Include FreeRTOS headers */
/* #include "FreeRTOS.h" */
/* #include "queue.h" */
/* #include "semphr.h" */
/* #include "task.h" */

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Controller UART instance */
#define MIDI_CONTROLLER_UART        0

/** Standard MIDI baud rate */
#define MIDI_BAUD_RATE              31250

/** MIDI status byte masks */
#define MIDI_STATUS_MASK            0x80
#define MIDI_CHANNEL_MASK           0x0F
#define MIDI_TYPE_MASK              0xF0

/** MIDI message types */
#define MIDI_NOTE_OFF               0x80
#define MIDI_NOTE_ON                0x90
#define MIDI_POLY_AFTERTOUCH        0xA0
#define MIDI_CONTROL_CHANGE         0xB0
#define MIDI_PROGRAM_CHANGE         0xC0
#define MIDI_CHANNEL_AFTERTOUCH     0xD0
#define MIDI_PITCH_BEND             0xE0
#define MIDI_SYSEX_START            0xF0
#define MIDI_SYSEX_END              0xF7
#define MIDI_TIMING_CLOCK           0xF8
#define MIDI_START                  0xFA
#define MIDI_CONTINUE               0xFB
#define MIDI_STOP                   0xFC
#define MIDI_ACTIVE_SENSING         0xFE
#define MIDI_SYSTEM_RESET           0xFF

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief Message queue entry
 */
typedef struct {
    midi_router_msg_t msg;
    midi_port_t destination;        /**< Pre-computed destination */
} queue_entry_t;

/**
 * @brief MIDI router context
 */
typedef struct {
    /* State */
    volatile bool initialized;

    /* Configuration */
    midi_router_config_t config;

    /* Routing rules */
    midi_routing_rule_t rules[MIDI_ROUTER_MAX_RULES];
    uint8_t num_rules;

    /* Port enabled flags */
    uint8_t enabled_ports;

    /* Message queue */
    queue_entry_t queue[MIDI_ROUTER_QUEUE_SIZE];
    volatile uint16_t queue_head;
    volatile uint16_t queue_tail;
    volatile uint16_t queue_count;

    /* Callbacks */
    midi_router_callback_t event_callback;
    void *event_user_data;
    midi_transform_callback_t transform_callback;
    void *transform_user_data;

    /* Statistics */
    midi_router_stats_t stats;

    /* Controller UART state */
    uint8_t controller_rx_buffer[MIDI_ROUTER_MAX_MSG_SIZE];
    uint16_t controller_rx_len;
    uint8_t controller_running_status;

    /* Synchronization */
    /* SemaphoreHandle_t queue_mutex; */
    /* QueueHandle_t msg_queue; */

} midi_router_ctx_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Global MIDI router context */
static midi_router_ctx_t g_midi_router_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

/* Queue operations */
static int queue_push(const queue_entry_t *entry);
static int queue_pop(queue_entry_t *entry);
static bool queue_is_empty(void);
static bool queue_is_full(void);

/* Message analysis */
static uint16_t get_message_filter(uint8_t status);
static uint8_t get_message_channel(const uint8_t *data);
static uint8_t get_message_length(uint8_t status);
static bool is_channel_message(uint8_t status);
static bool is_realtime_message(uint8_t status);

/* Message transformation */
static void apply_channel_offset(midi_router_msg_t *msg, int8_t offset);
static void apply_transpose(midi_router_msg_t *msg, int8_t transpose);

/* Routing */
static midi_port_t compute_destination(const midi_router_msg_t *msg);
static bool should_filter_message(const midi_routing_rule_t *rule,
                                  const midi_router_msg_t *msg);
static int route_to_port(midi_port_t port, const midi_router_msg_t *msg);

/* Port-specific send functions */
static int send_to_ble(const midi_router_msg_t *msg);
static int send_to_usb(const midi_router_msg_t *msg);
static int send_to_controller(const midi_router_msg_t *msg);

/* Interface callbacks */
static void ble_midi_callback(const midi_ble_event_t *event, void *user_data);
static void usb_midi_callback(const midi_usb_callback_event_t *event, void *user_data);
static void controller_rx_callback(const uint8_t *data, uint16_t length);

/* Controller UART */
static int controller_uart_init(uint32_t baud_rate);
static int controller_uart_send(const uint8_t *data, uint16_t length);

/*******************************************************************************
 * Queue Implementation
 ******************************************************************************/

static int queue_push(const queue_entry_t *entry)
{
    if (entry == NULL || queue_is_full()) {
        g_midi_router_ctx.stats.messages_dropped++;
        return -1;
    }

    g_midi_router_ctx.queue[g_midi_router_ctx.queue_head] = *entry;
    g_midi_router_ctx.queue_head = (g_midi_router_ctx.queue_head + 1) % MIDI_ROUTER_QUEUE_SIZE;
    g_midi_router_ctx.queue_count++;

    return 0;
}

static int queue_pop(queue_entry_t *entry)
{
    if (entry == NULL || queue_is_empty()) {
        return -1;
    }

    *entry = g_midi_router_ctx.queue[g_midi_router_ctx.queue_tail];
    g_midi_router_ctx.queue_tail = (g_midi_router_ctx.queue_tail + 1) % MIDI_ROUTER_QUEUE_SIZE;
    g_midi_router_ctx.queue_count--;

    return 0;
}

static bool queue_is_empty(void)
{
    return g_midi_router_ctx.queue_count == 0;
}

static bool queue_is_full(void)
{
    return g_midi_router_ctx.queue_count >= MIDI_ROUTER_QUEUE_SIZE;
}

/*******************************************************************************
 * Message Analysis
 ******************************************************************************/

static uint16_t get_message_filter(uint8_t status)
{
    if (status < 0x80) {
        return MIDI_FILTER_NONE;
    }

    switch (status & 0xF0) {
        case MIDI_NOTE_OFF:
        case MIDI_NOTE_ON:
            return MIDI_FILTER_NOTE;
        case MIDI_POLY_AFTERTOUCH:
        case MIDI_CHANNEL_AFTERTOUCH:
            return MIDI_FILTER_AFTERTOUCH;
        case MIDI_CONTROL_CHANGE:
            return MIDI_FILTER_CONTROL_CHANGE;
        case MIDI_PROGRAM_CHANGE:
            return MIDI_FILTER_PROGRAM_CHANGE;
        case MIDI_PITCH_BEND:
            return MIDI_FILTER_PITCH_BEND;
        default:
            break;
    }

    switch (status) {
        case MIDI_SYSEX_START:
            return MIDI_FILTER_SYSEX;
        case MIDI_TIMING_CLOCK:
            return MIDI_FILTER_CLOCK;
        case MIDI_START:
        case MIDI_CONTINUE:
        case MIDI_STOP:
            return MIDI_FILTER_TRANSPORT;
        case MIDI_ACTIVE_SENSING:
            return MIDI_FILTER_ACTIVE_SENSE;
        default:
            return MIDI_FILTER_NONE;
    }
}

static uint8_t get_message_channel(const uint8_t *data)
{
    if (data == NULL || !(data[0] & MIDI_STATUS_MASK)) {
        return 0xFF;  /* Invalid */
    }

    if ((data[0] & 0xF0) < 0xF0) {
        return data[0] & MIDI_CHANNEL_MASK;
    }

    return 0xFF;  /* System message - no channel */
}

static uint8_t get_message_length(uint8_t status)
{
    if (status < 0x80) {
        return 0;
    }

    switch (status & 0xF0) {
        case MIDI_NOTE_OFF:
        case MIDI_NOTE_ON:
        case MIDI_POLY_AFTERTOUCH:
        case MIDI_CONTROL_CHANGE:
        case MIDI_PITCH_BEND:
            return 3;
        case MIDI_PROGRAM_CHANGE:
        case MIDI_CHANNEL_AFTERTOUCH:
            return 2;
        default:
            break;
    }

    switch (status) {
        case MIDI_SYSEX_START:
            return 0;  /* Variable length */
        case MIDI_TIMING_CLOCK:
        case MIDI_START:
        case MIDI_CONTINUE:
        case MIDI_STOP:
        case MIDI_ACTIVE_SENSING:
        case MIDI_SYSTEM_RESET:
            return 1;
        default:
            return 0;
    }
}

static bool is_channel_message(uint8_t status)
{
    return (status >= 0x80 && status < 0xF0);
}

static bool is_realtime_message(uint8_t status)
{
    return (status >= 0xF8);
}

/*******************************************************************************
 * Message Transformation
 ******************************************************************************/

static void apply_channel_offset(midi_router_msg_t *msg, int8_t offset)
{
    uint8_t channel;
    int new_channel;

    if (msg == NULL || msg->length == 0 || offset == 0) {
        return;
    }

    if (!is_channel_message(msg->data[0])) {
        return;
    }

    channel = msg->data[0] & MIDI_CHANNEL_MASK;
    new_channel = (int)channel + offset;

    /* Wrap around */
    while (new_channel < 0) new_channel += 16;
    while (new_channel > 15) new_channel -= 16;

    msg->data[0] = (msg->data[0] & MIDI_TYPE_MASK) | (new_channel & MIDI_CHANNEL_MASK);
}

static void apply_transpose(midi_router_msg_t *msg, int8_t transpose)
{
    uint8_t status;
    int new_note;

    if (msg == NULL || msg->length < 2 || transpose == 0) {
        return;
    }

    status = msg->data[0] & MIDI_TYPE_MASK;

    /* Only transpose note messages */
    if (status != MIDI_NOTE_ON && status != MIDI_NOTE_OFF &&
        status != MIDI_POLY_AFTERTOUCH) {
        return;
    }

    new_note = (int)msg->data[1] + transpose;

    /* Clamp to valid range */
    if (new_note < 0) new_note = 0;
    if (new_note > 127) new_note = 127;

    msg->data[1] = (uint8_t)new_note;
}

/*******************************************************************************
 * Routing Logic
 ******************************************************************************/

static bool should_filter_message(const midi_routing_rule_t *rule,
                                  const midi_router_msg_t *msg)
{
    uint16_t msg_filter;
    uint8_t channel;

    if (rule == NULL || msg == NULL || msg->length == 0) {
        return true;  /* Filter (drop) invalid */
    }

    /* Check message type filter */
    msg_filter = get_message_filter(msg->data[0]);
    if (rule->filter != MIDI_FILTER_ALL && !(rule->filter & msg_filter)) {
        return true;  /* Filter out this message type */
    }

    /* Check channel filter */
    channel = get_message_channel(msg->data);
    if (channel != 0xFF && rule->channels != MIDI_ALL_CHANNELS) {
        if (!(rule->channels & (1 << channel))) {
            return true;  /* Filter out this channel */
        }
    }

    return false;  /* Don't filter - let it through */
}

static midi_port_t compute_destination(const midi_router_msg_t *msg)
{
    midi_port_t dest = MIDI_PORT_NONE;
    midi_routing_rule_t *rule;

    if (msg == NULL || msg->length == 0) {
        return MIDI_PORT_NONE;
    }

    /* Check each routing rule */
    for (int i = 0; i < g_midi_router_ctx.num_rules; i++) {
        rule = &g_midi_router_ctx.rules[i];

        if (!rule->enabled) {
            continue;
        }

        /* Check if source matches */
        if (!(rule->source & msg->source)) {
            continue;
        }

        /* Check if message should be filtered */
        if (should_filter_message(rule, msg)) {
            continue;
        }

        /* Add destination */
        dest |= rule->destination;
    }

    /* Apply soft-thru if enabled */
    if (g_midi_router_ctx.config.soft_thru) {
        dest |= msg->source;
    }

    /* Remove source from destination (no echo unless soft-thru) */
    if (!g_midi_router_ctx.config.soft_thru) {
        dest &= ~msg->source;
    }

    /* Filter by enabled ports */
    dest &= g_midi_router_ctx.enabled_ports;

    return dest;
}

static int route_to_port(midi_port_t port, const midi_router_msg_t *msg)
{
    int result = 0;

    if (port & MIDI_PORT_BLE) {
        result = send_to_ble(msg);
        if (result == 0) {
            g_midi_router_ctx.stats.ble_tx++;
        }
    }

    if (port & MIDI_PORT_USB) {
        result = send_to_usb(msg);
        if (result == 0) {
            g_midi_router_ctx.stats.usb_tx++;
        }
    }

    if (port & MIDI_PORT_CONTROLLER) {
        result = send_to_controller(msg);
        if (result == 0) {
            g_midi_router_ctx.stats.controller_tx++;
        }
    }

    if (port & MIDI_PORT_INTERNAL) {
        /* Notify application via callback */
        if (g_midi_router_ctx.event_callback != NULL) {
            midi_router_event_t event;
            event.type = MIDI_ROUTER_EVENT_MESSAGE;
            event.data.message = *msg;
            g_midi_router_ctx.event_callback(&event, g_midi_router_ctx.event_user_data);
        }
    }

    return result;
}

/*******************************************************************************
 * Port-Specific Send Functions
 ******************************************************************************/

static int send_to_ble(const midi_router_msg_t *msg)
{
    if (!g_midi_router_ctx.config.enable_ble) {
        return -1;
    }

    if (!midi_ble_is_subscribed()) {
        return -2;
    }

    return midi_ble_send_raw(msg->data, msg->length);
}

static int send_to_usb(const midi_router_msg_t *msg)
{
    if (!g_midi_router_ctx.config.enable_usb) {
        return -1;
    }

    if (!midi_usb_is_ready()) {
        return -2;
    }

    return midi_usb_send_raw(0, msg->data, msg->length);
}

static int send_to_controller(const midi_router_msg_t *msg)
{
    if (!g_midi_router_ctx.config.enable_controller) {
        return -1;
    }

    return controller_uart_send(msg->data, msg->length);
}

/*******************************************************************************
 * Interface Callbacks
 ******************************************************************************/

/**
 * @brief Callback for BLE MIDI events
 */
static void ble_midi_callback(const midi_ble_event_t *event, void *user_data)
{
    queue_entry_t entry;

    (void)user_data;

    if (event == NULL || event->type != MIDI_BLE_EVENT_MESSAGE_RX) {
        return;
    }

    /* Build queue entry */
    memset(&entry, 0, sizeof(entry));
    entry.msg.source = MIDI_PORT_BLE;
    entry.msg.length = event->data.message.length;
    entry.msg.timestamp = event->data.message.timestamp;
    memcpy(entry.msg.data, event->data.message.data, event->data.message.length);

    /* Compute destination */
    entry.destination = compute_destination(&entry.msg);

    if (entry.destination != MIDI_PORT_NONE) {
        queue_push(&entry);
        g_midi_router_ctx.stats.ble_rx++;
    }
}

/**
 * @brief Callback for USB MIDI events
 */
static void usb_midi_callback(const midi_usb_callback_event_t *event, void *user_data)
{
    queue_entry_t entry;
    uint8_t cin;
    uint8_t len;

    (void)user_data;

    if (event == NULL || event->type != MIDI_USB_EVENT_MESSAGE_RX) {
        return;
    }

    /* Extract CIN to determine message length */
    cin = event->data.event.cable_cin & 0x0F;

    /* Determine length from CIN */
    switch (cin) {
        case 0x5:  /* 1-byte system common / SysEx end */
        case 0xF:  /* Single byte (real-time) */
            len = 1;
            break;
        case 0x2:  /* 2-byte system common */
        case 0x6:  /* SysEx end with 2 bytes */
        case 0xC:  /* Program change */
        case 0xD:  /* Channel pressure */
            len = 2;
            break;
        default:   /* 3-byte messages */
            len = 3;
            break;
    }

    /* Build queue entry */
    memset(&entry, 0, sizeof(entry));
    entry.msg.source = MIDI_PORT_USB;
    entry.msg.data[0] = event->data.event.midi_0;
    entry.msg.data[1] = event->data.event.midi_1;
    entry.msg.data[2] = event->data.event.midi_2;
    entry.msg.length = len;
    entry.msg.timestamp = 0;  /* TODO: Get system timestamp */

    /* Compute destination */
    entry.destination = compute_destination(&entry.msg);

    if (entry.destination != MIDI_PORT_NONE) {
        queue_push(&entry);
        g_midi_router_ctx.stats.usb_rx++;
    }
}

/**
 * @brief Callback for controller UART RX
 */
static void controller_rx_callback(const uint8_t *data, uint16_t length)
{
    queue_entry_t entry;
    uint8_t status;
    uint8_t msg_len;
    uint16_t i = 0;

    if (data == NULL || length == 0) {
        return;
    }

    /* Parse MIDI messages from raw bytes */
    while (i < length) {
        status = data[i];

        /* Real-time messages can occur anywhere */
        if (is_realtime_message(status)) {
            memset(&entry, 0, sizeof(entry));
            entry.msg.source = MIDI_PORT_CONTROLLER;
            entry.msg.data[0] = status;
            entry.msg.length = 1;
            entry.destination = compute_destination(&entry.msg);

            if (entry.destination != MIDI_PORT_NONE) {
                queue_push(&entry);
                g_midi_router_ctx.stats.controller_rx++;
            }
            i++;
            continue;
        }

        /* Handle status bytes */
        if (status & MIDI_STATUS_MASK) {
            g_midi_router_ctx.controller_running_status = status;
            msg_len = get_message_length(status);

            if (msg_len > 0 && i + msg_len <= length) {
                memset(&entry, 0, sizeof(entry));
                entry.msg.source = MIDI_PORT_CONTROLLER;
                memcpy(entry.msg.data, &data[i], msg_len);
                entry.msg.length = msg_len;
                entry.destination = compute_destination(&entry.msg);

                if (entry.destination != MIDI_PORT_NONE) {
                    queue_push(&entry);
                    g_midi_router_ctx.stats.controller_rx++;
                }
                i += msg_len;
            } else {
                i++;  /* Skip incomplete message */
            }
        } else {
            /* Data byte - use running status */
            if (g_midi_router_ctx.controller_running_status != 0) {
                msg_len = get_message_length(g_midi_router_ctx.controller_running_status);
                if (msg_len > 1 && i + msg_len - 1 <= length) {
                    memset(&entry, 0, sizeof(entry));
                    entry.msg.source = MIDI_PORT_CONTROLLER;
                    entry.msg.data[0] = g_midi_router_ctx.controller_running_status;
                    memcpy(&entry.msg.data[1], &data[i], msg_len - 1);
                    entry.msg.length = msg_len;
                    entry.destination = compute_destination(&entry.msg);

                    if (entry.destination != MIDI_PORT_NONE) {
                        queue_push(&entry);
                        g_midi_router_ctx.stats.controller_rx++;
                    }
                    i += msg_len - 1;
                } else {
                    i++;
                }
            } else {
                i++;  /* Skip orphan data byte */
            }
        }
    }
}

/*******************************************************************************
 * Controller UART
 ******************************************************************************/

static int controller_uart_init(uint32_t baud_rate)
{
    (void)baud_rate;

    /*
     * TODO: Initialize UART for MIDI communication with main controller
     *
     * cyhal_uart_cfg_t uart_config = {
     *     .data_bits = 8,
     *     .stop_bits = 1,
     *     .parity = CYHAL_UART_PARITY_NONE,
     *     .rx_buffer = controller_rx_buffer,
     *     .rx_buffer_size = sizeof(controller_rx_buffer),
     * };
     *
     * cyhal_uart_init(&uart_obj, TX_PIN, RX_PIN, NC, NC, NULL, &uart_config);
     * cyhal_uart_set_baud(&uart_obj, baud_rate, NULL);
     * cyhal_uart_register_callback(&uart_obj, uart_event_handler, NULL);
     * cyhal_uart_enable_event(&uart_obj, CYHAL_UART_IRQ_RX_NOT_EMPTY, 3, true);
     */

    return 0;
}

static int controller_uart_send(const uint8_t *data, uint16_t length)
{
    (void)data;
    (void)length;

    /*
     * TODO: Send data over UART
     *
     * return cyhal_uart_write(&uart_obj, (void*)data, &length);
     */

    return 0;
}

/*******************************************************************************
 * Public API Implementation
 ******************************************************************************/

int midi_router_init(const midi_router_config_t *config)
{
    int result;

    if (g_midi_router_ctx.initialized) {
        return -1;  /* Already initialized */
    }

    /* Clear context */
    memset(&g_midi_router_ctx, 0, sizeof(g_midi_router_ctx));

    /* Apply configuration */
    if (config != NULL) {
        g_midi_router_ctx.config = *config;
    } else {
        midi_router_config_t default_config = MIDI_ROUTER_CONFIG_DEFAULT;
        g_midi_router_ctx.config = default_config;
    }

    /* Set enabled ports based on config */
    g_midi_router_ctx.enabled_ports = 0;
    if (g_midi_router_ctx.config.enable_ble) {
        g_midi_router_ctx.enabled_ports |= MIDI_PORT_BLE;
    }
    if (g_midi_router_ctx.config.enable_usb) {
        g_midi_router_ctx.enabled_ports |= MIDI_PORT_USB;
    }
    if (g_midi_router_ctx.config.enable_controller) {
        g_midi_router_ctx.enabled_ports |= MIDI_PORT_CONTROLLER;
    }
    g_midi_router_ctx.enabled_ports |= MIDI_PORT_INTERNAL;

    /* Initialize controller UART if enabled */
    if (g_midi_router_ctx.config.enable_controller) {
        result = controller_uart_init(g_midi_router_ctx.config.controller_baud);
        if (result != 0) {
            return -2;
        }
    }

    /* Register callbacks with MIDI interfaces */
    midi_ble_register_callback(ble_midi_callback, NULL);
    midi_usb_register_callback(usb_midi_callback, NULL);

    /* Set default routing if merge_inputs is enabled */
    if (g_midi_router_ctx.config.merge_inputs) {
        midi_router_set_default_routing();
    }

    /*
     * TODO: Create FreeRTOS synchronization
     *
     * g_midi_router_ctx.queue_mutex = xSemaphoreCreateMutex();
     * g_midi_router_ctx.msg_queue = xQueueCreate(MIDI_ROUTER_QUEUE_SIZE,
     *                                            sizeof(queue_entry_t));
     */

    g_midi_router_ctx.initialized = true;

    return 0;
}

void midi_router_deinit(void)
{
    if (!g_midi_router_ctx.initialized) {
        return;
    }

    /* Unregister callbacks */
    midi_ble_register_callback(NULL, NULL);
    midi_usb_register_callback(NULL, NULL);

    /*
     * TODO: Delete FreeRTOS synchronization
     *
     * if (g_midi_router_ctx.queue_mutex != NULL) {
     *     vSemaphoreDelete(g_midi_router_ctx.queue_mutex);
     * }
     * if (g_midi_router_ctx.msg_queue != NULL) {
     *     vQueueDelete(g_midi_router_ctx.msg_queue);
     * }
     */

    g_midi_router_ctx.initialized = false;
}

void midi_router_register_callback(midi_router_callback_t callback, void *user_data)
{
    g_midi_router_ctx.event_callback = callback;
    g_midi_router_ctx.event_user_data = user_data;
}

void midi_router_register_transform(midi_transform_callback_t callback, void *user_data)
{
    g_midi_router_ctx.transform_callback = callback;
    g_midi_router_ctx.transform_user_data = user_data;
}

void midi_router_process(void)
{
    queue_entry_t entry;
    midi_routing_rule_t *rule;
    midi_router_msg_t transformed_msg;

    if (!g_midi_router_ctx.initialized) {
        return;
    }

    /* Process all queued messages */
    while (!queue_is_empty()) {
        if (queue_pop(&entry) != 0) {
            break;
        }

        /* Filter active sensing if configured */
        if (g_midi_router_ctx.config.filter_active_sensing &&
            entry.msg.data[0] == MIDI_ACTIVE_SENSING) {
            g_midi_router_ctx.stats.messages_filtered++;
            continue;
        }

        /* Apply transform callback if registered */
        if (g_midi_router_ctx.transform_callback != NULL) {
            transformed_msg = entry.msg;
            if (!g_midi_router_ctx.transform_callback(&transformed_msg,
                                                       g_midi_router_ctx.transform_user_data)) {
                /* Callback returned false - drop message */
                g_midi_router_ctx.stats.messages_filtered++;
                continue;
            }
            entry.msg = transformed_msg;
        }

        /* Apply rule-specific transformations */
        for (int i = 0; i < g_midi_router_ctx.num_rules; i++) {
            rule = &g_midi_router_ctx.rules[i];

            if (!rule->enabled || !(rule->source & entry.msg.source)) {
                continue;
            }

            if (should_filter_message(rule, &entry.msg)) {
                continue;
            }

            /* Apply channel offset */
            if (rule->channel_offset != 0) {
                apply_channel_offset(&entry.msg, rule->channel_offset);
            }

            /* Apply transpose */
            if (rule->transpose != 0) {
                apply_transpose(&entry.msg, rule->transpose);
            }
        }

        /* Route to destinations */
        if (entry.destination != MIDI_PORT_NONE) {
            route_to_port(entry.destination, &entry.msg);
            g_midi_router_ctx.stats.messages_routed++;
        }
    }
}

int midi_router_send(const midi_router_msg_t *msg)
{
    queue_entry_t entry;

    if (!g_midi_router_ctx.initialized || msg == NULL) {
        return -1;
    }

    entry.msg = *msg;
    entry.destination = compute_destination(msg);

    if (entry.destination == MIDI_PORT_NONE) {
        return -2;  /* No destination */
    }

    return queue_push(&entry);
}

int midi_router_send_from(midi_port_t source, const uint8_t *data, uint16_t length)
{
    midi_router_msg_t msg;

    if (data == NULL || length == 0 || length > MIDI_ROUTER_MAX_MSG_SIZE) {
        return -1;
    }

    msg.source = source;
    msg.length = length;
    msg.timestamp = 0;  /* TODO: Get system timestamp */
    memcpy(msg.data, data, length);

    return midi_router_send(&msg);
}

/*******************************************************************************
 * Routing Rules API
 ******************************************************************************/

int midi_router_add_rule(const midi_routing_rule_t *rule)
{
    if (rule == NULL) {
        return -1;
    }

    if (g_midi_router_ctx.num_rules >= MIDI_ROUTER_MAX_RULES) {
        return -2;  /* Rules full */
    }

    g_midi_router_ctx.rules[g_midi_router_ctx.num_rules] = *rule;
    return g_midi_router_ctx.num_rules++;
}

int midi_router_remove_rule(int index)
{
    if (index < 0 || index >= g_midi_router_ctx.num_rules) {
        return -1;
    }

    /* Shift remaining rules */
    for (int i = index; i < g_midi_router_ctx.num_rules - 1; i++) {
        g_midi_router_ctx.rules[i] = g_midi_router_ctx.rules[i + 1];
    }

    g_midi_router_ctx.num_rules--;
    return 0;
}

int midi_router_enable_rule(int index, bool enabled)
{
    if (index < 0 || index >= g_midi_router_ctx.num_rules) {
        return -1;
    }

    g_midi_router_ctx.rules[index].enabled = enabled;
    return 0;
}

void midi_router_clear_rules(void)
{
    g_midi_router_ctx.num_rules = 0;
    memset(g_midi_router_ctx.rules, 0, sizeof(g_midi_router_ctx.rules));
}

int midi_router_get_rule(int index, midi_routing_rule_t *rule)
{
    if (index < 0 || index >= g_midi_router_ctx.num_rules || rule == NULL) {
        return -1;
    }

    *rule = g_midi_router_ctx.rules[index];
    return 0;
}

void midi_router_set_default_routing(void)
{
    midi_routing_rule_t rule;

    midi_router_clear_rules();

    /* BLE -> USB, Controller */
    memset(&rule, 0, sizeof(rule));
    rule.source = MIDI_PORT_BLE;
    rule.destination = MIDI_PORT_USB | MIDI_PORT_CONTROLLER | MIDI_PORT_INTERNAL;
    rule.filter = MIDI_FILTER_ALL;
    rule.channels = MIDI_ALL_CHANNELS;
    rule.enabled = true;
    midi_router_add_rule(&rule);

    /* USB -> BLE, Controller */
    rule.source = MIDI_PORT_USB;
    rule.destination = MIDI_PORT_BLE | MIDI_PORT_CONTROLLER | MIDI_PORT_INTERNAL;
    midi_router_add_rule(&rule);

    /* Controller -> BLE, USB */
    rule.source = MIDI_PORT_CONTROLLER;
    rule.destination = MIDI_PORT_BLE | MIDI_PORT_USB | MIDI_PORT_INTERNAL;
    midi_router_add_rule(&rule);

    /* Internal -> All */
    rule.source = MIDI_PORT_INTERNAL;
    rule.destination = MIDI_PORT_BLE | MIDI_PORT_USB | MIDI_PORT_CONTROLLER;
    midi_router_add_rule(&rule);
}

/*******************************************************************************
 * Port Control API
 ******************************************************************************/

int midi_router_set_port_enabled(midi_port_t port, bool enabled)
{
    if (enabled) {
        g_midi_router_ctx.enabled_ports |= port;
    } else {
        g_midi_router_ctx.enabled_ports &= ~port;
    }
    return 0;
}

bool midi_router_is_port_enabled(midi_port_t port)
{
    return (g_midi_router_ctx.enabled_ports & port) != 0;
}

void midi_router_set_soft_thru(bool enabled)
{
    g_midi_router_ctx.config.soft_thru = enabled;
}

/*******************************************************************************
 * Convenience Functions
 ******************************************************************************/

int midi_router_send_note_on(midi_port_t destination, uint8_t channel,
                             uint8_t note, uint8_t velocity)
{
    midi_router_msg_t msg;

    msg.source = MIDI_PORT_INTERNAL;
    msg.data[0] = MIDI_NOTE_ON | (channel & 0x0F);
    msg.data[1] = note & 0x7F;
    msg.data[2] = velocity & 0x7F;
    msg.length = 3;
    msg.timestamp = 0;

    /* Direct route to destination */
    return route_to_port(destination & g_midi_router_ctx.enabled_ports, &msg);
}

int midi_router_send_note_off(midi_port_t destination, uint8_t channel,
                              uint8_t note, uint8_t velocity)
{
    midi_router_msg_t msg;

    msg.source = MIDI_PORT_INTERNAL;
    msg.data[0] = MIDI_NOTE_OFF | (channel & 0x0F);
    msg.data[1] = note & 0x7F;
    msg.data[2] = velocity & 0x7F;
    msg.length = 3;
    msg.timestamp = 0;

    return route_to_port(destination & g_midi_router_ctx.enabled_ports, &msg);
}

int midi_router_send_control_change(midi_port_t destination, uint8_t channel,
                                    uint8_t controller, uint8_t value)
{
    midi_router_msg_t msg;

    msg.source = MIDI_PORT_INTERNAL;
    msg.data[0] = MIDI_CONTROL_CHANGE | (channel & 0x0F);
    msg.data[1] = controller & 0x7F;
    msg.data[2] = value & 0x7F;
    msg.length = 3;
    msg.timestamp = 0;

    return route_to_port(destination & g_midi_router_ctx.enabled_ports, &msg);
}

int midi_router_send_program_change(midi_port_t destination, uint8_t channel,
                                    uint8_t program)
{
    midi_router_msg_t msg;

    msg.source = MIDI_PORT_INTERNAL;
    msg.data[0] = MIDI_PROGRAM_CHANGE | (channel & 0x0F);
    msg.data[1] = program & 0x7F;
    msg.length = 2;
    msg.timestamp = 0;

    return route_to_port(destination & g_midi_router_ctx.enabled_ports, &msg);
}

int midi_router_panic(void)
{
    int result = 0;

    /* Send All Notes Off on all channels to all ports */
    for (uint8_t ch = 0; ch < 16; ch++) {
        result |= midi_router_send_control_change(MIDI_PORT_ALL, ch, 123, 0);
        result |= midi_router_send_control_change(MIDI_PORT_ALL, ch, 120, 0);  /* All Sound Off */
    }

    return result;
}

/*******************************************************************************
 * Statistics API
 ******************************************************************************/

void midi_router_get_stats(midi_router_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_midi_router_ctx.stats;
    }
}

void midi_router_reset_stats(void)
{
    memset(&g_midi_router_ctx.stats, 0, sizeof(g_midi_router_ctx.stats));
}
