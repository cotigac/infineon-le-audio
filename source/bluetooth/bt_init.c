/**
 * @file bt_init.c
 * @brief Bluetooth Stack Initialization Implementation
 *
 * Implements BTSTACK initialization, HCI transport setup, and
 * CYW55511 controller management for LE Audio applications.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bt_init.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Platform Includes (Infineon BTSTACK)
 ******************************************************************************/

/*
 * TODO: Include Infineon BTSTACK headers when integrating with real hardware
 *
 * #include "wiced_bt_stack.h"
 * #include "wiced_bt_dev.h"
 * #include "wiced_bt_ble.h"
 * #include "wiced_bt_gatt.h"
 * #include "wiced_bt_cfg.h"
 * #include "wiced_bt_isoc.h"
 * #include "wiced_hal_nvram.h"
 * #include "wiced_transport.h"
 * #include "cyhal.h"
 * #include "cybsp.h"
 */

/* FreeRTOS */
#ifdef FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#else
/* Stubs for non-FreeRTOS builds */
typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFF
#endif

/*******************************************************************************
 * Constants
 ******************************************************************************/

/** BT task stack size (words) */
#define BT_TASK_STACK_SIZE      (4096)

/** BT task priority */
#define BT_TASK_PRIORITY        (4)

/** Maximum pending events in queue */
#define BT_EVENT_QUEUE_SIZE     (16)

/** HCI reset timeout (ms) */
#define HCI_RESET_TIMEOUT_MS    (5000)

/** Firmware download timeout (ms) */
#define FW_DOWNLOAD_TIMEOUT_MS  (30000)

/** Infineon manufacturer ID */
#define MANUFACTURER_INFINEON   (0x0009)

/** HCI opcodes */
#define HCI_RESET               0x0C03
#define HCI_READ_LOCAL_VERSION  0x1001
#define HCI_READ_BD_ADDR        0x1009
#define HCI_LE_SET_ADV_PARAMS   0x2006
#define HCI_LE_SET_ADV_DATA     0x2008
#define HCI_LE_SET_ADV_ENABLE   0x200A
#define HCI_LE_READ_LOCAL_FEAT  0x2003

/** Advertising data types */
#define AD_TYPE_FLAGS           0x01
#define AD_TYPE_COMPLETE_NAME   0x09
#define AD_TYPE_TX_POWER        0x0A
#define AD_TYPE_SERVICE_UUID_16 0x03

/** Default advertising flags */
#define ADV_FLAGS_LE_LIMITED    0x01
#define ADV_FLAGS_LE_GENERAL    0x02
#define ADV_FLAGS_BR_EDR_NOT    0x04

/*******************************************************************************
 * Types
 ******************************************************************************/

/** Internal stack state */
typedef struct {
    bool initialized;
    bt_state_t state;
    bt_config_t config;
    bt_controller_info_t controller_info;
    bt_stats_t stats;
    bt_power_mode_t power_mode;

    /* Callback */
    bt_event_callback_t event_callback;
    void *callback_user_data;

    /* Active connections */
    bt_connection_info_t connections[8];
    uint8_t num_connections;

    /* Advertising state */
    bool advertising;
    bool connectable_adv;
    uint16_t adv_interval;

    /* Advertising data */
    uint8_t adv_data[31];
    uint8_t adv_data_len;
    uint8_t scan_rsp_data[31];
    uint8_t scan_rsp_len;

    /* FreeRTOS handles */
    SemaphoreHandle_t init_semaphore;
    SemaphoreHandle_t cmd_semaphore;
    QueueHandle_t event_queue;

    /* HCI command response */
    volatile bool cmd_complete;
    volatile int cmd_status;
    uint8_t cmd_response[256];
    uint16_t cmd_response_len;

} bt_context_t;

/*******************************************************************************
 * Module Variables
 ******************************************************************************/

static bt_context_t bt_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static int hci_transport_init(const bt_hci_config_t *config);
static void hci_transport_deinit(void);
static int hci_send_command(uint16_t opcode, const uint8_t *params, uint8_t len);
static int hci_wait_command_complete(uint32_t timeout_ms);
static int controller_init(void);
static int controller_read_local_info(void);
static int controller_configure_le_features(uint16_t features);
static int firmware_download(const char *path);
static void dispatch_event(const bt_event_t *event);
static void set_state(bt_state_t new_state);
static int build_default_adv_data(void);
static void hci_event_handler(uint8_t *event, uint16_t len);
static void process_connection_complete(const uint8_t *data);
static void process_disconnection_complete(const uint8_t *data);
static void process_le_meta_event(const uint8_t *data, uint16_t len);

/*******************************************************************************
 * HCI Transport (Platform Abstraction)
 ******************************************************************************/

/**
 * @brief Initialize HCI UART transport
 */
