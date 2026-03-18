/**
 * @file at_parser.h
 * @brief AT Command Parser
 *
 * This module implements an AT command parser supporting standard
 * AT command syntax for Bluetooth and Wi-Fi configuration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AT_PARSER_H
#define AT_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum AT command line length */
#define AT_MAX_LINE_LEN         256

/** Maximum number of command arguments */
#define AT_MAX_ARGS             16

/** Maximum command name length */
#define AT_MAX_CMD_NAME         32

/** Maximum number of registered command tables */
#define AT_MAX_CMD_TABLES       8

/*******************************************************************************
 * Types
 ******************************************************************************/

/**
 * @brief Command handler function signature
 *
 * @param argc Number of arguments
 * @param argv Array of argument strings (argv[0] is the command name)
 * @return 0 on success, error code on failure
 */
typedef int (*at_cmd_handler_t)(int argc, const char *argv[]);

/**
 * @brief Command table entry
 */
typedef struct {
    const char         *name;       /**< Command name (without AT+) */
    at_cmd_handler_t    exec;       /**< Execute handler (AT+CMD or AT+CMD=params) */
    at_cmd_handler_t    query;      /**< Query handler (AT+CMD?) */
    at_cmd_handler_t    test;       /**< Test handler (AT+CMD=?) */
    const char         *help;       /**< Help text */
    uint8_t             min_args;   /**< Minimum arguments for exec */
    uint8_t             max_args;   /**< Maximum arguments for exec */
} at_cmd_entry_t;

/**
 * @brief AT command type
 */
typedef enum {
    AT_CMD_TYPE_EXEC,       /**< Execute: AT+CMD or AT+CMD=params */
    AT_CMD_TYPE_QUERY,      /**< Query: AT+CMD? */
    AT_CMD_TYPE_TEST        /**< Test: AT+CMD=? */
} at_cmd_type_t;

/**
 * @brief AT parser result codes
 */
typedef enum {
    AT_RESULT_OK = 0,           /**< Command executed successfully */
    AT_RESULT_ERROR = 1,        /**< Generic error */
    AT_RESULT_NOT_FOUND = 2,    /**< Command not found */
    AT_RESULT_INVALID_ARGS = 3, /**< Invalid arguments */
    AT_RESULT_NOT_SUPPORTED = 4 /**< Command not supported */
} at_result_t;

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize AT parser
 *
 * @return 0 on success
 */
int at_parser_init(void);

/**
 * @brief Deinitialize AT parser
 */
void at_parser_deinit(void);

/**
 * @brief Register a command table
 *
 * Multiple tables can be registered for modular command organization.
 *
 * @param table Array of command entries (must remain valid)
 * @param count Number of entries in table
 * @return 0 on success, negative on error
 */
int at_parser_register_commands(const at_cmd_entry_t *table, size_t count);

/**
 * @brief Process a single character
 *
 * Feed characters one at a time for streaming input.
 * Command is executed when CR or LF is received.
 *
 * @param c Character received
 */
void at_parser_process_char(char c);

/**
 * @brief Process a complete line
 *
 * @param line Null-terminated line (without CR/LF)
 * @return Result code
 */
at_result_t at_parser_process_line(const char *line);

/**
 * @brief Enable/disable command echo
 *
 * @param enabled true to echo characters back
 */
void at_parser_set_echo(bool enabled);

/**
 * @brief Check if echo is enabled
 *
 * @return true if echo is enabled
 */
bool at_parser_get_echo(void);

/**
 * @brief Find a command by name
 *
 * @param name Command name (without AT+)
 * @return Pointer to command entry, or NULL if not found
 */
const at_cmd_entry_t *at_parser_find_command(const char *name);

/**
 * @brief Print help for all registered commands
 */
void at_parser_print_help(void);

/**
 * @brief Get the number of registered commands
 *
 * @return Number of commands
 */
size_t at_parser_get_command_count(void);

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * @brief Parse a string argument (remove quotes if present)
 *
 * @param arg Argument string
 * @param out Output buffer
 * @param max_len Maximum output length
 * @return Length of parsed string, negative on error
 */
int at_parse_string(const char *arg, char *out, size_t max_len);

/**
 * @brief Parse an integer argument
 *
 * @param arg Argument string
 * @param out Output value
 * @return 0 on success, negative on error
 */
int at_parse_int(const char *arg, int32_t *out);

/**
 * @brief Parse an unsigned integer argument
 *
 * @param arg Argument string
 * @param out Output value
 * @return 0 on success, negative on error
 */
int at_parse_uint(const char *arg, uint32_t *out);

/**
 * @brief Parse a hex string argument (e.g., "0xAABBCC" or "AABBCC")
 *
 * @param arg Argument string
 * @param out Output buffer
 * @param max_len Maximum output length
 * @return Number of bytes parsed, negative on error
 */
int at_parse_hex(const char *arg, uint8_t *out, size_t max_len);

/**
 * @brief Parse a MAC address (e.g., "AA:BB:CC:DD:EE:FF")
 *
 * @param arg Argument string
 * @param out Output buffer (6 bytes)
 * @return 0 on success, negative on error
 */
int at_parse_mac(const char *arg, uint8_t out[6]);

/**
 * @brief Format a MAC address to string
 *
 * @param mac MAC address (6 bytes)
 * @param out Output buffer (at least 18 bytes)
 */
void at_format_mac(const uint8_t mac[6], char *out);

#ifdef __cplusplus
}
#endif

#endif /* AT_PARSER_H */
