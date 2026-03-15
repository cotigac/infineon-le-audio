/**
 * @file midi_ble_service.c
 * @brief BLE MIDI Service Implementation
 *
 * This module implements the MIDI over Bluetooth Low Energy (BLE-MIDI)
 * specification as defined by Apple and adopted as the industry standard.
 *
 * BLE-MIDI Packet Format:
 * - Header byte: 1ttttttt (timestamp high bits, MSB always 1)
 * - Timestamp byte: 1ttttttt (timestamp low bits, MSB always 1)
 * - MIDI data: status + data bytes
 * - Multiple MIDI messages can be packed in one packet
 *
 * Timestamp: 13-bit millisecond counter (wraps at 8191ms)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "midi_ble_service.h"

#include <stdlib.h>
#include <string.h>

/* TODO: Include Infineon BTSTACK headers */
/* #include "wiced_bt_stack.h" */
/* #include "wiced_bt_gatt.h" */
/* #include "wiced_bt_ble.h" */
/* #include "wiced_bt_cfg.h" */

/* TODO: Include FreeRTOS headers */
/* #include "FreeRTOS.h" */
/* #include "task.h" */
/* #include "semphr.h" */

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** BLE-MIDI timestamp mask (13 bits) */
#define MIDI_BLE_TIMESTAMP_MASK         0x1FFF

/** BLE-MIDI header/timestamp MSB flag */
#define MIDI_BLE_TIMESTAMP_MSB          0x80

/** Maximum number of MIDI messages to batch in one packet */
#define MIDI_BLE_MAX_BATCH              8

/** Running status timeout (ms) */
#define MIDI_BLE_RUNNING_STATUS_TIMEOUT 100

/** Advertising interval (units of 0.625ms) */
#define MIDI_BLE_ADV_INTERVAL_MIN       48   /* 30ms */
#define MIDI_BLE_ADV_INTERVAL_MAX       96   /* 60ms */

/** Connection interval range (units of 1.25ms) */
#define MIDI_BLE_CONN_INTERVAL_MIN      6    /* 7.5ms */
#define MIDI_BLE_CONN_INTERVAL_MAX      12   /* 15ms */

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief MIDI message parser state
 */
typedef struct {
    uint8_t running_status;         /**< Running status byte */
    uint8_t expected_bytes;         /**< Expected data bytes for current message */
    uint8_t received_bytes;         /**< Received data bytes */
    uint8_t message[MIDI_BLE_MAX_MESSAGE_SIZE];  /**< Message buffer */
    uint16_t message_len;           /**< Current message length */
    bool in_sysex;                  /**< Currently receiving SysEx */
} midi_parser_state_t;

/**
 * @brief BLE MIDI service context
 */
typedef struct {
    /* State */
    volatile bool initialized;
    volatile midi_ble_state_t state;

    /* Configuration */
    midi_ble_config_t config;

    /* Connection info */
    uint16_t conn_handle;
    uint16_t mtu;

    /* GATT handles */
    uint16_t service_handle;
    uint16_t char_handle;
    uint16_t cccd_handle;

    /* Client subscription state */
    bool notifications_enabled;

    /* Parser state */
    midi_parser_state_t parser;

    /* TX buffer for batching */
    uint8_t tx_buffer[MIDI_BLE_MAX_PACKET_SIZE];
    uint16_t tx_len;
    uint16_t tx_timestamp;

    /* Callback */
    midi_ble_callback_t event_callback;
    void *callback_user_data;

    /* Statistics */
    midi_ble_stats_t stats;

    /* Synchronization */
    /* SemaphoreHandle_t tx_mutex; */

} midi_ble_ctx_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Global BLE MIDI context */
static midi_ble_ctx_t g_midi_ble_ctx;

/** BLE MIDI Service UUID (little-endian for BT stack) */
static const uint8_t midi_service_uuid[16] = MIDI_BLE_SERVICE_UUID;

/** BLE MIDI Characteristic UUID (little-endian for BT stack) */
static const uint8_t midi_char_uuid[16] = MIDI_BLE_CHAR_UUID;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

