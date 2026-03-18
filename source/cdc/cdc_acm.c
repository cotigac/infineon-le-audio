/**
 * @file cdc_acm.c
 * @brief USB CDC/ACM Virtual Serial Port Implementation
 *
 * This module implements a USB CDC/ACM interface using the
 * Segger emUSB-Device middleware for AT command communication.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cdc_acm.h"
#include "at_parser.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Segger emUSB-Device middleware headers */
#include "USB.h"
#include "USB_CDC.h"

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** USB endpoint addresses for CDC */
#define CDC_EP_IN               0x82    /**< Bulk IN endpoint */
#define CDC_EP_OUT              0x02    /**< Bulk OUT endpoint */
#define CDC_EP_INT              0x83    /**< Interrupt IN endpoint */

/** USB endpoint buffer size */
#define CDC_EP_BUFFER_SIZE      64

/** Line buffer for AT command assembly */
#define CDC_LINE_BUFFER_SIZE    256

/** TX buffer for formatted output */
#define CDC_TX_BUFFER_SIZE      512

/** Read timeout (ms) */
#define CDC_READ_TIMEOUT        10

/** Write timeout (ms) */
#define CDC_WRITE_TIMEOUT       100

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief CDC/ACM context
 */
typedef struct {
    /* State */
    volatile bool initialized;
    volatile cdc_acm_state_t state;

    /* Configuration */
    cdc_acm_config_t config;

    /* Endpoint buffers */
    uint8_t ep_in_buffer[CDC_EP_BUFFER_SIZE];
    uint8_t ep_out_buffer[CDC_EP_BUFFER_SIZE];
    uint8_t ep_int_buffer[8];

    /* Line buffer for command assembly */
    char line_buffer[CDC_LINE_BUFFER_SIZE];
    uint16_t line_pos;

    /* TX buffer for formatted output */
    char tx_buffer[CDC_TX_BUFFER_SIZE];

    /* Echo mode */
    bool echo_enabled;

    /* Callback */
    cdc_acm_line_callback_t line_callback;
    void *callback_user_data;

    /* Statistics */
    cdc_acm_stats_t stats;

    /* Synchronization */
    SemaphoreHandle_t tx_mutex;

    /* emUSB-Device CDC handle */
    USB_CDC_HANDLE cdc_handle;

    /* Line coding (informational) */
    USB_CDC_LINE_CODING line_coding;

} cdc_acm_ctx_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Global CDC/ACM context */
static cdc_acm_ctx_t g_cdc_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static void on_line_coding(USB_CDC_LINE_CODING *pLineCoding);
static void on_control_line_state(USB_CDC_CONTROL_LINE_STATE *pLineState);
static void process_rx_char(char c);
static void process_line(void);

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int cdc_acm_init(const cdc_acm_config_t *config)
{
    if (g_cdc_ctx.initialized) {
        return CDC_ACM_ERROR;
    }

    /* Clear context */
    memset(&g_cdc_ctx, 0, sizeof(g_cdc_ctx));

    /* Apply configuration */
    if (config != NULL) {
        g_cdc_ctx.config = *config;
    } else {
        cdc_acm_config_t defaults = CDC_ACM_CONFIG_DEFAULT;
        g_cdc_ctx.config = defaults;
    }

    /* Default settings */
    g_cdc_ctx.echo_enabled = true;
    g_cdc_ctx.line_coding.DTERate = 115200;
    g_cdc_ctx.line_coding.CharFormat = 0;  /* 1 stop bit */
    g_cdc_ctx.line_coding.ParityType = 0;  /* No parity */
    g_cdc_ctx.line_coding.DataBits = 8;

    /* Create TX mutex */
    g_cdc_ctx.tx_mutex = xSemaphoreCreateMutex();
    if (g_cdc_ctx.tx_mutex == NULL) {
        return CDC_ACM_ERROR_NO_MEM;
    }

    /* Note: USB device initialization and CDC endpoint setup is done
     * in the composite USB init function (usb_composite_init).
     * This function is called after USB is already initialized. */

    g_cdc_ctx.state = CDC_ACM_STATE_DETACHED;
    g_cdc_ctx.initialized = true;

    return CDC_ACM_OK;
}

void cdc_acm_deinit(void)
{
    if (!g_cdc_ctx.initialized) {
        return;
    }

    /* Delete mutex */
    if (g_cdc_ctx.tx_mutex != NULL) {
        vSemaphoreDelete(g_cdc_ctx.tx_mutex);
        g_cdc_ctx.tx_mutex = NULL;
    }

    g_cdc_ctx.initialized = false;
    g_cdc_ctx.state = CDC_ACM_STATE_NOT_INIT;
}

