/**
 * @file at_leaudio_cmds.c
 * @brief LE Audio AT Command Handlers Implementation
 *
 * Implements AT commands for LE Audio configuration and control:
 * - Initialization (AT+LEAINIT, AT+LEADEINIT)
 * - State/mode queries (AT+LEASTATE, AT+LEAMODE)
 * - Broadcast control (AT+LEABROADCAST)
 * - Unicast control (AT+LEAUNICAST)
 * - Codec configuration (AT+LEACODEC)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "at_leaudio_cmds.h"
#include "at_parser.h"
#include "at_commands.h"
#include "cdc_acm.h"
#include "le_audio/le_audio_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "queue.h"

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** URC queue depth */
#define LEA_URC_QUEUE_DEPTH     8

/** Maximum URC message length */
#define LEA_URC_MAX_LEN         128

/** URC message structure */
typedef struct {
    char message[LEA_URC_MAX_LEN];
} lea_urc_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** URC queue for async events */
static QueueHandle_t g_urc_queue = NULL;

/** Module initialized flag */
static bool g_module_init = false;

/** Current codec configuration (cached for queries) */
static le_audio_codec_config_t g_codec_config = LE_AUDIO_CODEC_CONFIG_DEFAULT;

/** Current broadcast configuration (cached for queries) */
static le_audio_broadcast_config_t g_broadcast_config = LE_AUDIO_BROADCAST_CONFIG_DEFAULT;

/** State name lookup table */
static const char *g_state_names[] = {
    [LE_AUDIO_STATE_IDLE]           = "IDLE",
    [LE_AUDIO_STATE_CONFIGURED]     = "CONFIGURED",
    [LE_AUDIO_STATE_QOS_CONFIGURED] = "QOS_CONFIGURED",
    [LE_AUDIO_STATE_ENABLING]       = "ENABLING",
    [LE_AUDIO_STATE_STREAMING]      = "STREAMING",
    [LE_AUDIO_STATE_DISABLING]      = "DISABLING",
    [LE_AUDIO_STATE_ERROR]          = "ERROR"
};

/** Mode name lookup table */
static const char *g_mode_names[] = {
    [LE_AUDIO_MODE_IDLE]             = "IDLE",
    [LE_AUDIO_MODE_UNICAST_SOURCE]   = "UNICAST_SOURCE",
    [LE_AUDIO_MODE_UNICAST_SINK]     = "UNICAST_SINK",
    [LE_AUDIO_MODE_UNICAST_DUPLEX]   = "UNICAST_DUPLEX",
    [LE_AUDIO_MODE_BROADCAST_SOURCE] = "BROADCAST_SOURCE",
    [LE_AUDIO_MODE_BROADCAST_SINK]   = "BROADCAST_SINK"
};

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

/* Command handlers */
static int cmd_leainit_exec(int argc, const char *argv[]);
static int cmd_leadeinit_exec(int argc, const char *argv[]);
static int cmd_leastate_query(int argc, const char *argv[]);
static int cmd_leamode_query(int argc, const char *argv[]);
static int cmd_leabroadcast_exec(int argc, const char *argv[]);
static int cmd_leabroadcast_query(int argc, const char *argv[]);
static int cmd_leaunicast_exec(int argc, const char *argv[]);
static int cmd_leaunicast_query(int argc, const char *argv[]);
static int cmd_leacodec_exec(int argc, const char *argv[]);
static int cmd_leacodec_query(int argc, const char *argv[]);
static int cmd_leainfo_query(int argc, const char *argv[]);
static int cmd_leascan_exec(int argc, const char *argv[]);
static int cmd_leascan_query(int argc, const char *argv[]);
static int cmd_leasync_exec(int argc, const char *argv[]);
static int cmd_leasink_exec(int argc, const char *argv[]);
static int cmd_leasink_query(int argc, const char *argv[]);
static int cmd_leademo_exec(int argc, const char *argv[]);

/* Helper functions */
static void send_urc(const char *fmt, ...);
static void le_audio_event_callback(const le_audio_event_t *event, void *user_data);
static const char *get_state_name(le_audio_state_t state);
static const char *get_mode_name(le_audio_mode_t mode);

/*******************************************************************************
 * Command Table
 ******************************************************************************/

