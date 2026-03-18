/**
 * @file usb_composite.c
 * @brief USB Composite Device Manager Implementation
 *
 * Implements USB composite device with MIDI and CDC/ACM interfaces
 * using Segger emUSB-Device middleware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb_composite.h"

#include <string.h>

/* Segger emUSB-Device middleware headers */
#include "USB.h"
#include "USB_Bulk.h"
#include "USB_CDC.h"

/* Interface modules */
#include "../midi/midi_usb.h"
#include "../cdc/cdc_acm.h"

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** MIDI endpoint addresses */
#define MIDI_EP_IN              0x81
#define MIDI_EP_OUT             0x01

/** CDC endpoint addresses */
#define CDC_EP_IN               0x82
#define CDC_EP_OUT              0x02
#define CDC_EP_INT              0x83

/** Endpoint buffer sizes */
#define EP_BUFFER_SIZE          64

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief USB composite context
 */
typedef struct {
    bool initialized;
    usb_composite_config_t config;
    usb_composite_state_t state;

    /* Interface handles */
    USB_BULK_HANDLE midi_handle;
    USB_CDC_HANDLE cdc_handle;

    /* Endpoint buffers */
    uint8_t midi_in_buffer[EP_BUFFER_SIZE];
    uint8_t midi_out_buffer[EP_BUFFER_SIZE];
    uint8_t cdc_in_buffer[EP_BUFFER_SIZE];
    uint8_t cdc_out_buffer[EP_BUFFER_SIZE];
    uint8_t cdc_int_buffer[8];

} usb_composite_ctx_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Global USB composite context */
static usb_composite_ctx_t g_usb_ctx;

/*******************************************************************************
 * USB Device Information
 ******************************************************************************/

/** USB device info structure for emUSB-Device */
static const USB_DEVICE_INFO g_device_info = {
    .VendorId = USB_COMPOSITE_VID,
    .ProductId = USB_COMPOSITE_PID,
    .sVendorName = "Infineon Technologies",
    .sProductName = "Infineon LE Audio",
    .sSerialNumber = "0001"
};

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief Initialize MIDI interface
 */
