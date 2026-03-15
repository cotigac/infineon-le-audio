/**
 * @file midi_usb.c
 * @brief USB MIDI Class Device Implementation
 *
 * This module implements the USB MIDI 1.0 class specification.
 * It handles USB enumeration, MIDI event packet formatting,
 * and bidirectional MIDI data transfer.
 *
 * USB MIDI Event Packet (4 bytes):
 * - Byte 0: Cable Number (4 bits) | Code Index Number (4 bits)
 * - Byte 1: MIDI_0 (status/data)
 * - Byte 2: MIDI_1 (data/0x00)
 * - Byte 3: MIDI_2 (data/0x00)
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#include "midi_usb.h"

#include <stdlib.h>
#include <string.h>

/* TODO: Include Infineon USB Device middleware headers */
/* #include "cy_usb_dev.h" */
/* #include "cy_usb_dev_audio.h" */
/* #include "cycfg_usbdev.h" */

/* TODO: Include FreeRTOS headers */
/* #include "FreeRTOS.h" */
/* #include "queue.h" */
/* #include "semphr.h" */

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** USB endpoint addresses */
#define MIDI_USB_EP_IN              0x81    /**< IN endpoint (device to host) */
#define MIDI_USB_EP_OUT             0x01    /**< OUT endpoint (host to device) */

/** USB endpoint buffer sizes */
#define MIDI_USB_EP_BUFFER_SIZE     64

/** Maximum events per USB packet (64 bytes / 4 bytes per event) */
#define MIDI_USB_MAX_EVENTS_PER_PKT 16

/** TX flush timeout (ms) */
#define MIDI_USB_TX_FLUSH_TIMEOUT   2

/** MIDI status byte range checks */
#define MIDI_IS_STATUS(b)           ((b) & 0x80)
#define MIDI_IS_REALTIME(b)         ((b) >= 0xF8)
#define MIDI_IS_SYSEX_START(b)      ((b) == 0xF0)
#define MIDI_IS_SYSEX_END(b)        ((b) == 0xF7)

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief SysEx parser state
 */
typedef struct {
    uint8_t buffer[3];          /**< SysEx fragment buffer */
    uint8_t count;              /**< Bytes in buffer */
    bool active;                /**< SysEx in progress */
} sysex_state_t;

/**
 * @brief TX queue entry
 */
typedef struct {
    midi_usb_event_t events[MIDI_USB_MAX_EVENTS_PER_PKT];
    uint8_t count;
} tx_packet_t;

/**
 * @brief USB MIDI context
 */