static int hci_transport_init(const bt_hci_config_t *config)
{
    /*
     * TODO: Implement for Infineon PSoC Edge + CYW55511
     *
     * This should:
     * 1. Configure UART peripheral for HCI
     * 2. Set baud rate (typically 3 Mbps for CYW55511)
     * 3. Enable hardware flow control (CTS/RTS)
     * 4. Configure DMA for efficient transfer
     * 5. Register HCI event callback
     *
     * Example with Infineon PDL:
     *
     * cy_stc_scb_uart_config_t uart_config = {
     *     .uartMode = CY_SCB_UART_STANDARD,
     *     .oversample = 8,
     *     .enableMsbFirst = false,
     *     .dataWidth = 8,
     *     .parity = CY_SCB_UART_PARITY_NONE,
     *     .stopBits = CY_SCB_UART_STOP_BITS_1,
     *     .enableInputFilter = false,
     *     .dropOnParityError = false,
     *     .dropOnFrameError = false,
     *     .enableCts = true,
     *     .ctsPolarity = CY_SCB_UART_ACTIVE_LOW,
     *     .rtsRxFifoLevel = 0,
     *     .rtsPolarity = CY_SCB_UART_ACTIVE_LOW
     * };
     *
     * Cy_SCB_UART_Init(HCI_UART_HW, &uart_config, &hci_uart_context);
     * Cy_SCB_UART_Enable(HCI_UART_HW);
     *
     * // Set baud rate
     * Cy_SysClk_PeriphSetDivider(...);
     *
     * // Enable interrupts
     * Cy_SysInt_Init(&hci_uart_isr_cfg, hci_uart_isr);
     * NVIC_EnableIRQ(hci_uart_isr_cfg.intrSrc);
     */

    (void)config; /* Unused in stub */

    /* Simulate successful initialization */
    return BT_OK;
}

/**
 * @brief Deinitialize HCI transport
 */
static void hci_transport_deinit(void)
{
    /*
     * TODO: Disable UART, release resources
     *
     * Cy_SCB_UART_Disable(HCI_UART_HW);
     * NVIC_DisableIRQ(hci_uart_isr_cfg.intrSrc);
     */
}

/**
 * @brief Send HCI command
 */
static int hci_send_command(uint16_t opcode, const uint8_t *params, uint8_t len)
{
    /*
     * TODO: Send HCI command packet
     *
     * HCI Command Packet format:
     * - Packet type: 0x01
     * - Opcode: 2 bytes (little endian)
     * - Parameter length: 1 byte
     * - Parameters: variable
     *
     * uint8_t packet[256];
     * packet[0] = 0x01;  // HCI command
     * packet[1] = opcode & 0xFF;
     * packet[2] = (opcode >> 8) & 0xFF;
     * packet[3] = len;
     * memcpy(&packet[4], params, len);
     *
     * return hci_uart_send(packet, 4 + len);
     */

    (void)opcode;
    (void)params;
    (void)len;

    bt_ctx.cmd_complete = false;

    /* Simulate command sent */
    return BT_OK;
}

/**
 * @brief Wait for HCI command complete event
 */
static int hci_wait_command_complete(uint32_t timeout_ms)
{
    /*
     * TODO: Wait for command complete using semaphore
     *
     * if (xSemaphoreTake(bt_ctx.cmd_semaphore, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
     *     return BT_ERROR_TIMEOUT;
     * }
     * return bt_ctx.cmd_status;
     */

    (void)timeout_ms;

    /* Simulate success */
    bt_ctx.cmd_complete = true;
    bt_ctx.cmd_status = BT_OK;

    return BT_OK;
}

/*******************************************************************************
 * Controller Management
 ******************************************************************************/

/**
 * @brief Initialize the controller
 */
static int controller_init(void)
{
    int result;

    /* Send HCI Reset */
    result = hci_send_command(HCI_RESET, NULL, 0);
    if (result != BT_OK) {
        return result;
    }

    result = hci_wait_command_complete(HCI_RESET_TIMEOUT_MS);
    if (result != BT_OK) {
        return BT_ERROR_CONTROLLER_INIT;
    }

    /* Read local version info */
    result = controller_read_local_info();
    if (result != BT_OK) {
        return result;
    }

    /* Configure LE features */
    result = controller_configure_le_features(bt_ctx.config.le_features);
    if (result != BT_OK) {
        return result;
    }

    return BT_OK;
}

/**
 * @brief Read controller local information
 */
