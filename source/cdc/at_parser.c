/**
 * @file at_parser.c
 * @brief AT Command Parser Implementation
 *
 * This module parses AT commands and dispatches them to
 * registered command handlers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "at_parser.h"
#include "at_commands.h"
#include "cdc_acm.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Command table entry */
typedef struct {
    const at_cmd_entry_t *table;
    size_t count;
} cmd_table_entry_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Registered command tables */
static cmd_table_entry_t g_cmd_tables[AT_MAX_CMD_TABLES];
static size_t g_cmd_table_count = 0;

/** Line buffer for character-by-character input */
static char g_line_buffer[AT_MAX_LINE_LEN];
static uint16_t g_line_pos = 0;

/** Echo mode */
static bool g_echo_enabled = true;

/** Parser initialized flag */
static bool g_initialized = false;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static at_result_t parse_and_execute(const char *line);
static const at_cmd_entry_t *find_command(const char *name);
static int tokenize(char *line, const char **argv, int max_args);
static void str_toupper(char *str);

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

int at_parser_init(void)
{
    g_cmd_table_count = 0;
    g_line_pos = 0;
    g_echo_enabled = true;
    g_initialized = true;

    memset(g_cmd_tables, 0, sizeof(g_cmd_tables));
    memset(g_line_buffer, 0, sizeof(g_line_buffer));

    return 0;
}

void at_parser_deinit(void)
{
    g_initialized = false;
    g_cmd_table_count = 0;
}

int at_parser_register_commands(const at_cmd_entry_t *table, size_t count)
{
    if (table == NULL || count == 0) {
        return -1;
    }

    if (g_cmd_table_count >= AT_MAX_CMD_TABLES) {
        return -1;
    }

    g_cmd_tables[g_cmd_table_count].table = table;
    g_cmd_tables[g_cmd_table_count].count = count;
    g_cmd_table_count++;

    return 0;
}

void at_parser_process_char(char c)
{
    if (!g_initialized) {
        return;
    }

    /* Echo if enabled */
    if (g_echo_enabled) {
        cdc_acm_write(&c, 1);
    }

    /* Handle line endings */
    if (c == '\r' || c == '\n') {
        if (g_line_pos > 0) {
            g_line_buffer[g_line_pos] = '\0';
            at_parser_process_line(g_line_buffer);
            g_line_pos = 0;
        }
        return;
    }

    /* Handle backspace */
    if (c == '\b' || c == 0x7F) {
        if (g_line_pos > 0) {
            g_line_pos--;
            if (g_echo_enabled) {
                cdc_acm_write("\b \b", 3);
            }
        }
        return;
    }

    /* Ignore control characters */
    if (c < 0x20) {
        return;
    }

    /* Add to buffer */
    if (g_line_pos < AT_MAX_LINE_LEN - 1) {
        g_line_buffer[g_line_pos++] = c;
    }
}

at_result_t at_parser_process_line(const char *line)
{
    if (!g_initialized || line == NULL) {
        return AT_RESULT_ERROR;
    }

    /* Skip leading whitespace */
    while (*line == ' ' || *line == '\t') {
        line++;
    }

    /* Empty line */
    if (*line == '\0') {
        return AT_RESULT_OK;
    }

    return parse_and_execute(line);
}

void at_parser_set_echo(bool enabled)
{
    g_echo_enabled = enabled;
    cdc_acm_set_echo(enabled);
}

bool at_parser_get_echo(void)
{
    return g_echo_enabled;
}

const at_cmd_entry_t *at_parser_find_command(const char *name)
{
    return find_command(name);
}

void at_parser_print_help(void)
{
    cdc_acm_printf("\r\nAvailable commands:\r\n");

    for (size_t t = 0; t < g_cmd_table_count; t++) {
        const at_cmd_entry_t *table = g_cmd_tables[t].table;
        size_t count = g_cmd_tables[t].count;

        for (size_t i = 0; i < count; i++) {
            const at_cmd_entry_t *cmd = &table[i];
            if (cmd->name != NULL) {
                cdc_acm_printf("  AT+%-16s %s\r\n",
                    cmd->name,
                    cmd->help ? cmd->help : "");
            }
        }
    }
}

size_t at_parser_get_command_count(void)
{
    size_t total = 0;
    for (size_t t = 0; t < g_cmd_table_count; t++) {
        total += g_cmd_tables[t].count;
    }
    return total;
}

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

int at_parse_string(const char *arg, char *out, size_t max_len)
{
    if (arg == NULL || out == NULL || max_len == 0) {
        return -1;
    }

    size_t len = strlen(arg);
    const char *start = arg;
    const char *end = arg + len;

    /* Remove surrounding quotes if present */
    if (len >= 2 && arg[0] == '"' && arg[len-1] == '"') {
        start++;
        end--;
        len -= 2;
    }

    if (len >= max_len) {
        len = max_len - 1;
    }

    memcpy(out, start, len);
    out[len] = '\0';

    return (int)len;
}

