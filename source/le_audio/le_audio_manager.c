/**
 * @file le_audio_manager.c
 * @brief LE Audio Manager Implementation
 *
 * This module provides the main control interface for LE Audio functionality
 * including unicast streaming (CIS) and broadcast (BIS/Auracast).
 *
 * Architecture:
 * - State machine for managing audio stream lifecycle
 * - Integration with LC3 codec for encode/decode
 * - Ring buffers for PCM audio data exchange
 * - HCI ISOC interface for Bluetooth audio transport
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "le_audio_manager.h"
#include "../audio/lc3_wrapper.h"
#include "../config/lc3_config.h"

#include <stdlib.h>
#include <string.h>

/* Infineon BTSTACK headers */
#include "wiced_bt_stack.h"
#include "wiced_bt_isoc.h"


/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/* Project headers */
#include "../bluetooth/hci_isoc.h"
#include "pacs.h"
#include "bap_unicast.h"
#include "bap_broadcast.h"

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Maximum number of connected devices (unicast) */
#define LE_AUDIO_MAX_DEVICES            2

/** Maximum number of BIS streams (broadcast) */
#define LE_AUDIO_MAX_BIS                2

/** Audio frame queue depth */
#define LE_AUDIO_FRAME_QUEUE_DEPTH      8

/** Maximum LC3 frame size in bytes */
#define LE_AUDIO_MAX_LC3_FRAME_SIZE     155

/** Maximum PCM samples per frame (48kHz, 10ms, stereo) */
#define LE_AUDIO_MAX_PCM_SAMPLES        960

/** Target latency values (per BAP spec) */
#define LE_AUDIO_TARGET_LATENCY_LOW         0x01
#define LE_AUDIO_TARGET_LATENCY_BALANCED    0x02
#define LE_AUDIO_TARGET_LATENCY_HIGH        0x03

/** QoS configuration presets */
#define LE_AUDIO_QOS_RETRANSMIT_LOW         2
#define LE_AUDIO_QOS_RETRANSMIT_BALANCED    4
#define LE_AUDIO_QOS_RETRANSMIT_HIGH        6

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/**
 * @brief LC3 audio frame structure
 */
typedef struct {
    uint8_t data[LE_AUDIO_MAX_LC3_FRAME_SIZE];  /**< Encoded LC3 data */
    uint16_t length;                             /**< Actual frame length */
    uint32_t timestamp;                          /**< SDU timestamp */
    uint8_t sequence_number;                     /**< Sequence number */
} lc3_frame_t;

/**
 * @brief PCM audio buffer structure
 */
typedef struct {
    int16_t samples[LE_AUDIO_MAX_PCM_SAMPLES];  /**< PCM samples */
    uint16_t sample_count;                       /**< Number of samples */
    uint32_t timestamp;                          /**< Timestamp */
} pcm_buffer_t;

/**
 * @brief Connected device info (unicast)
 */
typedef struct {
    uint16_t conn_handle;           /**< ACL connection handle */
    uint16_t cis_handle;            /**< CIS handle */
    uint8_t ase_id;                 /**< ASE ID */
    bool active;                    /**< Device is streaming */
} unicast_device_t;

/**
 * @brief BIS stream info (broadcast)
 */
typedef struct {
    uint8_t bis_index;              /**< BIS index */
    uint16_t bis_handle;            /**< BIS handle */
    bool active;                    /**< BIS is active */
} bis_stream_t;

/**
 * @brief Broadcast state structure
 */
typedef struct {
    le_audio_broadcast_config_t config;     /**< Broadcast configuration */
    uint8_t big_handle;                      /**< BIG handle */
    bis_stream_t bis[LE_AUDIO_MAX_BIS];     /**< BIS streams */
    uint8_t num_bis;                         /**< Number of active BIS */
    bool advertising;                        /**< Periodic advertising active */
} broadcast_state_t;

/**
 * @brief Unicast state structure
 */
typedef struct {
    le_audio_unicast_config_t config;               /**< Unicast configuration */
    unicast_device_t devices[LE_AUDIO_MAX_DEVICES]; /**< Connected devices */
    uint8_t num_devices;                            /**< Number of connected devices */
    uint16_t cig_id;                                /**< CIG ID */
} unicast_state_t;

/**
 * @brief Ring buffer for audio frames
 */
typedef struct {
    void *items;                /**< Buffer storage */
    uint16_t item_size;         /**< Size of each item */
    uint16_t capacity;          /**< Maximum items */
    volatile uint16_t head;     /**< Write position */
    volatile uint16_t tail;     /**< Read position */
    volatile uint16_t count;    /**< Current count */
} frame_queue_t;

/**
 * @brief LE Audio Manager context
 */
typedef struct {
    /* State */
    volatile bool initialized;
    volatile le_audio_state_t state;
    volatile le_audio_mode_t mode;

    /* Configuration */
    le_audio_codec_config_t codec_config;

    /* LC3 codec context */
    lc3_codec_ctx_t *lc3_ctx;

    /* Mode-specific state */
    union {
        unicast_state_t unicast;
        broadcast_state_t broadcast;
    } mode_state;

    /* Audio frame queues */
    frame_queue_t tx_queue;     /**< PCM frames to encode and send */
    frame_queue_t rx_queue;     /**< Received LC3 frames to decode */

    /* Queue storage */
    pcm_buffer_t tx_buffers[LE_AUDIO_FRAME_QUEUE_DEPTH];
    lc3_frame_t rx_frames[LE_AUDIO_FRAME_QUEUE_DEPTH];

    /* Callback */
    le_audio_event_callback_t event_callback;
    void *callback_user_data;

    /* Statistics */
    uint32_t frames_sent;
    uint32_t frames_received;
    uint32_t encode_errors;
    uint32_t decode_errors;

    /* Synchronization (FreeRTOS) */
    QueueHandle_t tx_queue_handle;
    QueueHandle_t rx_queue_handle;
    SemaphoreHandle_t state_mutex;

} le_audio_ctx_t;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/** Global LE Audio context */
static le_audio_ctx_t g_le_audio_ctx;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

/* State machine */
static void set_state(le_audio_state_t new_state);
static void notify_event(le_audio_event_type_t type);
static void notify_error(int error_code);

/* Frame queue operations */
static int frame_queue_init(frame_queue_t *queue, void *storage,
                            uint16_t item_size, uint16_t capacity);
static int frame_queue_push(frame_queue_t *queue, const void *item);
static int frame_queue_pop(frame_queue_t *queue, void *item);
static bool frame_queue_is_empty(const frame_queue_t *queue);
static bool frame_queue_is_full(const frame_queue_t *queue);

/* LC3 codec operations */
static int encode_audio_frame(const int16_t *pcm_in, uint8_t *lc3_out,
                              uint16_t *lc3_len);
static int decode_audio_frame(const uint8_t *lc3_in, uint16_t lc3_len,
                              int16_t *pcm_out);