static int controller_read_local_info(void)
{
    /*
     * TODO: Read HCI version, BD_ADDR, supported features
     *
     * // Read Local Version
     * hci_send_command(HCI_READ_LOCAL_VERSION, NULL, 0);
     * hci_wait_command_complete(1000);
     * // Parse response into bt_ctx.controller_info
     *
     * // Read BD_ADDR
     * hci_send_command(HCI_READ_BD_ADDR, NULL, 0);
     * hci_wait_command_complete(1000);
     *
     * // Read LE features
     * hci_send_command(HCI_LE_READ_LOCAL_FEAT, NULL, 0);
     * hci_wait_command_complete(1000);
     */

    /* Populate with simulated CYW55512 info */
    bt_ctx.controller_info.type = BT_CONTROLLER_CYW55511;  /* TODO: Add BT_CONTROLLER_CYW55512 enum */
    bt_ctx.controller_info.hci_version = 0x0F;  /* Bluetooth 6.0 */
    bt_ctx.controller_info.hci_revision = 0x0001;
    bt_ctx.controller_info.lmp_version = 0x0F;
    bt_ctx.controller_info.manufacturer = MANUFACTURER_INFINEON;
    bt_ctx.controller_info.lmp_subversion = 0x0001;

    /* Generate random-ish BD_ADDR for simulation */
    bt_ctx.controller_info.bd_addr[0] = 0x00;
    bt_ctx.controller_info.bd_addr[1] = 0xA0;
    bt_ctx.controller_info.bd_addr[2] = 0x50;
    bt_ctx.controller_info.bd_addr[3] = 0x00;
    bt_ctx.controller_info.bd_addr[4] = 0x00;
    bt_ctx.controller_info.bd_addr[5] = 0x01;

    snprintf(bt_ctx.controller_info.fw_version,
             sizeof(bt_ctx.controller_info.fw_version),
             "CYW55511 v1.0.0");

    /* LE Audio capabilities */
    bt_ctx.controller_info.le_audio_supported = true;
    bt_ctx.controller_info.isoc_supported = true;
    bt_ctx.controller_info.max_cig = 2;
    bt_ctx.controller_info.max_cis_per_cig = 4;
    bt_ctx.controller_info.max_big = 2;
    bt_ctx.controller_info.max_bis_per_big = 4;

    return BT_OK;
}

/**
 * @brief Configure LE features on controller
 */
static int controller_configure_le_features(uint16_t features)
{
    /*
     * TODO: Configure LE features via HCI commands
     *
     * This would typically include:
     * - LE Set Host Supported Features (for ISOC, etc.)
     * - Vendor-specific commands for CYW55511
     *
     * Example:
     * uint8_t params[8] = {0};
     * if (features & BT_LE_FEATURE_ISOC) {
     *     params[0] |= 0x20;  // ISOC bit
     * }
     * hci_send_command(HCI_LE_SET_HOST_FEAT, params, 8);
     */

    (void)features;

    return BT_OK;
}

/**
 * @brief Download firmware to controller
 */
static int firmware_download(const char *path)
{
    /*
     * TODO: Implement firmware download for CYW55511
     *
     * The CYW55511 requires firmware download over HCI at startup.
     * Infineon provides firmware files (.hcd format) and download utilities.
     *
     * Steps:
     * 1. Put controller in download mode
     * 2. Send firmware chunks via vendor HCI commands
     * 3. Verify firmware CRC
     * 4. Launch firmware
     *
     * With Infineon BTSTACK:
     * wiced_bt_stack_init(bt_management_callback, &bt_cfg_settings);
     *
     * The stack handles firmware download internally.
     */

    if (path != NULL) {
        /* Would read firmware from file */
        (void)path;
    }

    /* Simulate firmware download success */
    return BT_OK;
}

/*******************************************************************************
 * Event Handling
 ******************************************************************************/

/**
 * @brief Dispatch event to registered callback
 */
static void dispatch_event(const bt_event_t *event)
{
    if (bt_ctx.event_callback != NULL) {
        bt_ctx.event_callback(event, bt_ctx.callback_user_data);
    }
}

/**
 * @brief Set new state and dispatch event
 */
static void set_state(bt_state_t new_state)
{
    if (bt_ctx.state != new_state) {
        bt_ctx.state = new_state;

        bt_event_t event = {
            .type = BT_EVENT_STATE_CHANGED,
            .data.new_state = new_state
        };
        dispatch_event(&event);
    }
}

/**
 * @brief HCI event handler (called from HCI transport ISR/task)
 */
