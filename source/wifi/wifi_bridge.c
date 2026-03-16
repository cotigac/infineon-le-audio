/**
 * @file wifi_bridge.c
 * @brief USB-to-Wi-Fi Data Bridge Implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_bridge.h"
#include "whd_buffer_impl.h"
#include <string.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Wi-Fi Host Driver (WHD) headers */
#include "whd.h"
#include "whd_wifi_api.h"
#include "whd_network_types.h"
#include "whd_buffer_api.h"

/* Segger emUSB-Device headers */
#include "USB.h"
#include "USB_Bulk.h"

/*******************************************************************************
 * Forward Declarations
 ******************************************************************************/

static void send_event(wifi_bridge_event_type_t type, void *data);

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

    /* USB bulk endpoint handle (emUSB-Device) */
    USB_BULK_HANDLE usb_bulk_handle;
    volatile bool usb_rx_pending;
    uint8_t usb_rx_buffer[WIFI_BRIDGE_USB_BUFFER_SIZE];
    uint8_t usb_tx_buffer[WIFI_BRIDGE_USB_BUFFER_SIZE];

    /* WHD (Wi-Fi Host Driver) handles */
    whd_driver_t whd_driver;
    whd_interface_t whd_iface;
    bool whd_initialized;
    bool wifi_connected;

    /* FreeRTOS synchronization */
    SemaphoreHandle_t buffer_mutex;
    QueueHandle_t tx_queue;
    QueueHandle_t rx_queue;

} wifi_bridge_state_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static wifi_bridge_state_t bridge_state = {0};

/*******************************************************************************
 * USB Bulk Callbacks (emUSB-Device)
 ******************************************************************************/

/**
 * @brief USB bulk OUT (receive) callback
 *
 * Called by emUSB-Device when data is received from the host.
 */
static void usb_bulk_on_rx(void)
{
    int bytes_read;
    wifi_bridge_buffer_t *buffer;

    /* Read data from USB bulk endpoint */
    bytes_read = USBD_BULK_Read(bridge_state.usb_bulk_handle,
                                bridge_state.usb_rx_buffer,
                                WIFI_BRIDGE_USB_BUFFER_SIZE,
                                0);  /* Non-blocking */

    if (bytes_read > 0) {
        /* Get a free TX buffer to queue the data */
        if (xSemaphoreTake(bridge_state.buffer_mutex, 0) == pdTRUE) {
            buffer = NULL;
            for (int i = 0; i < WIFI_BRIDGE_NUM_BUFFERS; i++) {
                if (!bridge_state.tx_buffers[i].in_use) {
                    bridge_state.tx_buffers[i].in_use = true;
                    buffer = &bridge_state.tx_buffers[i];
                    break;
                }
            }

            if (buffer != NULL) {
                memcpy(buffer->data, bridge_state.usb_rx_buffer, bytes_read);
                buffer->length = (uint16_t)bytes_read;
                bridge_state.stats.usb_bytes_rx += bytes_read;

                /* Queue for Wi-Fi transmission */
                if (xQueueSendFromISR(bridge_state.tx_queue, &buffer, NULL) != pdTRUE) {
                    buffer->in_use = false;
                    bridge_state.stats.buffer_overflows++;
                }
            } else {
                bridge_state.stats.buffer_overflows++;
            }

            xSemaphoreGive(bridge_state.buffer_mutex);
        }
    }
}

/**
 * @brief USB bulk IN (transmit) complete callback
 */
static void usb_bulk_on_tx_complete(void)
{
    /* TX complete - can send more data if available */
    bridge_state.usb_rx_pending = false;
}

/*******************************************************************************
 * WHD Network Interface Callback
 *
 * This function is called by WHD via whd_netif_funcs when packets are received.
 * It should be registered during WHD initialization.
 ******************************************************************************/

/**
 * @brief Process Ethernet data received from WHD
 *
 * This is the network interface callback that will be set in whd_netif_funcs
 * structure during WHD initialization. WHD calls this when a packet is received.
 */