/* BAP Unicast operations */
static int unicast_configure(const le_audio_unicast_config_t *config);
static int unicast_enable(void);
static int unicast_disable(void);
static int unicast_release(void);

/* BAP Broadcast operations */
static int broadcast_configure(const le_audio_broadcast_config_t *config);
static int broadcast_enable(void);
static int broadcast_disable(void);
static int broadcast_update_base(const char *name, uint16_t context);

/* HCI ISOC operations */
static int isoc_send_sdu(uint16_t handle, const uint8_t *data, uint16_t length,
                         uint32_t timestamp, uint8_t seq_num);
static void isoc_rx_callback(uint16_t handle, const uint8_t *data,
                             uint16_t length, uint32_t timestamp);

/*******************************************************************************
 * Frame Queue Implementation
 ******************************************************************************/

static int frame_queue_init(frame_queue_t *queue, void *storage,
                            uint16_t item_size, uint16_t capacity)
{
    if (queue == NULL || storage == NULL || capacity == 0) {
        return -1;
    }

    queue->items = storage;
    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

    return 0;
}

static int frame_queue_push(frame_queue_t *queue, const void *item)
{
    uint8_t *dest;

    if (queue == NULL || item == NULL) {
        return -1;
    }

    if (frame_queue_is_full(queue)) {
        return -2;  /* Queue full */
    }

    dest = (uint8_t *)queue->items + (queue->head * queue->item_size);
    memcpy(dest, item, queue->item_size);

    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;

    return 0;
}

static int frame_queue_pop(frame_queue_t *queue, void *item)
{
    uint8_t *src;

    if (queue == NULL || item == NULL) {
        return -1;
    }

    if (frame_queue_is_empty(queue)) {
        return -2;  /* Queue empty */
    }

    src = (uint8_t *)queue->items + (queue->tail * queue->item_size);
    memcpy(item, src, queue->item_size);

    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;

    return 0;
}

static bool frame_queue_is_empty(const frame_queue_t *queue)
{
    return (queue == NULL) || (queue->count == 0);
}

static bool frame_queue_is_full(const frame_queue_t *queue)
{
    return (queue == NULL) || (queue->count >= queue->capacity);
}

/*******************************************************************************
 * State Machine
 ******************************************************************************/

static void set_state(le_audio_state_t new_state)
{
    le_audio_state_t old_state = g_le_audio_ctx.state;

    if (old_state == new_state) {
        return;
    }

    g_le_audio_ctx.state = new_state;

    /* Notify state change */
    notify_event(LE_AUDIO_EVENT_STATE_CHANGED);
}

static void notify_event(le_audio_event_type_t type)
{
    le_audio_event_t event;

    if (g_le_audio_ctx.event_callback == NULL) {
        return;
    }

    event.type = type;

    switch (type) {
        case LE_AUDIO_EVENT_STATE_CHANGED:
            event.data.new_state = g_le_audio_ctx.state;
            break;
        default:
            event.data.error_code = 0;
            break;
    }

    g_le_audio_ctx.event_callback(&event, g_le_audio_ctx.callback_user_data);
}

static void notify_error(int error_code)
{
    le_audio_event_t event;

    if (g_le_audio_ctx.event_callback == NULL) {
        return;
    }

    event.type = LE_AUDIO_EVENT_ERROR;
    event.data.error_code = error_code;

    g_le_audio_ctx.event_callback(&event, g_le_audio_ctx.callback_user_data);
}

/*******************************************************************************
 * LC3 Codec Operations
 ******************************************************************************/

static int encode_audio_frame(const int16_t *pcm_in, uint8_t *lc3_out,
                              uint16_t *lc3_len)
{
    int result;

    if (g_le_audio_ctx.lc3_ctx == NULL) {
        return -1;
    }

    /* Encode using LC3 wrapper */
    result = lc3_wrapper_encode(g_le_audio_ctx.lc3_ctx, pcm_in, lc3_out, 0);

    if (result == 0) {
        *lc3_len = lc3_wrapper_get_frame_bytes(g_le_audio_ctx.lc3_ctx);
    } else {
        g_le_audio_ctx.encode_errors++;
    }

    return result;
}

static int decode_audio_frame(const uint8_t *lc3_in, uint16_t lc3_len,
                              int16_t *pcm_out)
{
    int result;

    (void)lc3_len;  /* Length is configured at init */

    if (g_le_audio_ctx.lc3_ctx == NULL) {
        return -1;
    }

    if (lc3_in == NULL) {
        /* Packet loss - use PLC */
        result = lc3_wrapper_decode_plc(g_le_audio_ctx.lc3_ctx, pcm_out, 0);
    } else {
        /* Normal decode */
        result = lc3_wrapper_decode(g_le_audio_ctx.lc3_ctx, lc3_in, pcm_out, 0);
    }

    if (result != 0) {
        g_le_audio_ctx.decode_errors++;
    }

    return result;
}

/*******************************************************************************
 * BAP Unicast Operations
 ******************************************************************************/

/**
 * @brief Configure unicast audio stream
 */