typedef struct {
    /* State */
    volatile bool initialized;
    volatile midi_usb_state_t state;

    /* Configuration */
    midi_usb_config_t config;

    /* Endpoint buffers */
    uint8_t ep_in_buffer[MIDI_USB_EP_BUFFER_SIZE];
    uint8_t ep_out_buffer[MIDI_USB_EP_BUFFER_SIZE];

    /* TX queue */
    midi_usb_event_t tx_queue[MIDI_USB_QUEUE_SIZE];
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;
    volatile uint16_t tx_count;
    volatile bool tx_busy;

    /* RX queue */
    midi_usb_event_t rx_queue[MIDI_USB_QUEUE_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    volatile uint16_t rx_count;

    /* SysEx state per cable */
    sysex_state_t sysex[MIDI_USB_MAX_CABLES];

    /* Running status per cable */
    uint8_t running_status[MIDI_USB_MAX_CABLES];

    /* Callback */
    midi_usb_callback_t event_callback;
    void *callback_user_data;

    /* Statistics */
    midi_usb_stats_t stats;

    /* Synchronization */
    /* SemaphoreHandle_t tx_mutex; */
    /* SemaphoreHandle_t rx_mutex; */

    /* USB device handle */
    /* cy_stc_usb_dev_context_t *usb_context; */

} midi_usb_ctx_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Global USB MIDI context */
static midi_usb_ctx_t g_midi_usb_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

/* Event notification */
static void notify_event(midi_usb_event_type_t type);
static void notify_message(const midi_usb_event_t *event);
static void notify_error(int error_code);

/* Queue operations */
static int tx_queue_push(const midi_usb_event_t *event);
static int tx_queue_pop(midi_usb_event_t *event);
static bool tx_queue_is_empty(void);
static bool tx_queue_is_full(void);

static int rx_queue_push(const midi_usb_event_t *event);
static int rx_queue_pop(midi_usb_event_t *event);

/* USB MIDI packet conversion */
static midi_usb_cin_t get_cin_for_status(uint8_t status);
static int make_event(uint8_t cable, uint8_t cin, uint8_t midi_0,
                      uint8_t midi_1, uint8_t midi_2, midi_usb_event_t *event);
static int parse_event(const midi_usb_event_t *event);

/* SysEx handling */
static int send_sysex_fragment(uint8_t cable, const uint8_t *data, uint8_t len, bool end);

/* USB callbacks */
static void usb_set_config_callback(void);
static void usb_reset_callback(void);
static void usb_suspend_callback(void);
static void usb_resume_callback(void);
static void usb_ep_in_callback(void);
static void usb_ep_out_callback(uint8_t *data, uint16_t length);

/* USB stack integration */
static int usb_device_init(void);
static int usb_start_rx(void);
static int usb_send_packet(const uint8_t *data, uint16_t length);

/*******************************************************************************
 * Queue Implementation
 ******************************************************************************/

static int tx_queue_push(const midi_usb_event_t *event)
{
    if (event == NULL || tx_queue_is_full()) {
        return -1;
    }

    g_midi_usb_ctx.tx_queue[g_midi_usb_ctx.tx_head] = *event;
    g_midi_usb_ctx.tx_head = (g_midi_usb_ctx.tx_head + 1) % MIDI_USB_QUEUE_SIZE;
    g_midi_usb_ctx.tx_count++;

    return 0;
}

static int tx_queue_pop(midi_usb_event_t *event)
{
    if (event == NULL || tx_queue_is_empty()) {
        return -1;
    }

    *event = g_midi_usb_ctx.tx_queue[g_midi_usb_ctx.tx_tail];
    g_midi_usb_ctx.tx_tail = (g_midi_usb_ctx.tx_tail + 1) % MIDI_USB_QUEUE_SIZE;
    g_midi_usb_ctx.tx_count--;

    return 0;
}

static bool tx_queue_is_empty(void)
{
    return g_midi_usb_ctx.tx_count == 0;
}

static bool tx_queue_is_full(void)
{
    return g_midi_usb_ctx.tx_count >= MIDI_USB_QUEUE_SIZE;
}

static int rx_queue_push(const midi_usb_event_t *event)
{
    if (event == NULL || g_midi_usb_ctx.rx_count >= MIDI_USB_QUEUE_SIZE) {
        g_midi_usb_ctx.stats.buffer_overflows++;
        return -1;
    }

    g_midi_usb_ctx.rx_queue[g_midi_usb_ctx.rx_head] = *event;
    g_midi_usb_ctx.rx_head = (g_midi_usb_ctx.rx_head + 1) % MIDI_USB_QUEUE_SIZE;
    g_midi_usb_ctx.rx_count++;

    return 0;
}

static int rx_queue_pop(midi_usb_event_t *event)
{
    if (event == NULL || g_midi_usb_ctx.rx_count == 0) {
        return -1;
    }

    *event = g_midi_usb_ctx.rx_queue[g_midi_usb_ctx.rx_tail];
    g_midi_usb_ctx.rx_tail = (g_midi_usb_ctx.rx_tail + 1) % MIDI_USB_QUEUE_SIZE;
    g_midi_usb_ctx.rx_count--;

    return 0;
}

/*******************************************************************************
 * USB MIDI Event Handling
 ******************************************************************************/

/**
 * @brief Get Code Index Number (CIN) for a MIDI status byte
 */
static midi_usb_cin_t get_cin_for_status(uint8_t status)
{
    if (!MIDI_IS_STATUS(status)) {
        return MIDI_CIN_MISC;
    }

    /* Channel voice messages */
    switch (status & 0xF0) {
        case 0x80: return MIDI_CIN_NOTE_OFF;
        case 0x90: return MIDI_CIN_NOTE_ON;
        case 0xA0: return MIDI_CIN_POLY_KEYPRESS;
        case 0xB0: return MIDI_CIN_CONTROL_CHANGE;
        case 0xC0: return MIDI_CIN_PROGRAM_CHANGE;
        case 0xD0: return MIDI_CIN_CHANNEL_PRESS;
        case 0xE0: return MIDI_CIN_PITCH_BEND;
    }

    /* System messages */
    switch (status) {
        case 0xF0: return MIDI_CIN_SYSEX_START;
        case 0xF1: return MIDI_CIN_SYSCOMMON_2;  /* MTC Quarter Frame */
        case 0xF2: return MIDI_CIN_SYSCOMMON_3;  /* Song Position */
        case 0xF3: return MIDI_CIN_SYSCOMMON_2;  /* Song Select */
        case 0xF6: return MIDI_CIN_SYSCOMMON_1;  /* Tune Request */
        case 0xF7: return MIDI_CIN_SYSCOMMON_1;  /* End of SysEx */
        case 0xF8: /* Fall through - Real-time messages */
        case 0xF9:
        case 0xFA:
        case 0xFB:
        case 0xFC:
        case 0xFD:
        case 0xFE:
        case 0xFF:
            return MIDI_CIN_SINGLE_BYTE;
    }

    return MIDI_CIN_MISC;
}

/**
 * @brief Create a USB MIDI event packet
 */
static int make_event(uint8_t cable, uint8_t cin, uint8_t midi_0,
                      uint8_t midi_1, uint8_t midi_2, midi_usb_event_t *event)
{
    if (event == NULL || cable >= g_midi_usb_ctx.config.num_cables) {
        return -1;
    }

    event->cable_cin = ((cable & 0x0F) << 4) | (cin & 0x0F);
    event->midi_0 = midi_0;
    event->midi_1 = midi_1;
    event->midi_2 = midi_2;

    return 0;
}

/**
 * @brief Parse a received USB MIDI event
 */
static int parse_event(const midi_usb_event_t *event)
{
    uint8_t cable;
    uint8_t cin;

    if (event == NULL) {
        return -1;
    }

    cable = (event->cable_cin >> 4) & 0x0F;
    cin = event->cable_cin & 0x0F;

    /* Validate cable number */
    if (cable >= g_midi_usb_ctx.config.num_cables) {
        g_midi_usb_ctx.stats.rx_errors++;
        return -2;
    }

    /* Store running status for channel messages */
    if (cin >= MIDI_CIN_NOTE_OFF && cin <= MIDI_CIN_PITCH_BEND) {
        g_midi_usb_ctx.running_status[cable] = event->midi_0;
    }

    /* Notify application */
    notify_message(event);
    g_midi_usb_ctx.stats.messages_received++;

    return 0;
}

/*******************************************************************************
 * SysEx Handling
 ******************************************************************************/

/**
 * @brief Send a SysEx fragment (3 bytes max per event)
 */
static int send_sysex_fragment(uint8_t cable, const uint8_t *data, uint8_t len, bool end)
{
    midi_usb_event_t event;
    uint8_t cin;

    if (data == NULL || len == 0 || len > 3) {
        return -1;
    }

    /* Determine CIN based on fragment type and length */
    if (end) {
        switch (len) {
            case 1: cin = MIDI_CIN_SYSCOMMON_1; break;  /* SysEx end with 1 byte (F7) */
            case 2: cin = MIDI_CIN_SYSEX_END_2; break;  /* SysEx end with 2 bytes */
            case 3: cin = MIDI_CIN_SYSEX_END_3; break;  /* SysEx end with 3 bytes */
            default: return -2;
        }
    } else {
        cin = MIDI_CIN_SYSEX_START;  /* SysEx start or continue (3 bytes) */
    }

    /* Build event */
    event.cable_cin = ((cable & 0x0F) << 4) | (cin & 0x0F);
    event.midi_0 = (len > 0) ? data[0] : 0;
    event.midi_1 = (len > 1) ? data[1] : 0;
    event.midi_2 = (len > 2) ? data[2] : 0;

    return tx_queue_push(&event);
}

/*******************************************************************************
 * Event Notification
 ******************************************************************************/

static void notify_event(midi_usb_event_type_t type)
{
    midi_usb_callback_event_t event;

    if (g_midi_usb_ctx.event_callback == NULL) {
        return;
    }

    event.type = type;
    g_midi_usb_ctx.event_callback(&event, g_midi_usb_ctx.callback_user_data);
}

static void notify_message(const midi_usb_event_t *midi_event)
{
    midi_usb_callback_event_t event;

    if (g_midi_usb_ctx.event_callback == NULL || midi_event == NULL) {
        return;
    }

    event.type = MIDI_USB_EVENT_MESSAGE_RX;
    event.data.event = *midi_event;

    g_midi_usb_ctx.event_callback(&event, g_midi_usb_ctx.callback_user_data);
}

static void notify_error(int error_code)
{
    midi_usb_callback_event_t event;

    if (g_midi_usb_ctx.event_callback == NULL) {
        return;
    }

    event.type = MIDI_USB_EVENT_ERROR;
    event.data.error_code = error_code;

    g_midi_usb_ctx.event_callback(&event, g_midi_usb_ctx.callback_user_data);
}

/*******************************************************************************
 * USB Callbacks
 ******************************************************************************/

/**
 * @brief Called when USB device is configured
 */
static void usb_set_config_callback(void)
{
    g_midi_usb_ctx.state = MIDI_USB_STATE_CONFIGURED;

    /* Start receiving */
    usb_start_rx();

    notify_event(MIDI_USB_EVENT_CONNECTED);
}

/**
 * @brief Called when USB is reset
 */
static void usb_reset_callback(void)
{
    g_midi_usb_ctx.state = MIDI_USB_STATE_ATTACHED;

    /* Clear queues */
    g_midi_usb_ctx.tx_head = 0;
    g_midi_usb_ctx.tx_tail = 0;
    g_midi_usb_ctx.tx_count = 0;
    g_midi_usb_ctx.tx_busy = false;

    g_midi_usb_ctx.rx_head = 0;
    g_midi_usb_ctx.rx_tail = 0;
    g_midi_usb_ctx.rx_count = 0;

    /* Clear SysEx state */
    for (int i = 0; i < MIDI_USB_MAX_CABLES; i++) {
        g_midi_usb_ctx.sysex[i].count = 0;
        g_midi_usb_ctx.sysex[i].active = false;
        g_midi_usb_ctx.running_status[i] = 0;
    }

    notify_event(MIDI_USB_EVENT_DISCONNECTED);
}

/**
 * @brief Called when USB is suspended
 */
static void usb_suspend_callback(void)
{
    g_midi_usb_ctx.state = MIDI_USB_STATE_SUSPENDED;
    notify_event(MIDI_USB_EVENT_SUSPENDED);
}

/**
 * @brief Called when USB is resumed
 */
static void usb_resume_callback(void)
{
    g_midi_usb_ctx.state = MIDI_USB_STATE_CONFIGURED;
    notify_event(MIDI_USB_EVENT_RESUMED);
}

/**
 * @brief Called when IN endpoint transfer completes
 */
static void usb_ep_in_callback(void)
{
    g_midi_usb_ctx.tx_busy = false;
    g_midi_usb_ctx.stats.packets_sent++;

    notify_event(MIDI_USB_EVENT_TX_COMPLETE);
}

/**
 * @brief Called when OUT endpoint receives data
 */
static void usb_ep_out_callback(uint8_t *data, uint16_t length)
{
    uint16_t num_events;
    midi_usb_event_t *events;

    if (data == NULL || length == 0) {
        return;
    }

    g_midi_usb_ctx.stats.packets_received++;

    /* Each USB MIDI event is 4 bytes */
    num_events = length / MIDI_USB_EVENT_SIZE;
    events = (midi_usb_event_t *)data;

    for (uint16_t i = 0; i < num_events; i++) {
        /* Skip empty events */
        if (events[i].cable_cin == 0 && events[i].midi_0 == 0) {
            continue;
        }

        /* Parse and queue event */
        if (parse_event(&events[i]) == 0) {
            rx_queue_push(&events[i]);
        }
    }

    /* Continue receiving */
    usb_start_rx();
}

/*******************************************************************************
 * USB Stack Integration
 ******************************************************************************/

/**
 * @brief Initialize USB device
 *
 * TODO: Implement using Infineon USB Device middleware
 */
static int usb_device_init(void)
{
    /*
     * TODO: Initialize USB device with MIDI descriptors
     *
     * Example using Infineon USB Device middleware:
     *
     * // Initialize USB device
     * cy_stc_usb_dev_context_t usb_context;
     * Cy_USB_Dev_Init(USB_DEV_HW, &USB_DEV_config, &usb_context);
     *
     * // Register callbacks
     * Cy_USB_Dev_RegisterCallback(CY_USB_DEV_EVENT_SET_CONFIG, usb_set_config_callback);
     * Cy_USB_Dev_RegisterCallback(CY_USB_DEV_EVENT_BUS_RESET, usb_reset_callback);
     * Cy_USB_Dev_RegisterCallback(CY_USB_DEV_EVENT_SUSPEND, usb_suspend_callback);
     * Cy_USB_Dev_RegisterCallback(CY_USB_DEV_EVENT_RESUME, usb_resume_callback);
     *
     * // Register endpoint callbacks
     * Cy_USB_Dev_RegisterEndpointCallback(MIDI_USB_EP_IN, usb_ep_in_callback);
     * Cy_USB_Dev_RegisterEndpointCallback(MIDI_USB_EP_OUT, usb_ep_out_callback);
     *
     * // Connect to USB bus
     * Cy_USB_Dev_Connect(true, CY_USB_DEV_WAIT_FOREVER, &usb_context);
     *
     * USB Descriptor structure needed:
     *
     * - Device Descriptor (bDeviceClass = 0, defined at interface level)
     * - Configuration Descriptor
     *   - Interface 0: Audio Control (required but minimal)
     *   - Interface 1: MIDI Streaming
     *     - Standard MS Interface Descriptor
     *     - Class-Specific MS Interface Header
     *     - MIDI IN Jack (Embedded)
     *     - MIDI IN Jack (External)
     *     - MIDI OUT Jack (Embedded)
     *     - MIDI OUT Jack (External)
     *     - Bulk OUT Endpoint (for receiving from host)
     *     - Class-Specific MS Bulk OUT Endpoint
     *     - Bulk IN Endpoint (for sending to host)
     *     - Class-Specific MS Bulk IN Endpoint
     */

    return 0;
}

/**
 * @brief Start receiving on OUT endpoint
 */
static int usb_start_rx(void)
{
    /*
     * TODO: Start async receive on OUT endpoint
     *
     * Cy_USB_Dev_ReadOutEndpoint(MIDI_USB_EP_OUT,
     *                            g_midi_usb_ctx.ep_out_buffer,
     *                            MIDI_USB_EP_BUFFER_SIZE,
     *                            &usb_context);
     */

    return 0;
}

/**
 * @brief Send data on IN endpoint
 */
static int usb_send_packet(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0) {
        return -1;
    }

    if (g_midi_usb_ctx.state != MIDI_USB_STATE_CONFIGURED) {
        return -2;
    }

    /*
     * TODO: Send data on IN endpoint
     *
     * cy_en_usb_dev_status_t status;
     * status = Cy_USB_Dev_WriteInEndpoint(MIDI_USB_EP_IN,
     *                                     data,
     *                                     length,
     *                                     &usb_context);
     * return (status == CY_USB_DEV_SUCCESS) ? 0 : -3;
     */

    return 0;
}