static const at_cmd_entry_t g_leaudio_cmds[] = {
    /* AT+LEAINIT - Initialize LE Audio */
    {
        .name = "LEAINIT",
        .exec = cmd_leainit_exec,
        .query = NULL,
        .test = NULL,
        .help = "Initialize LE Audio [sample_rate,frame_duration_us]",
        .min_args = 0,
        .max_args = 2
    },

    /* AT+LEADEINIT - Deinitialize LE Audio */
    {
        .name = "LEADEINIT",
        .exec = cmd_leadeinit_exec,
        .query = NULL,
        .test = NULL,
        .help = "Deinitialize LE Audio",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+LEASTATE? - Query state */
    {
        .name = "LEASTATE",
        .exec = NULL,
        .query = cmd_leastate_query,
        .test = NULL,
        .help = "Query LE Audio state",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+LEAMODE? - Query mode */
    {
        .name = "LEAMODE",
        .exec = NULL,
        .query = cmd_leamode_query,
        .test = NULL,
        .help = "Query LE Audio mode",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+LEABROADCAST - Auracast broadcast control */
    {
        .name = "LEABROADCAST",
        .exec = cmd_leabroadcast_exec,
        .query = cmd_leabroadcast_query,
        .test = NULL,
        .help = "Start/stop broadcast (0|1[,name[,context]])",
        .min_args = 1,
        .max_args = 3
    },

    /* AT+LEAUNICAST - Unicast streaming control */
    {
        .name = "LEAUNICAST",
        .exec = cmd_leaunicast_exec,
        .query = cmd_leaunicast_query,
        .test = NULL,
        .help = "Start/stop unicast (0|1[,conn_handle[,bidirectional]])",
        .min_args = 1,
        .max_args = 3
    },

    /* AT+LEACODEC - Codec configuration */
    {
        .name = "LEACODEC",
        .exec = cmd_leacodec_exec,
        .query = cmd_leacodec_query,
        .test = NULL,
        .help = "Configure LC3 codec (sample_rate,frame_duration[,octets])",
        .min_args = 2,
        .max_args = 3
    },

    /* AT+LEAINFO? - LE Audio information */
    {
        .name = "LEAINFO",
        .exec = NULL,
        .query = cmd_leainfo_query,
        .test = NULL,
        .help = "Display LE Audio information",
        .min_args = 0,
        .max_args = 0
    },

    /* AT+LEASCAN - Scan for Auracast broadcasts (Sink) */
    {
        .name = "LEASCAN",
        .exec = cmd_leascan_exec,
        .query = cmd_leascan_query,
        .test = NULL,
        .help = "Start/stop scanning for broadcasts (0|1)",
        .min_args = 1,
        .max_args = 1
    },

    /* AT+LEASYNC - Sync to Auracast broadcast (Sink) */
    {
        .name = "LEASYNC",
        .exec = cmd_leasync_exec,
        .query = NULL,
        .test = NULL,
        .help = "Sync to broadcast (broadcast_id_hex[,broadcast_code_hex])",
        .min_args = 1,
        .max_args = 2
    },

    /* AT+LEASINK - Broadcast sink control */
    {
        .name = "LEASINK",
        .exec = cmd_leasink_exec,
        .query = cmd_leasink_query,
        .test = NULL,
        .help = "Stop broadcast sink (0)",
        .min_args = 1,
        .max_args = 1
    },

    /* AT+LEADEMO - Demo auto-sync */
    {
        .name = "LEADEMO",
        .exec = cmd_leademo_exec,
        .query = NULL,
        .test = NULL,
        .help = "Auto-sync to first broadcast [broadcast_code_hex]",
        .min_args = 0,
        .max_args = 1
    },
};

#define LEAUDIO_CMD_COUNT (sizeof(g_leaudio_cmds) / sizeof(g_leaudio_cmds[0]))

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

const at_cmd_entry_t *at_leaudio_cmds_get_table(void)
{
    return g_leaudio_cmds;
}

size_t at_leaudio_cmds_get_count(void)
{
    return LEAUDIO_CMD_COUNT;
}

int at_leaudio_cmds_register(void)
{
    /* Create URC queue if not exists */
    if (g_urc_queue == NULL) {
        g_urc_queue = xQueueCreate(LEA_URC_QUEUE_DEPTH, sizeof(lea_urc_t));
        if (g_urc_queue == NULL) {
            return -1;
        }
    }

    return at_parser_register_commands(g_leaudio_cmds, LEAUDIO_CMD_COUNT);
}

void at_leaudio_cmds_process(void)
{
    lea_urc_t urc;

    /* Process all pending URCs */
    while (g_urc_queue != NULL &&
           xQueueReceive(g_urc_queue, &urc, 0) == pdTRUE) {
        cdc_acm_printf("%s", urc.message);
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

    lea_urc_t urc;
    va_list args;
    va_start(args, fmt);
    vsnprintf(urc.message, sizeof(urc.message), fmt, args);
    va_end(args);

    xQueueSend(g_urc_queue, &urc, 0);
}

/**
 * @brief Get state name string
 */
static const char *get_state_name(le_audio_state_t state)
{
    if (state < sizeof(g_state_names) / sizeof(g_state_names[0])) {
        return g_state_names[state];
    }
    return "UNKNOWN";
}

/**
 * @brief Get mode name string
 */
static const char *get_mode_name(le_audio_mode_t mode)
{
    if (mode < sizeof(g_mode_names) / sizeof(g_mode_names[0])) {
        return g_mode_names[mode];
    }
    return "UNKNOWN";
}

/**
 * @brief LE Audio event callback for URCs
 */
static void le_audio_event_callback(const le_audio_event_t *event, void *user_data)
{
    (void)user_data;

    switch (event->type) {
        case LE_AUDIO_EVENT_STATE_CHANGED:
            send_urc("\r\n+LEASTATE: %s\r\n", get_state_name(event->data.new_state));
            break;

        case LE_AUDIO_EVENT_STREAM_STARTED:
            send_urc("\r\n+LEASTREAM: STARTED\r\n");
            break;

        case LE_AUDIO_EVENT_STREAM_STOPPED:
            send_urc("\r\n+LEASTREAM: STOPPED\r\n");
            break;

        case LE_AUDIO_EVENT_DEVICE_CONNECTED:
            send_urc("\r\n+LEADEVICE: CONNECTED\r\n");
            break;

        case LE_AUDIO_EVENT_DEVICE_DISCONNECTED:
            send_urc("\r\n+LEADEVICE: DISCONNECTED\r\n");
            break;

        case LE_AUDIO_EVENT_BROADCAST_FOUND:
            /* Report discovered Auracast broadcast */
            send_urc("\r\n+LEABROADCAST_FOUND: %02X%02X%02X,\"%s\",%d,%s\r\n",
                     event->data.broadcast.broadcast_id[0],
                     event->data.broadcast.broadcast_id[1],
                     event->data.broadcast.broadcast_id[2],
                     event->data.broadcast.broadcast_name,
                     event->data.broadcast.rssi,
                     event->data.broadcast.encrypted ? "ENCRYPTED" : "OPEN");
            break;

        case LE_AUDIO_EVENT_BROADCAST_SYNCED:
            send_urc("\r\n+LEASINK: SYNCED\r\n");
            break;

        case LE_AUDIO_EVENT_BROADCAST_LOST:
            send_urc("\r\n+LEASINK: LOST\r\n");
            break;

        case LE_AUDIO_EVENT_ERROR:
            send_urc("\r\n+LEAERROR: %d\r\n", event->data.error_code);
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * Command Handlers - Initialization
 ******************************************************************************/

/**
 * @brief AT+LEAINIT[=sample_rate,frame_duration] - Initialize LE Audio
 */
static int cmd_leainit_exec(int argc, const char *argv[])
{
    if (g_module_init) {
        return CME_LEA_ALREADY_INIT;
    }

    /* Parse optional codec parameters */
    if (argc >= 2) {
        uint32_t sample_rate = (uint32_t)atoi(argv[0]);
        uint32_t frame_duration = (uint32_t)atoi(argv[1]);

        /* Validate sample rate */
        if (sample_rate != 8000 && sample_rate != 16000 &&
            sample_rate != 24000 && sample_rate != 32000 &&
            sample_rate != 48000) {
            return CME_INVALID_PARAM;
        }

        /* Validate frame duration */
        if (frame_duration != 7500 && frame_duration != 10000) {
            return CME_INVALID_PARAM;
        }

        g_codec_config.sample_rate = sample_rate;
        g_codec_config.frame_duration_us = (uint16_t)frame_duration;

        /* Calculate octets per frame (80kbps default) */
        uint32_t bitrate = 80000;
        g_codec_config.octets_per_frame = (uint16_t)((bitrate * frame_duration) / (8 * 1000000));
    }

    /* Initialize LE Audio */
    int result = le_audio_init(&g_codec_config);
    if (result != 0) {
        return CME_LEA_CONFIG_ERROR;
    }

    /* Register event callback */
    le_audio_register_callback(le_audio_event_callback, NULL);

    g_module_init = true;

    cdc_acm_printf("\r\n+LEAINIT: OK,%lu,%u\r\n",
                   (unsigned long)g_codec_config.sample_rate,
                   g_codec_config.frame_duration_us);

    return CME_SUCCESS;
}

/**
 * @brief AT+LEADEINIT - Deinitialize LE Audio
 */
static int cmd_leadeinit_exec(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!g_module_init) {
        return CME_LEA_NOT_INIT;
    }

    le_audio_deinit();
    g_module_init = false;

    cdc_acm_printf("\r\n+LEADEINIT: OK\r\n");
    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - State Queries
 ******************************************************************************/

/**
 * @brief AT+LEASTATE? - Query LE Audio state
 */
static int cmd_leastate_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!g_module_init) {
        cdc_acm_printf("\r\n+LEASTATE: NOT_INITIALIZED\r\n");
        return CME_SUCCESS;
    }

    le_audio_state_t state = le_audio_get_state();
    cdc_acm_printf("\r\n+LEASTATE: %s\r\n", get_state_name(state));

    return CME_SUCCESS;
}

/**
 * @brief AT+LEAMODE? - Query LE Audio mode
 */
static int cmd_leamode_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!g_module_init) {
        cdc_acm_printf("\r\n+LEAMODE: NOT_INITIALIZED\r\n");
        return CME_SUCCESS;
    }

    le_audio_mode_t mode = le_audio_get_mode();
    cdc_acm_printf("\r\n+LEAMODE: %s\r\n", get_mode_name(mode));

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Broadcast
 ******************************************************************************/

/**
 * @brief AT+LEABROADCAST=enable[,name[,context]] - Start/stop broadcast
 */
static int cmd_leabroadcast_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    if (!g_module_init) {
        return CME_LEA_NOT_INIT;
    }

    int enable = atoi(argv[0]);

    if (enable) {
        /* Update broadcast config if parameters provided */
        if (argc >= 2 && argv[1][0] != '\0') {
            strncpy(g_broadcast_config.broadcast_name, argv[1],
                    sizeof(g_broadcast_config.broadcast_name) - 1);
        }
        if (argc >= 3 && argv[2][0] != '\0') {
            /* Parse context (simplified - just use MEDIA for now) */
            g_broadcast_config.audio_context = LE_AUDIO_CONTEXT_MEDIA;
        }

        int result = le_audio_broadcast_start(&g_broadcast_config);
        if (result != 0) {
            return CME_LEA_STREAM_ERROR;
        }

        cdc_acm_printf("\r\n+LEABROADCAST: STARTED,%s\r\n",
                       g_broadcast_config.broadcast_name);
    } else {
        int result = le_audio_broadcast_stop();
        if (result != 0) {
            return CME_LEA_STREAM_ERROR;
        }

        cdc_acm_printf("\r\n+LEABROADCAST: STOPPED\r\n");
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+LEABROADCAST? - Query broadcast status
 */
static int cmd_leabroadcast_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!g_module_init) {
        cdc_acm_printf("\r\n+LEABROADCAST: NOT_INITIALIZED\r\n");
        return CME_SUCCESS;
    }

    le_audio_mode_t mode = le_audio_get_mode();
    bool broadcasting = (mode == LE_AUDIO_MODE_BROADCAST_SOURCE);

    cdc_acm_printf("\r\n+LEABROADCAST: %d,%s\r\n",
                   broadcasting ? 1 : 0,
                   g_broadcast_config.broadcast_name);

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Unicast
 ******************************************************************************/

/**
 * @brief AT+LEAUNICAST=enable[,conn_handle[,bidirectional]] - Start/stop unicast
 */
static int cmd_leaunicast_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    if (!g_module_init) {
        return CME_LEA_NOT_INIT;
    }

    int enable = atoi(argv[0]);

    if (enable) {
        if (argc < 2) {
            return CME_INVALID_PARAM;  /* Connection handle required */
        }

        uint16_t conn_handle = (uint16_t)atoi(argv[1]);
        bool bidirectional = (argc >= 3) ? (atoi(argv[2]) != 0) : false;

        le_audio_unicast_config_t config = {
            .conn_handle = conn_handle,
            .ase_id = 0x01,
            .audio_context = LE_AUDIO_CONTEXT_MEDIA,
            .target_latency_ms = 20,
            .retransmissions = 2,
            .presentation_delay_us = 40000,
            .bidirectional = bidirectional
        };

        int result = le_audio_unicast_start(&config);
        if (result != 0) {
            return CME_LEA_STREAM_ERROR;
        }

        cdc_acm_printf("\r\n+LEAUNICAST: STARTED,%u,%s\r\n",
                       conn_handle,
                       bidirectional ? "DUPLEX" : "SOURCE");
    } else {
        int result = le_audio_unicast_stop();
        if (result != 0) {
            return CME_LEA_STREAM_ERROR;
        }

        cdc_acm_printf("\r\n+LEAUNICAST: STOPPED\r\n");
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+LEAUNICAST? - Query unicast status
 */
static int cmd_leaunicast_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!g_module_init) {
        cdc_acm_printf("\r\n+LEAUNICAST: NOT_INITIALIZED\r\n");
        return CME_SUCCESS;
    }

    le_audio_mode_t mode = le_audio_get_mode();
    bool unicasting = (mode == LE_AUDIO_MODE_UNICAST_SOURCE ||
                       mode == LE_AUDIO_MODE_UNICAST_SINK ||
                       mode == LE_AUDIO_MODE_UNICAST_DUPLEX);

    const char *mode_str = "IDLE";
    if (mode == LE_AUDIO_MODE_UNICAST_SOURCE) mode_str = "SOURCE";
    else if (mode == LE_AUDIO_MODE_UNICAST_SINK) mode_str = "SINK";
    else if (mode == LE_AUDIO_MODE_UNICAST_DUPLEX) mode_str = "DUPLEX";

    cdc_acm_printf("\r\n+LEAUNICAST: %d,%s\r\n",
                   unicasting ? 1 : 0, mode_str);

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Codec Configuration
 ******************************************************************************/

/**
 * @brief AT+LEACODEC=sample_rate,frame_duration[,octets] - Configure codec
 */
static int cmd_leacodec_exec(int argc, const char *argv[])
{
    if (argc < 2) {
        return CME_INVALID_PARAM;
    }

    /* Check if streaming - can't change codec during streaming */
    if (g_module_init) {
        le_audio_state_t state = le_audio_get_state();
        if (state != LE_AUDIO_STATE_IDLE && state != LE_AUDIO_STATE_CONFIGURED) {
            return CME_NOT_ALLOWED;
        }
    }

    uint32_t sample_rate = (uint32_t)atoi(argv[0]);
    uint32_t frame_duration = (uint32_t)atoi(argv[1]);

    /* Validate sample rate */
    if (sample_rate != 8000 && sample_rate != 16000 &&
        sample_rate != 24000 && sample_rate != 32000 &&
        sample_rate != 48000) {
        return CME_INVALID_PARAM;
    }

    /* Validate frame duration */
    if (frame_duration != 7500 && frame_duration != 10000) {
        return CME_INVALID_PARAM;
    }

    /* Update configuration */
    g_codec_config.sample_rate = sample_rate;
    g_codec_config.frame_duration_us = (uint16_t)frame_duration;

    if (argc >= 3) {
        g_codec_config.octets_per_frame = (uint16_t)atoi(argv[2]);
    } else {
        /* Calculate default octets */
        uint32_t bitrate = (sample_rate >= 32000) ? 80000 : 64000;
        g_codec_config.octets_per_frame = (uint16_t)((bitrate * frame_duration) / (8 * 1000000));
    }

    cdc_acm_printf("\r\n+LEACODEC: %lu,%u,%u\r\n",
                   (unsigned long)g_codec_config.sample_rate,
                   g_codec_config.frame_duration_us,
                   g_codec_config.octets_per_frame);

    return CME_SUCCESS;
}

/**
 * @brief AT+LEACODEC? - Query codec configuration
 */
static int cmd_leacodec_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\n+LEACODEC: %lu,%u,%u,%u\r\n",
                   (unsigned long)g_codec_config.sample_rate,
                   g_codec_config.frame_duration_us,
                   g_codec_config.octets_per_frame,
                   g_codec_config.channels);

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Information
 ******************************************************************************/

/**
 * @brief AT+LEAINFO? - Display LE Audio information
 */
static int cmd_leainfo_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\n+LEAINFO:\r\n");
    cdc_acm_printf("  Initialized: %s\r\n", g_module_init ? "YES" : "NO");

    if (g_module_init) {
        le_audio_state_t state = le_audio_get_state();
        le_audio_mode_t mode = le_audio_get_mode();

        cdc_acm_printf("  State: %s\r\n", get_state_name(state));
        cdc_acm_printf("  Mode: %s\r\n", get_mode_name(mode));
    }

    cdc_acm_printf("  Codec: LC3\r\n");
    cdc_acm_printf("  Sample Rate: %lu Hz\r\n", (unsigned long)g_codec_config.sample_rate);
    cdc_acm_printf("  Frame Duration: %u us\r\n", g_codec_config.frame_duration_us);
    cdc_acm_printf("  Octets/Frame: %u\r\n", g_codec_config.octets_per_frame);
    cdc_acm_printf("  Channels: %u\r\n", g_codec_config.channels);

    return CME_SUCCESS;
}

/*******************************************************************************
 * Command Handlers - Broadcast Sink (Auracast RX)
 ******************************************************************************/

/** Scanning state */
static bool g_scanning = false;

/**
 * @brief Parse hex string to bytes
 *
 * @param hex_str Hex string (e.g., "010203")
 * @param out Output buffer
 * @param out_len Expected number of bytes
 * @return true on success
 */
static bool parse_hex_string(const char *hex_str, uint8_t *out, size_t out_len)
{
    size_t str_len = strlen(hex_str);
    if (str_len != out_len * 2) {
        return false;
    }

    for (size_t i = 0; i < out_len; i++) {
        char byte_str[3] = { hex_str[i*2], hex_str[i*2 + 1], '\0' };
        char *endptr;
        unsigned long val = strtoul(byte_str, &endptr, 16);
        if (*endptr != '\0') {
            return false;
        }
        out[i] = (uint8_t)val;
    }
    return true;
}

/**
 * @brief AT+LEASCAN=0|1 - Start/stop scanning for Auracast broadcasts
 */
static int cmd_leascan_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    if (!g_module_init) {
        return CME_LEA_NOT_INIT;
    }

    int enable = atoi(argv[0]);

    if (enable) {
        if (g_scanning) {
            cdc_acm_printf("\r\n+LEASCAN: ALREADY_SCANNING\r\n");
            return CME_SUCCESS;
        }

        int result = le_audio_broadcast_sink_start_scan();
        if (result != 0) {
            return CME_LEA_STREAM_ERROR;
        }

        g_scanning = true;
        cdc_acm_printf("\r\n+LEASCAN: STARTED\r\n");
    } else {
        if (!g_scanning) {
            cdc_acm_printf("\r\n+LEASCAN: NOT_SCANNING\r\n");
            return CME_SUCCESS;
        }

        int result = le_audio_broadcast_sink_stop_scan();
        if (result != 0) {
            return CME_LEA_STREAM_ERROR;
        }

        g_scanning = false;
        cdc_acm_printf("\r\n+LEASCAN: STOPPED\r\n");
    }

    return CME_SUCCESS;
}