static int unicast_configure(const le_audio_unicast_config_t *config)
{
    int result;
    cig_config_t cig_config;
    bap_codec_config_request_t codec_req;
    bap_qos_config_request_t qos_req;

    if (config == NULL) {
        return -1;
    }

    /* Store configuration */
    g_le_audio_ctx.mode_state.unicast.config = *config;

    /* Step 1: Create CIG (Connected Isochronous Group) */
    memset(&cig_config, 0, sizeof(cig_config_t));
    cig_config.cig_id = 0;
    cig_config.sdu_interval_c_to_p = g_le_audio_ctx.codec_config.frame_duration_us;
    cig_config.sdu_interval_p_to_c = g_le_audio_ctx.codec_config.frame_duration_us;
    cig_config.sca = 0;  /* 251-500 ppm */
    cig_config.packing = HCI_ISOC_PACKING_SEQUENTIAL;
    cig_config.framing = HCI_ISOC_FRAMING_UNFRAMED;
    cig_config.max_transport_latency_c_to_p = config->target_latency_ms;
    cig_config.max_transport_latency_p_to_c = config->target_latency_ms;
    cig_config.num_cis = 1;

    /* Configure CIS within the CIG */
    cig_config.cis[0].cis_id = 0;
    cig_config.cis[0].max_sdu_c_to_p = g_le_audio_ctx.codec_config.octets_per_frame;
    cig_config.cis[0].max_sdu_p_to_c = config->bidirectional ?
        g_le_audio_ctx.codec_config.octets_per_frame : 0;
    cig_config.cis[0].phy_c_to_p = HCI_ISOC_PHY_2M;
    cig_config.cis[0].phy_p_to_c = HCI_ISOC_PHY_2M;
    cig_config.cis[0].rtn_c_to_p = config->retransmissions;
    cig_config.cis[0].rtn_p_to_c = config->retransmissions;

    result = hci_isoc_set_cig_params(&cig_config);
    if (result != HCI_ISOC_OK) {
        notify_error(result);
        return -2;
    }

    g_le_audio_ctx.mode_state.unicast.cig_id = cig_config.cig_id;

    /* Step 2: Configure codec via BAP (if connected) */
    if (config->conn_handle != 0) {
        memset(&codec_req, 0, sizeof(bap_codec_config_request_t));
        codec_req.ase_id = config->ase_id;
        codec_req.target_latency = (config->target_latency_ms <= 20) ?
            BAP_TARGET_LATENCY_LOW : BAP_TARGET_LATENCY_BALANCED;
        codec_req.target_phy = BAP_TARGET_PHY_2M;
        codec_req.codec_config.coding_format = 0x06;  /* LC3 */
        codec_req.codec_config.company_id = 0x0000;
        codec_req.codec_config.vendor_codec_id = 0x0000;

        result = bap_unicast_config_codec(config->conn_handle, &codec_req);
        if (result != BAP_UNICAST_OK) {
            notify_error(result);
            return -3;
        }

        /* Step 3: Configure QoS */
        memset(&qos_req, 0, sizeof(bap_qos_config_request_t));
        qos_req.ase_id = config->ase_id;
        qos_req.qos_config.cig_id = cig_config.cig_id;
        qos_req.qos_config.cis_id = 0;
        qos_req.qos_config.sdu_interval = g_le_audio_ctx.codec_config.frame_duration_us;
        qos_req.qos_config.framing = 0;
        qos_req.qos_config.phy = 0x02;  /* 2M PHY */
        qos_req.qos_config.max_sdu = g_le_audio_ctx.codec_config.octets_per_frame;
        qos_req.qos_config.retransmission_number = config->retransmissions;
        qos_req.qos_config.max_transport_latency = config->target_latency_ms;
        qos_req.qos_config.presentation_delay = config->presentation_delay_us;

        result = bap_unicast_config_qos(config->conn_handle, &qos_req);
        if (result != BAP_UNICAST_OK) {
            notify_error(result);
            return -4;
        }
    }

    set_state(LE_AUDIO_STATE_CONFIGURED);

    return 0;
}

/**
 * @brief Enable unicast streaming
 */
static int unicast_enable(void)
{
    int result;
    le_audio_unicast_config_t *config = &g_le_audio_ctx.mode_state.unicast.config;
    bap_enable_request_t enable_req;
    uint16_t cis_handle;
    uint16_t acl_handle;

    set_state(LE_AUDIO_STATE_ENABLING);

    /* Step 1: Send Enable operation to ASE via BAP */
    if (config->conn_handle != 0) {
        memset(&enable_req, 0, sizeof(bap_enable_request_t));
        enable_req.ase_id = config->ase_id;
        /* Set streaming audio context in metadata */
        enable_req.metadata[0] = 0x03;  /* Length */
        enable_req.metadata[1] = 0x02;  /* Streaming Audio Contexts type */
        enable_req.metadata[2] = 0x04;  /* Media context */
        enable_req.metadata[3] = 0x00;
        enable_req.metadata_len = 4;

        result = bap_unicast_enable(config->conn_handle, &enable_req);
        if (result != BAP_UNICAST_OK) {
            set_state(LE_AUDIO_STATE_CONFIGURED);
            notify_error(result);
            return -1;
        }
    }

    /* Step 2: Establish CIS connection */
    cis_handle = 0x0000;  /* Will be assigned by controller */
    acl_handle = config->conn_handle;

    result = hci_isoc_create_cis(g_le_audio_ctx.mode_state.unicast.cig_id,
                                  1, &cis_handle, &acl_handle);
    if (result != HCI_ISOC_OK) {
        set_state(LE_AUDIO_STATE_CONFIGURED);
        notify_error(result);
        return -2;
    }

    /* Store CIS handle in first device slot */
    g_le_audio_ctx.mode_state.unicast.devices[0].conn_handle = config->conn_handle;
    g_le_audio_ctx.mode_state.unicast.devices[0].cis_handle = cis_handle;
    g_le_audio_ctx.mode_state.unicast.devices[0].ase_id = config->ase_id;
    g_le_audio_ctx.mode_state.unicast.devices[0].active = true;
    g_le_audio_ctx.mode_state.unicast.num_devices = 1;

    /* Step 3: Set up ISO data path for TX (Host to Controller) */
    result = hci_isoc_setup_data_path(cis_handle,
                                       HCI_ISOC_DATA_PATH_INPUT,
                                       HCI_ISOC_DATA_PATH_HCI,
                                       NULL, 0, NULL, 0);
    if (result != HCI_ISOC_OK) {
        hci_isoc_disconnect_cis(cis_handle, 0x13);  /* Remote user terminated */
        set_state(LE_AUDIO_STATE_CONFIGURED);
        notify_error(result);
        return -3;
    }

    /* Step 4: Set up ISO data path for RX if bidirectional */
    if (config->bidirectional) {
        result = hci_isoc_setup_data_path(cis_handle,
                                           HCI_ISOC_DATA_PATH_OUTPUT,
                                           HCI_ISOC_DATA_PATH_HCI,
                                           NULL, 0, NULL, 0);
        if (result != HCI_ISOC_OK) {
            hci_isoc_remove_data_path(cis_handle, HCI_ISOC_DATA_PATH_INPUT);
            hci_isoc_disconnect_cis(cis_handle, 0x13);
            set_state(LE_AUDIO_STATE_CONFIGURED);
            notify_error(result);
            return -4;
        }
    }

    /* Step 5: Signal receiver start ready */
    if (config->conn_handle != 0) {
        result = bap_unicast_receiver_start_ready(config->conn_handle, config->ase_id);
        if (result != BAP_UNICAST_OK) {
            /* Non-fatal, continue anyway */
        }
    }

    set_state(LE_AUDIO_STATE_STREAMING);
    notify_event(LE_AUDIO_EVENT_STREAM_STARTED);

    return 0;
}

/**
 * @brief Disable unicast streaming
 */
static int unicast_disable(void)
{
    le_audio_unicast_config_t *config = &g_le_audio_ctx.mode_state.unicast.config;
    unicast_device_t *device;
    int i;

    set_state(LE_AUDIO_STATE_DISABLING);

    /* Process all connected devices */
    for (i = 0; i < g_le_audio_ctx.mode_state.unicast.num_devices; i++) {
        device = &g_le_audio_ctx.mode_state.unicast.devices[i];

        if (!device->active) {
            continue;
        }

        /* Step 1: Send Disable operation to ASE */
        if (device->conn_handle != 0) {
            bap_unicast_disable(device->conn_handle, device->ase_id);
        }

        /* Step 2: Remove ISO data paths */
        hci_isoc_remove_data_path(device->cis_handle, HCI_ISOC_DATA_PATH_INPUT);
        if (config->bidirectional) {
            hci_isoc_remove_data_path(device->cis_handle, HCI_ISOC_DATA_PATH_OUTPUT);
        }

        /* Step 3: Disconnect CIS */
        hci_isoc_disconnect_cis(device->cis_handle, 0x13);  /* Remote user terminated */

        /* Step 4: Signal receiver stop ready */
        if (device->conn_handle != 0) {
            bap_unicast_receiver_stop_ready(device->conn_handle, device->ase_id);
        }

        device->active = false;
        device->cis_handle = 0;
    }

    notify_event(LE_AUDIO_EVENT_STREAM_STOPPED);
    set_state(LE_AUDIO_STATE_CONFIGURED);

    return 0;
}