/*******************************************************************************
 * Public API Implementation
 ******************************************************************************/

int midi_usb_init(const midi_usb_config_t *config)
{
    int result;

    if (g_midi_usb_ctx.initialized) {
        return -1;  /* Already initialized */
    }

    /* Clear context */
    memset(&g_midi_usb_ctx, 0, sizeof(g_midi_usb_ctx));

    /* Apply configuration */
    if (config != NULL) {
        g_midi_usb_ctx.config = *config;
    } else {
        midi_usb_config_t default_config = MIDI_USB_CONFIG_DEFAULT;
        g_midi_usb_ctx.config = default_config;
    }

    /* Validate configuration */
    if (g_midi_usb_ctx.config.num_cables == 0 ||
        g_midi_usb_ctx.config.num_cables > MIDI_USB_MAX_CABLES) {
        g_midi_usb_ctx.config.num_cables = 1;
    }

    /* Initialize state */
    g_midi_usb_ctx.state = MIDI_USB_STATE_DETACHED;

    /*
     * TODO: Create FreeRTOS synchronization
     *
     * g_midi_usb_ctx.tx_mutex = xSemaphoreCreateMutex();
     * g_midi_usb_ctx.rx_mutex = xSemaphoreCreateMutex();
     */

    /* Initialize USB device */
    result = usb_device_init();
    if (result != 0) {
        return -2;
    }

    g_midi_usb_ctx.initialized = true;

    return 0;
}