/* Event notification */
static void notify_event(midi_ble_event_type_t type);
static void notify_message(const midi_message_t *message);
static void notify_error(int error_code);

/* BLE-MIDI packet encoding/decoding */
static uint16_t get_ble_midi_timestamp(void);
static int encode_ble_midi_packet(const uint8_t *midi_data, uint16_t midi_len,
                                  uint16_t timestamp, uint8_t *packet, uint16_t max_len);
static int decode_ble_midi_packet(const uint8_t *packet, uint16_t packet_len);

/* MIDI message parsing */
static void parser_reset(midi_parser_state_t *parser);
static int parser_process_byte(midi_parser_state_t *parser, uint8_t byte,
                               uint16_t timestamp);
static uint8_t get_midi_message_length(uint8_t status);

/* GATT callbacks */
static void gatt_connection_callback(uint16_t conn_handle, bool connected);
static void gatt_write_callback(uint16_t conn_handle, const uint8_t *data,
                                uint16_t length);
static void gatt_cccd_callback(uint16_t conn_handle, bool notifications_enabled);

/* BLE stack integration */
static int ble_stack_init(void);
static int gatt_register_service(void);
static int start_advertising(void);
static int stop_advertising(void);
static int send_notification(const uint8_t *data, uint16_t length);

/*******************************************************************************
 * Timestamp Functions
 ******************************************************************************/

/**
 * @brief Get current BLE-MIDI timestamp (13-bit millisecond counter)
 */
static uint16_t get_ble_midi_timestamp(void)
{
    /*
     * TODO: Get millisecond timestamp from system
     *
     * uint32_t ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
     * return (uint16_t)(ms & MIDI_BLE_TIMESTAMP_MASK);
     */

    /* Placeholder - return 0 for now */
    static uint16_t fake_timestamp = 0;
    fake_timestamp = (fake_timestamp + 1) & MIDI_BLE_TIMESTAMP_MASK;
    return fake_timestamp;
}

/*******************************************************************************
 * BLE-MIDI Packet Encoding
 ******************************************************************************/

/**
 * @brief Encode MIDI data into BLE-MIDI packet format
 *
 * BLE-MIDI packet structure:
 * [Header] [Timestamp] [MIDI Status] [MIDI Data...] [Timestamp] [MIDI Status] [MIDI Data...]
 *
 * Header byte:     1ttttttt (high 6 bits of timestamp, MSB=1)
 * Timestamp byte:  1ttttttt (low 7 bits of timestamp, MSB=1)
 *
 * @param midi_data Raw MIDI data
 * @param midi_len Length of MIDI data
 * @param timestamp BLE-MIDI timestamp
 * @param packet Output packet buffer
 * @param max_len Maximum packet length
 * @return Encoded packet length, or negative error code
 */
static int encode_ble_midi_packet(const uint8_t *midi_data, uint16_t midi_len,
                                  uint16_t timestamp, uint8_t *packet, uint16_t max_len)
{
    uint16_t pos = 0;

    if (midi_data == NULL || packet == NULL || midi_len == 0) {
        return -1;
    }

    if (max_len < midi_len + 2) {  /* Need at least header + timestamp + data */
        return -2;
    }

    /* Header byte: MSB=1, bits 6-0 = timestamp bits 12-6 */
    packet[pos++] = MIDI_BLE_TIMESTAMP_MSB | ((timestamp >> 7) & 0x3F);

    /* Timestamp byte: MSB=1, bits 6-0 = timestamp bits 6-0 */
    packet[pos++] = MIDI_BLE_TIMESTAMP_MSB | (timestamp & 0x7F);

    /* MIDI data */
    if (pos + midi_len > max_len) {
        return -3;
    }

    memcpy(&packet[pos], midi_data, midi_len);
    pos += midi_len;

    return (int)pos;
}

/*******************************************************************************
 * BLE-MIDI Packet Decoding
 ******************************************************************************/

