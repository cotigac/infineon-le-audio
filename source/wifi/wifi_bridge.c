/**
 * @file wifi_bridge.c
 * @brief USB-to-Wi-Fi Data Bridge Implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_bridge.h"
#include "wifi_sdio.h"
#include <string.h>

/* TODO: Include FreeRTOS headers */
/* #include "FreeRTOS.h" */
/* #include "task.h" */
/* #include "queue.h" */
/* #include "semphr.h" */

/* TODO: Include WHD headers */
/* #include "whd.h" */
/* #include "whd_wifi_api.h" */

/* TODO: Include USB headers */
/* #include "USB.h" */
/* #include "USB_Bulk.h" */

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Packet buffer structure */
typedef struct {
    uint8_t data[WIFI_BRIDGE_MAX_PACKET_SIZE];
    uint16_t length;
    bool in_use;
} wifi_bridge_buffer_t;

/** Bridge state */
typedef struct {
    bool initialized;
    wifi_bridge_config_t config;
    wifi_bridge_status_t status;
    wifi_bridge_stats_t stats;
    wifi_bridge_callback_t callback;
    void *callback_user_data;

    /* Packet buffers */
    wifi_bridge_buffer_t tx_buffers[WIFI_BRIDGE_NUM_BUFFERS];
    wifi_bridge_buffer_t rx_buffers[WIFI_BRIDGE_NUM_BUFFERS];
    uint8_t tx_head;
    uint8_t tx_tail;
    uint8_t rx_head;
    uint8_t rx_tail;

    /* USB endpoint handles */
    /* TODO: Add USB handles */

    /* WHD interface */
    /* TODO: Add WHD handles */
    /* whd_interface_t whd_iface; */

} wifi_bridge_state_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static wifi_bridge_state_t bridge_state = {0};

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief Get next free TX buffer
 */
static wifi_bridge_buffer_t* get_free_tx_buffer(void)
{
    for (int i = 0; i < WIFI_BRIDGE_NUM_BUFFERS; i++) {
        if (!bridge_state.tx_buffers[i].in_use) {
            bridge_state.tx_buffers[i].in_use = true;
            return &bridge_state.tx_buffers[i];
        }
    }
    return NULL;
}

/**
 * @brief Get next free RX buffer
 */
static wifi_bridge_buffer_t* get_free_rx_buffer(void)
{
    for (int i = 0; i < WIFI_BRIDGE_NUM_BUFFERS; i++) {
        if (!bridge_state.rx_buffers[i].in_use) {
            bridge_state.rx_buffers[i].in_use = true;
            return &bridge_state.rx_buffers[i];
        }
    }
    return NULL;
}

/**
 * @brief Release a buffer
 */
static void release_buffer(wifi_bridge_buffer_t *buffer)
{
    if (buffer != NULL) {
        buffer->in_use = false;
        buffer->length = 0;
    }
}

/**
 * @brief Send event to callback
 */
static void send_event(wifi_bridge_event_type_t type, void *data)
{
    if (bridge_state.callback != NULL) {
        wifi_bridge_event_t event = {0};
        event.type = type;

        if (type == WIFI_BRIDGE_EVENT_ERROR && data != NULL) {
            event.data.error_code = *((int*)data);
        }

        bridge_state.callback(&event, bridge_state.callback_user_data);
    }
}

/**
 * @brief Process USB RX (data from app processor)
 */
static void process_usb_rx(void)
{
    /* TODO: Implement USB bulk receive
     *
     * uint8_t buffer[WIFI_BRIDGE_USB_BUFFER_SIZE];
     * int bytes_read = USBD_BULK_Read(buffer, sizeof(buffer), 0);
     *
     * if (bytes_read > 0) {
     *     wifi_bridge_send_to_wifi(buffer, bytes_read);
     * }
     */
}

/**
 * @brief Process USB TX (data to app processor)
 */
static void process_usb_tx(void)
{
    /* TODO: Implement USB bulk send
     *
     * Check for pending RX buffers and send to USB
     */
}

/**
 * @brief Process Wi-Fi RX (data from network)
 */
static void process_wifi_rx(void)
{
    /* TODO: Implement Wi-Fi packet receive
     *
     * whd_buffer_t packet;
     * if (whd_network_receive(&bridge_state.whd_iface, &packet) == WHD_SUCCESS) {
     *     wifi_bridge_send_to_usb(packet.data, packet.length);
     *     whd_buffer_release(&packet);
     * }
     */
}

/**
 * @brief Process Wi-Fi TX (data to network)
 */