static void hci_event_handler(uint8_t *event, uint16_t len)
{
    if (len < 2) {
        return;
    }

    uint8_t event_code = event[0];
    uint8_t param_len = event[1];

    (void)param_len;

    switch (event_code) {
        case 0x0E:  /* Command Complete */
            bt_ctx.cmd_complete = true;
            bt_ctx.cmd_status = BT_OK;
            if (len > 6) {
                bt_ctx.cmd_response_len = len - 6;
                memcpy(bt_ctx.cmd_response, &event[6], bt_ctx.cmd_response_len);
            }
#ifdef FREERTOS
            if (bt_ctx.cmd_semaphore != NULL) {
                xSemaphoreGive(bt_ctx.cmd_semaphore);
            }
#endif
            break;

        case 0x0F:  /* Command Status */
            bt_ctx.cmd_complete = true;
            bt_ctx.cmd_status = (event[2] == 0) ? BT_OK : -event[2];
#ifdef FREERTOS
            if (bt_ctx.cmd_semaphore != NULL) {
                xSemaphoreGive(bt_ctx.cmd_semaphore);
            }
#endif
            break;

        case 0x03:  /* Connection Complete */
            process_connection_complete(&event[2]);
            break;

        case 0x05:  /* Disconnection Complete */
            process_disconnection_complete(&event[2]);
            break;

        case 0x3E:  /* LE Meta Event */
            process_le_meta_event(&event[2], len - 2);
            break;

        default:
            /* Other events */
            break;
    }

    bt_ctx.stats.rx_packets++;
    bt_ctx.stats.rx_bytes += len;
}

/**
 * @brief Process connection complete event
 */
static void process_connection_complete(const uint8_t *data)
{
    uint8_t status = data[0];

    if (status != 0) {
        return;
    }

    uint16_t conn_handle = data[1] | (data[2] << 8);

    /* Add to connection list */
    if (bt_ctx.num_connections < 8) {
        bt_connection_info_t *conn = &bt_ctx.connections[bt_ctx.num_connections];
        conn->conn_handle = conn_handle;
        memcpy(conn->peer_addr, &data[5], BT_ADDR_SIZE);
        bt_ctx.num_connections++;

        bt_ctx.stats.connections++;
        set_state(BT_STATE_CONNECTED);

        /* Dispatch event */
        bt_event_t event = {
            .type = BT_EVENT_CONNECTION_UP,
            .data.connection = *conn
        };
        dispatch_event(&event);
    }
}

/**
 * @brief Process disconnection complete event
 */
static void process_disconnection_complete(const uint8_t *data)
{
    uint8_t status = data[0];

    if (status != 0) {
        return;
    }

    uint16_t conn_handle = data[1] | (data[2] << 8);

    /* Remove from connection list */
    for (int i = 0; i < bt_ctx.num_connections; i++) {
        if (bt_ctx.connections[i].conn_handle == conn_handle) {
            /* Shift remaining connections */
            for (int j = i; j < bt_ctx.num_connections - 1; j++) {
                bt_ctx.connections[j] = bt_ctx.connections[j + 1];
            }
            bt_ctx.num_connections--;
            break;
        }
    }

    bt_ctx.stats.disconnections++;

    /* Update state */
    if (bt_ctx.num_connections == 0) {
        if (bt_ctx.advertising) {
            set_state(BT_STATE_ADVERTISING);
        } else {
            set_state(BT_STATE_READY);
        }
    }

    /* Dispatch event */
    bt_event_t event = {
        .type = BT_EVENT_CONNECTION_DOWN,
        .data.conn_handle = conn_handle
    };
    dispatch_event(&event);
}

/**
 * @brief Process LE Meta Event
 */
static void process_le_meta_event(const uint8_t *data, uint16_t len)
{
    if (len < 1) {
        return;
    }

    uint8_t subevent = data[0];

    switch (subevent) {
        case 0x01:  /* LE Connection Complete */
            /* Similar to connection complete but with LE-specific fields */
            if (data[1] == 0) {  /* Success */
                uint16_t conn_handle = data[2] | (data[3] << 8);

                if (bt_ctx.num_connections < 8) {
                    bt_connection_info_t *conn = &bt_ctx.connections[bt_ctx.num_connections];
                    conn->conn_handle = conn_handle;
                    conn->peer_addr_type = data[5];
                    memcpy(conn->peer_addr, &data[6], BT_ADDR_SIZE);
                    conn->conn_interval = data[12] | (data[13] << 8);
                    conn->conn_latency = data[14] | (data[15] << 8);
                    conn->supervision_timeout = data[16] | (data[17] << 8);
                    bt_ctx.num_connections++;

                    bt_ctx.stats.connections++;
                    set_state(BT_STATE_CONNECTED);

                    bt_event_t event = {
                        .type = BT_EVENT_CONNECTION_UP,
                        .data.connection = *conn
                    };
                    dispatch_event(&event);
                }
            }
            break;

        case 0x03:  /* LE Connection Update Complete */
            {
                uint16_t conn_handle = data[2] | (data[3] << 8);

                /* Update connection parameters */
                for (int i = 0; i < bt_ctx.num_connections; i++) {
                    if (bt_ctx.connections[i].conn_handle == conn_handle) {
                        bt_ctx.connections[i].conn_interval = data[4] | (data[5] << 8);
                        bt_ctx.connections[i].conn_latency = data[6] | (data[7] << 8);
                        bt_ctx.connections[i].supervision_timeout = data[8] | (data[9] << 8);
                        break;
                    }
                }
            }
            break;

        case 0x0C:  /* LE PHY Update Complete */
            {
                bt_event_t event = {
                    .type = BT_EVENT_PHY_UPDATED,
                    .data.conn_handle = data[2] | (data[3] << 8)
                };
                dispatch_event(&event);
            }
            break;

        case 0x19:  /* LE CIS Established */
            {
                bt_event_t event = {
                    .type = BT_EVENT_ISOC_ESTABLISHED
                };
                event.data.isoc.cis_handle = data[2] | (data[3] << 8);
                event.data.isoc.is_broadcast = false;
                dispatch_event(&event);

                set_state(BT_STATE_STREAMING);
            }
            break;

        case 0x1A:  /* LE CIS Request */
            /* Auto-accept CIS for now */
            /* TODO: Send LE Accept CIS Request */
            break;

        case 0x1B:  /* LE Create BIG Complete */
            {
                bt_event_t event = {
                    .type = BT_EVENT_ISOC_ESTABLISHED
                };
                event.data.isoc.big_handle = data[1];
                event.data.isoc.is_broadcast = true;
                dispatch_event(&event);

                set_state(BT_STATE_STREAMING);
            }
            break;

        case 0x1D:  /* LE Terminate BIG Complete */
            {
                bt_event_t event = {
                    .type = BT_EVENT_ISOC_TERMINATED
                };
                event.data.isoc.big_handle = data[1];
                event.data.isoc.is_broadcast = true;
                dispatch_event(&event);
            }
            break;

        default:
            /* Other LE events */
            break;
    }
}

