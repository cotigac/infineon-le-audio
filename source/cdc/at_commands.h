/**
 * @file at_commands.h
 * @brief AT Command Definitions and Error Codes
 *
 * Common definitions for AT command handling including
 * error codes and command categories.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * CME Error Codes
 ******************************************************************************/

/**
 * @brief CME (Mobile Equipment) Error Codes
 *
 * Based on 3GPP TS 27.007 with extensions for BT/Wi-Fi
 */
typedef enum {
    /* General errors (0-9) */
    CME_SUCCESS             = 0,    /**< Operation successful */
    CME_FAILURE             = 1,    /**< Generic failure */
    CME_NOT_ALLOWED         = 2,    /**< Operation not allowed in current state */
    CME_NOT_SUPPORTED       = 3,    /**< Command not supported */
    CME_INVALID_PARAM       = 4,    /**< Invalid parameter */
    CME_NOT_FOUND           = 5,    /**< Resource not found */
    CME_NO_MEMORY           = 6,    /**< Out of memory */
    CME_BUSY                = 7,    /**< Resource busy */
    CME_TIMEOUT             = 8,    /**< Operation timeout */
    CME_INVALID_INDEX       = 9,    /**< Invalid index */

    /* Bluetooth errors (10-19) */
    CME_BT_NOT_INIT         = 10,   /**< Bluetooth not initialized */
    CME_BT_ALREADY_INIT     = 11,   /**< Bluetooth already initialized */
    CME_BT_HCI_ERROR        = 12,   /**< HCI transport error */
    CME_BT_NO_CONNECTION    = 13,   /**< No active connection */
    CME_BT_CONN_FAILED      = 14,   /**< Connection failed */
    CME_BT_AUTH_FAILED      = 15,   /**< Authentication failed */
    CME_BT_PAIRING_FAILED   = 16,   /**< Pairing failed */
    CME_BT_ADV_FAILED       = 17,   /**< Advertising failed */
    CME_BT_SCAN_FAILED      = 18,   /**< Scanning failed */

    /* Wi-Fi errors (20-29) */
    CME_WIFI_NOT_INIT       = 20,   /**< Wi-Fi not initialized */
    CME_WIFI_ALREADY_INIT   = 21,   /**< Wi-Fi already initialized */
    CME_WIFI_NOT_CONNECTED  = 22,   /**< Wi-Fi not connected */
    CME_WIFI_AUTH_FAILED    = 23,   /**< Wi-Fi authentication failed */
    CME_WIFI_ASSOC_FAILED   = 24,   /**< Wi-Fi association failed */
    CME_WIFI_SCAN_FAILED    = 25,   /**< Wi-Fi scan failed */
    CME_WIFI_AP_FAILED      = 26,   /**< Access point start failed */
    CME_WIFI_DHCP_FAILED    = 27,   /**< DHCP failed */
    CME_WIFI_INVALID_SEC    = 28,   /**< Invalid security type */

    /* LE Audio errors (30-39) */
    CME_LEA_NOT_INIT        = 30,   /**< LE Audio not initialized */
    CME_LEA_ALREADY_INIT    = 31,   /**< LE Audio already initialized */
    CME_LEA_CODEC_ERROR     = 32,   /**< Codec error */
    CME_LEA_STREAM_ERROR    = 33,   /**< Stream error */
    CME_LEA_NO_SINK         = 34,   /**< No audio sink available */
    CME_LEA_NO_SOURCE       = 35,   /**< No audio source available */
    CME_LEA_CONFIG_ERROR    = 36,   /**< Configuration error */

    /* System errors (50-59) */
    CME_SYS_FLASH_ERROR     = 50,   /**< Flash operation failed */
    CME_SYS_NVM_ERROR       = 51,   /**< NVM operation failed */
    CME_SYS_RESET_PENDING   = 52,   /**< Reset pending */

} cme_error_t;

/*******************************************************************************
 * Command Categories
 ******************************************************************************/

/**
 * @brief Command categories for organization
 */
typedef enum {
    AT_CMD_CAT_SYSTEM   = 0,    /**< System commands */
    AT_CMD_CAT_BT       = 1,    /**< Bluetooth commands */
    AT_CMD_CAT_GAP      = 2,    /**< GAP (advertising, scanning) */
    AT_CMD_CAT_GATT     = 3,    /**< GATT commands */
    AT_CMD_CAT_LEA      = 4,    /**< LE Audio commands */
    AT_CMD_CAT_WIFI     = 5,    /**< Wi-Fi commands */
    AT_CMD_CAT_WIFIAP   = 6,    /**< Wi-Fi AP commands */
} at_cmd_category_t;

/*******************************************************************************
 * Version Information
 ******************************************************************************/

/** Firmware version major */
#define FW_VERSION_MAJOR    1

/** Firmware version minor */
#define FW_VERSION_MINOR    0

/** Firmware version patch */
#define FW_VERSION_PATCH    0

/** Firmware version string */
#define FW_VERSION_STRING   "1.0.0"

/** Device model string */
#define DEVICE_MODEL        "Infineon LE Audio"

/** Manufacturer string */
#define DEVICE_MANUFACTURER "Infineon Technologies"

/*******************************************************************************
 * Helper Macros
 ******************************************************************************/

/**
 * @brief Define a command entry with exec handler only
 */
#define AT_CMD_EXEC(name, handler, help_text) \
    { name, handler, NULL, NULL, help_text, 0, 0 }

/**
 * @brief Define a command entry with exec and query handlers
 */
#define AT_CMD_EXEC_QUERY(name, exec_handler, query_handler, help_text) \
    { name, exec_handler, query_handler, NULL, help_text, 0, 0 }

/**
 * @brief Define a command entry with query handler only
 */
#define AT_CMD_QUERY(name, handler, help_text) \
    { name, NULL, handler, NULL, help_text, 0, 0 }

/**
 * @brief Define a command entry with all handlers
 */
#define AT_CMD_FULL(name, exec, query, test, help_text, min, max) \
    { name, exec, query, test, help_text, min, max }

/**
 * @brief Get array size
 */
#define AT_ARRAY_SIZE(arr)  (sizeof(arr) / sizeof((arr)[0]))

#ifdef __cplusplus
}
#endif

#endif /* AT_COMMANDS_H */