/**
 * @brief Decode BLE-MIDI packet and extract MIDI messages
 *
 * @param packet BLE-MIDI packet data
 * @param packet_len Packet length
 * @return 0 on success, negative error code on failure
 */
static int decode_ble_midi_packet(const uint8_t *packet, uint16_t packet_len)
{
    uint16_t pos = 0;
    uint16_t timestamp = 0;
    uint8_t header;
    uint8_t byte;

    if (packet == NULL || packet_len < MIDI_BLE_MIN_PACKET_SIZE) {
        return -1;
    }

    /* Parse header byte */
    header = packet[pos++];
    if ((header & MIDI_BLE_TIMESTAMP_MSB) == 0) {
        return -2;  /* Invalid header */
    }

    timestamp = (header & 0x3F) << 7;

    /* Parse remaining bytes */
    while (pos < packet_len) {
        byte = packet[pos];

        if (byte & MIDI_BLE_TIMESTAMP_MSB) {
            /* This is a timestamp byte */
            timestamp = (timestamp & 0x1F80) | (byte & 0x7F);
            pos++;
        } else {
            /* This is MIDI data */
            int result = parser_process_byte(&g_midi_ble_ctx.parser, byte, timestamp);
            if (result < 0) {
                g_midi_ble_ctx.stats.rx_errors++;
            }
            pos++;
        }
    }

    return 0;
}

/*******************************************************************************
 * MIDI Message Parser
 ******************************************************************************/

/**
 * @brief Reset MIDI parser state
 */
static void parser_reset(midi_parser_state_t *parser)
{
    if (parser != NULL) {
        parser->running_status = 0;
        parser->expected_bytes = 0;
        parser->received_bytes = 0;
        parser->message_len = 0;
        parser->in_sysex = false;
    }
}

/**
 * @brief Get expected data bytes for a MIDI status byte
 */
static uint8_t get_midi_message_length(uint8_t status)
{
    if (status < 0x80) {
        return 0;  /* Not a status byte */
    }

    switch (status & 0xF0) {
        case MIDI_MSG_NOTE_OFF:
        case MIDI_MSG_NOTE_ON:
        case MIDI_MSG_POLY_AFTERTOUCH:
        case MIDI_MSG_CONTROL_CHANGE:
        case MIDI_MSG_PITCH_BEND:
            return 2;  /* 2 data bytes */

        case MIDI_MSG_PROGRAM_CHANGE:
        case MIDI_MSG_CHANNEL_AFTERTOUCH:
            return 1;  /* 1 data byte */

        default:
            break;
    }

    /* System messages */
    switch (status) {
        case MIDI_MSG_SYSEX_START:
            return 0;  /* Variable length */
        case MIDI_MSG_TIME_CODE:
        case MIDI_MSG_SONG_SELECT:
            return 1;
        case MIDI_MSG_SONG_POSITION:
            return 2;
        case MIDI_MSG_TUNE_REQUEST:
        case MIDI_MSG_SYSEX_END:
        case MIDI_MSG_TIMING_CLOCK:
        case MIDI_MSG_START:
        case MIDI_MSG_CONTINUE:
        case MIDI_MSG_STOP:
        case MIDI_MSG_ACTIVE_SENSING:
        case MIDI_MSG_SYSTEM_RESET:
            return 0;  /* No data bytes */
        default:
            return 0;
    }
}

/**
 * @brief Process a single MIDI byte through the parser
 *
 * @param parser Parser state
 * @param byte MIDI byte to process
 * @param timestamp BLE-MIDI timestamp
 * @return 0 on success, 1 if message complete, negative on error
 */