/*******************************************************************************
 * Advertising
 ******************************************************************************/

/**
 * @brief Build default advertising data
 */
static int build_default_adv_data(void)
{
    uint8_t pos = 0;

    /* Flags */
    bt_ctx.adv_data[pos++] = 2;  /* Length */
    bt_ctx.adv_data[pos++] = AD_TYPE_FLAGS;
    bt_ctx.adv_data[pos++] = ADV_FLAGS_LE_GENERAL | ADV_FLAGS_BR_EDR_NOT;

    /* Complete Local Name */
    uint8_t name_len = (uint8_t)strlen(bt_ctx.config.device_name);
    if (name_len > 25) {
        name_len = 25;  /* Limit to fit in advertising data */
    }
    bt_ctx.adv_data[pos++] = name_len + 1;
    bt_ctx.adv_data[pos++] = AD_TYPE_COMPLETE_NAME;
    memcpy(&bt_ctx.adv_data[pos], bt_ctx.config.device_name, name_len);
    pos += name_len;

    bt_ctx.adv_data_len = pos;

    /* Scan response - TX Power */
    pos = 0;
    bt_ctx.scan_rsp_data[pos++] = 2;
    bt_ctx.scan_rsp_data[pos++] = AD_TYPE_TX_POWER;
    bt_ctx.scan_rsp_data[pos++] = 0;  /* 0 dBm */
    bt_ctx.scan_rsp_len = pos;

    return BT_OK;
}

/*******************************************************************************
 * Public API - Initialization
 ******************************************************************************/

int bt_init(const bt_config_t *config)
{
    int result;

    if (bt_ctx.initialized) {
        return BT_ERROR_ALREADY_INITIALIZED;
    }

    /* Clear context */
    memset(&bt_ctx, 0, sizeof(bt_ctx));

    /* Apply configuration */
    if (config != NULL) {
        bt_ctx.config = *config;
    } else {
        /* Use defaults */
        bt_config_t defaults = BT_CONFIG_DEFAULT;
        bt_ctx.config = defaults;
    }

    set_state(BT_STATE_INITIALIZING);

#ifdef FREERTOS
    /* Create synchronization primitives */
    bt_ctx.init_semaphore = xSemaphoreCreateBinary();
    bt_ctx.cmd_semaphore = xSemaphoreCreateBinary();
    bt_ctx.event_queue = xQueueCreate(BT_EVENT_QUEUE_SIZE, sizeof(bt_event_t));

    if (bt_ctx.init_semaphore == NULL ||
        bt_ctx.cmd_semaphore == NULL ||
        bt_ctx.event_queue == NULL) {
        bt_deinit();
        return BT_ERROR_NO_MEMORY;
    }
#endif

    /* Initialize HCI transport */
    result = hci_transport_init(&bt_ctx.config.hci);
    if (result != BT_OK) {
        bt_deinit();
        return BT_ERROR_HCI_TRANSPORT;
    }

    /* Download firmware if configured */
    if (bt_ctx.config.hci.download_firmware) {
        result = firmware_download(bt_ctx.config.hci.firmware_path);
        if (result != BT_OK) {
            bt_deinit();
            return BT_ERROR_FIRMWARE_DOWNLOAD;
        }
    }

    /* Initialize controller */
    result = controller_init();
    if (result != BT_OK) {
        bt_deinit();
        return BT_ERROR_CONTROLLER_INIT;
    }

    /* Set device address if specified */
    if (bt_ctx.config.device_addr[0] != 0 ||
        bt_ctx.config.device_addr[1] != 0 ||
        bt_ctx.config.device_addr[2] != 0) {
        result = bt_set_device_address(bt_ctx.config.device_addr,
                                        bt_ctx.config.use_random_addr);
        if (result != BT_OK) {
            /* Non-fatal, use default address */
        }
    }

    /* Build default advertising data */
    build_default_adv_data();

    bt_ctx.initialized = true;
    bt_ctx.power_mode = BT_POWER_ACTIVE;
    set_state(BT_STATE_READY);

    /* Dispatch initialization event */
    bt_event_t event = {
        .type = BT_EVENT_INITIALIZED
    };
    dispatch_event(&event);

    return BT_OK;
}

