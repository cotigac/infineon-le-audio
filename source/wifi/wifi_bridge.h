/**
 * @file wifi_bridge.h
 * @brief USB-to-Wi-Fi Data Bridge API
 *
 * This module implements a high-speed data bridge between USB and Wi-Fi,
 * allowing an external application processor to transmit data over Wi-Fi
 * through the PSoC Edge E82.
 *
 * Data Flow:
 *   App Processor -> USB HS (480 Mbps) -> PSoC Edge -> SDIO -> CYW55512 -> Wi-Fi
 *
 * The bridge supports:
 * - Raw Ethernet frame forwarding
 * - TCP/UDP socket tunneling
 * - Transparent bridge mode
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WIFI_BRIDGE_H
#define WIFI_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum packet size (MTU + headers) */
#define WIFI_BRIDGE_MAX_PACKET_SIZE     1600

/** USB endpoint buffer size (USB HS bulk = 512 bytes) */
#define WIFI_BRIDGE_USB_BUFFER_SIZE     512

/** Number of packet buffers */
#define WIFI_BRIDGE_NUM_BUFFERS         16

/** Bridge modes */
typedef enum {
    WIFI_BRIDGE_MODE_DISABLED = 0,      /**< Bridge disabled */
    WIFI_BRIDGE_MODE_ETHERNET,          /**< Raw Ethernet frame forwarding */
    WIFI_BRIDGE_MODE_IP,                /**< IP packet forwarding */
    WIFI_BRIDGE_MODE_SOCKET             /**< Socket tunneling */
} wifi_bridge_mode_t;

/** Bridge direction */
typedef enum {
    WIFI_BRIDGE_DIR_USB_TO_WIFI = 0,    /**< USB -> Wi-Fi */
    WIFI_BRIDGE_DIR_WIFI_TO_USB,        /**< Wi-Fi -> USB */
    WIFI_BRIDGE_DIR_BIDIRECTIONAL       /**< Both directions */
} wifi_bridge_direction_t;

/** Bridge status */
typedef enum {
    WIFI_BRIDGE_STATUS_STOPPED = 0,     /**< Bridge stopped */
    WIFI_BRIDGE_STATUS_STARTING,        /**< Bridge starting */
    WIFI_BRIDGE_STATUS_RUNNING,         /**< Bridge running */
    WIFI_BRIDGE_STATUS_ERROR            /**< Bridge error */
} wifi_bridge_status_t;

/** Bridge configuration */
typedef struct {
    wifi_bridge_mode_t mode;            /**< Operating mode */
    wifi_bridge_direction_t direction;  /**< Data direction */
    uint16_t mtu;                       /**< Maximum transmission unit */
    bool enable_flow_control;           /**< Enable USB flow control */
    bool enable_checksum_offload;       /**< Enable checksum offload */
    uint8_t usb_cable;                  /**< USB MIDI cable number for data */
} wifi_bridge_config_t;

/** Bridge statistics */
typedef struct {
    uint64_t usb_bytes_rx;              /**< Bytes received from USB */
    uint64_t usb_bytes_tx;              /**< Bytes sent to USB */
    uint64_t wifi_bytes_rx;             /**< Bytes received from Wi-Fi */
    uint64_t wifi_bytes_tx;             /**< Bytes sent to Wi-Fi */
    uint32_t packets_forwarded;         /**< Total packets forwarded */
    uint32_t packets_dropped;           /**< Packets dropped */
    uint32_t usb_errors;                /**< USB transfer errors */
    uint32_t wifi_errors;               /**< Wi-Fi transfer errors */
    uint32_t buffer_overflows;          /**< Buffer overflow count */
} wifi_bridge_stats_t;