static int parser_process_byte(midi_parser_state_t *parser, uint8_t byte,
                               uint16_t timestamp)
{
    midi_message_t message;

    if (parser == NULL) {
        return -1;
    }

    /* Check if this is a status byte */
    if (byte & MIDI_STATUS_MASK) {
        /* Handle system real-time messages (can interrupt anything) */
        if (byte >= MIDI_MSG_TIMING_CLOCK) {
            message.data[0] = byte;
            message.length = 1;
            message.timestamp = timestamp;
            notify_message(&message);
            g_midi_ble_ctx.stats.messages_received++;
            return 1;
        }

        /* Handle SysEx start/end */
        if (byte == MIDI_MSG_SYSEX_START) {
            parser->in_sysex = true;
            parser->message[0] = byte;
            parser->message_len = 1;
            return 0;
        }

        if (byte == MIDI_MSG_SYSEX_END) {
            if (parser->in_sysex) {
                parser->message[parser->message_len++] = byte;

                /* Complete SysEx message */
                message.length = parser->message_len;
                memcpy(message.data, parser->message, parser->message_len);
                message.timestamp = timestamp;
                notify_message(&message);
                g_midi_ble_ctx.stats.messages_received++;

                parser->in_sysex = false;
                parser->message_len = 0;
                return 1;
            }
            return 0;
        }

        /* New status byte - cancel any SysEx in progress */
        parser->in_sysex = false;

        /* Store new running status */
        parser->running_status = byte;
        parser->expected_bytes = get_midi_message_length(byte);
        parser->received_bytes = 0;
        parser->message[0] = byte;
        parser->message_len = 1;

        /* Handle messages with no data bytes */
        if (parser->expected_bytes == 0) {
            message.data[0] = byte;
            message.length = 1;
            message.timestamp = timestamp;
            notify_message(&message);
            g_midi_ble_ctx.stats.messages_received++;
            return 1;
        }

        return 0;
    }

    /* This is a data byte */

    /* Handle SysEx data */
    if (parser->in_sysex) {
        if (parser->message_len < MIDI_BLE_MAX_MESSAGE_SIZE) {
            parser->message[parser->message_len++] = byte;
        }
        return 0;
    }

    /* Use running status if no status byte preceded this data */
    if (parser->expected_bytes == 0) {
        if (parser->running_status == 0) {
            return -2;  /* No running status */
        }
        /* Reuse running status */
        parser->expected_bytes = get_midi_message_length(parser->running_status);
        parser->received_bytes = 0;
        parser->message[0] = parser->running_status;
        parser->message_len = 1;
    }

    /* Add data byte to message */
    parser->message[parser->message_len++] = byte;
    parser->received_bytes++;

    /* Check if message is complete */
    if (parser->received_bytes >= parser->expected_bytes) {
        message.length = parser->message_len;
        memcpy(message.data, parser->message, parser->message_len);
        message.timestamp = timestamp;
        notify_message(&message);
        g_midi_ble_ctx.stats.messages_received++;

        /* Reset for next message (keep running status) */
        parser->expected_bytes = 0;
        parser->received_bytes = 0;
        parser->message_len = 0;

        return 1;
    }

    return 0;
}

/*******************************************************************************
 * Event Notification
 ******************************************************************************/

static void notify_event(midi_ble_event_type_t type)
{
    midi_ble_event_t event;

    if (g_midi_ble_ctx.event_callback == NULL) {
        return;
    }

    event.type = type;
    event.conn_handle = g_midi_ble_ctx.conn_handle;

    g_midi_ble_ctx.event_callback(&event, g_midi_ble_ctx.callback_user_data);
}

static void notify_message(const midi_message_t *message)
{
    midi_ble_event_t event;

    if (g_midi_ble_ctx.event_callback == NULL || message == NULL) {
        return;
    }

    event.type = MIDI_BLE_EVENT_MESSAGE_RX;
    event.conn_handle = g_midi_ble_ctx.conn_handle;
    event.data.message = *message;

    g_midi_ble_ctx.event_callback(&event, g_midi_ble_ctx.callback_user_data);
}

static void notify_error(int error_code)
{
    midi_ble_event_t event;

    if (g_midi_ble_ctx.event_callback == NULL) {
        return;
    }

    event.type = MIDI_BLE_EVENT_ERROR;
    event.conn_handle = g_midi_ble_ctx.conn_handle;
    event.data.error_code = error_code;

    g_midi_ble_ctx.event_callback(&event, g_midi_ble_ctx.callback_user_data);
}

/*******************************************************************************
 * GATT Callbacks
 ******************************************************************************/