void midi_usb_deinit(void)
{
    if (!g_midi_usb_ctx.initialized) {
        return;
    }

    /*
     * TODO: Disconnect USB device
     *
     * Cy_USB_Dev_Connect(false, 0, &usb_context);
     * Cy_USB_Dev_DeInit(&usb_context);
     */

    /*
     * TODO: Delete FreeRTOS synchronization
     *
     * if (g_midi_usb_ctx.tx_mutex != NULL) {
     *     vSemaphoreDelete(g_midi_usb_ctx.tx_mutex);
     * }
     * if (g_midi_usb_ctx.rx_mutex != NULL) {
     *     vSemaphoreDelete(g_midi_usb_ctx.rx_mutex);
     * }
     */

    g_midi_usb_ctx.initialized = false;
}

void midi_usb_register_callback(midi_usb_callback_t callback, void *user_data)
{
    g_midi_usb_ctx.event_callback = callback;
    g_midi_usb_ctx.callback_user_data = user_data;
}

void midi_usb_process(void)
{
    midi_usb_event_t events[MIDI_USB_MAX_EVENTS_PER_PKT];
    uint8_t count = 0;

    if (!g_midi_usb_ctx.initialized) {
        return;
    }

    /* Process TX queue - batch events into USB packets */
    if (!g_midi_usb_ctx.tx_busy && !tx_queue_is_empty()) {
        /* Collect events for this packet */
        while (count < MIDI_USB_MAX_EVENTS_PER_PKT && !tx_queue_is_empty()) {
            if (tx_queue_pop(&events[count]) == 0) {
                count++;
            }
        }

        if (count > 0) {
            g_midi_usb_ctx.tx_busy = true;

            /* Send packet */
            int result = usb_send_packet((uint8_t *)events, count * MIDI_USB_EVENT_SIZE);
            if (result != 0) {
                g_midi_usb_ctx.tx_busy = false;
                g_midi_usb_ctx.stats.tx_errors++;
            }
        }
    }

    /*
     * TODO: Process USB device events
     *
     * Cy_USB_Dev_Process(&usb_context);
     */
}