static int init_midi_interface(void)
{
    USB_BULK_INIT_DATA init_data;

    memset(&init_data, 0, sizeof(init_data));

    /* Configure endpoints */
    init_data.EPIn = USBD_AddEP(USB_DIR_IN, USB_TRANSFER_TYPE_BULK, 0,
                                g_usb_ctx.midi_in_buffer, EP_BUFFER_SIZE);
    init_data.EPOut = USBD_AddEP(USB_DIR_OUT, USB_TRANSFER_TYPE_BULK, 0,
                                 g_usb_ctx.midi_out_buffer, EP_BUFFER_SIZE);

    /* Add MIDI (via BULK) interface */
    g_usb_ctx.midi_handle = USBD_BULK_Add(&init_data);

    if (g_usb_ctx.midi_handle == 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Initialize CDC/ACM interface
 */
static int init_cdc_interface(void)
{
    USB_CDC_INIT_DATA init_data;

    memset(&init_data, 0, sizeof(init_data));

    /* Configure endpoints */
    init_data.EPIn = USBD_AddEP(USB_DIR_IN, USB_TRANSFER_TYPE_BULK, 0,
                                g_usb_ctx.cdc_in_buffer, EP_BUFFER_SIZE);
    init_data.EPOut = USBD_AddEP(USB_DIR_OUT, USB_TRANSFER_TYPE_BULK, 0,
                                 g_usb_ctx.cdc_out_buffer, EP_BUFFER_SIZE);
    init_data.EPInt = USBD_AddEP(USB_DIR_IN, USB_TRANSFER_TYPE_INT, 10,
                                 g_usb_ctx.cdc_int_buffer, sizeof(g_usb_ctx.cdc_int_buffer));

    /* Add CDC interface */
    g_usb_ctx.cdc_handle = USBD_CDC_Add(&init_data);

    if (g_usb_ctx.cdc_handle == 0) {
        return -1;
    }

    /* Pass handle to CDC ACM module */
    cdc_acm_set_handle(g_usb_ctx.cdc_handle);

    return 0;
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int usb_composite_init(const usb_composite_config_t *config)
{
    int result;

    if (g_usb_ctx.initialized) {
        return -1;
    }

    /* Clear context */
    memset(&g_usb_ctx, 0, sizeof(g_usb_ctx));

    /* Apply configuration */
    if (config != NULL) {
        g_usb_ctx.config = *config;
    } else {
        usb_composite_config_t defaults = USB_COMPOSITE_CONFIG_DEFAULT;
        g_usb_ctx.config = defaults;
    }

    /* Initialize USB device stack */
    USBD_Init();

    /* Set device info */
    USBD_SetDeviceInfo(&g_device_info);

    /* Initialize enabled interfaces */
    if (g_usb_ctx.config.enable_midi) {
        /* Initialize MIDI module for composite mode */
        result = midi_usb_init_composite(NULL);
        if (result != 0) {
            USBD_DeInit();
            return -2;
        }

        /* Add MIDI USB interface */
        result = init_midi_interface();
        if (result != 0) {
            midi_usb_deinit();
            USBD_DeInit();
            return -3;
        }

        /* Set the handle in MIDI module */
        midi_usb_set_handle(g_usb_ctx.midi_handle);
    }

    if (g_usb_ctx.config.enable_cdc) {
        /* Initialize CDC ACM module first */
        result = cdc_acm_init(NULL);
        if (result != 0) {
            if (g_usb_ctx.config.enable_midi) {
                midi_usb_deinit();
            }
            USBD_DeInit();
            return -4;
        }

        /* Then add CDC interface */
        result = init_cdc_interface();
        if (result != 0) {
            cdc_acm_deinit();
            if (g_usb_ctx.config.enable_midi) {
                midi_usb_deinit();
            }
            USBD_DeInit();
            return -5;
        }
    }

    g_usb_ctx.state = USB_COMPOSITE_STATE_DETACHED;
    g_usb_ctx.initialized = true;

    return 0;
}

void usb_composite_deinit(void)
{
    if (!g_usb_ctx.initialized) {
        return;
    }

    /* Stop USB */
    USBD_Stop();

    /* Deinitialize CDC if enabled */
    if (g_usb_ctx.config.enable_cdc) {
        cdc_acm_deinit();
    }

    /* Deinitialize MIDI if enabled */
    if (g_usb_ctx.config.enable_midi) {
        midi_usb_deinit();
    }

    /* Deinitialize USB device stack */
    USBD_DeInit();

    g_usb_ctx.initialized = false;
    g_usb_ctx.state = USB_COMPOSITE_STATE_NOT_INIT;
}

int usb_composite_start(void)
{
    if (!g_usb_ctx.initialized) {
        return -1;
    }

    /* Start USB enumeration */
    USBD_Start();

    return 0;
}

int usb_composite_stop(void)
{
    if (!g_usb_ctx.initialized) {
        return -1;
    }

    USBD_Stop();
    g_usb_ctx.state = USB_COMPOSITE_STATE_DETACHED;

    return 0;
}

usb_composite_state_t usb_composite_get_state(void)
{
    return g_usb_ctx.state;
}

bool usb_composite_is_configured(void)
{
    return g_usb_ctx.state == USB_COMPOSITE_STATE_CONFIGURED;
}

void usb_composite_process(void)
{
    unsigned usb_state;

    if (!g_usb_ctx.initialized) {
        return;
    }

    /* Update state based on USB stack state */
    usb_state = USBD_GetState();

    if (usb_state & USB_STAT_CONFIGURED) {
        g_usb_ctx.state = USB_COMPOSITE_STATE_CONFIGURED;
    } else if (usb_state & USB_STAT_ATTACHED) {
        g_usb_ctx.state = USB_COMPOSITE_STATE_ATTACHED;
    } else if (usb_state & USB_STAT_SUSPENDED) {
        g_usb_ctx.state = USB_COMPOSITE_STATE_SUSPENDED;
    } else {
        g_usb_ctx.state = USB_COMPOSITE_STATE_DETACHED;
    }

    /* Process CDC interface */
    if (g_usb_ctx.config.enable_cdc) {
        cdc_acm_process();
    }

    /* Note: MIDI processing is done in midi_usb_process() which
     * should be called from the MIDI task separately */
}

/*******************************************************************************
 * Interface Handle Access
 ******************************************************************************/

/**
 * @brief Get MIDI bulk handle
 *
 * Used by midi_usb module when using composite device.
 *
 * @return MIDI bulk handle
 */
USB_BULK_HANDLE usb_composite_get_midi_handle(void)
{
    return g_usb_ctx.midi_handle;
}

/**
 * @brief Get CDC handle
 *
 * Used by cdc_acm module when using composite device.
 *
 * @return CDC handle
 */
USB_CDC_HANDLE usb_composite_get_cdc_handle(void)
{
    return g_usb_ctx.cdc_handle;
}