/**
 * @brief Release unicast resources
 */
static int unicast_release(void)
{
    unicast_device_t *device;
    int i;

    /* Step 1: Send Release operation to all ASEs */
    for (i = 0; i < g_le_audio_ctx.mode_state.unicast.num_devices; i++) {
        device = &g_le_audio_ctx.mode_state.unicast.devices[i];

        if (device->conn_handle != 0) {
            bap_unicast_release(device->conn_handle, device->ase_id);
        }
    }

    /* Step 2: Remove CIG */
    hci_isoc_remove_cig(g_le_audio_ctx.mode_state.unicast.cig_id);

    /* Clear state */
    memset(&g_le_audio_ctx.mode_state.unicast, 0, sizeof(unicast_state_t));
    set_state(LE_AUDIO_STATE_IDLE);

    return 0;
}

/*******************************************************************************
 * BAP Broadcast Operations
 ******************************************************************************/

/**
 * @brief Configure broadcast audio (Auracast)
 */
static int broadcast_configure(const le_audio_broadcast_config_t *config)
{
    int result;
    bap_broadcast_config_t bap_config;
    int sg, b;

    if (config == NULL) {
        return -1;
    }

    /* Validate configuration */
    if (config->num_subgroups == 0 || config->num_bis_per_subgroup == 0) {
        return -2;
    }

    /* Store configuration */
    g_le_audio_ctx.mode_state.broadcast.config = *config;

    /* Build BAP broadcast configuration */
    memset(&bap_config, 0, sizeof(bap_broadcast_config_t));

    /* Generate Broadcast_ID if not provided */
    if (config->broadcast_id[0] == 0 && config->broadcast_id[1] == 0 &&
        config->broadcast_id[2] == 0) {
        bap_broadcast_generate_id(bap_config.broadcast_id);
    } else {
        memcpy(bap_config.broadcast_id, config->broadcast_id, 3);
    }

    /* Set broadcast name */
    strncpy(bap_config.broadcast_name, config->broadcast_name,
            BAP_BROADCAST_MAX_NAME_LEN - 1);

    /* Encryption settings */
    bap_config.encrypted = config->encrypted;
    if (config->encrypted) {
        memcpy(bap_config.broadcast_code, config->broadcast_code,
               BAP_BROADCAST_CODE_SIZE);
    }

    /* Timing parameters */
    bap_config.presentation_delay_us = config->presentation_delay_us;
    bap_config.max_transport_latency_ms = config->target_latency_ms;
    bap_config.rtn = config->retransmissions;
    bap_config.phy = 0x02;  /* 2M PHY */

    /* Advertising parameters */
    bap_config.adv_interval_min = 160;  /* 100ms in 0.625ms units */
    bap_config.adv_interval_max = 160;
    bap_config.tx_power = 0;

    /* Configure subgroups */
    bap_config.num_subgroups = config->num_subgroups;
    for (sg = 0; sg < config->num_subgroups && sg < BAP_BROADCAST_MAX_SUBGROUPS; sg++) {
        bap_subgroup_config_t *subgroup = &bap_config.subgroups[sg];

        /* LC3 codec configuration */
        subgroup->codec_config.sampling_freq =
            bap_broadcast_sample_rate_to_lc3(g_le_audio_ctx.codec_config.sample_rate);
        subgroup->codec_config.frame_duration =
            (g_le_audio_ctx.codec_config.frame_duration_us == 7500) ?
            BAP_LC3_DURATION_7_5MS : BAP_LC3_DURATION_10MS;
        subgroup->codec_config.octets_per_frame =
            g_le_audio_ctx.codec_config.octets_per_frame;
        subgroup->codec_config.frames_per_sdu = 1;

        /* Audio context and language */
        subgroup->audio_context = config->audio_context;
        strncpy(subgroup->language, "eng", sizeof(subgroup->language) - 1);

        /* Configure BIS within subgroup */
        subgroup->num_bis = config->num_bis_per_subgroup;
        for (b = 0; b < config->num_bis_per_subgroup && b < BAP_BROADCAST_MAX_BIS; b++) {
            subgroup->bis[b].bis_index = (uint8_t)(b + 1);
            /* Stereo: Left for first BIS, Right for second */
            if (config->num_bis_per_subgroup >= 2) {
                subgroup->bis[b].audio_location = (b == 0) ?
                    BAP_AUDIO_LOCATION_FRONT_LEFT : BAP_AUDIO_LOCATION_FRONT_RIGHT;
            } else {
                subgroup->bis[b].audio_location = BAP_AUDIO_LOCATION_MONO;
            }
        }
    }

    /* Configure BAP broadcast source */
    result = bap_broadcast_configure(&bap_config);
    if (result != BAP_BROADCAST_OK) {
        notify_error(result);
        return -3;
    }

    /* Store BIG handle (will be assigned when started) */
    g_le_audio_ctx.mode_state.broadcast.big_handle = 0;
    g_le_audio_ctx.mode_state.broadcast.num_bis =
        config->num_subgroups * config->num_bis_per_subgroup;

    set_state(LE_AUDIO_STATE_CONFIGURED);

    return 0;
}

/**
 * @brief Enable broadcast streaming
 */