int midi_usb_send_event(const midi_usb_event_t *event)
{
    if (!g_midi_usb_ctx.initialized || event == NULL) {
        return -1;
    }

    if (g_midi_usb_ctx.state != MIDI_USB_STATE_CONFIGURED) {
        return -2;
    }

    /* Queue the event */
    if (tx_queue_push(event) != 0) {
        g_midi_usb_ctx.stats.buffer_overflows++;
        return -3;
    }

    g_midi_usb_ctx.stats.messages_sent++;

    return 0;
}

int midi_usb_send_raw(uint8_t cable, const uint8_t *data, uint16_t length)
{
    midi_usb_event_t event;
    uint8_t status;
    uint8_t cin;
    uint16_t i = 0;

    if (!g_midi_usb_ctx.initialized || data == NULL || length == 0) {
        return -1;
    }

    if (cable >= g_midi_usb_ctx.config.num_cables) {
        return -2;
    }

    while (i < length) {
        status = data[i];

        /* Handle SysEx */
        if (MIDI_IS_SYSEX_START(status)) {
            return midi_usb_send_sysex(cable, &data[i], length - i);
        }

        /* Handle real-time messages */
        if (MIDI_IS_REALTIME(status)) {
            make_event(cable, MIDI_CIN_SINGLE_BYTE, status, 0, 0, &event);
            tx_queue_push(&event);
            i++;
            continue;
        }

        /* Handle channel messages */
        if (MIDI_IS_STATUS(status)) {
            cin = get_cin_for_status(status);

            switch (cin) {
                case MIDI_CIN_NOTE_OFF:
                case MIDI_CIN_NOTE_ON:
                case MIDI_CIN_POLY_KEYPRESS:
                case MIDI_CIN_CONTROL_CHANGE:
                case MIDI_CIN_PITCH_BEND:
                    if (i + 2 < length) {
                        make_event(cable, cin, status, data[i+1], data[i+2], &event);
                        tx_queue_push(&event);
                        i += 3;
                    } else {
                        return -3;  /* Incomplete message */
                    }
                    break;

                case MIDI_CIN_PROGRAM_CHANGE:
                case MIDI_CIN_CHANNEL_PRESS:
                    if (i + 1 < length) {
                        make_event(cable, cin, status, data[i+1], 0, &event);
                        tx_queue_push(&event);
                        i += 2;
                    } else {
                        return -3;
                    }
                    break;

                default:
                    i++;  /* Skip unknown */
                    break;
            }
        } else {
            i++;  /* Skip non-status byte without running status */
        }
    }

    return 0;
}