static void process_wifi_tx(void)
{
    /* TODO: Implement Wi-Fi packet send
     *
     * Check for pending TX buffers and send to Wi-Fi
     */
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int wifi_bridge_init(const wifi_bridge_config_t *config)
{
    if (bridge_state.initialized) {
        return -1; /* Already initialized */
    }

    /* Use default config if not provided */
    if (config != NULL) {
        memcpy(&bridge_state.config, config, sizeof(wifi_bridge_config_t));
    } else {
        wifi_bridge_config_t default_config = WIFI_BRIDGE_CONFIG_DEFAULT;
        memcpy(&bridge_state.config, &default_config, sizeof(wifi_bridge_config_t));
    }

    /* Reset state */
    memset(&bridge_state.stats, 0, sizeof(wifi_bridge_stats_t));
    memset(bridge_state.tx_buffers, 0, sizeof(bridge_state.tx_buffers));
    memset(bridge_state.rx_buffers, 0, sizeof(bridge_state.rx_buffers));
    bridge_state.tx_head = 0;
    bridge_state.tx_tail = 0;
    bridge_state.rx_head = 0;
    bridge_state.rx_tail = 0;

    /* Initialize SDIO for Wi-Fi */
    if (wifi_sdio_init(NULL) != 0) {
        return -2;
    }

    /* TODO: Initialize WHD (Wi-Fi Host Driver)
     *
     * whd_init_config_t whd_config = {0};
     * if (whd_init(&whd_config, &bridge_state.whd_iface) != WHD_SUCCESS) {
     *     wifi_sdio_deinit();
     *     return -3;
     * }
     *
     * if (whd_wifi_on() != WHD_SUCCESS) {
     *     whd_deinit(bridge_state.whd_iface);
     *     wifi_sdio_deinit();
     *     return -4;
     * }
     */

    /* TODO: Initialize USB bulk endpoints
     *
     * USBD_BULK_Init();
     */

    bridge_state.status = WIFI_BRIDGE_STATUS_STOPPED;
    bridge_state.initialized = true;

    return 0;
}

void wifi_bridge_deinit(void)
{
    if (!bridge_state.initialized) {
        return;
    }

    wifi_bridge_stop();

    /* TODO: Deinitialize WHD
     * whd_wifi_off();
     * whd_deinit(bridge_state.whd_iface);
     */

    wifi_sdio_deinit();

    bridge_state.initialized = false;
}

int wifi_bridge_start(void)
{
    if (!bridge_state.initialized) {
        return -1;
    }

    if (bridge_state.status == WIFI_BRIDGE_STATUS_RUNNING) {
        return 0; /* Already running */
    }

    bridge_state.status = WIFI_BRIDGE_STATUS_STARTING;

    /* TODO: Start USB endpoints
     *
     * USBD_BULK_SetContinuousReadMode(1);
     */

    /* TODO: Register WHD callbacks for packet reception */

    bridge_state.status = WIFI_BRIDGE_STATUS_RUNNING;
    send_event(WIFI_BRIDGE_EVENT_STARTED, NULL);

    return 0;
}

int wifi_bridge_stop(void)
{
    if (!bridge_state.initialized) {
        return -1;
    }

    if (bridge_state.status == WIFI_BRIDGE_STATUS_STOPPED) {
        return 0; /* Already stopped */
    }

    /* TODO: Stop USB endpoints */

    /* TODO: Unregister WHD callbacks */

    /* Release all buffers */
    for (int i = 0; i < WIFI_BRIDGE_NUM_BUFFERS; i++) {
        bridge_state.tx_buffers[i].in_use = false;
        bridge_state.rx_buffers[i].in_use = false;
    }

    bridge_state.status = WIFI_BRIDGE_STATUS_STOPPED;
    send_event(WIFI_BRIDGE_EVENT_STOPPED, NULL);

    return 0;
}

void wifi_bridge_register_callback(wifi_bridge_callback_t callback, void *user_data)
{
    bridge_state.callback = callback;
    bridge_state.callback_user_data = user_data;
}

void wifi_bridge_process(void)
{
    if (!bridge_state.initialized ||
        bridge_state.status != WIFI_BRIDGE_STATUS_RUNNING) {
        return;
    }

    /* Process USB -> Wi-Fi direction */
    if (bridge_state.config.direction == WIFI_BRIDGE_DIR_USB_TO_WIFI ||
        bridge_state.config.direction == WIFI_BRIDGE_DIR_BIDIRECTIONAL) {
        process_usb_rx();
        process_wifi_tx();
    }

    /* Process Wi-Fi -> USB direction */
    if (bridge_state.config.direction == WIFI_BRIDGE_DIR_WIFI_TO_USB ||
        bridge_state.config.direction == WIFI_BRIDGE_DIR_BIDIRECTIONAL) {
        process_wifi_rx();
        process_usb_tx();
    }
}

int wifi_bridge_send_to_wifi(const uint8_t *data, uint16_t length)
{
    if (!bridge_state.initialized || data == NULL || length == 0) {
        return -1;
    }

    if (length > bridge_state.config.mtu) {
        return -2; /* Packet too large */
    }

    wifi_bridge_buffer_t *buffer = get_free_tx_buffer();
    if (buffer == NULL) {
        bridge_state.stats.buffer_overflows++;
        bridge_state.stats.packets_dropped++;
        return -3; /* No buffer available */
    }

    memcpy(buffer->data, data, length);
    buffer->length = length;

    /* TODO: Queue for Wi-Fi transmission
     *
     * whd_buffer_t whd_buf;
     * whd_buf.data = buffer->data;
     * whd_buf.length = buffer->length;
     *
     * whd_result_t result = whd_network_send(&bridge_state.whd_iface, &whd_buf);
     * if (result != WHD_SUCCESS) {
     *     release_buffer(buffer);
     *     bridge_state.stats.wifi_errors++;
     *     return -4;
     * }
     */

    bridge_state.stats.usb_bytes_rx += length;
    bridge_state.stats.wifi_bytes_tx += length;
    bridge_state.stats.packets_forwarded++;

    release_buffer(buffer);
    return 0;
}

int wifi_bridge_send_to_usb(const uint8_t *data, uint16_t length)
{
    if (!bridge_state.initialized || data == NULL || length == 0) {
        return -1;
    }

    wifi_bridge_buffer_t *buffer = get_free_rx_buffer();
    if (buffer == NULL) {
        bridge_state.stats.buffer_overflows++;
        bridge_state.stats.packets_dropped++;
        return -2; /* No buffer available */
    }

    memcpy(buffer->data, data, length);
    buffer->length = length;

    /* TODO: Send via USB bulk endpoint
     *
     * int result = USBD_BULK_Write(buffer->data, buffer->length, 0);
     * if (result < 0) {
     *     release_buffer(buffer);
     *     bridge_state.stats.usb_errors++;
     *     return -3;
     * }
     */

    bridge_state.stats.wifi_bytes_rx += length;
    bridge_state.stats.usb_bytes_tx += length;
    bridge_state.stats.packets_forwarded++;

    release_buffer(buffer);
    return 0;
}

wifi_bridge_status_t wifi_bridge_get_status(void)
{
    return bridge_state.status;
}

void wifi_bridge_get_stats(wifi_bridge_stats_t *stats)
{
    if (stats != NULL) {
        memcpy(stats, &bridge_state.stats, sizeof(wifi_bridge_stats_t));
    }
}

void wifi_bridge_reset_stats(void)
{
    memset(&bridge_state.stats, 0, sizeof(wifi_bridge_stats_t));
}

int wifi_bridge_set_mode(wifi_bridge_mode_t mode)
{
    if (!bridge_state.initialized) {
        return -1;
    }

    if (bridge_state.status == WIFI_BRIDGE_STATUS_RUNNING) {
        return -2; /* Cannot change mode while running */
    }

    bridge_state.config.mode = mode;
    return 0;
}

wifi_bridge_mode_t wifi_bridge_get_mode(void)
{
    return bridge_state.config.mode;
}

bool wifi_bridge_is_ready(void)
{
    return bridge_state.initialized &&
           bridge_state.status == WIFI_BRIDGE_STATUS_RUNNING;
}

uint32_t wifi_bridge_get_tx_available(void)
{
    uint32_t free_buffers = 0;
    for (int i = 0; i < WIFI_BRIDGE_NUM_BUFFERS; i++) {
        if (!bridge_state.tx_buffers[i].in_use) {
            free_buffers++;
        }
    }
    return free_buffers * bridge_state.config.mtu;
}

uint32_t wifi_bridge_get_rx_pending(void)
{
    uint32_t pending_bytes = 0;
    for (int i = 0; i < WIFI_BRIDGE_NUM_BUFFERS; i++) {
        if (bridge_state.rx_buffers[i].in_use) {
            pending_bytes += bridge_state.rx_buffers[i].length;
        }
    }
    return pending_bytes;
}