static int broadcast_enable(void)
{
    int result;
    bap_broadcast_info_t info;
    int i;

    set_state(LE_AUDIO_STATE_ENABLING);

    /*
     * Step 1-3: Start extended advertising, periodic advertising, and create BIG
     * bap_broadcast_start() handles all of this internally
     */
    result = bap_broadcast_start();
    if (result != BAP_BROADCAST_OK) {
        set_state(LE_AUDIO_STATE_CONFIGURED);
        notify_error(result);
        return -1;
    }

    /* Get broadcast info with BIG handle and BIS handles */
    result = bap_broadcast_get_info(&info);
    if (result != BAP_BROADCAST_OK) {
        bap_broadcast_stop();
        set_state(LE_AUDIO_STATE_CONFIGURED);
        notify_error(result);
        return -2;
    }

    /* Store BIG and BIS handles */
    g_le_audio_ctx.mode_state.broadcast.big_handle = info.big_handle;
    g_le_audio_ctx.mode_state.broadcast.num_bis = info.num_bis;

    for (i = 0; i < info.num_bis && i < LE_AUDIO_MAX_BIS; i++) {
        g_le_audio_ctx.mode_state.broadcast.bis[i].bis_index = (uint8_t)(i + 1);
        g_le_audio_ctx.mode_state.broadcast.bis[i].bis_handle = info.bis_handles[i];
        g_le_audio_ctx.mode_state.broadcast.bis[i].active = true;

        /* Step 4: Set up ISO data path for each BIS (TX direction) */
        result = hci_isoc_setup_data_path(info.bis_handles[i],
                                           HCI_ISOC_DATA_PATH_INPUT,
                                           HCI_ISOC_DATA_PATH_HCI,
                                           NULL, 0, NULL, 0);
        if (result != HCI_ISOC_OK) {
            /* Non-fatal, log but continue */
            g_le_audio_ctx.mode_state.broadcast.bis[i].active = false;
        }
    }

    g_le_audio_ctx.mode_state.broadcast.advertising = true;
    set_state(LE_AUDIO_STATE_STREAMING);
    notify_event(LE_AUDIO_EVENT_STREAM_STARTED);

    return 0;
}

/**
 * @brief Disable broadcast streaming
 */
static int broadcast_disable(void)
{
    int i;

    set_state(LE_AUDIO_STATE_DISABLING);

    /* Step 1: Remove ISO data paths for all BIS */
    for (i = 0; i < g_le_audio_ctx.mode_state.broadcast.num_bis; i++) {
        bis_stream_t *bis = &g_le_audio_ctx.mode_state.broadcast.bis[i];
        if (bis->active && bis->bis_handle != 0) {
            hci_isoc_remove_data_path(bis->bis_handle, HCI_ISOC_DATA_PATH_INPUT);
            bis->active = false;
        }
    }

    /*
     * Step 2-3: Terminate BIG, stop periodic advertising, stop extended advertising
     * bap_broadcast_stop() handles all of this internally
     */
    bap_broadcast_stop();

    g_le_audio_ctx.mode_state.broadcast.advertising = false;
    g_le_audio_ctx.mode_state.broadcast.big_handle = 0;

    notify_event(LE_AUDIO_EVENT_STREAM_STOPPED);
    set_state(LE_AUDIO_STATE_CONFIGURED);

    return 0;
}

/**
 * @brief Update broadcast metadata (for Auracast)
 */
static int broadcast_update_base(const char *name, uint16_t context)
{
    le_audio_broadcast_config_t *config = &g_le_audio_ctx.mode_state.broadcast.config;
    int result = 0;

    if (name != NULL) {
        strncpy(config->broadcast_name, name, sizeof(config->broadcast_name) - 1);
        config->broadcast_name[sizeof(config->broadcast_name) - 1] = '\0';

        /* Update broadcast name in extended advertising data */
        result = bap_broadcast_update_name(name);
        if (result != BAP_BROADCAST_OK) {
            return result;
        }
    }

    if (context != 0) {
        config->audio_context = context;

        /* Update streaming context in periodic advertising BASE */
        result = bap_broadcast_update_context(0, context);  /* Subgroup 0 */
        if (result != BAP_BROADCAST_OK) {
            return result;
        }
    }

    return 0;
}

/*******************************************************************************
 * HCI ISOC Operations
 ******************************************************************************/

/**
 * @brief Send SDU over isochronous channel
 */
static int isoc_send_sdu(uint16_t handle, const uint8_t *data, uint16_t length,
                         uint32_t timestamp, uint8_t seq_num)
{
    int result;

    if (data == NULL || length == 0) {
        return -1;
    }

    if (handle == HCI_ISOC_INVALID_HANDLE) {
        return -2;
    }

    /* Send ISO data with timestamp and sequence number */
    result = hci_isoc_send_data_ts(handle, data, length, timestamp, seq_num);
    if (result != HCI_ISOC_OK) {
        g_le_audio_ctx.encode_errors++;
        return result;
    }

    return 0;
}

/**
 * @brief Callback for received ISOC data
 *
 * Called from HCI ISOC layer when audio data is received.
 */
