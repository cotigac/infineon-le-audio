/**
 * @file at_system_cmds.c
 * @brief System AT Command Handlers Implementation
 *
 * Implements system commands for device information,
 * configuration, and general control.
 *
 * Commands:
 * - AT       : Test communication
 * - ATI      : Device information
 * - AT+VERSION : Firmware version
 * - AT+RST   : Soft reset
 * - AT+ECHO  : Echo control
 * - AT+HELP  : List commands
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "at_system_cmds.h"
#include "at_commands.h"
#include "cdc_acm.h"

#include <string.h>
#include <stdlib.h>

/* For software reset */
#include "cy_syslib.h"

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static int cmd_i_exec(int argc, const char *argv[]);
static int cmd_version_query(int argc, const char *argv[]);
static int cmd_rst_exec(int argc, const char *argv[]);
static int cmd_echo_exec(int argc, const char *argv[]);
static int cmd_echo_query(int argc, const char *argv[]);
static int cmd_help_exec(int argc, const char *argv[]);
static int cmd_gmr_query(int argc, const char *argv[]);

/*******************************************************************************
 * Command Table
 ******************************************************************************/

/**
 * @brief System command table
 */
static const at_cmd_entry_t g_system_cmds[] = {
    /* ATI - Device information */
    {
        .name = "I",
        .exec = cmd_i_exec,
        .query = NULL,
        .test = NULL,
        .help = "Device information",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+VERSION - Firmware version */
    {
        .name = "VERSION",
        .exec = NULL,
        .query = cmd_version_query,
        .test = NULL,
        .help = "Firmware version",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+GMR - Also firmware version (ESP32 compatible) */
    {
        .name = "GMR",
        .exec = NULL,
        .query = cmd_gmr_query,
        .test = NULL,
        .help = "Firmware version (GMR)",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+RST - Soft reset */
    {
        .name = "RST",
        .exec = cmd_rst_exec,
        .query = NULL,
        .test = NULL,
        .help = "Soft reset",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+ECHO - Echo control */
    {
        .name = "ECHO",
        .exec = cmd_echo_exec,
        .query = cmd_echo_query,
        .test = NULL,
        .help = "Echo on/off",
        .min_args = 1,
        .max_args = 1
    },

    /* AT+HELP - List commands */
    {
        .name = "HELP",
        .exec = cmd_help_exec,
        .query = NULL,
        .test = NULL,
        .help = "List commands",
        .min_args = 0,
        .max_args = 0
    },
};

#define SYSTEM_CMD_COUNT (sizeof(g_system_cmds) / sizeof(g_system_cmds[0]))

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

const at_cmd_entry_t *at_system_cmds_get_table(void)
{
    return g_system_cmds;
}

size_t at_system_cmds_get_count(void)
{
    return SYSTEM_CMD_COUNT;
}

int at_system_cmds_register(void)
{
    return at_parser_register_commands(g_system_cmds, SYSTEM_CMD_COUNT);
}

/*******************************************************************************
 * Command Handlers
 ******************************************************************************/

/**
 * @brief ATI - Device information
 */
static int cmd_i_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\n+INFO: %s,%s\r\n", DEVICE_MODEL, FW_VERSION_STRING);
    cdc_acm_printf("+INFO: Manufacturer: %s\r\n", DEVICE_MANUFACTURER);
    cdc_acm_printf("+INFO: Build: %s %s\r\n", __DATE__, __TIME__);

    return CME_SUCCESS;
}

/**
 * @brief AT+VERSION? - Query firmware version
 */
static int cmd_version_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_send_response("VERSION", FW_VERSION_STRING);

    return CME_SUCCESS;
}

/**
 * @brief AT+GMR? - Query firmware version (ESP32 compatible)
 */
static int cmd_gmr_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\nAT version:%s\r\n", FW_VERSION_STRING);
    cdc_acm_printf("Build:%s %s\r\n", __DATE__, __TIME__);

    return CME_SUCCESS;
}

/**
 * @brief AT+RST - Soft reset
 */
static int cmd_rst_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\nResetting...\r\n");

    /* Small delay to let the message be sent */
    for (volatile int i = 0; i < 100000; i++) {
        __asm("nop");
    }

    /* Perform software reset */
    NVIC_SystemReset();

    /* Should not reach here */
    return CME_SUCCESS;
}

/**
 * @brief AT+ECHO=<n> - Set echo mode
 */
static int cmd_echo_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    int32_t value;
    if (at_parse_int(argv[0], &value) != 0) {
        return CME_INVALID_PARAM;
    }

    if (value != 0 && value != 1) {
        return CME_INVALID_PARAM;
    }

    at_parser_set_echo(value != 0);

    return CME_SUCCESS;
}

/**
 * @brief AT+ECHO? - Query echo mode
 */
static int cmd_echo_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\n+ECHO: %d\r\n", at_parser_get_echo() ? 1 : 0);

    return CME_SUCCESS;
}

/**
 * @brief AT+HELP - List commands
 */
static int cmd_help_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    at_parser_print_help();

    return CME_SUCCESS;
}