/**
 * @brief AT+LEASCAN? - Query scanning state
 */
static int cmd_leascan_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    cdc_acm_printf("\r\n+LEASCAN: %d\r\n", g_scanning ? 1 : 0);
    return CME_SUCCESS;
}

/**
 * @brief AT+LEASYNC=broadcast_id[,broadcast_code] - Sync to a broadcast
 *
 * broadcast_id: 3-byte hex string (e.g., "010203")
 * broadcast_code: 16-byte hex string for encrypted broadcasts (optional)
 */
static int cmd_leasync_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    if (!g_module_init) {
        return CME_LEA_NOT_INIT;
    }

    /* Parse broadcast ID (3 bytes) */
    uint8_t broadcast_id[3];
    if (!parse_hex_string(argv[0], broadcast_id, 3)) {
        cdc_acm_printf("\r\nERROR: Invalid broadcast_id (expected 6 hex chars)\r\n");
        return CME_INVALID_PARAM;
    }

    /* Parse optional broadcast code (16 bytes) */
    uint8_t broadcast_code[16];
    uint8_t *code_ptr = NULL;

    if (argc >= 2 && argv[1][0] != '\0') {
        if (!parse_hex_string(argv[1], broadcast_code, 16)) {
            cdc_acm_printf("\r\nERROR: Invalid broadcast_code (expected 32 hex chars)\r\n");
            return CME_INVALID_PARAM;
        }
        code_ptr = broadcast_code;
    }

    /* Stop scanning if active */
    if (g_scanning) {
        le_audio_broadcast_sink_stop_scan();
        g_scanning = false;
    }

    /* Sync to broadcast */
    int result = le_audio_broadcast_sink_sync(broadcast_id, code_ptr);
    if (result != 0) {
        cdc_acm_printf("\r\n+LEASYNC: FAILED,%d\r\n", result);
        return CME_LEA_STREAM_ERROR;
    }

    cdc_acm_printf("\r\n+LEASYNC: SYNCING,%02X%02X%02X\r\n",
                   broadcast_id[0], broadcast_id[1], broadcast_id[2]);

    return CME_SUCCESS;
}

