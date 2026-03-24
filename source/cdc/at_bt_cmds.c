/**
 * @file at_bt_cmds.c
 * @brief Bluetooth AT Command Handlers Implementation
 *
 * Implements Bluetooth commands for device control via AT interface.
 *
 * Commands:
 * - AT+BTINIT      : Initialize Bluetooth stack
 * - AT+BTDEINIT    : Deinitialize Bluetooth stack
 * - AT+BTSTATE?    : Query Bluetooth state
 * - AT+BTADDR?     : Query device address
 * - AT+BTADDR=     : Set device address
 * - AT+BTNAME?     : Query device name
 * - AT+BTNAME=     : Set device name
 * - AT+BTPWR?      : Query TX power
 * - AT+BTPWR=      : Set TX power
 * - AT+BTINFO?     : Query controller info
 * - AT+GAPADVSTART : Start advertising
 * - AT+GAPADVSTOP  : Stop advertising
 * - AT+GAPSCAN=    : Start/stop scanning
 * - AT+GAPCONN=    : Connect to device
 * - AT+GAPDISCONN  : Disconnect
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "at_bt_cmds.h"
#include "at_commands.h"
#include "cdc_acm.h"

#include "bluetooth/bt_init.h"
#include "bluetooth/gap_config.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* FreeRTOS for URC queue */
#include "FreeRTOS.h"
#include "queue.h"

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** URC queue depth for async events */
#define BT_URC_QUEUE_DEPTH      16

/** Maximum URC message size */
#define BT_URC_MAX_LEN          128

/** Device name storage */
static char g_device_name[BT_MAX_NAME_LEN + 1] = "Infineon LE Audio";

/** Scanning state */
static volatile bool g_scanning = false;

/** Scan timeout (0 = indefinite) */
static uint32_t g_scan_timeout_ms = 0;

/*******************************************************************************
 * URC Queue for Async Events
 ******************************************************************************/

/** URC message structure */
typedef struct {
    char message[BT_URC_MAX_LEN];
} bt_urc_t;

/** URC queue handle */
static QueueHandle_t g_urc_queue = NULL;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

/* Command handlers */
static int cmd_btinit_exec(int argc, const char *argv[]);
static int cmd_btdeinit_exec(int argc, const char *argv[]);
static int cmd_btstate_query(int argc, const char *argv[]);
static int cmd_btaddr_query(int argc, const char *argv[]);
static int cmd_btaddr_exec(int argc, const char *argv[]);
static int cmd_btname_query(int argc, const char *argv[]);
static int cmd_btname_exec(int argc, const char *argv[]);
static int cmd_btpwr_query(int argc, const char *argv[]);
static int cmd_btpwr_exec(int argc, const char *argv[]);
static int cmd_btinfo_query(int argc, const char *argv[]);
static int cmd_gapadvstart_exec(int argc, const char *argv[]);
static int cmd_gapadvstop_exec(int argc, const char *argv[]);
static int cmd_gapscan_exec(int argc, const char *argv[]);
static int cmd_gapconn_exec(int argc, const char *argv[]);
static int cmd_gapdisconn_exec(int argc, const char *argv[]);
static int cmd_gapconnlist_query(int argc, const char *argv[]);

/* Helper functions */
static void gap_event_callback(const gap_event_t *event, void *user_data);
static void send_urc(const char *fmt, ...);
static const char *bt_state_to_string(bt_state_t state);
static int parse_mac_address(const char *str, uint8_t addr[6]);
static void format_mac_address(const uint8_t addr[6], char *str);

/*******************************************************************************
 * Command Table
 ******************************************************************************/