static void isoc_rx_callback(uint16_t handle, const uint8_t *data,
                             uint16_t length, uint32_t timestamp)
{
    lc3_frame_t frame;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    (void)handle;

    if (!g_le_audio_ctx.initialized || g_le_audio_ctx.state != LE_AUDIO_STATE_STREAMING) {
        return;
    }

    /* Check for valid data */
    if (data == NULL || length == 0 || length > LE_AUDIO_MAX_LC3_FRAME_SIZE) {
        /* Packet loss - queue PLC frame */
        frame.length = 0;
        frame.timestamp = timestamp;
    } else {
        /* Valid frame - copy data */
        memcpy(frame.data, data, length);
        frame.length = length;
        frame.timestamp = timestamp;
    }

    /* Push to RX queue */
    if (frame_queue_push(&g_le_audio_ctx.rx_queue, &frame) != 0) {
        /* Queue full - frame dropped */
        g_le_audio_ctx.decode_errors++;
    } else {
        g_le_audio_ctx.frames_received++;
    }

    /* Signal FreeRTOS task that new frame is available via queue */
    if (g_le_audio_ctx.rx_queue_handle != NULL) {
        xQueueSendFromISR(g_le_audio_ctx.rx_queue_handle, &frame, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/*******************************************************************************
 * HCI ISOC Event Handler
 ******************************************************************************/

/**
 * @brief Handle HCI ISOC events
 */
static void le_audio_isoc_event_handler(const hci_isoc_event_t *event, void *user_data)
{
    (void)user_data;

    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case HCI_ISOC_EVENT_CIS_ESTABLISHED:
            /* CIS connection established - update device info */
            if (g_le_audio_ctx.mode == LE_AUDIO_MODE_UNICAST_SOURCE ||
                g_le_audio_ctx.mode == LE_AUDIO_MODE_UNICAST_DUPLEX) {
                notify_event(LE_AUDIO_EVENT_STATE_CHANGED);
            }
            break;

        case HCI_ISOC_EVENT_CIS_DISCONNECTED:
            /* CIS disconnected - handle cleanup */
            if (g_le_audio_ctx.state == LE_AUDIO_STATE_STREAMING) {
                /* Unexpected disconnect - notify error */
                notify_error(HCI_ISOC_ERROR_COMMAND_FAILED);
            }
            break;

        case HCI_ISOC_EVENT_BIG_CREATED:
            /* BIG created successfully - store info */
            if (g_le_audio_ctx.mode == LE_AUDIO_MODE_BROADCAST_SOURCE) {
                g_le_audio_ctx.mode_state.broadcast.big_handle =
                    event->data.big_info.big_handle;
            }
            break;

        case HCI_ISOC_EVENT_BIG_TERMINATED:
            /* BIG terminated */
            if (g_le_audio_ctx.mode == LE_AUDIO_MODE_BROADCAST_SOURCE) {
                g_le_audio_ctx.mode_state.broadcast.big_handle = 0;
                g_le_audio_ctx.mode_state.broadcast.advertising = false;
            }
            break;

        case HCI_ISOC_EVENT_RX_DATA:
            /* Received ISO data - forward to callback */
            isoc_rx_callback(event->data.rx_data.handle,
                            event->data.rx_data.data,
                            event->data.rx_data.sdu_length,
                            event->data.rx_data.timestamp);
            break;

        case HCI_ISOC_EVENT_ERROR:
            notify_error(event->data.error_code);
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * Public API Implementation
 ******************************************************************************/

int le_audio_init(const le_audio_codec_config_t *codec_config)
{
    lc3_config_t lc3_config;
    int result;

    if (codec_config == NULL) {
        return -1;
    }

    if (g_le_audio_ctx.initialized) {
        return -2;  /* Already initialized */
    }

    /* Clear context */
    memset(&g_le_audio_ctx, 0, sizeof(g_le_audio_ctx));

    /* Store codec configuration */
    g_le_audio_ctx.codec_config = *codec_config;

    /* Initialize LC3 codec */
    lc3_config.sample_rate = codec_config->sample_rate;
    lc3_config.frame_duration = (codec_config->frame_duration_us == 7500) ?
                                 LC3_FRAME_DURATION_7_5MS : LC3_FRAME_DURATION_10MS;
    lc3_config.octets_per_frame = codec_config->octets_per_frame;
    lc3_config.channels = codec_config->channels;

    g_le_audio_ctx.lc3_ctx = lc3_wrapper_init(&lc3_config);
    if (g_le_audio_ctx.lc3_ctx == NULL) {
        return -3;
    }

    /* Initialize TX queue (PCM buffers) */
    result = frame_queue_init(&g_le_audio_ctx.tx_queue,
                              g_le_audio_ctx.tx_buffers,
                              sizeof(pcm_buffer_t),
                              LE_AUDIO_FRAME_QUEUE_DEPTH);
    if (result != 0) {
        lc3_wrapper_deinit(g_le_audio_ctx.lc3_ctx);
        return -4;
    }

    /* Initialize RX queue (LC3 frames) */
    result = frame_queue_init(&g_le_audio_ctx.rx_queue,
                              g_le_audio_ctx.rx_frames,
                              sizeof(lc3_frame_t),
                              LE_AUDIO_FRAME_QUEUE_DEPTH);
    if (result != 0) {
        lc3_wrapper_deinit(g_le_audio_ctx.lc3_ctx);
        return -5;
    }

    /* Initialize FreeRTOS synchronization */
    g_le_audio_ctx.tx_queue_handle = xQueueCreate(LE_AUDIO_FRAME_QUEUE_DEPTH,
                                                  sizeof(pcm_buffer_t));
    g_le_audio_ctx.rx_queue_handle = xQueueCreate(LE_AUDIO_FRAME_QUEUE_DEPTH,
                                                  sizeof(lc3_frame_t));
    g_le_audio_ctx.state_mutex = xSemaphoreCreateMutex();
    if (g_le_audio_ctx.tx_queue_handle == NULL ||
        g_le_audio_ctx.rx_queue_handle == NULL ||
        g_le_audio_ctx.state_mutex == NULL) {
        return -6;  /* FreeRTOS resource allocation failed */
    }

    /* Initialize HCI ISOC module */
    result = hci_isoc_init();
    if (result != HCI_ISOC_OK) {
        lc3_wrapper_deinit(g_le_audio_ctx.lc3_ctx);
        vQueueDelete(g_le_audio_ctx.tx_queue_handle);
        vQueueDelete(g_le_audio_ctx.rx_queue_handle);
        vSemaphoreDelete(g_le_audio_ctx.state_mutex);
        return -7;
    }

    /* Register ISOC callback for RX data */
    hci_isoc_register_callback(le_audio_isoc_event_handler, NULL);

    /* Initialize BAP Unicast profile */
    result = bap_unicast_init();
    if (result != BAP_UNICAST_OK) {
        hci_isoc_deinit();
        lc3_wrapper_deinit(g_le_audio_ctx.lc3_ctx);
        vQueueDelete(g_le_audio_ctx.tx_queue_handle);
        vQueueDelete(g_le_audio_ctx.rx_queue_handle);
        vSemaphoreDelete(g_le_audio_ctx.state_mutex);
        return -8;
    }

    /* Initialize BAP Broadcast profile (Auracast) */
    result = bap_broadcast_init();
    if (result != BAP_BROADCAST_OK) {
        bap_unicast_deinit();
        hci_isoc_deinit();
        lc3_wrapper_deinit(g_le_audio_ctx.lc3_ctx);
        vQueueDelete(g_le_audio_ctx.tx_queue_handle);
        vQueueDelete(g_le_audio_ctx.rx_queue_handle);
        vSemaphoreDelete(g_le_audio_ctx.state_mutex);
        return -9;
    }

    /* Initialize PACS (Published Audio Capabilities Service) */
    result = pacs_init();
    if (result != 0) {
        /* PACS init failure is non-fatal, continue anyway */
    }

    g_le_audio_ctx.state = LE_AUDIO_STATE_IDLE;
    g_le_audio_ctx.mode = LE_AUDIO_MODE_IDLE;
    g_le_audio_ctx.initialized = true;

    return 0;
}

void le_audio_deinit(void)
{
    if (!g_le_audio_ctx.initialized) {
        return;
    }

    /* Stop any active streaming */
    if (g_le_audio_ctx.state == LE_AUDIO_STATE_STREAMING) {
        switch (g_le_audio_ctx.mode) {
            case LE_AUDIO_MODE_UNICAST_SOURCE:
            case LE_AUDIO_MODE_UNICAST_SINK:
            case LE_AUDIO_MODE_UNICAST_DUPLEX:
                unicast_disable();
                unicast_release();
                break;
            case LE_AUDIO_MODE_BROADCAST_SOURCE:
                broadcast_disable();
                break;
            default:
                break;
        }
    }

    /* Deinitialize LC3 codec */
    if (g_le_audio_ctx.lc3_ctx != NULL) {
        lc3_wrapper_deinit(g_le_audio_ctx.lc3_ctx);
        g_le_audio_ctx.lc3_ctx = NULL;
    }

    /* Delete FreeRTOS synchronization */
    if (g_le_audio_ctx.tx_queue_handle != NULL) {
        vQueueDelete(g_le_audio_ctx.tx_queue_handle);
        g_le_audio_ctx.tx_queue_handle = NULL;
    }
    if (g_le_audio_ctx.rx_queue_handle != NULL) {
        vQueueDelete(g_le_audio_ctx.rx_queue_handle);
        g_le_audio_ctx.rx_queue_handle = NULL;
    }
    if (g_le_audio_ctx.state_mutex != NULL) {
        vSemaphoreDelete(g_le_audio_ctx.state_mutex);
        g_le_audio_ctx.state_mutex = NULL;
    }

    g_le_audio_ctx.initialized = false;
}

void le_audio_register_callback(le_audio_event_callback_t callback, void *user_data)
{
    g_le_audio_ctx.event_callback = callback;
    g_le_audio_ctx.callback_user_data = user_data;
}

le_audio_state_t le_audio_get_state(void)
{
    return g_le_audio_ctx.state;
}

le_audio_mode_t le_audio_get_mode(void)
{
    return g_le_audio_ctx.mode;
}

/*******************************************************************************
 * Unicast API
 ******************************************************************************/

int le_audio_unicast_start(const le_audio_unicast_config_t *config)
{
    int result;

    if (!g_le_audio_ctx.initialized) {
        return -1;
    }

    if (config == NULL) {
        return -2;
    }

    if (g_le_audio_ctx.state != LE_AUDIO_STATE_IDLE) {
        return -3;  /* Invalid state */
    }

    /* Determine mode based on configuration */
    if (config->bidirectional) {
        g_le_audio_ctx.mode = LE_AUDIO_MODE_UNICAST_DUPLEX;
    } else {
        g_le_audio_ctx.mode = LE_AUDIO_MODE_UNICAST_SOURCE;
    }

    /* Configure unicast */
    result = unicast_configure(config);
    if (result != 0) {
        g_le_audio_ctx.mode = LE_AUDIO_MODE_IDLE;
        return -4;
    }

    /* Enable streaming */
    result = unicast_enable();
    if (result != 0) {
        unicast_release();
        g_le_audio_ctx.mode = LE_AUDIO_MODE_IDLE;
        return -5;
    }

    return 0;
}

int le_audio_unicast_stop(void)
{
    if (!g_le_audio_ctx.initialized) {
        return -1;
    }

    if (g_le_audio_ctx.mode != LE_AUDIO_MODE_UNICAST_SOURCE &&
        g_le_audio_ctx.mode != LE_AUDIO_MODE_UNICAST_SINK &&
        g_le_audio_ctx.mode != LE_AUDIO_MODE_UNICAST_DUPLEX) {
        return -2;  /* Not in unicast mode */
    }

    /* Disable and release */
    unicast_disable();
    unicast_release();

    g_le_audio_ctx.mode = LE_AUDIO_MODE_IDLE;

    return 0;
}

/*******************************************************************************
 * Broadcast API
 ******************************************************************************/

int le_audio_broadcast_start(const le_audio_broadcast_config_t *config)
{
    int result;

    if (!g_le_audio_ctx.initialized) {
        return -1;
    }

    if (config == NULL) {
        return -2;
    }

    if (g_le_audio_ctx.state != LE_AUDIO_STATE_IDLE) {
        return -3;  /* Invalid state */
    }

    g_le_audio_ctx.mode = LE_AUDIO_MODE_BROADCAST_SOURCE;

    /* Configure broadcast */
    result = broadcast_configure(config);
    if (result != 0) {
        g_le_audio_ctx.mode = LE_AUDIO_MODE_IDLE;
        return -4;
    }

    /* Enable streaming */
    result = broadcast_enable();
    if (result != 0) {
        g_le_audio_ctx.mode = LE_AUDIO_MODE_IDLE;
        set_state(LE_AUDIO_STATE_IDLE);
        return -5;
    }

    return 0;
}

int le_audio_broadcast_stop(void)
{
    if (!g_le_audio_ctx.initialized) {
        return -1;
    }

    if (g_le_audio_ctx.mode != LE_AUDIO_MODE_BROADCAST_SOURCE) {
        return -2;  /* Not in broadcast mode */
    }

    broadcast_disable();

    g_le_audio_ctx.mode = LE_AUDIO_MODE_IDLE;
    set_state(LE_AUDIO_STATE_IDLE);

    return 0;
}

int le_audio_broadcast_update_metadata(const char *name, uint16_t context)
{
    if (!g_le_audio_ctx.initialized) {
        return -1;
    }

    if (g_le_audio_ctx.mode != LE_AUDIO_MODE_BROADCAST_SOURCE) {
        return -2;
    }

    return broadcast_update_base(name, context);
}

/*******************************************************************************
 * Audio Data API
 ******************************************************************************/

int le_audio_send_audio(const int16_t *pcm_data, uint16_t sample_count)
{
    pcm_buffer_t buffer;
    uint8_t lc3_frame[LE_AUDIO_MAX_LC3_FRAME_SIZE];
    uint16_t lc3_len;
    uint16_t samples_per_frame;
    int result;

    if (!g_le_audio_ctx.initialized) {
        return -1;
    }

    if (pcm_data == NULL || sample_count == 0) {
        return -2;
    }

    if (g_le_audio_ctx.state != LE_AUDIO_STATE_STREAMING) {
        return -3;  /* Not streaming */
    }

    /* Get samples per frame from LC3 context */
    samples_per_frame = lc3_wrapper_get_samples_per_frame(g_le_audio_ctx.lc3_ctx);

    /* Validate sample count */
    if (sample_count > samples_per_frame) {
        sample_count = samples_per_frame;
    }

    /* Encode PCM to LC3 */
    result = encode_audio_frame(pcm_data, lc3_frame, &lc3_len);
    if (result != 0) {
        return -4;
    }

    /* Send LC3 frame over ISOC */
    switch (g_le_audio_ctx.mode) {
        case LE_AUDIO_MODE_UNICAST_SOURCE:
        case LE_AUDIO_MODE_UNICAST_DUPLEX:
            /* Send to connected CIS */
            if (g_le_audio_ctx.mode_state.unicast.num_devices > 0) {
                unicast_device_t *dev = &g_le_audio_ctx.mode_state.unicast.devices[0];
                result = isoc_send_sdu(dev->cis_handle, lc3_frame, lc3_len, 0, 0);
            }
            break;

        case LE_AUDIO_MODE_BROADCAST_SOURCE:
            /* Send to BIG */
            for (int i = 0; i < g_le_audio_ctx.mode_state.broadcast.num_bis; i++) {
                bis_stream_t *bis = &g_le_audio_ctx.mode_state.broadcast.bis[i];
                if (bis->active) {
                    result = isoc_send_sdu(bis->bis_handle, lc3_frame, lc3_len, 0, 0);
                }
            }
            break;

        default:
            return -5;
    }

    if (result == 0) {
        g_le_audio_ctx.frames_sent++;
    }

    return result;
}

int le_audio_receive_audio(int16_t *pcm_data, uint16_t sample_count, uint32_t timeout_ms)
{
    lc3_frame_t frame;
    uint16_t samples_per_frame;
    int result;

    (void)timeout_ms;  /* TODO: Implement timeout with FreeRTOS */

    if (!g_le_audio_ctx.initialized) {
        return -1;
    }

    if (pcm_data == NULL || sample_count == 0) {
        return -2;
    }

    if (g_le_audio_ctx.state != LE_AUDIO_STATE_STREAMING) {
        return -3;  /* Not streaming */
    }

    /* Only unicast sink/duplex can receive */
    if (g_le_audio_ctx.mode != LE_AUDIO_MODE_UNICAST_SINK &&
        g_le_audio_ctx.mode != LE_AUDIO_MODE_UNICAST_DUPLEX) {
        return -4;
    }

    /* Get samples per frame */
    samples_per_frame = lc3_wrapper_get_samples_per_frame(g_le_audio_ctx.lc3_ctx);

    /* Check if we have enough space */
    if (sample_count < samples_per_frame) {
        return -5;  /* Buffer too small */
    }

    /* Try to get a frame from the queue */
    result = frame_queue_pop(&g_le_audio_ctx.rx_queue, &frame);
    if (result != 0) {
        return 0;  /* No frames available */
    }

    /* Decode LC3 to PCM */
    if (frame.length > 0) {
        result = decode_audio_frame(frame.data, frame.length, pcm_data);
    } else {
        /* Packet loss - use PLC */
        result = decode_audio_frame(NULL, 0, pcm_data);
    }

    if (result != 0) {
        return -6;  /* Decode error */
    }

    return (int)samples_per_frame;
}

/*******************************************************************************
 * Statistics and Debug Functions
 ******************************************************************************/

/**
 * @brief Get LE Audio statistics
 *
 * @param frames_sent     Pointer to store frames sent count
 * @param frames_received Pointer to store frames received count
 * @param encode_errors   Pointer to store encode error count
 * @param decode_errors   Pointer to store decode error count
 */
void le_audio_get_stats(uint32_t *frames_sent, uint32_t *frames_received,
                        uint32_t *encode_errors, uint32_t *decode_errors)
{
    if (frames_sent != NULL) {
        *frames_sent = g_le_audio_ctx.frames_sent;
    }
    if (frames_received != NULL) {
        *frames_received = g_le_audio_ctx.frames_received;
    }
    if (encode_errors != NULL) {
        *encode_errors = g_le_audio_ctx.encode_errors;
    }
    if (decode_errors != NULL) {
        *decode_errors = g_le_audio_ctx.decode_errors;
    }
}

/**
 * @brief Reset LE Audio statistics
 */
void le_audio_reset_stats(void)
{
    g_le_audio_ctx.frames_sent = 0;
    g_le_audio_ctx.frames_received = 0;
    g_le_audio_ctx.encode_errors = 0;
    g_le_audio_ctx.decode_errors = 0;
}

/**
 * @brief Get current broadcast configuration
 *
 * @param config Pointer to store current broadcast config
 * @return 0 on success, -1 if not in broadcast mode
 */
int le_audio_broadcast_get_config(le_audio_broadcast_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    if (g_le_audio_ctx.mode != LE_AUDIO_MODE_BROADCAST_SOURCE) {
        return -2;
    }

    *config = g_le_audio_ctx.mode_state.broadcast.config;
    return 0;
}

/**
 * @brief Check if broadcast is advertising
 *
 * @return true if broadcast periodic advertising is active
 */
bool le_audio_broadcast_is_advertising(void)
{
    if (g_le_audio_ctx.mode != LE_AUDIO_MODE_BROADCAST_SOURCE) {
        return false;
    }

    return g_le_audio_ctx.mode_state.broadcast.advertising;
}

void le_audio_process(void)
{
    pcm_buffer_t pcm_buffer;
    lc3_frame_t lc3_frame;
    uint8_t encoded_frame[LE_AUDIO_MAX_LC3_FRAME_SIZE];
    uint16_t encoded_len;
    int16_t decoded_samples[LE_AUDIO_MAX_PCM_SAMPLES];
    static uint8_t tx_seq_num = 0;
    int result;

    if (!g_le_audio_ctx.initialized) {
        return;
    }

    /* Take state mutex for thread-safe operation */
    if (xSemaphoreTake(g_le_audio_ctx.state_mutex, 0) != pdTRUE) {
        return;  /* Could not acquire mutex */
    }

    switch (g_le_audio_ctx.state) {
        case LE_AUDIO_STATE_IDLE:
        case LE_AUDIO_STATE_CONFIGURED:
            /* Nothing to process in these states */
            break;

        case LE_AUDIO_STATE_ENABLING:
            /* Wait for enable to complete - handled by callbacks */
            break;

        case LE_AUDIO_STATE_STREAMING:
            /* Process TX queue - encode and send pending PCM frames */
            while (xQueueReceive(g_le_audio_ctx.tx_queue_handle, &pcm_buffer, 0) == pdTRUE) {
                /* Encode PCM to LC3 */
                result = encode_audio_frame(pcm_buffer.samples, encoded_frame, &encoded_len);
                if (result == 0) {
                    /* Send based on mode */
                    if (g_le_audio_ctx.mode == LE_AUDIO_MODE_UNICAST_SOURCE ||
                        g_le_audio_ctx.mode == LE_AUDIO_MODE_UNICAST_DUPLEX) {
                        /* Send to first connected device */
                        if (g_le_audio_ctx.mode_state.unicast.num_devices > 0) {
                            unicast_device_t *dev = &g_le_audio_ctx.mode_state.unicast.devices[0];
                            isoc_send_sdu(dev->cis_handle, encoded_frame, encoded_len,
                                         pcm_buffer.timestamp, tx_seq_num++);
                        }
                    } else if (g_le_audio_ctx.mode == LE_AUDIO_MODE_BROADCAST_SOURCE) {
                        /* Send to all active BIS */
                        for (int i = 0; i < g_le_audio_ctx.mode_state.broadcast.num_bis; i++) {
                            bis_stream_t *bis = &g_le_audio_ctx.mode_state.broadcast.bis[i];
                            if (bis->active) {
                                isoc_send_sdu(bis->bis_handle, encoded_frame, encoded_len,
                                             pcm_buffer.timestamp, tx_seq_num);
                            }
                        }
                        tx_seq_num++;
                    }
                    g_le_audio_ctx.frames_sent++;
                }
            }

            /* Process RX queue - decode received LC3 frames */
            while (xQueueReceive(g_le_audio_ctx.rx_queue_handle, &lc3_frame, 0) == pdTRUE) {
                if (lc3_frame.length > 0) {
                    /* Normal decode */
                    result = decode_audio_frame(lc3_frame.data, lc3_frame.length, decoded_samples);
                } else {
                    /* Packet loss - use PLC */
                    result = decode_audio_frame(NULL, 0, decoded_samples);
                }

                if (result == 0) {
                    /* Push decoded samples to output - could notify audio task here */
                    /* For now, samples are decoded and available */
                }
            }
            break;

        case LE_AUDIO_STATE_DISABLING:
            /* Wait for disable to complete - handled by callbacks */
            break;

        default:
            break;
    }

    xSemaphoreGive(g_le_audio_ctx.state_mutex);
}