int at_parse_int(const char *arg, int32_t *out)
{
    if (arg == NULL || out == NULL) {
        return -1;
    }

    char *endptr;
    long val = strtol(arg, &endptr, 0);

    if (endptr == arg || *endptr != '\0') {
        return -1;
    }

    *out = (int32_t)val;
    return 0;
}

int at_parse_uint(const char *arg, uint32_t *out)
{
    if (arg == NULL || out == NULL) {
        return -1;
    }

    char *endptr;
    unsigned long val = strtoul(arg, &endptr, 0);

    if (endptr == arg || *endptr != '\0') {
        return -1;
    }

    *out = (uint32_t)val;
    return 0;
}

int at_parse_hex(const char *arg, uint8_t *out, size_t max_len)
{
    if (arg == NULL || out == NULL || max_len == 0) {
        return -1;
    }

    /* Skip optional "0x" prefix */
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
        arg += 2;
    }

    size_t len = strlen(arg);
    if (len % 2 != 0) {
        return -1;  /* Must be even number of hex digits */
    }

    size_t bytes = len / 2;
    if (bytes > max_len) {
        bytes = max_len;
    }

    for (size_t i = 0; i < bytes; i++) {
        char hex[3] = { arg[i*2], arg[i*2+1], '\0' };
        char *endptr;
        unsigned long val = strtoul(hex, &endptr, 16);
        if (endptr != hex + 2) {
            return -1;
        }
        out[i] = (uint8_t)val;
    }

    return (int)bytes;
}

int at_parse_mac(const char *arg, uint8_t out[6])
{
    if (arg == NULL || out == NULL) {
        return -1;
    }

    /* Parse MAC address: AA:BB:CC:DD:EE:FF or AABBCCDDEEFF */
    unsigned int bytes[6];
    int parsed;

    /* Try colon-separated format */
    parsed = sscanf(arg, "%02X:%02X:%02X:%02X:%02X:%02X",
        &bytes[0], &bytes[1], &bytes[2],
        &bytes[3], &bytes[4], &bytes[5]);

    if (parsed != 6) {
        /* Try dash-separated format */
        parsed = sscanf(arg, "%02X-%02X-%02X-%02X-%02X-%02X",
            &bytes[0], &bytes[1], &bytes[2],
            &bytes[3], &bytes[4], &bytes[5]);
    }

    if (parsed != 6) {
        /* Try continuous format */
        parsed = sscanf(arg, "%02X%02X%02X%02X%02X%02X",
            &bytes[0], &bytes[1], &bytes[2],
            &bytes[3], &bytes[4], &bytes[5]);
    }

    if (parsed != 6) {
        return -1;
    }

    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)bytes[i];
    }

    return 0;
}