void bt_deinit(void)
{
    if (!bt_ctx.initialized) {
        return;
    }

    /* Stop advertising */
    if (bt_ctx.advertising) {
        bt_stop_advertising();
    }

    /* Disconnect all connections */
    for (int i = 0; i < bt_ctx.num_connections; i++) {
        bt_disconnect(bt_ctx.connections[i].conn_handle, 0x13);
    }

    /* Deinitialize transport */
    hci_transport_deinit();

#ifdef FREERTOS
    /* Delete FreeRTOS objects */
    if (bt_ctx.init_semaphore != NULL) {
        vSemaphoreDelete(bt_ctx.init_semaphore);
    }
    if (bt_ctx.cmd_semaphore != NULL) {
        vSemaphoreDelete(bt_ctx.cmd_semaphore);
    }
    if (bt_ctx.event_queue != NULL) {
        vQueueDelete(bt_ctx.event_queue);
    }
#endif

    set_state(BT_STATE_OFF);
    bt_ctx.initialized = false;
}

bool bt_is_initialized(void)
{
    return bt_ctx.initialized;
}

void bt_register_callback(bt_event_callback_t callback, void *user_data)
{
    bt_ctx.event_callback = callback;
    bt_ctx.callback_user_data = user_data;
}

bt_state_t bt_get_state(void)
{
    return bt_ctx.state;
}

/*******************************************************************************
 * Public API - Controller
 ******************************************************************************/

int bt_get_controller_info(bt_controller_info_t *info)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (info == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    *info = bt_ctx.controller_info;
    return BT_OK;
}

int bt_reset_controller(void)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    int result = hci_send_command(HCI_RESET, NULL, 0);
    if (result != BT_OK) {
        return result;
    }

    result = hci_wait_command_complete(HCI_RESET_TIMEOUT_MS);
    if (result != BT_OK) {
        return result;
    }

    /* Re-read controller info */
    return controller_read_local_info();
}

int bt_set_device_address(const uint8_t addr[BT_ADDR_SIZE], bool random)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (addr == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    /*
     * TODO: Set device address via HCI
     *
     * For random static address:
     * - HCI LE Set Random Address (opcode 0x2005)
     *
     * For public address:
     * - Usually vendor-specific command
     */

    if (random) {
        /* Set random static address */
        uint8_t params[6];
        memcpy(params, addr, 6);

        /* Ensure static address format (two MSBs = 11) */
        params[5] |= 0xC0;

        hci_send_command(0x2005, params, 6);  /* LE Set Random Address */
        hci_wait_command_complete(1000);
    }

    /* Update local copy */
    memcpy(bt_ctx.controller_info.bd_addr, addr, BT_ADDR_SIZE);

    return BT_OK;
}

int bt_get_device_address(uint8_t addr[BT_ADDR_SIZE], bool *random)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (addr == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    memcpy(addr, bt_ctx.controller_info.bd_addr, BT_ADDR_SIZE);

    if (random != NULL) {
        *random = bt_ctx.config.use_random_addr;
    }

    return BT_OK;
}

/*******************************************************************************
 * Public API - Advertising
 ******************************************************************************/