/**
 * @brief AT+LEASINK=0 - Stop broadcast sink
 */
static int cmd_leasink_exec(int argc, const char *argv[])
{
    if (argc < 1) {
        return CME_INVALID_PARAM;
    }

    if (!g_module_init) {
        return CME_LEA_NOT_INIT;
    }

    int enable = atoi(argv[0]);

    if (enable) {
        /* Use AT+LEASCAN or AT+LEASYNC to start sink */
        cdc_acm_printf("\r\nERROR: Use AT+LEASCAN=1 or AT+LEASYNC to start sink\r\n");
        return CME_NOT_ALLOWED;
    }

    /* Stop sink */
    int result = le_audio_broadcast_sink_stop();
    if (result != 0) {
        return CME_LEA_STREAM_ERROR;
    }

    cdc_acm_printf("\r\n+LEASINK: STOPPED\r\n");
    return CME_SUCCESS;
}

/**
 * @brief AT+LEASINK? - Query broadcast sink status
 */
static int cmd_leasink_query(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (!g_module_init) {
        cdc_acm_printf("\r\n+LEASINK: NOT_INITIALIZED\r\n");
        return CME_SUCCESS;
    }

    le_audio_mode_t mode = le_audio_get_mode();
    bool sinking = (mode == LE_AUDIO_MODE_BROADCAST_SINK);

    cdc_acm_printf("\r\n+LEASINK: %d,%s\r\n",
                   sinking ? 1 : 0,
                   g_scanning ? "SCANNING" : (sinking ? "STREAMING" : "IDLE"));

    return CME_SUCCESS;
}

/**
 * @brief AT+LEADEMO[=broadcast_code] - Demo auto-sync to first broadcast
 */
static int cmd_leademo_exec(int argc, const char *argv[])
{
    if (!g_module_init) {
        return CME_LEA_NOT_INIT;
    }

    /* Parse optional broadcast code (16 bytes) */
    uint8_t broadcast_code[16];
    uint8_t *code_ptr = NULL;

    if (argc >= 1 && argv[0][0] != '\0') {
        if (!parse_hex_string(argv[0], broadcast_code, 16)) {
            cdc_acm_printf("\r\nERROR: Invalid broadcast_code (expected 32 hex chars)\r\n");
            return CME_INVALID_PARAM;
        }
        code_ptr = broadcast_code;
    }

    /* Start demo - auto sync to first broadcast found */
    int result = le_audio_broadcast_sink_demo_auto_sync(code_ptr);
    if (result != 0) {
        cdc_acm_printf("\r\n+LEADEMO: FAILED,%d\r\n", result);
        return CME_LEA_STREAM_ERROR;
    }

    cdc_acm_printf("\r\n+LEADEMO: STARTED\r\n");
    cdc_acm_printf("Scanning for broadcasts... (will auto-sync to first found)\r\n");

    return CME_SUCCESS;
}