void cdc_acm_process(void)
{
    if (!g_cdc_ctx.initialized) {
        return;
    }

    /* Check USB state */
    unsigned usb_state = USBD_GetState();

    if (usb_state & USB_STAT_CONFIGURED) {
        if (g_cdc_ctx.state != CDC_ACM_STATE_READY) {
            g_cdc_ctx.state = CDC_ACM_STATE_READY;
        }
    } else if (usb_state & USB_STAT_ATTACHED) {
        g_cdc_ctx.state = CDC_ACM_STATE_ATTACHED;
    } else if (usb_state & USB_STAT_SUSPENDED) {
        g_cdc_ctx.state = CDC_ACM_STATE_SUSPENDED;
    } else {
        g_cdc_ctx.state = CDC_ACM_STATE_DETACHED;
        return;
    }

    /* Only process if ready */
    if (g_cdc_ctx.state != CDC_ACM_STATE_READY) {
        return;
    }

    /* Read available data */
    uint8_t rx_buf[64];
    int rx_len = USBD_CDC_Receive(g_cdc_ctx.cdc_handle, rx_buf, sizeof(rx_buf), CDC_READ_TIMEOUT);

    if (rx_len > 0) {
        g_cdc_ctx.stats.bytes_received += rx_len;

        /* Process each character */
        for (int i = 0; i < rx_len; i++) {
            process_rx_char((char)rx_buf[i]);
        }
    }
}

void cdc_acm_register_callback(cdc_acm_line_callback_t callback, void *user_data)
{
    g_cdc_ctx.line_callback = callback;
    g_cdc_ctx.callback_user_data = user_data;
}

bool cdc_acm_is_connected(void)
{
    return g_cdc_ctx.state == CDC_ACM_STATE_READY;
}

cdc_acm_state_t cdc_acm_get_state(void)
{
    return g_cdc_ctx.state;
}

int cdc_acm_read(char *buffer, size_t max_len)
{
    if (!g_cdc_ctx.initialized || g_cdc_ctx.state != CDC_ACM_STATE_READY) {
        return 0;
    }

    int len = USBD_CDC_Receive(g_cdc_ctx.cdc_handle, buffer, max_len, 0);
    if (len > 0) {
        g_cdc_ctx.stats.bytes_received += len;
    }
    return len > 0 ? len : 0;
}

int cdc_acm_write(const char *data, size_t len)
{
    if (!g_cdc_ctx.initialized || g_cdc_ctx.state != CDC_ACM_STATE_READY) {
        return CDC_ACM_ERROR_NOT_INIT;
    }

    if (data == NULL || len == 0) {
        return 0;
    }

    /* Take TX mutex */
    if (xSemaphoreTake(g_cdc_ctx.tx_mutex, pdMS_TO_TICKS(CDC_WRITE_TIMEOUT)) != pdTRUE) {
        return CDC_ACM_ERROR_BUSY;
    }

    int written = USBD_CDC_Write(g_cdc_ctx.cdc_handle, data, len, CDC_WRITE_TIMEOUT);

    xSemaphoreGive(g_cdc_ctx.tx_mutex);

    if (written > 0) {
        g_cdc_ctx.stats.bytes_sent += written;
    } else {
        g_cdc_ctx.stats.tx_errors++;
    }

    return written;
}

int cdc_acm_printf(const char *fmt, ...)
{
    if (!g_cdc_ctx.initialized || g_cdc_ctx.state != CDC_ACM_STATE_READY) {
        return CDC_ACM_ERROR_NOT_INIT;
    }

    /* Take TX mutex */
    if (xSemaphoreTake(g_cdc_ctx.tx_mutex, pdMS_TO_TICKS(CDC_WRITE_TIMEOUT)) != pdTRUE) {
        return CDC_ACM_ERROR_BUSY;
    }

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(g_cdc_ctx.tx_buffer, sizeof(g_cdc_ctx.tx_buffer), fmt, args);
    va_end(args);

    if (len > 0) {
        if (len > (int)sizeof(g_cdc_ctx.tx_buffer)) {
            len = sizeof(g_cdc_ctx.tx_buffer);
        }

        int written = USBD_CDC_Write(g_cdc_ctx.cdc_handle, g_cdc_ctx.tx_buffer, len, CDC_WRITE_TIMEOUT);

        if (written > 0) {
            g_cdc_ctx.stats.bytes_sent += written;
        } else {
            g_cdc_ctx.stats.tx_errors++;
            len = written;
        }
    }

    xSemaphoreGive(g_cdc_ctx.tx_mutex);

    return len;
}

void cdc_acm_send_ok(void)
{
    cdc_acm_write("\r\nOK\r\n", 6);
}