int midi_usb_send_note_on(uint8_t cable, uint8_t channel, uint8_t note, uint8_t velocity)
{
    midi_usb_event_t event;

    if (make_event(cable, MIDI_CIN_NOTE_ON,
                   0x90 | (channel & 0x0F),
                   note & 0x7F,
                   velocity & 0x7F,
                   &event) != 0) {
        return -1;
    }

    return midi_usb_send_event(&event);
}

int midi_usb_send_note_off(uint8_t cable, uint8_t channel, uint8_t note, uint8_t velocity)
{
    midi_usb_event_t event;

    if (make_event(cable, MIDI_CIN_NOTE_OFF,
                   0x80 | (channel & 0x0F),
                   note & 0x7F,
                   velocity & 0x7F,
                   &event) != 0) {
        return -1;
    }

    return midi_usb_send_event(&event);
}

int midi_usb_send_control_change(uint8_t cable, uint8_t channel, uint8_t controller, uint8_t value)
{
    midi_usb_event_t event;

    if (make_event(cable, MIDI_CIN_CONTROL_CHANGE,
                   0xB0 | (channel & 0x0F),
                   controller & 0x7F,
                   value & 0x7F,
                   &event) != 0) {
        return -1;
    }

    return midi_usb_send_event(&event);
}

int midi_usb_send_program_change(uint8_t cable, uint8_t channel, uint8_t program)
{
    midi_usb_event_t event;

    if (make_event(cable, MIDI_CIN_PROGRAM_CHANGE,
                   0xC0 | (channel & 0x0F),
                   program & 0x7F,
                   0,
                   &event) != 0) {
        return -1;
    }

    return midi_usb_send_event(&event);
}

