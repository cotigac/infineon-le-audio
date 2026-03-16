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
    /* QueueHandle_t tx_queue_handle; */
    /* QueueHandle_t rx_queue_handle; */
    /* SemaphoreHandle_t state_mutex; */

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
 *
 * TODO: Implement using Infineon BTSTACK BAP API
 */
static int unicast_configure(const le_audio_unicast_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    /* Store configuration */
    g_le_audio_ctx.mode_state.unicast.config = *config;

    /*
     * TODO: Implement BAP Unicast Client configuration
     *
     * Steps:
     * 1. Create CIG (Connected Isochronous Group)
     *    - wiced_bt_isoc_central_create_cig()
     *
     * 2. Configure codec via ASCS (Audio Stream Control Service)
     *    - Send Config Codec operation to ASE
     *    - Set LC3 configuration (sample rate, frame duration, octets)
     *
     * 3. Configure QoS
     *    - Send Config QoS operation to ASE
     *    - Set SDU interval, framing, latency, retransmissions
     *
     * Example CIG parameters:
     *
     * wiced_bt_isoc_cig_param_t cig_param = {
     *     .cig_id = 0,
     *     .sdu_interval_c_to_p = 10000,  // 10ms in microseconds
     *     .sdu_interval_p_to_c = 10000,
     *     .worst_case_sca = 0,
     *     .packing = 0,  // Sequential
     *     .framing = 0,  // Unframed
     *     .max_transport_latency_c_to_p = 40,  // 40ms
     *     .max_transport_latency_p_to_c = 40,
     *     .cis_count = 1,
     * };
     */

    set_state(LE_AUDIO_STATE_CONFIGURED);

    return 0;
}

/**
 * @brief Enable unicast streaming
 */
static int unicast_enable(void)
{
    /*
     * TODO: Implement BAP Enable operation
     *
     * Steps:
     * 1. Send Enable operation to ASE
     * 2. Establish CIS connection
     *    - wiced_bt_isoc_central_create_cis()
     * 3. Set up ISO data path
     *    - wiced_bt_isoc_setup_data_path()
     * 4. Start streaming
     *
     * Example CIS parameters:
     *
     * wiced_bt_isoc_cis_param_t cis_param = {
     *     .cis_id = 0,
     *     .max_sdu_c_to_p = 100,  // octets_per_frame
     *     .max_sdu_p_to_c = 100,
     *     .phy_c_to_p = 2,  // 2M PHY
     *     .phy_p_to_c = 2,
     *     .rtn_c_to_p = 2,  // Retransmissions
     *     .rtn_p_to_c = 2,
     * };
     */

    set_state(LE_AUDIO_STATE_ENABLING);

    /* Simulate successful enable for now */
    set_state(LE_AUDIO_STATE_STREAMING);
    notify_event(LE_AUDIO_EVENT_STREAM_STARTED);

    return 0;
}

/**
 * @brief Disable unicast streaming
 */
static int unicast_disable(void)
{
    /*
     * TODO: Implement BAP Disable operation
     *
     * Steps:
     * 1. Send Disable operation to ASE
     * 2. Remove ISO data path
     *    - wiced_bt_isoc_remove_data_path()
     * 3. Disconnect CIS
     *    - wiced_bt_isoc_disconnect_cis()
     */

    set_state(LE_AUDIO_STATE_DISABLING);

    notify_event(LE_AUDIO_EVENT_STREAM_STOPPED);
    set_state(LE_AUDIO_STATE_CONFIGURED);

    return 0;
}

/**
 * @brief Release unicast resources
 */
static int unicast_release(void)
{
    /*
     * TODO: Implement BAP Release operation
     *
     * Steps:
     * 1. Send Release operation to ASE
     * 2. Remove CIG
     *    - wiced_bt_isoc_central_remove_cig()
     */

    memset(&g_le_audio_ctx.mode_state.unicast, 0, sizeof(unicast_state_t));
    set_state(LE_AUDIO_STATE_IDLE);

    return 0;
}