/**
 * @brief Handle GATT connection events
 */
static void gatt_connection_callback(uint16_t conn_handle, bool connected)
{
    if (connected) {
        g_midi_ble_ctx.conn_handle = conn_handle;
        g_midi_ble_ctx.state = MIDI_BLE_STATE_CONNECTED;
        g_midi_ble_ctx.notifications_enabled = false;
        parser_reset(&g_midi_ble_ctx.parser);

        notify_event(MIDI_BLE_EVENT_CONNECTED);
    } else {
        g_midi_ble_ctx.state = MIDI_BLE_STATE_DISCONNECTED;
        g_midi_ble_ctx.conn_handle = MIDI_BLE_INVALID_CONN_HANDLE;
        g_midi_ble_ctx.notifications_enabled = false;

        notify_event(MIDI_BLE_EVENT_DISCONNECTED);

        /* Restart advertising if configured */
        if (g_midi_ble_ctx.config.auto_advertise) {
            start_advertising();
        }
    }
}

/**
 * @brief Handle GATT write events (MIDI data from client)
 */
static void gatt_write_callback(uint16_t conn_handle, const uint8_t *data,
                                uint16_t length)
{
    (void)conn_handle;

    if (data == NULL || length == 0) {
        return;
    }

    g_midi_ble_ctx.stats.bytes_received += length;

    /* Decode BLE-MIDI packet */
    int result = decode_ble_midi_packet(data, length);
    if (result < 0) {
        g_midi_ble_ctx.stats.rx_errors++;
        notify_error(result);
    }
}

/**
 * @brief Handle CCCD (Client Characteristic Configuration Descriptor) changes
 */
static void gatt_cccd_callback(uint16_t conn_handle, bool notifications_enabled)
{
    (void)conn_handle;

    g_midi_ble_ctx.notifications_enabled = notifications_enabled;

    if (notifications_enabled) {
        g_midi_ble_ctx.state = MIDI_BLE_STATE_SUBSCRIBED;
        notify_event(MIDI_BLE_EVENT_SUBSCRIBED);
    } else {
        g_midi_ble_ctx.state = MIDI_BLE_STATE_CONNECTED;
        notify_event(MIDI_BLE_EVENT_UNSUBSCRIBED);
    }
}

/*******************************************************************************
 * BLE Stack Integration
 ******************************************************************************/

/**
 * @brief Initialize BLE stack
 *
 * TODO: Implement using Infineon BTSTACK
 */
static int ble_stack_init(void)
{
    /*
     * TODO: Initialize Bluetooth stack
     *
     * wiced_bt_stack_init(bt_management_callback, &wiced_bt_cfg_settings);
     *
     * The management callback should handle:
     * - BTM_ENABLED_EVT: Stack ready, register GATT service
     * - BTM_PAIRING_COMPLETE_EVT: Pairing status
     * - BTM_BLE_ADVERT_STATE_CHANGED_EVT: Advertising state changes
     * - BTM_BLE_CONNECTION_PARAM_UPDATE: Connection parameter updates
     */

    return 0;
}

/**
 * @brief Register MIDI GATT service
 *
 * TODO: Implement using Infineon BTSTACK GATT API
 */