void at_format_mac(const uint8_t mac[6], char *out)
{
    if (mac == NULL || out == NULL) {
        return;
    }

    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

/**
 * @brief Parse and execute an AT command line
 */
static at_result_t parse_and_execute(const char *line)
{
    /* Make a working copy */
    char work[AT_MAX_LINE_LEN];
    strncpy(work, line, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    /* Convert to uppercase for command matching */
    char cmd_part[AT_MAX_CMD_NAME];
    strncpy(cmd_part, work, sizeof(cmd_part) - 1);
    cmd_part[sizeof(cmd_part) - 1] = '\0';

    /* Find command part (before = or ?) */
    char *eq_pos = strchr(cmd_part, '=');
    char *q_pos = strchr(cmd_part, '?');

    if (eq_pos != NULL) *eq_pos = '\0';
    if (q_pos != NULL && (eq_pos == NULL || q_pos < eq_pos)) *q_pos = '\0';

    str_toupper(cmd_part);

    /* Check for "AT" prefix */
    if (strncmp(cmd_part, "AT", 2) != 0) {
        cdc_acm_send_error();
        return AT_RESULT_ERROR;
    }

    /* Handle basic "AT" command */
    if (strcmp(cmd_part, "AT") == 0) {
        cdc_acm_send_ok();
        return AT_RESULT_OK;
    }

    /* Handle "AT+" prefix */
    if (cmd_part[2] != '+') {
        /* Could be ATI, ATE, etc. - handle specially */
        if (strcmp(cmd_part, "ATI") == 0) {
            /* Device info - delegate to system commands */
            const at_cmd_entry_t *cmd = find_command("I");
            if (cmd != NULL && cmd->exec != NULL) {
                int result = cmd->exec(0, NULL);
                if (result == 0) {
                    cdc_acm_send_ok();
                    return AT_RESULT_OK;
                }
            }
            cdc_acm_send_error();
            return AT_RESULT_ERROR;
        }

        cdc_acm_send_error();
        return AT_RESULT_ERROR;
    }

    /* Extract command name (after AT+) */
    const char *cmd_name = cmd_part + 3;

    /* Determine command type */
    at_cmd_type_t cmd_type = AT_CMD_TYPE_EXEC;
    char *args_start = NULL;

    /* Find = or ? in original line */
    char *orig_eq = strchr(work + 3, '=');
    char *orig_q = strchr(work + 3, '?');

    if (orig_q != NULL && (orig_eq == NULL || orig_q < orig_eq)) {
        /* Query: AT+CMD? */
        cmd_type = AT_CMD_TYPE_QUERY;
    } else if (orig_eq != NULL) {
        if (orig_eq[1] == '?') {
            /* Test: AT+CMD=? */
            cmd_type = AT_CMD_TYPE_TEST;
        } else {
            /* Execute with args: AT+CMD=args */
            cmd_type = AT_CMD_TYPE_EXEC;
            args_start = orig_eq + 1;
        }
    }

    /* Find command handler */
    const at_cmd_entry_t *cmd = find_command(cmd_name);

    if (cmd == NULL) {
        cdc_acm_send_cme_error(CME_NOT_SUPPORTED);
        return AT_RESULT_NOT_FOUND;
    }

    /* Parse arguments if present */
    const char *argv[AT_MAX_ARGS];
    int argc = 0;

    if (args_start != NULL) {
        argc = tokenize(args_start, argv, AT_MAX_ARGS);
    }

    /* Execute appropriate handler */
    int result = CME_NOT_SUPPORTED;

    switch (cmd_type) {
        case AT_CMD_TYPE_EXEC:
            if (cmd->exec != NULL) {
                /* Check argument count */
                if (argc < cmd->min_args) {
                    cdc_acm_send_cme_error(CME_INVALID_PARAM);
                    return AT_RESULT_INVALID_ARGS;
                }
                if (cmd->max_args > 0 && argc > cmd->max_args) {
                    cdc_acm_send_cme_error(CME_INVALID_PARAM);
                    return AT_RESULT_INVALID_ARGS;
                }
                result = cmd->exec(argc, argv);
            }
            break;

        case AT_CMD_TYPE_QUERY:
            if (cmd->query != NULL) {
                result = cmd->query(0, NULL);
            } else if (cmd->exec != NULL) {
                /* Some commands use exec for query too */
                result = cmd->exec(0, NULL);
            }
            break;

        case AT_CMD_TYPE_TEST:
            if (cmd->test != NULL) {
                result = cmd->test(0, NULL);
            } else {
                /* Default test response */
                cdc_acm_printf("\r\n+%s: (supported)\r\n", cmd->name);
                result = 0;
            }
            break;
    }

    if (result == 0) {
        cdc_acm_send_ok();
        return AT_RESULT_OK;
    } else {
        cdc_acm_send_cme_error(result);
        return AT_RESULT_ERROR;
    }
}

/**
 * @brief Find a command by name
 */
static const at_cmd_entry_t *find_command(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    /* Convert name to uppercase for comparison */
    char upper_name[AT_MAX_CMD_NAME];
    strncpy(upper_name, name, sizeof(upper_name) - 1);
    upper_name[sizeof(upper_name) - 1] = '\0';
    str_toupper(upper_name);

    for (size_t t = 0; t < g_cmd_table_count; t++) {
        const at_cmd_entry_t *table = g_cmd_tables[t].table;
        size_t count = g_cmd_tables[t].count;

        for (size_t i = 0; i < count; i++) {
            if (table[i].name != NULL) {
                /* Compare uppercase */
                char cmd_upper[AT_MAX_CMD_NAME];
                strncpy(cmd_upper, table[i].name, sizeof(cmd_upper) - 1);
                cmd_upper[sizeof(cmd_upper) - 1] = '\0';
                str_toupper(cmd_upper);

                if (strcmp(upper_name, cmd_upper) == 0) {
                    return &table[i];
                }
            }
        }
    }

    return NULL;
}

/**
 * @brief Tokenize argument string
 *
 * Handles quoted strings and comma-separated values.
 */
static int tokenize(char *line, const char **argv, int max_args)
{
    int argc = 0;
    char *p = line;
    bool in_quotes = false;

    while (*p != '\0' && argc < max_args) {
        /* Skip leading whitespace and commas */
        while (*p == ' ' || *p == '\t' || (*p == ',' && !in_quotes)) {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        /* Check for quoted string */
        if (*p == '"') {
            in_quotes = true;
            p++;
            argv[argc++] = p;

            /* Find closing quote */
            while (*p != '\0' && *p != '"') {
                p++;
            }

            if (*p == '"') {
                *p = '\0';
                p++;
            }
            in_quotes = false;
        } else {
            /* Unquoted argument */
            argv[argc++] = p;

            /* Find end of argument */
            while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t') {
                p++;
            }

            if (*p != '\0') {
                *p = '\0';
                p++;
            }
        }
    }

    return argc;
}

/**
 * @brief Convert string to uppercase
 */
static void str_toupper(char *str)
{
    while (*str) {
        *str = toupper((unsigned char)*str);
        str++;
    }
}