void wifi_bridge_network_process_data(whd_interface_t iface, whd_buffer_t buffer)
{
    (void)iface;
    wifi_bridge_buffer_t *bridge_buffer;
    uint8_t *packet_data;
    uint16_t packet_len;

    if (!bridge_state.initialized || bridge_state.status != WIFI_BRIDGE_STATUS_RUNNING) {
        /* Bridge not ready, release buffer */
        whd_buffer_release(bridge_state.whd_driver, buffer, WHD_NETWORK_RX);
        return;
    }

    /* Get packet data from WHD buffer */
    packet_data = whd_buffer_get_current_piece_data_pointer(bridge_state.whd_driver, buffer);
    packet_len = whd_buffer_get_current_piece_size(bridge_state.whd_driver, buffer);

    if (packet_data != NULL && packet_len > 0 && packet_len <= WIFI_BRIDGE_MAX_PACKET_SIZE) {
        /* Get a free RX buffer */
        if (xSemaphoreTake(bridge_state.buffer_mutex, 0) == pdTRUE) {
            bridge_buffer = NULL;
            for (int i = 0; i < WIFI_BRIDGE_NUM_BUFFERS; i++) {
                if (!bridge_state.rx_buffers[i].in_use) {
                    bridge_state.rx_buffers[i].in_use = true;
                    bridge_buffer = &bridge_state.rx_buffers[i];
                    break;
                }
            }

            if (bridge_buffer != NULL) {
                memcpy(bridge_buffer->data, packet_data, packet_len);
                bridge_buffer->length = packet_len;
                bridge_state.stats.wifi_bytes_rx += packet_len;

                /* Queue for USB transmission */
                if (xQueueSendFromISR(bridge_state.rx_queue, &bridge_buffer, NULL) != pdTRUE) {
                    bridge_buffer->in_use = false;
                    bridge_state.stats.buffer_overflows++;
                }
            } else {
                bridge_state.stats.buffer_overflows++;
            }

            xSemaphoreGive(bridge_state.buffer_mutex);
        }
    }

    /* Release WHD buffer */
    whd_buffer_release(bridge_state.whd_driver, buffer, WHD_NETWORK_RX);
}

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
 *
 * USB bulk receive is handled by USB device middleware callbacks.
 * This function forwards pending data to Wi-Fi via WHD.
 */
static void process_usb_rx(void)
{
    wifi_bridge_buffer_t *buffer;
    whd_buffer_t whd_buf;
    uint8_t *whd_data;

    /* Check TX queue for pending USB data to forward to Wi-Fi */
    while (xQueueReceive(bridge_state.tx_queue, &buffer, 0) == pdTRUE) {
        if (buffer != NULL && buffer->in_use && buffer->length > 0) {
            /* Allocate WHD buffer for transmission */
            if (whd_host_buffer_get(bridge_state.whd_driver, &whd_buf,
                                    WHD_NETWORK_TX, buffer->length, 100) == WHD_SUCCESS) {
                /* Copy data to WHD buffer */
                whd_data = whd_buffer_get_current_piece_data_pointer(bridge_state.whd_driver, whd_buf);
                if (whd_data != NULL) {
                    memcpy(whd_data, buffer->data, buffer->length);
                    whd_buffer_set_size(bridge_state.whd_driver, whd_buf, buffer->length);

                    /* Send via WHD - it handles SDIO internally */
                    whd_result_t result = whd_network_send_ethernet_data(bridge_state.whd_iface, whd_buf);
                    if (result != WHD_SUCCESS) {
                        bridge_state.stats.wifi_errors++;
                        /* WHD releases buffer on failure */
                    } else {
                        bridge_state.stats.wifi_bytes_tx += buffer->length;
                        bridge_state.stats.packets_forwarded++;
                    }
                } else {
                    whd_buffer_release(bridge_state.whd_driver, whd_buf, WHD_NETWORK_TX);
                    bridge_state.stats.wifi_errors++;
                }
            } else {
                bridge_state.stats.buffer_overflows++;
            }
            release_buffer(buffer);
        }
    }
}

/**
 * @brief Process USB TX (data to app processor)
 *
 * Note: Sends pending RX buffers to USB endpoint.
 */
static void process_usb_tx(void)
{
    wifi_bridge_buffer_t *buffer;
    int bytes_written;

    /* Check RX queue for pending Wi-Fi data to forward to USB */
    while (xQueueReceive(bridge_state.rx_queue, &buffer, 0) == pdTRUE) {
        if (buffer != NULL && buffer->in_use && buffer->length > 0) {
            /* Send data to USB bulk IN endpoint */
            bytes_written = USBD_BULK_Write(bridge_state.usb_bulk_handle,
                                            buffer->data,
                                            buffer->length,
                                            0);  /* Non-blocking */

            if (bytes_written > 0) {
                bridge_state.stats.usb_bytes_tx += bytes_written;
                bridge_state.stats.packets_forwarded++;
            } else if (bytes_written < 0) {
                bridge_state.stats.usb_errors++;
                bridge_state.stats.packets_dropped++;
            }
            /* bytes_written == 0 means endpoint busy, packet will be retried */

            release_buffer(buffer);
        }
    }
}

/**
 * @brief Process Wi-Fi RX (data from network)
 *
 * Wi-Fi RX is handled by WHD via interrupt-driven callbacks.
 * WHD calls wifi_bridge_network_process_data() when packets arrive.
 * This function is kept for API compatibility but does nothing.
 */
static void process_wifi_rx(void)
{
    /* No polling needed - WHD handles RX via callback */
    /* See wifi_bridge_network_process_data() for packet reception */
}