/*******************************************************************************
 * BAP Broadcast Operations
 ******************************************************************************/

/**
 * @brief Configure broadcast audio (Auracast)
 *
 * TODO: Port from Zephyr BAP broadcast source implementation
 */
static int broadcast_configure(const le_audio_broadcast_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    /* Validate configuration */
    if (config->num_subgroups == 0 || config->num_bis_per_subgroup == 0) {
        return -2;
    }

    /* Store configuration */
    g_le_audio_ctx.mode_state.broadcast.config = *config;

    /*
     * TODO: Implement BAP Broadcast Source configuration
     *
     * This requires porting from Zephyr's bap_broadcast_source.c
     *
     * Steps:
     * 1. Create BASE (Broadcast Audio Source Endpoint) structure
     *    - Presentation delay
     *    - Codec configuration (LC3)
     *    - Subgroup metadata
     *    - BIS configuration
     *
     * 2. Set up periodic advertising
     *    - Extended advertising with broadcast audio announcement
     *    - Periodic advertising with BASE
     *
     * 3. Create BIG (Broadcast Isochronous Group)
     *    - wiced_bt_isoc_create_big() or HCI_LE_Create_BIG
     *
     * BASE structure example:
     *
     * struct bt_bap_base {
     *     uint32_t presentation_delay;
     *     uint8_t num_subgroups;
     *     struct bt_bap_base_subgroup {
     *         uint8_t codec_id[5];      // LC3 codec ID
     *         uint8_t codec_config[];   // Sample rate, frame duration, etc.
     *         uint8_t metadata[];       // Audio context, language, etc.
     *         uint8_t num_bis;
     *         struct bt_bap_base_bis {
     *             uint8_t index;
     *             uint8_t codec_config[];  // Per-BIS configuration
     *         } bis[];
     *     } subgroups[];
     * };
     *
     * HCI_LE_Create_BIG parameters:
     *
     * typedef struct {
     *     uint8_t big_handle;
     *     uint8_t advertising_handle;
     *     uint8_t num_bis;
     *     uint32_t sdu_interval;        // 10000 (10ms)
     *     uint16_t max_sdu;             // 100 (octets_per_frame)
     *     uint16_t max_transport_latency;
     *     uint8_t rtn;                  // Retransmissions
     *     uint8_t phy;                  // 2M PHY
     *     uint8_t packing;              // Sequential
     *     uint8_t framing;              // Unframed
     *     uint8_t encryption;
     *     uint8_t broadcast_code[16];   // Optional encryption key
     * } hci_le_create_big_params_t;
     */

    set_state(LE_AUDIO_STATE_CONFIGURED);

    return 0;
}

/**
 * @brief Enable broadcast streaming
 */
static int broadcast_enable(void)
{
    /*
     * TODO: Implement broadcast enable
     *
     * Steps:
     * 1. Start extended advertising (broadcast audio announcement)
     * 2. Start periodic advertising (BASE data)
     * 3. Create BIG
     * 4. Set up ISO data path for each BIS
     */

    set_state(LE_AUDIO_STATE_ENABLING);

    /*
     * TODO: Wait for BIG creation complete callback
     *
     * On HCI_LE_BIG_Complete event:
     * - Store BIG handle and BIS handles
     * - Set up ISO data paths
     * - Transition to STREAMING state
     */

    /* Simulate successful enable for now */
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
    /*
     * TODO: Implement broadcast disable
     *
     * Steps:
     * 1. Terminate BIG
     *    - wiced_bt_isoc_terminate_big() or HCI_LE_Terminate_BIG
     * 2. Stop periodic advertising
     * 3. Stop extended advertising
     */

    set_state(LE_AUDIO_STATE_DISABLING);

    g_le_audio_ctx.mode_state.broadcast.advertising = false;

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

    if (name != NULL) {
        strncpy(config->broadcast_name, name, sizeof(config->broadcast_name) - 1);
        config->broadcast_name[sizeof(config->broadcast_name) - 1] = '\0';
    }

    if (context != 0) {
        config->audio_context = context;
    }

    /*
     * TODO: Update periodic advertising data with new BASE
     *
     * This requires updating the periodic advertising data while
     * broadcast is active. The BASE structure contains the metadata
     * that receivers use to understand the broadcast content.
     */

    return 0;
}

