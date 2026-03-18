/**
 * @file at_wifi_cmds.c
 * @brief Wi-Fi AT Command Handlers Implementation
 *
 * Implements AT commands for Wi-Fi configuration and control using WHD
 * (Wi-Fi Host Driver) for CYW55512 combo IC.
 *
 * Commands:
 * - AT+WIFIINIT - Initialize Wi-Fi
 * - AT+WIFIDEINIT - Deinitialize Wi-Fi
 * - AT+WIFISTATE? - Query Wi-Fi state
 * - AT+WIFISCAN - Scan for networks
 * - AT+WIFIJOIN=<ssid>,<password>[,<security>] - Join network
 * - AT+WIFILEAVE - Leave network
 * - AT+WIFIRSSI? - Get RSSI
 * - AT+WIFIMAC? - Get MAC address
 * - AT+WIFIBRIDGE=<0|1> - Start/stop USB-Wi-Fi bridge
 * - AT+WIFIINFO? - Display Wi-Fi information
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "at_wifi_cmds.h"
#include "at_parser.h"
#include "at_commands.h"
#include "cdc_acm.h"
#include "wifi/wifi_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

/* WHD (Wi-Fi Host Driver) includes */
#include "whd.h"
#include "whd_wifi_api.h"
#include "whd_types.h"

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** URC queue depth */
#define WIFI_URC_QUEUE_DEPTH    16

/** Maximum URC message length */
#define WIFI_URC_MAX_LEN        256

/** Maximum SSID length */
#define MAX_SSID_LEN            32

/** Maximum password length */
#define MAX_PASSWORD_LEN        64

/** Maximum scan results to store */
#define MAX_SCAN_RESULTS        20

/** URC message structure */
typedef struct {
    char message[WIFI_URC_MAX_LEN];
} wifi_urc_t;

/** Scan result entry */
typedef struct {
    char ssid[MAX_SSID_LEN + 1];
    uint8_t bssid[6];
    int16_t rssi;
    uint8_t channel;
    whd_security_t security;
} scan_result_entry_t;