/** Bridge event types */
typedef enum {
    WIFI_BRIDGE_EVENT_STARTED,          /**< Bridge started */
    WIFI_BRIDGE_EVENT_STOPPED,          /**< Bridge stopped */
    WIFI_BRIDGE_EVENT_CONNECTED,        /**< Wi-Fi connected */
    WIFI_BRIDGE_EVENT_DISCONNECTED,     /**< Wi-Fi disconnected */
    WIFI_BRIDGE_EVENT_PACKET_RX,        /**< Packet received */
    WIFI_BRIDGE_EVENT_PACKET_TX,        /**< Packet transmitted */
    WIFI_BRIDGE_EVENT_ERROR             /**< Error occurred */
} wifi_bridge_event_type_t;

/** Bridge event data */
typedef struct {
    wifi_bridge_event_type_t type;
    union {
        struct {
            uint8_t *data;
            uint16_t length;
        } packet;
        int error_code;
    } data;
} wifi_bridge_event_t;

/** Bridge event callback */
typedef void (*wifi_bridge_callback_t)(const wifi_bridge_event_t *event, void *user_data);

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default bridge configuration */
#define WIFI_BRIDGE_CONFIG_DEFAULT {        \
    .mode = WIFI_BRIDGE_MODE_ETHERNET,      \
    .direction = WIFI_BRIDGE_DIR_BIDIRECTIONAL, \
    .mtu = 1500,                            \
    .enable_flow_control = true,            \
    .enable_checksum_offload = false,       \
    .usb_cable = 0                          \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize the Wi-Fi bridge
 *
 * @param config Pointer to configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int wifi_bridge_init(const wifi_bridge_config_t *config);

/**
 * @brief Deinitialize the Wi-Fi bridge
 */
void wifi_bridge_deinit(void);

/**
 * @brief Start the bridge
 *
 * @return 0 on success, negative error code on failure
 */
int wifi_bridge_start(void);

/**
 * @brief Stop the bridge
 *
 * @return 0 on success, negative error code on failure
 */
int wifi_bridge_stop(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void wifi_bridge_register_callback(wifi_bridge_callback_t callback, void *user_data);

/**
 * @brief Process bridge (call from task loop)
 *
 * Processes pending USB and Wi-Fi data transfers.
 * Should be called periodically from the bridge task.
 */
void wifi_bridge_process(void);

/**
 * @brief Send data from USB to Wi-Fi
 *
 * @param data Data buffer
 * @param length Data length
 * @return 0 on success, negative error code on failure
 */
int wifi_bridge_send_to_wifi(const uint8_t *data, uint16_t length);

/**
 * @brief Send data from Wi-Fi to USB
 *
 * @param data Data buffer
 * @param length Data length
 * @return 0 on success, negative error code on failure
 */
int wifi_bridge_send_to_usb(const uint8_t *data, uint16_t length);

/**
 * @brief Get current bridge status
 *
 * @return Current status
 */
wifi_bridge_status_t wifi_bridge_get_status(void);

/**
 * @brief Get bridge statistics
 *
 * @param stats Pointer to statistics structure
 */
void wifi_bridge_get_stats(wifi_bridge_stats_t *stats);

/**
 * @brief Reset bridge statistics
 */
void wifi_bridge_reset_stats(void);

/**
 * @brief Set bridge mode
 *
 * @param mode New operating mode
 * @return 0 on success, negative error code on failure
 */
int wifi_bridge_set_mode(wifi_bridge_mode_t mode);

/**
 * @brief Get current bridge mode
 *
 * @return Current mode
 */
wifi_bridge_mode_t wifi_bridge_get_mode(void);

/**
 * @brief Check if bridge is ready for data transfer
 *
 * @return true if ready
 */
bool wifi_bridge_is_ready(void);

/**
 * @brief Get available buffer space for USB->Wi-Fi
 *
 * @return Available bytes
 */
uint32_t wifi_bridge_get_tx_available(void);

/**
 * @brief Get pending data bytes for Wi-Fi->USB
 *
 * @return Pending bytes
 */
uint32_t wifi_bridge_get_rx_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_BRIDGE_H */