int midi_usb_send_pitch_bend(uint8_t cable, uint8_t channel, uint16_t value)
{
    midi_usb_event_t event;

    if (make_event(cable, MIDI_CIN_PITCH_BEND,
                   0xE0 | (channel & 0x0F),
                   value & 0x7F,           /* LSB */
                   (value >> 7) & 0x7F,    /* MSB */
                   &event) != 0) {
        return -1;
    }

    return midi_usb_send_event(&event);
}

int midi_usb_send_sysex(uint8_t cable, const uint8_t *data, uint16_t length)
{
    uint16_t i = 0;
    uint8_t fragment[3];
    uint8_t frag_len;
    bool end;
    int result;

    if (!g_midi_usb_ctx.initialized || data == NULL || length == 0) {
        return -1;
    }

    if (cable >= g_midi_usb_ctx.config.num_cables) {
        return -2;
    }

    /* Verify SysEx framing */
    if (data[0] != 0xF0 || data[length - 1] != 0xF7) {
        return -3;  /* Invalid SysEx */
    }

    /* Send SysEx in 3-byte fragments */
    while (i < length) {
        frag_len = 0;
        end = false;

        /* Collect up to 3 bytes */
        while (frag_len < 3 && i < length) {
            fragment[frag_len++] = data[i++];

            /* Check for end of SysEx */
            if (fragment[frag_len - 1] == 0xF7) {
                end = true;
                break;
            }
        }

        /* Send fragment */
        result = send_sysex_fragment(cable, fragment, frag_len, end);
        if (result != 0) {
            return result;
        }
    }

    return 0;
}