static const at_cmd_entry_t g_bt_cmds[] = {
    /* AT+BTINIT - Initialize Bluetooth */
    {
        .name = "BTINIT",
        .exec = cmd_btinit_exec,
        .query = NULL,
        .test = NULL,
        .help = "Initialize Bluetooth stack",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+BTDEINIT - Deinitialize Bluetooth */
    {
        .name = "BTDEINIT",
        .exec = cmd_btdeinit_exec,
        .query = NULL,
        .test = NULL,
        .help = "Deinitialize Bluetooth stack",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+BTSTATE? - Query state */
    {
        .name = "BTSTATE",
        .exec = NULL,
        .query = cmd_btstate_query,
        .test = NULL,
        .help = "Query Bluetooth state",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+BTADDR - Device address */
    {
        .name = "BTADDR",
        .exec = cmd_btaddr_exec,
        .query = cmd_btaddr_query,
        .test = NULL,
        .help = "Get/set device address",
        .min_args = 1,
        .max_args = 2
    },

    /* AT+BTNAME - Device name */
    {
        .name = "BTNAME",
        .exec = cmd_btname_exec,
        .query = cmd_btname_query,
        .test = NULL,
        .help = "Get/set device name",
        .min_args = 1,
        .max_args = 1
    },

    /* AT+BTPWR - TX power */
    {
        .name = "BTPWR",
        .exec = cmd_btpwr_exec,
        .query = cmd_btpwr_query,
        .test = NULL,
        .help = "Get/set TX power (dBm)",
        .min_args = 1,
        .max_args = 1
    },

    /* AT+BTINFO? - Controller info */
    {
        .name = "BTINFO",
        .exec = NULL,
        .query = cmd_btinfo_query,
        .test = NULL,
        .help = "Query controller info",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+GAPADVSTART - Start advertising */
    {
        .name = "GAPADVSTART",
        .exec = cmd_gapadvstart_exec,
        .query = NULL,
        .test = NULL,
        .help = "Start advertising [connectable,interval_ms]",
        .min_args = 0,
        .max_args = 2
    },

    /* AT+GAPADVSTOP - Stop advertising */
    {
        .name = "GAPADVSTOP",
        .exec = cmd_gapadvstop_exec,
        .query = NULL,
        .test = NULL,
        .help = "Stop advertising",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+GAPSCAN - Scanning */
    {
        .name = "GAPSCAN",
        .exec = cmd_gapscan_exec,
        .query = NULL,
        .test = NULL,
        .help = "Start/stop scanning (0=stop, 1=start [,timeout_ms])",
        .min_args = 1,
        .max_args = 2
    },

    /* AT+GAPCONN - Connect */
    {
        .name = "GAPCONN",
        .exec = cmd_gapconn_exec,
        .query = NULL,
        .test = NULL,
        .help = "Connect to device (addr[,addr_type])",
        .min_args = 1,
        .max_args = 2
    },

    /* AT+GAPDISCONN - Disconnect */
    {
        .name = "GAPDISCONN",
        .exec = cmd_gapdisconn_exec,
        .query = NULL,
        .test = NULL,
        .help = "Disconnect [conn_handle]",
        .min_args = 0,
        .max_args = 1
    },

    /* AT+GAPCONNLIST? - List connections */
    {
        .name = "GAPCONNLIST",
        .exec = NULL,
        .query = cmd_gapconnlist_query,
        .test = NULL,
        .help = "List active connections",
        .min_args = 0,
        .max_args = 0
    },
};

#define BT_CMD_COUNT (sizeof(g_bt_cmds) / sizeof(g_bt_cmds[0]))

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

const at_cmd_entry_t *at_bt_cmds_get_table(void)
{
    return g_bt_cmds;
}

size_t at_bt_cmds_get_count(void)
{
    return BT_CMD_COUNT;
}

int at_bt_cmds_register(void)
{
    /* Create URC queue if not exists */
    if (g_urc_queue == NULL) {
        g_urc_queue = xQueueCreate(BT_URC_QUEUE_DEPTH, sizeof(bt_urc_t));
        if (g_urc_queue == NULL) {
            return -1;
        }
    }

    /* Register GAP callback for scan results and connection events */
    gap_register_callback(gap_event_callback, NULL);

    return at_parser_register_commands(g_bt_cmds, BT_CMD_COUNT);
}

void at_bt_cmds_process(void)
{
    bt_urc_t urc;

    /* Process all pending URCs */
    while (g_urc_queue != NULL &&
           xQueueReceive(g_urc_queue, &urc, 0) == pdTRUE) {
        cdc_acm_printf("%s", urc.message);
    }
}

/*******************************************************************************
 * Command Handlers - Initialization
 ******************************************************************************/

/**
 * @brief AT+BTINIT - Initialize Bluetooth stack
 */
static int cmd_btinit_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (bt_is_initialized()) {
        return CME_BT_ALREADY_INIT;
    }

    bt_config_t config = BT_CONFIG_DEFAULT;
    strncpy(config.device_name, g_device_name, sizeof(config.device_name) - 1);

    int result = bt_init_with_config(&config);
    if (result != BT_OK) {
        return CME_BT_HCI_ERROR;
    }

    /* Initialize GAP */
    result = gap_init();
    if (result != GAP_OK) {
        bt_deinit();
        return CME_BT_HCI_ERROR;
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+BTDEINIT - Deinitialize Bluetooth stack
 */
static int cmd_btdeinit_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!bt_is_initialized()) {
        return CME_BT_NOT_INIT;
    }

    gap_deinit();
    bt_deinit();

    return CME_SUCCESS;
}

/**
 * @brief AT+BTSTATE? - Query Bluetooth state
 */
static int cmd_btstate_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    bt_state_t state = bt_get_state();
    cdc_acm_printf("\r\n+BTSTATE: %s\r\n", bt_state_to_string(state));

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Device Configuration
 ******************************************************************************/

/**
 * @brief AT+BTADDR? - Query device address
 */
static int cmd_btaddr_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    uint8_t addr[6];
    bool random;
    char addr_str[18];

    int result = bt_get_device_address(addr, &random);
    if (result != BT_OK) {
        return CME_BT_NOT_INIT;
    }

    format_mac_address(addr, addr_str);
    cdc_acm_printf("\r\n+BTADDR: %s,%s\r\n", addr_str, random ? "RANDOM" : "PUBLIC");

    return CME_SUCCESS;
}

/**
 * @brief AT+BTADDR=<addr>[,<type>] - Set device address
 */
static int cmd_btaddr_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    uint8_t addr[6];
    if (parse_mac_address(argv[0], addr) != 0) {
        return CME_INVALID_PARAM;
    }

    bool random = true;  /* Default to random */
    if (argc >= 2) {
        if (strcasecmp(argv[1], "PUBLIC") == 0 || strcmp(argv[1], "0") == 0) {
            random = false;
        } else if (strcasecmp(argv[1], "RANDOM") == 0 || strcmp(argv[1], "1") == 0) {
            random = true;
        } else {
            return CME_INVALID_PARAM;
        }
    }

    int result = bt_set_device_address(addr, random);
    if (result != BT_OK) {
        return CME_BT_HCI_ERROR;
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+BTNAME? - Query device name
 */
static int cmd_btname_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    char name[GAP_MAX_DEVICE_NAME_LEN];
    int result = gap_get_device_name(name, sizeof(name));

    if (result != GAP_OK) {
        /* Return stored name if GAP not ready */
        cdc_acm_printf("\r\n+BTNAME: \"%s\"\r\n", g_device_name);
    } else {
        cdc_acm_printf("\r\n+BTNAME: \"%s\"\r\n", name);
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+BTNAME=<name> - Set device name
 */
static int cmd_btname_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    const char *name = argv[0];
    size_t len = strlen(name);

    if (len == 0 || len > BT_MAX_NAME_LEN) {
        return CME_INVALID_PARAM;
    }

    /* Store locally */
    strncpy(g_device_name, name, sizeof(g_device_name) - 1);
    g_device_name[sizeof(g_device_name) - 1] = '\0';

    /* Set in BT stack if initialized */
    if (bt_is_initialized()) {
        int result = bt_set_device_name(name);
        if (result != BT_OK) {
            return CME_BT_HCI_ERROR;
        }

        result = gap_set_device_name(name);
        if (result != GAP_OK) {
            return CME_BT_HCI_ERROR;
        }
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+BTPWR? - Query TX power
 */
static int cmd_btpwr_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    /* Note: BTSTACK doesn't have a direct query API, return nominal */
    cdc_acm_printf("\r\n+BTPWR: 0\r\n");

    return CME_SUCCESS;
}

/**
 * @brief AT+BTPWR=<dbm> - Set TX power
 */
static int cmd_btpwr_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    int32_t power;
    if (at_parse_int(argv[0], &power) != 0) {
        return CME_INVALID_PARAM;
    }

    if (power < -20 || power > 10) {
        return CME_INVALID_PARAM;
    }

    int result = bt_set_tx_power((int8_t)power);
    if (result != BT_OK) {
        return CME_BT_HCI_ERROR;
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+BTINFO? - Query controller info
 */
static int cmd_btinfo_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!bt_is_initialized()) {
        return CME_BT_NOT_INIT;
    }

    bt_controller_info_t info;
    int result = bt_get_controller_info(&info);
    if (result != BT_OK) {
        return CME_BT_HCI_ERROR;
    }

    cdc_acm_printf("\r\n+BTINFO: HCI=%d.%d,LMP=%d.%d,MFR=0x%04X\r\n",
                   info.hci_version, info.hci_revision,
                   info.lmp_version, info.lmp_subversion,
                   info.manufacturer);
    cdc_acm_printf("+BTINFO: FW=\"%s\"\r\n", info.fw_version);
    cdc_acm_printf("+BTINFO: LE_AUDIO=%s,ISOC=%s\r\n",
                   info.le_audio_supported ? "YES" : "NO",
                   info.isoc_supported ? "YES" : "NO");

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - GAP Advertising
 ******************************************************************************/

/**
 * @brief AT+GAPADVSTART[=<connectable>,<interval_ms>] - Start advertising
 */
static int cmd_gapadvstart_exec(int argc, const char *argv[])
{
    if (!bt_is_initialized()) {
        return CME_BT_NOT_INIT;
    }

    bool connectable = true;
    uint16_t interval_ms = BT_DEFAULT_ADV_INTERVAL;

    if (argc >= 1) {
        int32_t val;
        if (at_parse_int(argv[0], &val) != 0) {
            return CME_INVALID_PARAM;
        }
        connectable = (val != 0);
    }

    if (argc >= 2) {
        int32_t val;
        if (at_parse_int(argv[1], &val) != 0 || val < 20 || val > 10240) {
            return CME_INVALID_PARAM;
        }
        interval_ms = (uint16_t)val;
    }

    int result = bt_start_advertising(connectable, interval_ms);
    if (result != BT_OK) {
        return CME_BT_ADV_FAILED;
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+GAPADVSTOP - Stop advertising
 */
static int cmd_gapadvstop_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!bt_is_initialized()) {
        return CME_BT_NOT_INIT;
    }

    int result = bt_stop_advertising();
    if (result != BT_OK) {
        return CME_BT_ADV_FAILED;
    }

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - GAP Scanning
 ******************************************************************************/

/**
 * @brief AT+GAPSCAN=<enable>[,<timeout_ms>] - Start/stop scanning
 */
static int cmd_gapscan_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    if (!bt_is_initialized()) {
        return CME_BT_NOT_INIT;
    }

    int32_t enable;
    if (at_parse_int(argv[0], &enable) != 0) {
        return CME_INVALID_PARAM;
    }

    if (enable == 0) {
        /* Stop scanning */
        int result = gap_stop_scanning();
        if (result != GAP_OK) {
            return CME_BT_SCAN_FAILED;
        }
        g_scanning = false;
        send_urc("\r\n+SCANDONE\r\n");
    } else {
        /* Parse timeout if provided */
        g_scan_timeout_ms = 0;
        if (argc >= 2) {
            int32_t timeout;
            if (at_parse_int(argv[1], &timeout) == 0 && timeout > 0) {
                g_scan_timeout_ms = (uint32_t)timeout;
            }
        }

        /* Set scan parameters */
        gap_scan_params_t params = GAP_SCAN_PARAMS_DEFAULT;
        int result = gap_set_scan_params(&params);
        if (result != GAP_OK) {
            return CME_BT_SCAN_FAILED;
        }

        /* Start scanning */
        result = gap_start_scanning(GAP_SCAN_DUP_FILTER_ENABLED);
        if (result != GAP_OK) {
            return CME_BT_SCAN_FAILED;
        }

        g_scanning = true;
    }

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - GAP Connection
 ******************************************************************************/

/**
 * @brief AT+GAPCONN=<addr>[,<addr_type>] - Connect to device
 */
static int cmd_gapconn_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    if (!bt_is_initialized()) {
        return CME_BT_NOT_INIT;
    }

    gap_address_t peer_addr;
    if (parse_mac_address(argv[0], peer_addr.addr) != 0) {
        return CME_INVALID_PARAM;
    }

    peer_addr.type = GAP_ADDR_TYPE_PUBLIC;
    if (argc >= 2) {
        int32_t type;
        if (at_parse_int(argv[1], &type) == 0) {
            if (type == 0) {
                peer_addr.type = GAP_ADDR_TYPE_PUBLIC;
            } else if (type == 1) {
                peer_addr.type = GAP_ADDR_TYPE_RANDOM;
            } else {
                return CME_INVALID_PARAM;
            }
        }
    }

    /* Stop scanning if active */
    if (g_scanning) {
        gap_stop_scanning();
        g_scanning = false;
    }

    /* Connect with default parameters */
    gap_conn_params_t params = GAP_CONN_PARAMS_DEFAULT;
    int result = gap_connect(&peer_addr, &params);
    if (result != GAP_OK) {
        return CME_BT_CONN_FAILED;
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+GAPDISCONN[=<conn_handle>] - Disconnect
 */
static int cmd_gapdisconn_exec(int argc, const char *argv[])
{
    if (!bt_is_initialized()) {
        return CME_BT_NOT_INIT;
    }

    uint16_t conn_handle = 0;  /* Default: first connection */

    if (argc >= 1) {
        int32_t handle;
        if (at_parse_int(argv[0], &handle) != 0 || handle < 0 || handle > 0xFFFF) {
            return CME_INVALID_PARAM;
        }
        conn_handle = (uint16_t)handle;
    }

    int result = gap_disconnect(conn_handle, 0x13);  /* Remote user terminated */
    if (result != GAP_OK) {
        return CME_BT_NO_CONNECTION;
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+GAPCONNLIST? - List active connections
 */
static int cmd_gapconnlist_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!bt_is_initialized()) {
        return CME_BT_NOT_INIT;
    }

    /* Note: Would need to iterate connections from link.c or similar */
    /* For now, report based on bt_get_state */
    bt_state_t state = bt_get_state();
    if (state >= BT_STATE_CONNECTED) {
        cdc_acm_printf("\r\n+GAPCONN: 0,CONNECTED\r\n");
    } else {
        cdc_acm_printf("\r\n+GAPCONN: NONE\r\n");
    }

    return CME_SUCCESS;
}

/*******************************************************************************
 * GAP Event Callback
 ******************************************************************************/

/**
 * @brief Handle GAP events for async responses
 */
static void gap_event_callback(const gap_event_t *event, void *user_data)
{
    (void)user_data;
    char addr_str[18];

    switch (event->type) {
        case GAP_EVENT_SCAN_RESULT:
            format_mac_address(event->data.scan_result.address.addr, addr_str);
            send_urc("\r\n+SCANRESULT: %s,%d,%d\r\n",
                     addr_str,
                     event->data.scan_result.rssi,
                     event->data.scan_result.adv_type);
            break;

        case GAP_EVENT_SCAN_STOPPED:
            g_scanning = false;
            send_urc("\r\n+SCANDONE\r\n");
            break;

        case GAP_EVENT_CONNECTION_COMPLETE:
            format_mac_address(event->data.connection.peer_addr.addr, addr_str);
            send_urc("\r\n+GAPCONNECTED: %d,%s\r\n",
                     event->data.connection.conn_handle,
                     addr_str);
            break;

        case GAP_EVENT_DISCONNECTION:
            send_urc("\r\n+GAPDISCONNECTED: %d,0x%02X\r\n",
                     event->data.disconnection.conn_handle,
                     event->data.disconnection.reason);
            break;

        case GAP_EVENT_ADV_STARTED:
            send_urc("\r\n+GAPADVSTARTED\r\n");
            break;

        case GAP_EVENT_ADV_STOPPED:
            send_urc("\r\n+GAPADVSTOPPED\r\n");
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

/**
 * @brief Send URC via queue (thread-safe)
 */
static void send_urc(const char *fmt, ...)
{
    if (g_urc_queue == NULL) {
        return;
    }

    bt_urc_t urc;
    va_list args;

    va_start(args, fmt);
    vsnprintf(urc.message, sizeof(urc.message), fmt, args);
    va_end(args);

    /* Use ISR-safe send if in interrupt context */
    BaseType_t higher_woken = pdFALSE;
    if (xPortIsInsideInterrupt()) {
        xQueueSendFromISR(g_urc_queue, &urc, &higher_woken);
        portYIELD_FROM_ISR(higher_woken);
    } else {
        xQueueSend(g_urc_queue, &urc, 0);
    }
}

/**
 * @brief Convert BT state to string
 */
static const char *bt_state_to_string(bt_state_t state)
{
    switch (state) {
        case BT_STATE_OFF:          return "OFF";
        case BT_STATE_INITIALIZING: return "INIT";
        case BT_STATE_READY:        return "READY";
        case BT_STATE_ADVERTISING:  return "ADV";
        case BT_STATE_CONNECTED:    return "CONN";
        case BT_STATE_STREAMING:    return "STREAM";
        case BT_STATE_ERROR:        return "ERROR";
        default:                    return "UNKNOWN";
    }
}

/**
 * @brief Parse MAC address string "AA:BB:CC:DD:EE:FF"
 */
static int parse_mac_address(const char *str, uint8_t addr[6])
{
    unsigned int bytes[6];
    int count = sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
                       &bytes[0], &bytes[1], &bytes[2],
                       &bytes[3], &bytes[4], &bytes[5]);

    if (count != 6) {
        /* Try without colons */
        count = sscanf(str, "%02X%02X%02X%02X%02X%02X",
                       &bytes[0], &bytes[1], &bytes[2],
                       &bytes[3], &bytes[4], &bytes[5]);
        if (count != 6) {
            return -1;
        }
    }

    for (int i = 0; i < 6; i++) {
        addr[i] = (uint8_t)bytes[i];
    }

    return 0;
}

/**
 * @brief Format MAC address to string "AA:BB:CC:DD:EE:FF"
 */
static void format_mac_address(const uint8_t addr[6], char *str)
{
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}