/*******************************************************************************
 * HCI ISOC Operations
 ******************************************************************************/

/**
 * @brief Send SDU over isochronous channel
 *
 * TODO: Implement using Infineon BTSTACK ISOC API
 */
static int isoc_send_sdu(uint16_t handle, const uint8_t *data, uint16_t length,
                         uint32_t timestamp, uint8_t seq_num)
{
    (void)handle;
    (void)data;
    (void)length;
    (void)timestamp;
    (void)seq_num;

    /*
     * TODO: Implement HCI ISOC data transmission
     *
     * For CIS (unicast):
     *   wiced_bt_isoc_write_sdu(handle, data, length, timestamp);
     *
     * For BIS (broadcast):
     *   wiced_bt_isoc_write_big_sdu(big_handle, bis_index, data, length, timestamp);
     *
     * The timestamp should be synchronized with the ISO interval.
     * The sequence number is used for packet ordering.
     *
     * HCI ISO data packet format:
     * - Connection handle (12 bits) + PB flag (2 bits) + TS flag (1 bit)
     * - Data length (14 bits) + RFU (2 bits)
     * - Timestamp (optional, 4 bytes)
     * - Packet sequence number (2 bytes)
     * - ISO SDU length (2 bytes) + RFU
     * - ISO SDU data
     */

    return 0;
}

/**
 * @brief Callback for received ISOC data
 *
 * TODO: Register with Infineon BTSTACK ISOC layer
 */
static void isoc_rx_callback(uint16_t handle, const uint8_t *data,
                             uint16_t length, uint32_t timestamp)
{
    lc3_frame_t frame;

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

    /*
     * TODO: Signal FreeRTOS task that new frame is available
     *
     * BaseType_t xHigherPriorityTaskWoken = pdFALSE;
     * xQueueSendFromISR(g_le_audio_ctx.rx_queue_handle, &frame, &xHigherPriorityTaskWoken);
     * portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
     */
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

    /*
     * TODO: Initialize FreeRTOS synchronization
     *
     * g_le_audio_ctx.tx_queue_handle = xQueueCreate(LE_AUDIO_FRAME_QUEUE_DEPTH,
     *                                               sizeof(pcm_buffer_t));
     * g_le_audio_ctx.rx_queue_handle = xQueueCreate(LE_AUDIO_FRAME_QUEUE_DEPTH,
     *                                               sizeof(lc3_frame_t));
     * g_le_audio_ctx.state_mutex = xSemaphoreCreateMutex();
     */

    /*
     * TODO: Initialize Bluetooth stack integration
     *
     * - Register ISOC callbacks
     * - Initialize BAP profiles
     * - Set up PACS (Published Audio Capabilities Service)
     */

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

    /*
     * TODO: Delete FreeRTOS synchronization
     *
     * if (g_le_audio_ctx.tx_queue_handle != NULL) {
     *     vQueueDelete(g_le_audio_ctx.tx_queue_handle);
     * }
     * if (g_le_audio_ctx.rx_queue_handle != NULL) {
     *     vQueueDelete(g_le_audio_ctx.rx_queue_handle);
     * }
     * if (g_le_audio_ctx.state_mutex != NULL) {
     *     vSemaphoreDelete(g_le_audio_ctx.state_mutex);
     * }
     */

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
    if (!g_le_audio_ctx.initialized) {
        return;
    }

    /*
     * TODO: Process LE Audio state machine
     *
     * This function handles:
     * - State transitions (idle → configuring → streaming)
     * - Codec negotiations
     * - QoS updates
     * - ISOC stream monitoring
     */

    /* Placeholder - actual implementation needs BAP integration */
}