void cdc_acm_send_error(void)
{
    cdc_acm_write("\r\nERROR\r\n", 9);
}

void cdc_acm_send_cme_error(int code)
{
    cdc_acm_printf("\r\n+CME ERROR: %d\r\n", code);
}

void cdc_acm_send_response(const char *prefix, const char *data)
{
    if (data != NULL && data[0] != '\0') {
        cdc_acm_printf("\r\n+%s: %s\r\n", prefix, data);
    } else {
        cdc_acm_printf("\r\n+%s\r\n", prefix);
    }
}

void cdc_acm_send_urc(const char *event, const char *data)
{
    if (data != NULL && data[0] != '\0') {
        cdc_acm_printf("\r\n+%s: %s\r\n", event, data);
    } else {
        cdc_acm_printf("\r\n+%s\r\n", event);
    }
}

void cdc_acm_set_echo(bool enabled)
{
    g_cdc_ctx.echo_enabled = enabled;
}

bool cdc_acm_get_echo(void)
{
    return g_cdc_ctx.echo_enabled;
}

void cdc_acm_get_stats(cdc_acm_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_cdc_ctx.stats;
    }
}

void cdc_acm_reset_stats(void)
{
    memset(&g_cdc_ctx.stats, 0, sizeof(g_cdc_ctx.stats));
}

/*******************************************************************************
 * USB Composite Device Setup
 ******************************************************************************/

/**
 * @brief Set the CDC handle after USB composite init
 *
 * Called from usb_composite_init() after CDC endpoints are configured.
 */
void cdc_acm_set_handle(USB_CDC_HANDLE handle)
{
    g_cdc_ctx.cdc_handle = handle;

    /* Set callbacks */
    USBD_CDC_SetOnLineCoding(handle, on_line_coding);
    USBD_CDC_SetOnControlLineState(handle, on_control_line_state);
}

/**
 * @brief Get CDC init data for USB composite setup
 */
void cdc_acm_get_init_data(void *init_data)
{
    USB_CDC_INIT_DATA *cdc_init = (USB_CDC_INIT_DATA *)init_data;
    if (cdc_init != NULL) {
        cdc_init->EPIn = CDC_EP_IN;
        cdc_init->EPOut = CDC_EP_OUT;
        cdc_init->EPInt = CDC_EP_INT;
    }
}

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief Handle line coding change from host
 */
static void on_line_coding(USB_CDC_LINE_CODING *pLineCoding)
{
    if (pLineCoding != NULL) {
        g_cdc_ctx.line_coding = *pLineCoding;
    }
}

/**
 * @brief Handle control line state change from host
 */
static void on_control_line_state(USB_CDC_CONTROL_LINE_STATE *pLineState)
{
    (void)pLineState;
    /* DTR/RTS changes - could be used to detect terminal open/close */
}

/**
 * @brief Process a received character
 */
static void process_rx_char(char c)
{
    /* Echo if enabled */
    if (g_cdc_ctx.echo_enabled) {
        cdc_acm_write(&c, 1);
    }

    /* Handle special characters */
    if (c == '\r' || c == '\n') {
        /* End of line - process command */
        if (g_cdc_ctx.line_pos > 0) {
            process_line();
        }
        return;
    }

    /* Handle backspace */
    if (c == '\b' || c == 0x7F) {
        if (g_cdc_ctx.line_pos > 0) {
            g_cdc_ctx.line_pos--;
            /* Echo backspace sequence if enabled */
            if (g_cdc_ctx.echo_enabled) {
                cdc_acm_write("\b \b", 3);
            }
        }
        return;
    }

    /* Ignore other control characters */
    if (c < 0x20) {
        return;
    }

    /* Add to line buffer */
    if (g_cdc_ctx.line_pos < CDC_LINE_BUFFER_SIZE - 1) {
        g_cdc_ctx.line_buffer[g_cdc_ctx.line_pos++] = c;
    } else {
        /* Buffer overflow */
        g_cdc_ctx.stats.buffer_overflows++;
    }
}

/**
 * @brief Process a complete line
 */
static void process_line(void)
{
    /* Null-terminate */
    g_cdc_ctx.line_buffer[g_cdc_ctx.line_pos] = '\0';

    /* Update stats */
    g_cdc_ctx.stats.lines_received++;

    /* Call callback if registered */
    if (g_cdc_ctx.line_callback != NULL) {
        g_cdc_ctx.line_callback(g_cdc_ctx.line_buffer, g_cdc_ctx.callback_user_data);
    } else {
        /* Default: pass to AT parser */
        at_parser_process_line(g_cdc_ctx.line_buffer);
    }

    /* Reset line buffer */
    g_cdc_ctx.line_pos = 0;
}