/** Wi-Fi connection state */
typedef enum {
    WIFI_STATE_NOT_INITIALIZED = 0,
    WIFI_STATE_INITIALIZED,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** URC queue for async events */
static QueueHandle_t g_urc_queue = NULL;

/** Module state */
static wifi_state_t g_wifi_state = WIFI_STATE_NOT_INITIALIZED;

/** WHD driver handle */
static whd_driver_t g_whd_driver = NULL;

/** WHD interface handle */
static whd_interface_t g_whd_iface = NULL;

/** Scan results buffer */
static scan_result_entry_t g_scan_results[MAX_SCAN_RESULTS];
static uint8_t g_scan_result_count = 0;
static SemaphoreHandle_t g_scan_mutex = NULL;

/** Current connection info */
static char g_connected_ssid[MAX_SSID_LEN + 1] = {0};
static uint8_t g_connected_bssid[6] = {0};

/** State name lookup */
static const char *g_state_names[] = {
    [WIFI_STATE_NOT_INITIALIZED] = "NOT_INITIALIZED",
    [WIFI_STATE_INITIALIZED]     = "INITIALIZED",
    [WIFI_STATE_SCANNING]        = "SCANNING",
    [WIFI_STATE_CONNECTING]      = "CONNECTING",
    [WIFI_STATE_CONNECTED]       = "CONNECTED",
    [WIFI_STATE_DISCONNECTED]    = "DISCONNECTED",
    [WIFI_STATE_ERROR]           = "ERROR"
};

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

/* Command handlers */
static int cmd_wifiinit_exec(int argc, const char *argv[]);
static int cmd_wifideinit_exec(int argc, const char *argv[]);
static int cmd_wifistate_query(int argc, const char *argv[]);
static int cmd_wifiscan_exec(int argc, const char *argv[]);
static int cmd_wifiscan_query(int argc, const char *argv[]);
static int cmd_wifijoin_exec(int argc, const char *argv[]);
static int cmd_wifijoin_query(int argc, const char *argv[]);
static int cmd_wifileave_exec(int argc, const char *argv[]);
static int cmd_wifirssi_query(int argc, const char *argv[]);
static int cmd_wifimac_query(int argc, const char *argv[]);
static int cmd_wifibridge_exec(int argc, const char *argv[]);
static int cmd_wifibridge_query(int argc, const char *argv[]);
static int cmd_wifiinfo_query(int argc, const char *argv[]);

/* Helper functions */
static void send_urc(const char *fmt, ...);
static void format_mac(char *buf, size_t buf_size, const uint8_t *mac);
static const char *get_state_name(wifi_state_t state);
static const char *get_security_name(whd_security_t security);
static void scan_callback(whd_scan_result_t **result_ptr, void *user_data, whd_scan_status_t status);

/*******************************************************************************
 * Command Table
 ******************************************************************************/

static const at_cmd_entry_t g_wifi_cmds[] = {
    /* AT+WIFIINIT - Initialize Wi-Fi */
    {
        .name = "WIFIINIT",
        .exec = cmd_wifiinit_exec,
        .query = NULL,
        .test = NULL,
        .help = "Initialize Wi-Fi subsystem",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+WIFIDEINIT - Deinitialize Wi-Fi */
    {
        .name = "WIFIDEINIT",
        .exec = cmd_wifideinit_exec,
        .query = NULL,
        .test = NULL,
        .help = "Deinitialize Wi-Fi",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+WIFISTATE? - Query state */
    {
        .name = "WIFISTATE",
        .exec = NULL,
        .query = cmd_wifistate_query,
        .test = NULL,
        .help = "Query Wi-Fi state",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+WIFISCAN - Scan for networks */
    {
        .name = "WIFISCAN",
        .exec = cmd_wifiscan_exec,
        .query = cmd_wifiscan_query,
        .test = NULL,
        .help = "Scan for networks [ssid_filter]",
        .min_args = 0,
        .max_args = 1
    },

    /* AT+WIFIJOIN - Join network */
    {
        .name = "WIFIJOIN",
        .exec = cmd_wifijoin_exec,
        .query = cmd_wifijoin_query,
        .test = NULL,
        .help = "Join network (ssid,password[,security])",
        .min_args = 2,
        .max_args = 3
    },

    /* AT+WIFILEAVE - Leave network */
    {
        .name = "WIFILEAVE",
        .exec = cmd_wifileave_exec,
        .query = NULL,
        .test = NULL,
        .help = "Leave Wi-Fi network",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+WIFIRSSI? - Get RSSI */
    {
        .name = "WIFIRSSI",
        .exec = NULL,
        .query = cmd_wifirssi_query,
        .test = NULL,
        .help = "Get signal strength",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+WIFIMAC? - Get MAC address */
    {
        .name = "WIFIMAC",
        .exec = NULL,
        .query = cmd_wifimac_query,
        .test = NULL,
        .help = "Get MAC address",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+WIFIBRIDGE - USB-Wi-Fi bridge */
    {
        .name = "WIFIBRIDGE",
        .exec = cmd_wifibridge_exec,
        .query = cmd_wifibridge_query,
        .test = NULL,
        .help = "USB-Wi-Fi bridge (0=stop, 1=start)",
        .min_args = 1,
        .max_args = 1
    },

    /* AT+WIFIINFO? - Wi-Fi information */
    {
        .name = "WIFIINFO",
        .exec = NULL,
        .query = cmd_wifiinfo_query,
        .test = NULL,
        .help = "Display Wi-Fi information",
        .min_args = 0,
        .max_args = 0
    },
};

#define WIFI_CMD_COUNT (sizeof(g_wifi_cmds) / sizeof(g_wifi_cmds[0]))

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

const at_cmd_entry_t *at_wifi_cmds_get_table(void)
{
    return g_wifi_cmds;
}

size_t at_wifi_cmds_get_count(void)
{
    return WIFI_CMD_COUNT;
}

int at_wifi_cmds_register(void)
{
    /* Create URC queue if not exists */
    if (g_urc_queue == NULL) {
        g_urc_queue = xQueueCreate(WIFI_URC_QUEUE_DEPTH, sizeof(wifi_urc_t));
        if (g_urc_queue == NULL) {
            return -1;
        }
    }

    /* Create scan mutex if not exists */
    if (g_scan_mutex == NULL) {
        g_scan_mutex = xSemaphoreCreateMutex();
        if (g_scan_mutex == NULL) {
            return -1;
        }
    }

    return at_parser_register_commands(g_wifi_cmds, WIFI_CMD_COUNT);
}

void at_wifi_cmds_process(void)
{
    wifi_urc_t urc;

    /* Process all pending URCs */
    while (g_urc_queue != NULL &&
           xQueueReceive(g_urc_queue, &urc, 0) == pdTRUE) {
        cdc_acm_printf("%s", urc.message);
    }
}

void at_wifi_cmds_set_whd_handles(void *driver, void *iface)
{
    g_whd_driver = (whd_driver_t)driver;
    g_whd_iface = (whd_interface_t)iface;

    if (driver != NULL && iface != NULL) {
        if (g_wifi_state == WIFI_STATE_NOT_INITIALIZED) {
            g_wifi_state = WIFI_STATE_INITIALIZED;
        }
    }
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

/**
 * @brief Send URC (Unsolicited Result Code)
 */
static void send_urc(const char *fmt, ...)
{
    if (g_urc_queue == NULL) {
        return;
    }

    wifi_urc_t urc;
    va_list args;
    va_start(args, fmt);
    vsnprintf(urc.message, sizeof(urc.message), fmt, args);
    va_end(args);

    xQueueSend(g_urc_queue, &urc, 0);
}

/**
 * @brief Format MAC address
 */
static void format_mac(char *buf, size_t buf_size, const uint8_t *mac)
{
    snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Get state name string
 */
static const char *get_state_name(wifi_state_t state)
{
    if (state < sizeof(g_state_names) / sizeof(g_state_names[0])) {
        return g_state_names[state];
    }
    return "UNKNOWN";
}

/**
 * @brief Get security type name
 */
static const char *get_security_name(whd_security_t security)
{
    if (security == WHD_SECURITY_OPEN) return "OPEN";
    if (security & WHD_SECURITY_WPA3_SAE) return "WPA3_SAE";
    if (security & WHD_SECURITY_WPA2_AES_PSK) return "WPA2_PSK";
    if (security & WHD_SECURITY_WPA_TKIP_PSK) return "WPA_PSK";
    if (security & WHD_SECURITY_WEP_PSK) return "WEP";
    return "UNKNOWN";
}

/**
 * @brief Scan callback for async scan results
 */
static void scan_callback(whd_scan_result_t **result_ptr, void *user_data,
                          whd_scan_status_t status)
{
    (void)user_data;

    if (status == WHD_SCAN_INCOMPLETE && result_ptr != NULL && *result_ptr != NULL) {
        whd_scan_result_t *result = *result_ptr;

        /* Lock scan results */
        if (xSemaphoreTake(g_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (g_scan_result_count < MAX_SCAN_RESULTS) {
                scan_result_entry_t *entry = &g_scan_results[g_scan_result_count];

                /* Copy SSID */
                memset(entry->ssid, 0, sizeof(entry->ssid));
                size_t ssid_len = (result->SSID.length < MAX_SSID_LEN) ?
                                  result->SSID.length : MAX_SSID_LEN;
                memcpy(entry->ssid, result->SSID.value, ssid_len);

                /* Copy BSSID */
                memcpy(entry->bssid, result->BSSID.octet, 6);

                /* Copy other fields */
                entry->rssi = result->signal_strength;
                entry->channel = result->channel;
                entry->security = result->security;

                g_scan_result_count++;

                /* Send URC for this result */
                char mac_str[18];
                format_mac(mac_str, sizeof(mac_str), entry->bssid);
                send_urc("\r\n+WIFISCAN: %d,\"%s\",%s,%d,%d,%s\r\n",
                         g_scan_result_count - 1,
                         entry->ssid,
                         mac_str,
                         entry->rssi,
                         entry->channel,
                         get_security_name(entry->security));
            }
            xSemaphoreGive(g_scan_mutex);
        }
    } else if (status == WHD_SCAN_COMPLETED_SUCCESSFULLY) {
        g_wifi_state = WIFI_STATE_INITIALIZED;
        send_urc("\r\n+WIFISCAN: DONE,%d\r\n", g_scan_result_count);
    } else if (status == WHD_SCAN_ABORTED) {
        g_wifi_state = WIFI_STATE_INITIALIZED;
        send_urc("\r\n+WIFISCAN: ABORTED\r\n");
    }
}

/*******************************************************************************
 * Command Handlers - Initialization
 ******************************************************************************/

/**
 * @brief AT+WIFIINIT - Initialize Wi-Fi
 */
static int cmd_wifiinit_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (g_wifi_state != WIFI_STATE_NOT_INITIALIZED) {
        return CME_WIFI_ALREADY_INIT;
    }

    /* Check if WHD is ready (should be initialized by wifi_bridge) */
    if (g_whd_driver == NULL || g_whd_iface == NULL) {
        return CME_WIFI_NOT_INIT;
    }

    g_wifi_state = WIFI_STATE_INITIALIZED;

    cdc_acm_printf("\r\n+WIFIINIT: OK\r\n");
    return CME_SUCCESS;
}

/**
 * @brief AT+WIFIDEINIT - Deinitialize Wi-Fi
 */
static int cmd_wifideinit_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (g_wifi_state == WIFI_STATE_NOT_INITIALIZED) {
        return CME_WIFI_NOT_INIT;
    }

    /* Leave network if connected */
    if (g_wifi_state == WIFI_STATE_CONNECTED && g_whd_iface != NULL) {
        whd_wifi_leave(g_whd_iface);
    }

    g_wifi_state = WIFI_STATE_NOT_INITIALIZED;
    memset(g_connected_ssid, 0, sizeof(g_connected_ssid));
    memset(g_connected_bssid, 0, sizeof(g_connected_bssid));

    cdc_acm_printf("\r\n+WIFIDEINIT: OK\r\n");
    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - State Query
 ******************************************************************************/

/**
 * @brief AT+WIFISTATE? - Query Wi-Fi state
 */
static int cmd_wifistate_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\n+WIFISTATE: %s\r\n", get_state_name(g_wifi_state));
    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Scanning
 ******************************************************************************/

/**
 * @brief AT+WIFISCAN[=ssid_filter] - Scan for networks
 */
static int cmd_wifiscan_exec(int argc, const char *argv[])
{
    if (g_wifi_state == WIFI_STATE_NOT_INITIALIZED) {
        return CME_WIFI_NOT_INIT;
    }

    if (g_whd_iface == NULL) {
        return CME_WIFI_NOT_INIT;
    }

    /* Clear previous scan results */
    if (xSemaphoreTake(g_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_scan_result_count = 0;
        memset(g_scan_results, 0, sizeof(g_scan_results));
        xSemaphoreGive(g_scan_mutex);
    }

    /* Prepare SSID filter if provided */
    whd_ssid_t ssid_filter;
    whd_ssid_t *ssid_ptr = NULL;

    if (argc >= 1 && argv[0][0] != '\0') {
        memset(&ssid_filter, 0, sizeof(ssid_filter));
        ssid_filter.length = strlen(argv[0]);
        if (ssid_filter.length > MAX_SSID_LEN) {
            ssid_filter.length = MAX_SSID_LEN;
        }
        memcpy(ssid_filter.value, argv[0], ssid_filter.length);
        ssid_ptr = &ssid_filter;
    }

    g_wifi_state = WIFI_STATE_SCANNING;

    /* Start async scan */
    whd_result_t result = whd_wifi_scan(g_whd_iface,
                                         WHD_SCAN_TYPE_ACTIVE,
                                         WHD_BSS_TYPE_ANY,
                                         ssid_ptr,
                                         NULL,  /* No MAC filter */
                                         NULL,  /* All channels */
                                         NULL,  /* Extended params */
                                         scan_callback,
                                         NULL,  /* Scan result buffer managed internally */
                                         NULL); /* No user data */

    if (result != WHD_SUCCESS) {
        g_wifi_state = WIFI_STATE_INITIALIZED;
        return CME_WIFI_SCAN_FAILED;
    }

    cdc_acm_printf("\r\n+WIFISCAN: STARTED\r\n");
    return CME_SUCCESS;
}

/**
 * @brief AT+WIFISCAN? - Get cached scan results
 */
static int cmd_wifiscan_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (xSemaphoreTake(g_scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return CME_BUSY;
    }

    cdc_acm_printf("\r\n+WIFISCAN: %d results\r\n", g_scan_result_count);

    for (uint8_t i = 0; i < g_scan_result_count; i++) {
        scan_result_entry_t *entry = &g_scan_results[i];
        char mac_str[18];
        format_mac(mac_str, sizeof(mac_str), entry->bssid);

        cdc_acm_printf("  %d: \"%s\" %s %ddBm ch%d %s\r\n",
                       i, entry->ssid, mac_str, entry->rssi, entry->channel,
                       get_security_name(entry->security));
    }

    xSemaphoreGive(g_scan_mutex);
    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Connection
 ******************************************************************************/

/**
 * @brief AT+WIFIJOIN=ssid,password[,security] - Join network
 */
static int cmd_wifijoin_exec(int argc, const char *argv[])
{
    if (argc < 2) {
        return CME_INVALID_PARAM;
    }

    if (g_wifi_state == WIFI_STATE_NOT_INITIALIZED) {
        return CME_WIFI_NOT_INIT;
    }

    if (g_whd_iface == NULL) {
        return CME_WIFI_NOT_INIT;
    }

    const char *ssid = argv[0];
    const char *password = argv[1];

    /* Determine security type */
    whd_security_t security = WHD_SECURITY_WPA2_AES_PSK;  /* Default */
    if (argc >= 3) {
        const char *sec_str = argv[2];
        if (strcmp(sec_str, "OPEN") == 0) {
            security = WHD_SECURITY_OPEN;
        } else if (strcmp(sec_str, "WEP") == 0) {
            security = WHD_SECURITY_WEP_PSK;
        } else if (strcmp(sec_str, "WPA_PSK") == 0) {
            security = WHD_SECURITY_WPA_TKIP_PSK;
        } else if (strcmp(sec_str, "WPA2_PSK") == 0) {
            security = WHD_SECURITY_WPA2_AES_PSK;
        } else if (strcmp(sec_str, "WPA3_SAE") == 0) {
            security = WHD_SECURITY_WPA3_SAE;
        }
    }

    /* Prepare SSID structure */
    whd_ssid_t whd_ssid;
    memset(&whd_ssid, 0, sizeof(whd_ssid));
    whd_ssid.length = strlen(ssid);
    if (whd_ssid.length > MAX_SSID_LEN) {
        whd_ssid.length = MAX_SSID_LEN;
    }
    memcpy(whd_ssid.value, ssid, whd_ssid.length);

    g_wifi_state = WIFI_STATE_CONNECTING;

    /* Join network */
    whd_result_t result = whd_wifi_join(g_whd_iface,
                                         &whd_ssid,
                                         security,
                                         (const uint8_t *)password,
                                         strlen(password));

    if (result != WHD_SUCCESS) {
        g_wifi_state = WIFI_STATE_DISCONNECTED;
        return CME_WIFI_ASSOC_FAILED;
    }

    /* Store connection info */
    strncpy(g_connected_ssid, ssid, MAX_SSID_LEN);
    g_wifi_state = WIFI_STATE_CONNECTED;

    cdc_acm_printf("\r\n+WIFIJOIN: OK,\"%s\"\r\n", ssid);
    return CME_SUCCESS;
}

/**
 * @brief AT+WIFIJOIN? - Query current connection
 */
static int cmd_wifijoin_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (g_wifi_state == WIFI_STATE_CONNECTED) {
        char mac_str[18];
        format_mac(mac_str, sizeof(mac_str), g_connected_bssid);
        cdc_acm_printf("\r\n+WIFIJOIN: 1,\"%s\",%s\r\n", g_connected_ssid, mac_str);
    } else {
        cdc_acm_printf("\r\n+WIFIJOIN: 0\r\n");
    }
    return CME_SUCCESS;
}

/**
 * @brief AT+WIFILEAVE - Leave network
 */
static int cmd_wifileave_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (g_wifi_state != WIFI_STATE_CONNECTED) {
        return CME_WIFI_NOT_CONNECTED;
    }

    if (g_whd_iface == NULL) {
        return CME_WIFI_NOT_INIT;
    }

    whd_result_t result = whd_wifi_leave(g_whd_iface);
    if (result != WHD_SUCCESS) {
        return CME_FAILURE;
    }

    g_wifi_state = WIFI_STATE_DISCONNECTED;
    memset(g_connected_ssid, 0, sizeof(g_connected_ssid));
    memset(g_connected_bssid, 0, sizeof(g_connected_bssid));

    cdc_acm_printf("\r\n+WIFILEAVE: OK\r\n");
    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Status Queries
 ******************************************************************************/

/**
 * @brief AT+WIFIRSSI? - Get signal strength
 */
static int cmd_wifirssi_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (g_wifi_state != WIFI_STATE_CONNECTED) {
        cdc_acm_printf("\r\n+WIFIRSSI: N/A\r\n");
        return CME_SUCCESS;
    }

    if (g_whd_iface == NULL) {
        return CME_WIFI_NOT_INIT;
    }

    int32_t rssi;
    whd_result_t result = whd_wifi_get_rssi(g_whd_iface, &rssi);
    if (result != WHD_SUCCESS) {
        return CME_FAILURE;
    }

    cdc_acm_printf("\r\n+WIFIRSSI: %d dBm\r\n", (int)rssi);
    return CME_SUCCESS;
}

/**
 * @brief AT+WIFIMAC? - Get MAC address
 */
static int cmd_wifimac_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (g_whd_iface == NULL) {
        return CME_WIFI_NOT_INIT;
    }

    whd_mac_t mac;
    whd_result_t result = whd_wifi_get_mac_address(g_whd_iface, &mac);
    if (result != WHD_SUCCESS) {
        return CME_FAILURE;
    }

    char mac_str[18];
    format_mac(mac_str, sizeof(mac_str), mac.octet);
    cdc_acm_printf("\r\n+WIFIMAC: %s\r\n", mac_str);
    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Bridge
 ******************************************************************************/

/**
 * @brief AT+WIFIBRIDGE=enable - Start/stop USB-Wi-Fi bridge
 */
static int cmd_wifibridge_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    int enable = atoi(argv[0]);

    if (enable) {
        int result = wifi_bridge_start();
        if (result != 0) {
            return CME_FAILURE;
        }
        cdc_acm_printf("\r\n+WIFIBRIDGE: STARTED\r\n");
    } else {
        int result = wifi_bridge_stop();
        if (result != 0) {
            return CME_FAILURE;
        }
        cdc_acm_printf("\r\n+WIFIBRIDGE: STOPPED\r\n");
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+WIFIBRIDGE? - Query bridge status
 */
static int cmd_wifibridge_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    wifi_bridge_status_t status = wifi_bridge_get_status();
    wifi_bridge_stats_t stats;
    wifi_bridge_get_stats(&stats);

    const char *status_str;
    switch (status) {
        case WIFI_BRIDGE_STATUS_STOPPED:  status_str = "STOPPED"; break;
        case WIFI_BRIDGE_STATUS_STARTING: status_str = "STARTING"; break;
        case WIFI_BRIDGE_STATUS_RUNNING:  status_str = "RUNNING"; break;
        case WIFI_BRIDGE_STATUS_ERROR:    status_str = "ERROR"; break;
        default: status_str = "UNKNOWN"; break;
    }

    cdc_acm_printf("\r\n+WIFIBRIDGE: %s\r\n", status_str);
    cdc_acm_printf("  USB RX: %lu bytes\r\n", (unsigned long)stats.usb_bytes_rx);
    cdc_acm_printf("  USB TX: %lu bytes\r\n", (unsigned long)stats.usb_bytes_tx);
    cdc_acm_printf("  WiFi RX: %lu bytes\r\n", (unsigned long)stats.wifi_bytes_rx);
    cdc_acm_printf("  WiFi TX: %lu bytes\r\n", (unsigned long)stats.wifi_bytes_tx);
    cdc_acm_printf("  Forwarded: %lu, Dropped: %lu\r\n",
                   (unsigned long)stats.packets_forwarded,
                   (unsigned long)stats.packets_dropped);

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Information
 ******************************************************************************/

/**
 * @brief AT+WIFIINFO? - Display Wi-Fi information
 */
static int cmd_wifiinfo_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\n+WIFIINFO:\r\n");
    cdc_acm_printf("  State: %s\r\n", get_state_name(g_wifi_state));

    if (g_wifi_state == WIFI_STATE_CONNECTED) {
        char mac_str[18];
        format_mac(mac_str, sizeof(mac_str), g_connected_bssid);
        cdc_acm_printf("  SSID: %s\r\n", g_connected_ssid);
        cdc_acm_printf("  BSSID: %s\r\n", mac_str);

        /* Get RSSI if connected */
        if (g_whd_iface != NULL) {
            int32_t rssi;
            if (whd_wifi_get_rssi(g_whd_iface, &rssi) == WHD_SUCCESS) {
                cdc_acm_printf("  RSSI: %d dBm\r\n", (int)rssi);
            }
        }
    }

    /* Bridge status */
    wifi_bridge_status_t bridge_status = wifi_bridge_get_status();
    cdc_acm_printf("  Bridge: %s\r\n",
                   (bridge_status == WIFI_BRIDGE_STATUS_RUNNING) ? "RUNNING" : "STOPPED");

    return CME_SUCCESS;
}
