/**
 * @file usb_composite.h
 * @brief USB Composite Device Manager
 *
 * Manages USB composite device with MIDI, CDC/ACM, and Wi-Fi Bridge interfaces.
 * Handles centralized USB initialization and interface coordination.
 *
 * USB Interfaces:
 * - Interface 0: Audio Control
 * - Interface 1: MIDI Streaming (Audio Class)
 * - Interface 2: CDC Control (ACM)
 * - Interface 3: CDC Data
 * - Interface 4: Wi-Fi Bridge (Bulk Data)
 *
 * USB Endpoints:
 * - EP0:          Control (64 bytes)
 * - EP 0x81/0x01: MIDI IN/OUT (512 bytes, High-Speed)
 * - EP 0x82/0x02: CDC Data IN/OUT (64 bytes)
 * - EP 0x83:      CDC Notifications (8 bytes, Interrupt)
 * - EP 0x84/0x04: Wi-Fi Bridge IN/OUT (512 bytes, High-Speed)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USB_COMPOSITE_H
#define USB_COMPOSITE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** USB Vendor ID (Infineon/Cypress) */
#define USB_COMPOSITE_VID           0x04B4

/** USB Product ID for LE Audio device */
#define USB_COMPOSITE_PID           0x00F2

/** USB device version (BCD) */
#define USB_COMPOSITE_DEV_VERSION   0x0100

/*******************************************************************************
 * Types
 ******************************************************************************/

/**
 * @brief USB composite device configuration
 */
typedef struct {
    const char *manufacturer;       /**< Manufacturer string */
    const char *product;            /**< Product string */
    const char *serial_number;      /**< Serial number (NULL for auto) */
    uint16_t vendor_id;             /**< USB Vendor ID */
    uint16_t product_id;            /**< USB Product ID */
    bool enable_midi;               /**< Enable MIDI interface */
    bool enable_cdc;                /**< Enable CDC/ACM interface */
    bool enable_wifi_bridge;        /**< Enable Wi-Fi bridge interface */
} usb_composite_config_t;

/**
 * @brief USB composite device state
 */
typedef enum {
    USB_COMPOSITE_STATE_NOT_INIT = 0,   /**< Not initialized */
    USB_COMPOSITE_STATE_DETACHED,       /**< USB cable not connected */
    USB_COMPOSITE_STATE_ATTACHED,       /**< USB attached but not configured */
    USB_COMPOSITE_STATE_CONFIGURED,     /**< USB configured and ready */
    USB_COMPOSITE_STATE_SUSPENDED       /**< USB suspended */
} usb_composite_state_t;

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default USB composite configuration */
#define USB_COMPOSITE_CONFIG_DEFAULT {              \
    .manufacturer = "Infineon Technologies",        \
    .product = "Infineon LE Audio",                 \
    .serial_number = NULL,                          \
    .vendor_id = USB_COMPOSITE_VID,                 \
    .product_id = USB_COMPOSITE_PID,                \
    .enable_midi = true,                            \
    .enable_cdc = true,                             \
    .enable_wifi_bridge = true                      \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize USB composite device
 *
 * Initializes the USB device stack and configures all enabled
 * interfaces (MIDI, CDC, Wi-Fi Bridge). Must be called before starting USB.
 *
 * @param config Configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int usb_composite_init(const usb_composite_config_t *config);

/**
 * @brief Deinitialize USB composite device
 *
 * Stops USB and releases all resources.
 */
void usb_composite_deinit(void);

/**
 * @brief Start USB device
 *
 * Starts USB enumeration after initialization.
 *
 * @return 0 on success, negative error code on failure
 */
int usb_composite_start(void);

/**
 * @brief Stop USB device
 *
 * @return 0 on success, negative error code on failure
 */
int usb_composite_stop(void);

/**
 * @brief Get current USB state
 *
 * @return Current USB composite state
 */
usb_composite_state_t usb_composite_get_state(void);

/**
 * @brief Check if USB is configured
 *
 * @return true if USB is configured and ready
 */
bool usb_composite_is_configured(void);

/**
 * @brief Process USB events
 *
 * Should be called periodically from USB task.
 * Internally calls process functions for enabled interfaces.
 */
void usb_composite_process(void);

/**
 * @brief Get Wi-Fi bridge bulk handle
 *
 * Used by wifi_bridge module when using composite device.
 *
 * @return Wi-Fi bridge bulk handle (USB_BULK_HANDLE)
 */
int usb_composite_get_wifi_bridge_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_COMPOSITE_H */