int midi_usb_send_realtime(uint8_t cable, uint8_t status)
{
    midi_usb_event_t event;

    if (!MIDI_IS_REALTIME(status)) {
        return -1;
    }

    if (make_event(cable, MIDI_CIN_SINGLE_BYTE, status, 0, 0, &event) != 0) {
        return -2;
    }

    return midi_usb_send_event(&event);
}

midi_usb_state_t midi_usb_get_state(void)
{
    return g_midi_usb_ctx.state;
}

bool midi_usb_is_ready(void)
{
    return g_midi_usb_ctx.state == MIDI_USB_STATE_CONFIGURED;
}

void midi_usb_get_stats(midi_usb_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_midi_usb_ctx.stats;
    }
}

void midi_usb_reset_stats(void)
{
    memset(&g_midi_usb_ctx.stats, 0, sizeof(g_midi_usb_ctx.stats));
}

int midi_usb_flush(void)
{
    midi_usb_event_t events[MIDI_USB_MAX_EVENTS_PER_PKT];
    uint8_t count = 0;

    if (!g_midi_usb_ctx.initialized) {
        return -1;
    }

    if (g_midi_usb_ctx.state != MIDI_USB_STATE_CONFIGURED) {
        return -2;
    }

    /* Wait for current transfer to complete */
    /*
     * TODO: Wait with timeout
     *
     * TickType_t start = xTaskGetTickCount();
     * while (g_midi_usb_ctx.tx_busy) {
     *     if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(MIDI_USB_TX_FLUSH_TIMEOUT)) {
     *         return -3;  // Timeout
     *     }
     *     vTaskDelay(1);
     * }
     */

    /* Send any queued events */
    while (!tx_queue_is_empty()) {
        count = 0;

        while (count < MIDI_USB_MAX_EVENTS_PER_PKT && !tx_queue_is_empty()) {
            if (tx_queue_pop(&events[count]) == 0) {
                count++;
            }
        }

        if (count > 0) {
            int result = usb_send_packet((uint8_t *)events, count * MIDI_USB_EVENT_SIZE);
            if (result != 0) {
                g_midi_usb_ctx.stats.tx_errors++;
                return -4;
            }
        }
    }

    return 0;
}

/*******************************************************************************
 * Additional Helper Functions
 ******************************************************************************/

/**
 * @brief Send All Notes Off on all channels
 *
 * @param cable Cable number
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_all_notes_off(uint8_t cable)
{
    int result;

    for (uint8_t ch = 0; ch < 16; ch++) {
        result = midi_usb_send_control_change(cable, ch, 123, 0);
        if (result != 0) {
            return result;
        }
    }

    return 0;
}

/**
 * @brief Send timing clock message
 *
 * @param cable Cable number
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_clock(uint8_t cable)
{
    return midi_usb_send_realtime(cable, 0xF8);
}

/**
 * @brief Send start message
 *
 * @param cable Cable number
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_start(uint8_t cable)
{
    return midi_usb_send_realtime(cable, 0xFA);
}

/**
 * @brief Send stop message
 *
 * @param cable Cable number
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_stop(uint8_t cable)
{
    return midi_usb_send_realtime(cable, 0xFC);
}

/**
 * @brief Send continue message
 *
 * @param cable Cable number
 * @return 0 on success, negative error code on failure
 */
int midi_usb_send_continue(uint8_t cable)
{
    return midi_usb_send_realtime(cable, 0xFB);
}
