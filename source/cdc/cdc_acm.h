/**
 * @file cdc_acm.h
 * @brief USB CDC/ACM Virtual Serial Port Interface
 *
 * This module implements a USB CDC/ACM (Abstract Control Model) interface
 * for AT command communication with an external host processor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CDC_ACM_H
#define CDC_ACM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum line length for AT commands */
#define CDC_ACM_MAX_LINE_LEN        256

/** Default TX buffer size */
#define CDC_ACM_TX_BUFFER_SIZE      512

/** Default RX buffer size */
#define CDC_ACM_RX_BUFFER_SIZE      256

/** CDC ACM error codes */
#define CDC_ACM_OK                  0
#define CDC_ACM_ERROR              -1
#define CDC_ACM_ERROR_NOT_INIT     -2
#define CDC_ACM_ERROR_BUSY         -3
#define CDC_ACM_ERROR_NO_MEM       -4
#define CDC_ACM_ERROR_TIMEOUT      -5

/*******************************************************************************
 * Types
 ******************************************************************************/

/**
 * @brief CDC/ACM configuration
 */
typedef struct {
    uint16_t rx_buffer_size;    /**< RX buffer size (default: 256) */
    uint16_t tx_buffer_size;    /**< TX buffer size (default: 512) */
    uint8_t  rx_queue_depth;    /**< RX queue depth (default: 4) */
    uint8_t  tx_queue_depth;    /**< TX queue depth (default: 8) */
} cdc_acm_config_t;

/**
 * @brief CDC/ACM connection state
 */
typedef enum {
    CDC_ACM_STATE_NOT_INIT = 0, /**< Not initialized */
    CDC_ACM_STATE_DETACHED,     /**< USB not connected */
    CDC_ACM_STATE_ATTACHED,     /**< USB connected, not configured */
    CDC_ACM_STATE_READY,        /**< Ready for communication */
    CDC_ACM_STATE_SUSPENDED     /**< USB suspended */
} cdc_acm_state_t;

/**
 * @brief CDC/ACM statistics
 */
typedef struct {
    uint32_t bytes_sent;        /**< Total bytes sent */
    uint32_t bytes_received;    /**< Total bytes received */
    uint32_t lines_received;    /**< Total lines (commands) received */
    uint32_t tx_errors;         /**< Transmission errors */
    uint32_t rx_errors;         /**< Reception errors */
    uint32_t buffer_overflows;  /**< Buffer overflow count */
} cdc_acm_stats_t;

/**
 * @brief Line received callback function type
 * @param line Null-terminated line (without CR/LF)
 * @param user_data User data passed during registration
 */
typedef void (*cdc_acm_line_callback_t)(const char *line, void *user_data);

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default CDC/ACM configuration */
#define CDC_ACM_CONFIG_DEFAULT {    \
    .rx_buffer_size = 256,          \
    .tx_buffer_size = 512,          \
    .rx_queue_depth = 4,            \
    .tx_queue_depth = 8             \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize CDC/ACM interface
 *
 * @param config Pointer to configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int cdc_acm_init(const cdc_acm_config_t *config);

/**
 * @brief Deinitialize CDC/ACM interface
 */
void cdc_acm_deinit(void);

/**
 * @brief Process CDC events (call from task loop)
 *
 * This should be called periodically to process USB events
 * and handle incoming data.
 */
void cdc_acm_process(void);

/**
 * @brief Register line received callback
 *
 * @param callback Function to call when a complete line is received
 * @param user_data User data passed to callback
 */
void cdc_acm_register_callback(cdc_acm_line_callback_t callback, void *user_data);

/**
 * @brief Check if host is connected
 *
 * @return true if CDC port is open and ready
 */
bool cdc_acm_is_connected(void);

/**
 * @brief Get current state
 *
 * @return Current CDC/ACM state
 */
cdc_acm_state_t cdc_acm_get_state(void);

/**
 * @brief Read data from host (non-blocking)
 *
 * @param buffer Buffer to store data
 * @param max_len Maximum bytes to read
 * @return Number of bytes read, 0 if none available
 */
int cdc_acm_read(char *buffer, size_t max_len);

/**
 * @brief Write data to host
 *
 * @param data Data to send
 * @param len Number of bytes
 * @return Number of bytes written, negative on error
 */
int cdc_acm_write(const char *data, size_t len);

/**
 * @brief Printf-style output to host
 *
 * @param fmt Format string
 * @param ... Arguments
 * @return Number of characters written, negative on error
 */
int cdc_acm_printf(const char *fmt, ...);

/**
 * @brief Send "OK" response
 */
void cdc_acm_send_ok(void);

/**
 * @brief Send "ERROR" response
 */
void cdc_acm_send_error(void);

/**
 * @brief Send error with code
 *
 * @param code Error code
 */
void cdc_acm_send_cme_error(int code);

/**
 * @brief Send response with data
 *
 * @param prefix Response prefix (without +)
 * @param data Response data (can be NULL)
 */
void cdc_acm_send_response(const char *prefix, const char *data);

/**
 * @brief Send unsolicited result code (async event)
 *
 * @param event Event name (without +)
 * @param data Event data (can be NULL)
 */
void cdc_acm_send_urc(const char *event, const char *data);

/**
 * @brief Enable/disable local echo
 *
 * @param enabled true to echo characters back to host
 */
void cdc_acm_set_echo(bool enabled);

/**
 * @brief Check if echo is enabled
 *
 * @return true if echo is enabled
 */
bool cdc_acm_get_echo(void);

/**
 * @brief Get statistics
 *
 * @param stats Pointer to statistics structure to fill
 */
void cdc_acm_get_stats(cdc_acm_stats_t *stats);

/**
 * @brief Reset statistics
 */
void cdc_acm_reset_stats(void);

/*******************************************************************************
 * USB Composite Device Support
 ******************************************************************************/

/**
 * @brief Set CDC handle from USB composite init
 *
 * Called from usb_composite_init() after CDC endpoints are configured.
 * Note: USB_CDC_HANDLE is defined as 'int' in USB_CDC.h
 *
 * @param handle CDC handle from USBD_CDC_Add()
 */
void cdc_acm_set_handle(int handle);

/**
 * @brief Get CDC init data for USB composite setup
 *
 * @param init_data Pointer to init data structure to fill
 */
void cdc_acm_get_init_data(void *init_data);

#ifdef __cplusplus
}
#endif

#endif /* CDC_ACM_H */