int bt_start_advertising(bool connectable, uint16_t interval_ms)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (bt_ctx.advertising) {
        /* Already advertising, update parameters */
        bt_stop_advertising();
    }

    /*
     * TODO: Configure and start advertising via HCI
     *
     * Steps:
     * 1. LE Set Advertising Parameters
     * 2. LE Set Advertising Data
     * 3. LE Set Scan Response Data (optional)
     * 4. LE Set Advertising Enable
     *
     * Example:
     * uint8_t adv_params[15];
     * uint16_t min_interval = (interval_ms * 1000) / 625;  // Convert to 0.625ms units
     * uint16_t max_interval = min_interval + 16;
     *
     * adv_params[0] = min_interval & 0xFF;
     * adv_params[1] = (min_interval >> 8) & 0xFF;
     * adv_params[2] = max_interval & 0xFF;
     * adv_params[3] = (max_interval >> 8) & 0xFF;
     * adv_params[4] = connectable ? 0x00 : 0x03;  // ADV_IND or ADV_NONCONN_IND
     * // ... rest of parameters
     *
     * hci_send_command(HCI_LE_SET_ADV_PARAMS, adv_params, 15);
     */

    bt_ctx.connectable_adv = connectable;
    bt_ctx.adv_interval = interval_ms;

    /* Set advertising data */
    hci_send_command(HCI_LE_SET_ADV_DATA, bt_ctx.adv_data, bt_ctx.adv_data_len);
    hci_wait_command_complete(1000);

    /* Enable advertising */
    uint8_t enable = 1;
    hci_send_command(HCI_LE_SET_ADV_ENABLE, &enable, 1);
    hci_wait_command_complete(1000);

    bt_ctx.advertising = true;
    set_state(BT_STATE_ADVERTISING);

    return BT_OK;
}

int bt_stop_advertising(void)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (!bt_ctx.advertising) {
        return BT_OK;
    }

    /* Disable advertising */
    uint8_t enable = 0;
    hci_send_command(HCI_LE_SET_ADV_ENABLE, &enable, 1);
    hci_wait_command_complete(1000);

    bt_ctx.advertising = false;

    if (bt_ctx.num_connections == 0) {
        set_state(BT_STATE_READY);
    }

    return BT_OK;
}

int bt_set_advertising_data(const uint8_t *data, uint8_t len)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || len > 31) {
        return BT_ERROR_INVALID_PARAM;
    }

    memcpy(bt_ctx.adv_data, data, len);
    bt_ctx.adv_data_len = len;

    /* Update if currently advertising */
    if (bt_ctx.advertising) {
        hci_send_command(HCI_LE_SET_ADV_DATA, bt_ctx.adv_data, bt_ctx.adv_data_len);
        hci_wait_command_complete(1000);
    }

    return BT_OK;
}

int bt_set_scan_response_data(const uint8_t *data, uint8_t len)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (data == NULL || len > 31) {
        return BT_ERROR_INVALID_PARAM;
    }

    memcpy(bt_ctx.scan_rsp_data, data, len);
    bt_ctx.scan_rsp_len = len;

    /* Update if currently advertising */
    if (bt_ctx.advertising) {
        /* LE Set Scan Response Data (opcode 0x2009) */
        hci_send_command(0x2009, bt_ctx.scan_rsp_data, bt_ctx.scan_rsp_len);
        hci_wait_command_complete(1000);
    }

    return BT_OK;
}

int bt_set_device_name(const char *name)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (name == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    strncpy(bt_ctx.config.device_name, name, BT_MAX_NAME_LEN - 1);
    bt_ctx.config.device_name[BT_MAX_NAME_LEN - 1] = '\0';

    /* Rebuild advertising data */
    build_default_adv_data();

    /* Update if currently advertising */
    if (bt_ctx.advertising) {
        hci_send_command(HCI_LE_SET_ADV_DATA, bt_ctx.adv_data, bt_ctx.adv_data_len);
        hci_wait_command_complete(1000);
    }

    return BT_OK;
}

/*******************************************************************************
 * Public API - Connection Management
 ******************************************************************************/

int bt_disconnect(uint16_t conn_handle, uint8_t reason)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    /* HCI Disconnect command */
    uint8_t params[3];
    params[0] = conn_handle & 0xFF;
    params[1] = (conn_handle >> 8) & 0xFF;
    params[2] = reason;

    int result = hci_send_command(0x0406, params, 3);  /* HCI Disconnect */
    if (result != BT_OK) {
        return result;
    }

    return hci_wait_command_complete(5000);
}

int bt_update_connection_params(uint16_t conn_handle,
                                uint16_t interval_min,
                                uint16_t interval_max,
                                uint16_t latency,
                                uint16_t timeout)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    /* LE Connection Update command */
    uint8_t params[14];
    params[0] = conn_handle & 0xFF;
    params[1] = (conn_handle >> 8) & 0xFF;
    params[2] = interval_min & 0xFF;
    params[3] = (interval_min >> 8) & 0xFF;
    params[4] = interval_max & 0xFF;
    params[5] = (interval_max >> 8) & 0xFF;
    params[6] = latency & 0xFF;
    params[7] = (latency >> 8) & 0xFF;
    params[8] = timeout & 0xFF;
    params[9] = (timeout >> 8) & 0xFF;
    params[10] = 0;  /* CE length min */
    params[11] = 0;
    params[12] = 0xFF;  /* CE length max */
    params[13] = 0xFF;

    int result = hci_send_command(0x2013, params, 14);  /* LE Connection Update */
    if (result != BT_OK) {
        return result;
    }

    return hci_wait_command_complete(5000);
}