/**
 * @brief Process Wi-Fi TX (data to network)
 *
 * Sends pending TX buffers to Wi-Fi via WHD.
 * WHD handles the actual SDIO communication internally.
 */
static void process_wifi_tx(void)
{
    wifi_bridge_buffer_t *buffer;
    whd_buffer_t whd_buf;
    uint8_t *whd_data;

    /* Check TX queue for pending data to send to Wi-Fi */
    while (xQueueReceive(bridge_state.tx_queue, &buffer, 0) == pdTRUE) {
        if (buffer != NULL && buffer->in_use && buffer->length > 0) {
            /* Allocate WHD buffer for transmission */
            if (whd_host_buffer_get(bridge_state.whd_driver, &whd_buf,
                                    WHD_NETWORK_TX, buffer->length, 100) == WHD_SUCCESS) {
                /* Copy data to WHD buffer */
                whd_data = whd_buffer_get_current_piece_data_pointer(bridge_state.whd_driver, whd_buf);
                if (whd_data != NULL) {
                    memcpy(whd_data, buffer->data, buffer->length);
                    whd_buffer_set_size(bridge_state.whd_driver, whd_buf, buffer->length);

                    /* Send via WHD */
                    whd_result_t result = whd_network_send_ethernet_data(bridge_state.whd_iface, whd_buf);
                    if (result != WHD_SUCCESS) {
                        bridge_state.stats.wifi_errors++;
                        bridge_state.stats.packets_dropped++;
                    } else {
                        bridge_state.stats.wifi_bytes_tx += buffer->length;
                        bridge_state.stats.packets_forwarded++;
                    }
                } else {
                    whd_buffer_release(bridge_state.whd_driver, whd_buf, WHD_NETWORK_TX);
                    bridge_state.stats.wifi_errors++;
                    bridge_state.stats.packets_dropped++;
                }
            } else {
                bridge_state.stats.buffer_overflows++;
                bridge_state.stats.packets_dropped++;
            }
            release_buffer(buffer);
        }
    }
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int wifi_bridge_init(const wifi_bridge_config_t *config)
{
    whd_init_config_t whd_config;
    whd_result_t whd_result;
    USB_BULK_INIT_DATA usb_bulk_init;

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
    bridge_state.usb_rx_pending = false;
    bridge_state.whd_initialized = false;
    bridge_state.wifi_connected = false;

    /* Create FreeRTOS synchronization primitives */
    bridge_state.buffer_mutex = xSemaphoreCreateMutex();
    bridge_state.tx_queue = xQueueCreate(WIFI_BRIDGE_NUM_BUFFERS, sizeof(wifi_bridge_buffer_t *));
    bridge_state.rx_queue = xQueueCreate(WIFI_BRIDGE_NUM_BUFFERS, sizeof(wifi_bridge_buffer_t *));
    if (bridge_state.buffer_mutex == NULL ||
        bridge_state.tx_queue == NULL ||
        bridge_state.rx_queue == NULL) {
        return -2;  /* FreeRTOS resource allocation failed */
    }

    /* Initialize WHD buffer implementation */
    if (whd_buffer_impl_init() != 0) {
        return -3;
    }

    /* Initialize WHD (Wi-Fi Host Driver)
     * WHD handles SDIO communication internally via cyhal_sdio HAL layer.
     * We provide our buffer implementation for packet allocation.
     */
    memset(&whd_config, 0, sizeof(whd_init_config_t));
    whd_config.thread_stack_size = 4096;
    whd_config.thread_priority = tskIDLE_PRIORITY + 2;  /* Normal priority */
    whd_config.country = WHD_COUNTRY_UNITED_STATES;

    whd_result = whd_init(&bridge_state.whd_driver, &whd_config,
                          NULL,                        /* Resource interface (use default) */
                          whd_buffer_impl_get_funcs(), /* Buffer interface */
                          NULL);                       /* SDIO interface (use default HAL) */
    if (whd_result != WHD_SUCCESS) {
        whd_buffer_impl_deinit();
        return -4;
    }

    /* Power on Wi-Fi */
    whd_result = whd_wifi_on(bridge_state.whd_driver, &bridge_state.whd_iface);
    if (whd_result != WHD_SUCCESS) {
        whd_deinit(bridge_state.whd_iface);
        whd_buffer_impl_deinit();
        return -5;
    }

    bridge_state.whd_initialized = true;

    /* Initialize USB bulk endpoint for data bridge */
    memset(&usb_bulk_init, 0, sizeof(USB_BULK_INIT_DATA));
    usb_bulk_init.EPIn = USBD_AddEP(USB_DIR_IN, USB_TRANSFER_TYPE_BULK, 0,
                                    bridge_state.usb_tx_buffer,
                                    WIFI_BRIDGE_USB_BUFFER_SIZE);
    usb_bulk_init.EPOut = USBD_AddEP(USB_DIR_OUT, USB_TRANSFER_TYPE_BULK, 0,
                                     bridge_state.usb_rx_buffer,
                                     WIFI_BRIDGE_USB_BUFFER_SIZE);

    bridge_state.usb_bulk_handle = USBD_BULK_Add(&usb_bulk_init);
    if (bridge_state.usb_bulk_handle == 0) {
        whd_wifi_off(bridge_state.whd_iface);
        whd_deinit(bridge_state.whd_iface);
        whd_buffer_impl_deinit();
        return -6;
    }

    bridge_state.status = WIFI_BRIDGE_STATUS_STOPPED;
    bridge_state.initialized = true;

    return 0;
}

void wifi_bridge_deinit(void)
{
    if (!bridge_state.initialized) {
        return;
    }

    /* Stop bridge if running */
    wifi_bridge_stop();

    /* Deinitialize WHD */
    if (bridge_state.whd_initialized) {
        /* Disconnect from network if connected */
        if (bridge_state.wifi_connected) {
            whd_wifi_leave(bridge_state.whd_iface);
            bridge_state.wifi_connected = false;
        }

        /* Power off Wi-Fi */
        whd_wifi_off(bridge_state.whd_iface);

        /* Deinitialize WHD driver */
        whd_deinit(bridge_state.whd_iface);

        bridge_state.whd_initialized = false;
        bridge_state.whd_driver = NULL;
        bridge_state.whd_iface = NULL;
    }

    /* Note: USB bulk handle is managed by emUSB-Device stack
     * It will be cleaned up when USB is deinitialized */
    bridge_state.usb_bulk_handle = 0;

    /* Deinitialize WHD buffer implementation */
    whd_buffer_impl_deinit();

    /* Delete FreeRTOS synchronization primitives */
    if (bridge_state.buffer_mutex != NULL) {
        vSemaphoreDelete(bridge_state.buffer_mutex);
        bridge_state.buffer_mutex = NULL;
    }
    if (bridge_state.tx_queue != NULL) {
        vQueueDelete(bridge_state.tx_queue);
        bridge_state.tx_queue = NULL;
    }
    if (bridge_state.rx_queue != NULL) {
        vQueueDelete(bridge_state.rx_queue);
        bridge_state.rx_queue = NULL;
    }

    bridge_state.initialized = false;
}

int wifi_bridge_start(void)
{
    whd_result_t whd_result;

    if (!bridge_state.initialized) {
        return -1;
    }

    if (bridge_state.status == WIFI_BRIDGE_STATUS_RUNNING) {
        return 0; /* Already running */
    }

    bridge_state.status = WIFI_BRIDGE_STATUS_STARTING;

    /* Enable continuous read mode on USB bulk endpoint */
    USBD_BULK_SetContinuousReadMode(bridge_state.usb_bulk_handle);

    /* Note: Link state callbacks and raw packet processing are configured
     * via whd_netif_funcs during WHD initialization. The WHD will call
     * whd_network_process_ethernet_data() for received packets.
     *
     * For link state monitoring, use whd_wifi_is_ready_to_transceive()
     * in the bridge processing loop.
     */

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

    /* Cancel any pending USB operations */
    USBD_BULK_CancelRead(bridge_state.usb_bulk_handle);
    USBD_BULK_CancelWrite(bridge_state.usb_bulk_handle);

    /* Note: Link state and packet callbacks are managed via whd_netif_funcs
     * during WHD initialization and persist until WHD is deinitialized.
     */

    /* Flush queues */
    wifi_bridge_buffer_t *buffer;
    while (xQueueReceive(bridge_state.tx_queue, &buffer, 0) == pdTRUE) {
        if (buffer != NULL) {
            buffer->in_use = false;
        }
    }
    while (xQueueReceive(bridge_state.rx_queue, &buffer, 0) == pdTRUE) {
        if (buffer != NULL) {
            buffer->in_use = false;
        }
    }

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

    /* Queue for Wi-Fi transmission via SDIO */
    if (xQueueSend(bridge_state.tx_queue, &buffer, 0) != pdTRUE) {
        release_buffer(buffer);
        bridge_state.stats.buffer_overflows++;
        bridge_state.stats.packets_dropped++;
        return -4;
    }

    bridge_state.stats.usb_bytes_rx += length;

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

    /* Queue for USB transmission */
    if (xQueueSend(bridge_state.rx_queue, &buffer, 0) != pdTRUE) {
        release_buffer(buffer);
        bridge_state.stats.buffer_overflows++;
        bridge_state.stats.packets_dropped++;
        return -3;
    }

    bridge_state.stats.wifi_bytes_rx += length;

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