static int gatt_register_service(void)
{
    /*
     * TODO: Register GATT database with MIDI service
     *
     * The GATT database should include:
     *
     * Primary Service: MIDI Service (UUID: 03B80E5A-...)
     *   ├── Characteristic: MIDI I/O (UUID: 7772E5DB-...)
     *   │   ├── Properties: Read, Write Without Response, Notify
     *   │   ├── Value: Variable length (up to MTU-3)
     *   │   └── CCCD: Client Characteristic Configuration
     *
     * Example using WICED GATT:
     *
     * const uint8_t gatt_database[] = {
     *     // MIDI Service Declaration
     *     HDLC_MIDI_SERVICE,
     *     GATT_UUID_PRI_SERVICE,
     *     UUID_128BIT,
     *     midi_service_uuid[0], ..., midi_service_uuid[15],
     *
     *     // MIDI I/O Characteristic Declaration
     *     HDLC_MIDI_CHAR,
     *     GATT_UUID_CHAR_DECLARE,
     *     GATT_PROP_READ | GATT_PROP_WRITE_NO_RESPONSE | GATT_PROP_NOTIFY,
     *
     *     // MIDI I/O Characteristic Value
     *     HDLC_MIDI_CHAR_VALUE,
     *     UUID_128BIT,
     *     midi_char_uuid[0], ..., midi_char_uuid[15],
     *
     *     // CCCD
     *     HDLC_MIDI_CHAR_CCCD,
     *     GATT_UUID_CHAR_CLIENT_CONFIG,
     * };
     *
     * wiced_bt_gatt_register(gatt_callback);
     * wiced_bt_gatt_db_init(gatt_database, sizeof(gatt_database));
     */

    return 0;
}

/**
 * @brief Start BLE advertising
 */
static int start_advertising(void)
{
    /*
     * TODO: Start BLE advertising with MIDI service UUID
     *
     * wiced_bt_ble_advert_elem_t adv_elem[3];
     * uint8_t adv_flags = BTM_BLE_GENERAL_DISCOVERABLE_FLAG | BTM_BLE_BREDR_NOT_SUPPORTED;
     *
     * // Flags
     * adv_elem[0].advert_type = BTM_BLE_ADVERT_TYPE_FLAG;
     * adv_elem[0].len = 1;
     * adv_elem[0].p_data = &adv_flags;
     *
     * // Complete local name
     * adv_elem[1].advert_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE;
     * adv_elem[1].len = strlen(g_midi_ble_ctx.config.device_name);
     * adv_elem[1].p_data = (uint8_t*)g_midi_ble_ctx.config.device_name;
     *
     * // MIDI Service UUID
     * adv_elem[2].advert_type = BTM_BLE_ADVERT_TYPE_128SRV_COMPLETE;
     * adv_elem[2].len = 16;
     * adv_elem[2].p_data = midi_service_uuid;
     *
     * wiced_bt_ble_set_raw_advertisement_data(3, adv_elem);
     * wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
     */

    return 0;
}

/**
 * @brief Stop BLE advertising
 */
static int stop_advertising(void)
{
    /*
     * TODO: Stop BLE advertising
     *
     * wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF, 0, NULL);
     */

    return 0;
}

/**
 * @brief Send GATT notification
 */
static int send_notification(const uint8_t *data, uint16_t length)
{
    if (!g_midi_ble_ctx.notifications_enabled) {
        return -1;
    }

    if (data == NULL || length == 0) {
        return -2;
    }

    /*
     * TODO: Send GATT notification
     *
     * wiced_bt_gatt_send_notification(
     *     g_midi_ble_ctx.conn_handle,
     *     HDLC_MIDI_CHAR_VALUE,
     *     length,
     *     (uint8_t*)data
     * );
     */

    g_midi_ble_ctx.stats.bytes_sent += length;

    return 0;
}

/*******************************************************************************
 * Public API Implementation
 ******************************************************************************/

int midi_ble_init(const midi_ble_config_t *config)
{
    int result;

    if (g_midi_ble_ctx.initialized) {
        return -1;  /* Already initialized */
    }

    /* Clear context */
    memset(&g_midi_ble_ctx, 0, sizeof(g_midi_ble_ctx));

    /* Apply configuration */
    if (config != NULL) {
        g_midi_ble_ctx.config = *config;
    } else {
        midi_ble_config_t default_config = MIDI_BLE_CONFIG_DEFAULT;
        g_midi_ble_ctx.config = default_config;
    }

    /* Initialize state */
    g_midi_ble_ctx.state = MIDI_BLE_STATE_DISCONNECTED;
    g_midi_ble_ctx.conn_handle = MIDI_BLE_INVALID_CONN_HANDLE;
    g_midi_ble_ctx.mtu = g_midi_ble_ctx.config.mtu;

    /* Initialize parser */
    parser_reset(&g_midi_ble_ctx.parser);

    /*
     * TODO: Create FreeRTOS synchronization
     *
     * g_midi_ble_ctx.tx_mutex = xSemaphoreCreateMutex();
     * if (g_midi_ble_ctx.tx_mutex == NULL) {
     *     return -2;
     * }
     */

    /* Initialize BLE stack */
    result = ble_stack_init();
    if (result != 0) {
        return -3;
    }

    /* Register GATT service */
    result = gatt_register_service();
    if (result != 0) {
        return -4;
    }

    g_midi_ble_ctx.initialized = true;

    /* Start advertising if configured */
    if (g_midi_ble_ctx.config.auto_advertise) {
        start_advertising();
    }

    return 0;
}