int bt_get_connection_info(uint16_t conn_handle, bt_connection_info_t *info)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (info == NULL) {
        return BT_ERROR_INVALID_PARAM;
    }

    for (int i = 0; i < bt_ctx.num_connections; i++) {
        if (bt_ctx.connections[i].conn_handle == conn_handle) {
            *info = bt_ctx.connections[i];
            return BT_OK;
        }
    }

    return BT_ERROR_INVALID_PARAM;
}

int bt_set_phy(uint16_t conn_handle, uint8_t tx_phy, uint8_t rx_phy)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    /* LE Set PHY command */
    uint8_t params[7];
    params[0] = conn_handle & 0xFF;
    params[1] = (conn_handle >> 8) & 0xFF;
    params[2] = 0;  /* All PHYs (no preference) */
    params[3] = tx_phy;
    params[4] = rx_phy;
    params[5] = 0;  /* PHY options (no preference) */
    params[6] = 0;

    int result = hci_send_command(0x2032, params, 7);  /* LE Set PHY */
    if (result != BT_OK) {
        return result;
    }

    return hci_wait_command_complete(5000);
}

/*******************************************************************************
 * Public API - Power Management
 ******************************************************************************/

int bt_set_power_mode(bt_power_mode_t mode)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    /*
     * TODO: Implement power management for CYW55511
     *
     * This would typically use vendor-specific HCI commands to
     * configure the controller's power mode:
     * - Active: Full power, lowest latency
     * - Low latency: Light sleep between events
     * - Low power: Deeper sleep, slower wake
     * - Deep sleep: Controller mostly off
     */

    bt_ctx.power_mode = mode;

    return BT_OK;
}

bt_power_mode_t bt_get_power_mode(void)
{
    return bt_ctx.power_mode;
}

int bt_set_tx_power(int8_t power_dbm)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    /*
     * TODO: Set TX power via vendor-specific command
     *
     * Typical range: -20 to +10 dBm
     *
     * For CYW55511, use vendor-specific HCI command
     */

    (void)power_dbm;

    return BT_OK;
}

/*******************************************************************************
 * Public API - Statistics
 ******************************************************************************/

void bt_get_stats(bt_stats_t *stats)
{
    if (stats != NULL) {
        *stats = bt_ctx.stats;
    }
}

void bt_reset_stats(void)
{
    memset(&bt_ctx.stats, 0, sizeof(bt_ctx.stats));
}

/*******************************************************************************
 * Public API - ISOC Support
 ******************************************************************************/

bool bt_isoc_is_supported(void)
{
    if (!bt_ctx.initialized) {
        return false;
    }

    return bt_ctx.controller_info.isoc_supported;
}

int bt_isoc_get_capabilities(uint8_t *max_cig, uint8_t *max_cis,
                              uint8_t *max_big, uint8_t *max_bis)
{
    if (!bt_ctx.initialized) {
        return BT_ERROR_NOT_INITIALIZED;
    }

    if (max_cig != NULL) {
        *max_cig = bt_ctx.controller_info.max_cig;
    }
    if (max_cis != NULL) {
        *max_cis = bt_ctx.controller_info.max_cis_per_cig;
    }
    if (max_big != NULL) {
        *max_big = bt_ctx.controller_info.max_big;
    }
    if (max_bis != NULL) {
        *max_bis = bt_ctx.controller_info.max_bis_per_big;
    }

    return BT_OK;
}

/*******************************************************************************
 * Public API - FreeRTOS Task
 ******************************************************************************/

void bt_task(void *pvParameters)
{
    (void)pvParameters;

    /*
     * TODO: Implement main BT task loop
     *
     * This task:
     * 1. Processes HCI events from UART
     * 2. Dispatches events to registered callbacks
     * 3. Handles timer-based operations
     * 4. Manages GATT operations
     *
     * With Infineon BTSTACK:
     *
     * while (1) {
     *     wiced_bt_stack_process();
     *     vTaskDelay(pdMS_TO_TICKS(1));
     * }
     *
     * Or event-driven:
     *
     * while (1) {
     *     bt_event_t event;
     *     if (xQueueReceive(bt_ctx.event_queue, &event, portMAX_DELAY) == pdTRUE) {
     *         dispatch_event(&event);
     *     }
     * }
     */

#ifdef FREERTOS
    while (1) {
        /* Process pending HCI data */
        /* wiced_bt_stack_process(); */

        /* Yield to other tasks */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#else
    /* Non-RTOS: just return */
#endif
}

uint32_t bt_get_task_stack_size(void)
{
    return BT_TASK_STACK_SIZE;
}

uint32_t bt_get_task_priority(void)
{
    return BT_TASK_PRIORITY;
}