void midi_ble_deinit(void)
{
    if (!g_midi_ble_ctx.initialized) {
        return;
    }

    /* Stop advertising */
    stop_advertising();

    /* Disconnect if connected */
    if (g_midi_ble_ctx.state != MIDI_BLE_STATE_DISCONNECTED) {
        midi_ble_disconnect();
    }

    /*
     * TODO: Delete FreeRTOS synchronization
     *
     * if (g_midi_ble_ctx.tx_mutex != NULL) {
     *     vSemaphoreDelete(g_midi_ble_ctx.tx_mutex);
     * }
     */

    g_midi_ble_ctx.initialized = false;
}

void midi_ble_register_callback(midi_ble_callback_t callback, void *user_data)
{
    g_midi_ble_ctx.event_callback = callback;
    g_midi_ble_ctx.callback_user_data = user_data;
}

int midi_ble_start_advertising(void)
{
    if (!g_midi_ble_ctx.initialized) {
        return -1;
    }

    return start_advertising();
}

int midi_ble_stop_advertising(void)
{
    if (!g_midi_ble_ctx.initialized) {
        return -1;
    }

    return stop_advertising();
}

int midi_ble_send(const midi_message_t *message)
{
    if (!g_midi_ble_ctx.initialized || message == NULL) {
        return -1;
    }

    return midi_ble_send_raw(message->data, message->length);
}

int midi_ble_send_raw(const uint8_t *data, uint16_t length)
{
    uint8_t packet[MIDI_BLE_MAX_PACKET_SIZE];
    int packet_len;
    uint16_t timestamp;
    int result;

    if (!g_midi_ble_ctx.initialized) {
        return -1;
    }

    if (data == NULL || length == 0) {
        return -2;
    }

    if (g_midi_ble_ctx.state != MIDI_BLE_STATE_SUBSCRIBED) {
        return -3;  /* No subscribed client */
    }

    /* Get timestamp */
    timestamp = get_ble_midi_timestamp();

    /* Encode BLE-MIDI packet */
    packet_len = encode_ble_midi_packet(data, length, timestamp,
                                        packet, sizeof(packet));
    if (packet_len < 0) {
        g_midi_ble_ctx.stats.tx_errors++;
        return -4;
    }

    /* Send notification */
    result = send_notification(packet, packet_len);
    if (result != 0) {
        g_midi_ble_ctx.stats.tx_errors++;
        return -5;
    }

    g_midi_ble_ctx.stats.messages_sent++;

    return 0;
}

int midi_ble_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    uint8_t data[3];

    data[0] = MIDI_MSG_NOTE_ON | (channel & 0x0F);
    data[1] = note & 0x7F;
    data[2] = velocity & 0x7F;

    return midi_ble_send_raw(data, 3);
}

int midi_ble_send_note_off(uint8_t channel, uint8_t note, uint8_t velocity)
{
    uint8_t data[3];

    data[0] = MIDI_MSG_NOTE_OFF | (channel & 0x0F);
    data[1] = note & 0x7F;
    data[2] = velocity & 0x7F;

    return midi_ble_send_raw(data, 3);
}

int midi_ble_send_control_change(uint8_t channel, uint8_t controller, uint8_t value)
{
    uint8_t data[3];

    data[0] = MIDI_MSG_CONTROL_CHANGE | (channel & 0x0F);
    data[1] = controller & 0x7F;
    data[2] = value & 0x7F;

    return midi_ble_send_raw(data, 3);
}

int midi_ble_send_program_change(uint8_t channel, uint8_t program)
{
    uint8_t data[2];

    data[0] = MIDI_MSG_PROGRAM_CHANGE | (channel & 0x0F);
    data[1] = program & 0x7F;

    return midi_ble_send_raw(data, 2);
}

int midi_ble_send_pitch_bend(uint8_t channel, uint16_t value)
{
    uint8_t data[3];

    /* Pitch bend uses 14-bit value (0-16383, center at 8192) */
    data[0] = MIDI_MSG_PITCH_BEND | (channel & 0x0F);
    data[1] = value & 0x7F;           /* LSB (bits 0-6) */
    data[2] = (value >> 7) & 0x7F;    /* MSB (bits 7-13) */

    return midi_ble_send_raw(data, 3);
}

midi_ble_state_t midi_ble_get_state(void)
{
    return g_midi_ble_ctx.state;
}

bool midi_ble_is_connected(void)
{
    return (g_midi_ble_ctx.state == MIDI_BLE_STATE_CONNECTED ||
            g_midi_ble_ctx.state == MIDI_BLE_STATE_SUBSCRIBED);
}

bool midi_ble_is_subscribed(void)
{
    return (g_midi_ble_ctx.state == MIDI_BLE_STATE_SUBSCRIBED);
}

uint16_t midi_ble_get_conn_handle(void)
{
    return g_midi_ble_ctx.conn_handle;
}

void midi_ble_get_stats(midi_ble_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_midi_ble_ctx.stats;
    }
}

void midi_ble_reset_stats(void)
{
    memset(&g_midi_ble_ctx.stats, 0, sizeof(g_midi_ble_ctx.stats));
}

int midi_ble_disconnect(void)
{
    if (!g_midi_ble_ctx.initialized) {
        return -1;
    }

    if (g_midi_ble_ctx.state == MIDI_BLE_STATE_DISCONNECTED) {
        return 0;  /* Already disconnected */
    }

    /*
     * TODO: Disconnect BLE connection
     *
     * wiced_bt_gatt_disconnect(g_midi_ble_ctx.conn_handle);
     */

    return 0;
}

/*******************************************************************************
 * Additional MIDI Helper Functions
 ******************************************************************************/

/**
 * @brief Send a SysEx message
 *
 * @param manufacturer_id Manufacturer ID (1 or 3 bytes)
 * @param data SysEx data (without F0/F7 framing)
 * @param length Data length
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_sysex(const uint8_t *manufacturer_id, uint8_t mfr_len,
                        const uint8_t *data, uint16_t length)
{
    uint8_t sysex[MIDI_BLE_MAX_MESSAGE_SIZE];
    uint16_t pos = 0;

    if (manufacturer_id == NULL || mfr_len == 0 || mfr_len > 3) {
        return -1;
    }

    if (1 + mfr_len + length + 1 > MIDI_BLE_MAX_MESSAGE_SIZE) {
        return -2;  /* Too large */
    }

    /* Build SysEx message */
    sysex[pos++] = MIDI_MSG_SYSEX_START;

    /* Manufacturer ID */
    memcpy(&sysex[pos], manufacturer_id, mfr_len);
    pos += mfr_len;

    /* Data */
    if (data != NULL && length > 0) {
        memcpy(&sysex[pos], data, length);
        pos += length;
    }

    sysex[pos++] = MIDI_MSG_SYSEX_END;

    return midi_ble_send_raw(sysex, pos);
}

/**
 * @brief Send All Notes Off message
 *
 * @param channel MIDI channel (0-15)
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_all_notes_off(uint8_t channel)
{
    return midi_ble_send_control_change(channel, 123, 0);
}

/**
 * @brief Send All Sound Off message
 *
 * @param channel MIDI channel (0-15)
 * @return 0 on success, negative error code on failure
 */
int midi_ble_send_all_sound_off(uint8_t channel)
{
    return midi_ble_send_control_change(channel, 120, 0);
}
