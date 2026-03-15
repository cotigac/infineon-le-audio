/**
 * @file bap_unicast.h
 * @brief BAP Unicast Client/Server API
 *
 * This module implements the Basic Audio Profile (BAP) Unicast roles
 * for connected isochronous audio streaming:
 * - Unicast Client: Connects to LE Audio sinks (headphones, speakers)
 * - Unicast Server: Accepts connections from LE Audio sources
 *
 * Implements ASE (Audio Stream Endpoint) state machine per BAP 1.0.1
 * and ASCS (Audio Stream Control Service) for stream management.
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BAP_UNICAST_H
#define BAP_UNICAST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum number of ASEs (Audio Stream Endpoints) */
#define BAP_UNICAST_MAX_ASE             4

/** Maximum number of connected devices */
#define BAP_UNICAST_MAX_CONNECTIONS     2

/** Maximum codec configuration length */
#define BAP_UNICAST_MAX_CODEC_CFG_LEN   32

/** Maximum metadata length */
#define BAP_UNICAST_MAX_METADATA_LEN    64

/** Maximum QoS configuration length */
#define BAP_UNICAST_MAX_QOS_CFG_LEN     16

/** ASE ID range */
#define BAP_UNICAST_ASE_ID_MIN          0x01
#define BAP_UNICAST_ASE_ID_MAX          0xFF

/*******************************************************************************
 * ASCS UUIDs (Audio Stream Control Service)
 ******************************************************************************/

/** ASCS Service UUID */
#define UUID_ASCS_SERVICE               0x184E

/** ASCS Characteristic UUIDs */
#define UUID_ASCS_SINK_ASE              0x2BC4
#define UUID_ASCS_SOURCE_ASE            0x2BC5
#define UUID_ASCS_ASE_CONTROL_POINT     0x2BC6

/*******************************************************************************
 * ASE State Machine (per BAP 1.0.1)
 ******************************************************************************/

/** ASE States */
typedef enum {
    BAP_ASE_STATE_IDLE = 0x00,          /**< Idle - no audio stream */
    BAP_ASE_STATE_CODEC_CONFIGURED = 0x01, /**< Codec configured */
    BAP_ASE_STATE_QOS_CONFIGURED = 0x02,   /**< QoS configured */
    BAP_ASE_STATE_ENABLING = 0x03,       /**< Enabling stream */
    BAP_ASE_STATE_STREAMING = 0x04,      /**< Streaming audio */
    BAP_ASE_STATE_DISABLING = 0x05,      /**< Disabling stream */
    BAP_ASE_STATE_RELEASING = 0x06       /**< Releasing resources */
} bap_ase_state_t;

/** ASE Direction */
typedef enum {
    BAP_ASE_DIRECTION_SINK = 0x01,      /**< Audio Sink (receives audio) */
    BAP_ASE_DIRECTION_SOURCE = 0x02     /**< Audio Source (sends audio) */
} bap_ase_direction_t;

/*******************************************************************************
 * ASE Control Point Opcodes
 ******************************************************************************/

typedef enum {
    BAP_ASE_OPCODE_CONFIG_CODEC = 0x01,
    BAP_ASE_OPCODE_CONFIG_QOS = 0x02,
    BAP_ASE_OPCODE_ENABLE = 0x03,
    BAP_ASE_OPCODE_RECEIVER_START_READY = 0x04,
    BAP_ASE_OPCODE_DISABLE = 0x05,
    BAP_ASE_OPCODE_RECEIVER_STOP_READY = 0x06,
    BAP_ASE_OPCODE_UPDATE_METADATA = 0x07,
    BAP_ASE_OPCODE_RELEASE = 0x08
} bap_ase_opcode_t;

/** ASE Control Point Response Codes */
typedef enum {
    BAP_ASE_RESPONSE_SUCCESS = 0x00,
    BAP_ASE_RESPONSE_UNSUPPORTED_OPCODE = 0x01,
    BAP_ASE_RESPONSE_INVALID_LENGTH = 0x02,
    BAP_ASE_RESPONSE_INVALID_ASE_ID = 0x03,
    BAP_ASE_RESPONSE_INVALID_ASE_STATE = 0x04,
    BAP_ASE_RESPONSE_INVALID_ASE_DIRECTION = 0x05,
    BAP_ASE_RESPONSE_UNSUPPORTED_AUDIO_CAPS = 0x06,
    BAP_ASE_RESPONSE_UNSUPPORTED_CONFIG = 0x07,
    BAP_ASE_RESPONSE_REJECTED_CONFIG = 0x08,
    BAP_ASE_RESPONSE_INVALID_CONFIG = 0x09,
    BAP_ASE_RESPONSE_UNSUPPORTED_METADATA = 0x0A,
    BAP_ASE_RESPONSE_REJECTED_METADATA = 0x0B,
    BAP_ASE_RESPONSE_INVALID_METADATA = 0x0C,
    BAP_ASE_RESPONSE_INSUFFICIENT_RESOURCES = 0x0D,
    BAP_ASE_RESPONSE_UNSPECIFIED_ERROR = 0x0E
} bap_ase_response_t;

/*******************************************************************************
 * Target Latency
 ******************************************************************************/

typedef enum {
    BAP_TARGET_LATENCY_LOW = 0x01,      /**< Low latency (gaming, calls) */
    BAP_TARGET_LATENCY_BALANCED = 0x02, /**< Balanced */
    BAP_TARGET_LATENCY_HIGH = 0x03      /**< High reliability (media) */
} bap_target_latency_t;

/** Target PHY */
typedef enum {
    BAP_TARGET_PHY_1M = 0x01,
    BAP_TARGET_PHY_2M = 0x02,
    BAP_TARGET_PHY_CODED = 0x03
} bap_target_phy_t;

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    BAP_UNICAST_OK = 0,
    BAP_UNICAST_ERROR_INVALID_PARAM = -1,
    BAP_UNICAST_ERROR_NOT_INITIALIZED = -2,
    BAP_UNICAST_ERROR_ALREADY_INITIALIZED = -3,
    BAP_UNICAST_ERROR_INVALID_STATE = -4,
    BAP_UNICAST_ERROR_NO_RESOURCES = -5,
    BAP_UNICAST_ERROR_NOT_CONNECTED = -6,
    BAP_UNICAST_ERROR_ASE_NOT_FOUND = -7,
    BAP_UNICAST_ERROR_CODEC_ERROR = -8,
    BAP_UNICAST_ERROR_QOS_ERROR = -9,
    BAP_UNICAST_ERROR_CIS_FAILED = -10,
    BAP_UNICAST_ERROR_TIMEOUT = -11,
    BAP_UNICAST_ERROR_REJECTED = -12
} bap_unicast_error_t;

/*******************************************************************************
 * Types - Codec Configuration
 ******************************************************************************/

/** LC3 Codec Configuration */
typedef struct {
    uint8_t sampling_frequency;         /**< LC3 sampling frequency code */
    uint8_t frame_duration;             /**< LC3 frame duration code */
    uint32_t audio_channel_allocation;  /**< Audio location bitmask */
    uint16_t octets_per_codec_frame;    /**< Octets per codec frame */
    uint8_t codec_frames_per_sdu;       /**< Codec frames per SDU */
} bap_lc3_codec_config_t;

/** Generic Codec Configuration */
typedef struct {
    uint8_t coding_format;              /**< Coding format (0x06 = LC3) */
    uint16_t company_id;                /**< Company ID (0x0000 for BT SIG) */
    uint16_t vendor_codec_id;           /**< Vendor codec ID */
    uint8_t codec_specific_config[BAP_UNICAST_MAX_CODEC_CFG_LEN];
    uint8_t codec_specific_config_len;
} bap_codec_config_t;

/*******************************************************************************
 * Types - QoS Configuration
 ******************************************************************************/

/** QoS Configuration */
typedef struct {
    uint8_t cig_id;                     /**< CIG ID */
    uint8_t cis_id;                     /**< CIS ID */
    uint32_t sdu_interval;              /**< SDU interval (microseconds) */
    uint8_t framing;                    /**< Framing mode */
    uint8_t phy;                        /**< PHY */
    uint16_t max_sdu;                   /**< Maximum SDU size */
    uint8_t retransmission_number;      /**< Retransmission number */
    uint16_t max_transport_latency;     /**< Max transport latency (ms) */
    uint32_t presentation_delay;        /**< Presentation delay (us) */
} bap_qos_config_t;

/*******************************************************************************
 * Types - ASE
 ******************************************************************************/

/** ASE (Audio Stream Endpoint) information */
typedef struct {
    uint8_t ase_id;                     /**< ASE identifier */
    bap_ase_direction_t direction;      /**< Sink or Source */
    bap_ase_state_t state;              /**< Current state */
    uint16_t conn_handle;               /**< ACL connection handle */
    uint16_t cis_handle;                /**< CIS handle (when streaming) */

    /* Codec configuration */
    bap_codec_config_t codec_config;
    bap_lc3_codec_config_t lc3_config;  /**< Parsed LC3 config */

    /* QoS configuration */
    bap_qos_config_t qos_config;

    /* Metadata */
    uint8_t metadata[BAP_UNICAST_MAX_METADATA_LEN];
    uint8_t metadata_len;

    /* Runtime info */
    bool data_path_configured;
    uint16_t seq_num;                   /**< TX sequence number */
} bap_ase_t;

/*******************************************************************************
 * Types - Connection
 ******************************************************************************/

/** Connected device information */
typedef struct {
    uint16_t conn_handle;               /**< ACL connection handle */
    uint8_t peer_addr[6];               /**< Peer device address */
    uint8_t peer_addr_type;             /**< Peer address type */
    bool ascs_discovered;               /**< ASCS service discovered */
    uint8_t num_sink_ase;               /**< Number of sink ASEs */
    uint8_t num_source_ase;             /**< Number of source ASEs */
    uint8_t sink_ase_ids[BAP_UNICAST_MAX_ASE];    /**< Sink ASE IDs */
    uint8_t source_ase_ids[BAP_UNICAST_MAX_ASE];  /**< Source ASE IDs */
} bap_unicast_connection_t;

/*******************************************************************************
 * Types - Configuration Requests
 ******************************************************************************/

/** Codec configuration request */
typedef struct {
    uint8_t ase_id;
    bap_target_latency_t target_latency;
    bap_target_phy_t target_phy;
    bap_codec_config_t codec_config;
} bap_codec_config_request_t;

/** QoS configuration request */
typedef struct {
    uint8_t ase_id;
    bap_qos_config_t qos_config;
} bap_qos_config_request_t;

/** Enable request */
typedef struct {
    uint8_t ase_id;
    uint8_t metadata[BAP_UNICAST_MAX_METADATA_LEN];
    uint8_t metadata_len;
} bap_enable_request_t;

/*******************************************************************************
 * Types - Events
 ******************************************************************************/

/** Unicast event types */
typedef enum {
    BAP_UNICAST_EVENT_CONNECTED,        /**< Device connected */
    BAP_UNICAST_EVENT_DISCONNECTED,     /**< Device disconnected */
    BAP_UNICAST_EVENT_ASCS_DISCOVERED,  /**< ASCS service discovered */
    BAP_UNICAST_EVENT_ASE_STATE_CHANGED,/**< ASE state changed */
    BAP_UNICAST_EVENT_CODEC_CONFIGURED, /**< Codec configured */
    BAP_UNICAST_EVENT_QOS_CONFIGURED,   /**< QoS configured */
    BAP_UNICAST_EVENT_ENABLED,          /**< ASE enabled */
    BAP_UNICAST_EVENT_STREAMING,        /**< Streaming started */
    BAP_UNICAST_EVENT_DISABLED,         /**< ASE disabled */
    BAP_UNICAST_EVENT_RELEASED,         /**< ASE released */
    BAP_UNICAST_EVENT_CIS_ESTABLISHED,  /**< CIS established */
    BAP_UNICAST_EVENT_CIS_DISCONNECTED, /**< CIS disconnected */
    BAP_UNICAST_EVENT_RX_DATA,          /**< Audio data received */
    BAP_UNICAST_EVENT_ERROR             /**< Error occurred */
} bap_unicast_event_type_t;

/** Audio data received */
typedef struct {
    uint8_t ase_id;
    const uint8_t *data;
    uint16_t length;
    uint32_t timestamp;
    uint16_t seq_num;
} bap_unicast_rx_data_t;

/** Unicast event data */
typedef struct {
    bap_unicast_event_type_t type;
    uint16_t conn_handle;
    union {
        bap_unicast_connection_t connection;
        bap_ase_t ase;
        bap_unicast_rx_data_t rx_data;
        int error_code;
    } data;
} bap_unicast_event_t;

/** Unicast event callback */
typedef void (*bap_unicast_callback_t)(const bap_unicast_event_t *event, void *user_data);

/*******************************************************************************
 * Types - Statistics
 ******************************************************************************/

/** Unicast statistics */
typedef struct {
    uint32_t connections;               /**< Total connections */
    uint32_t disconnections;            /**< Total disconnections */
    uint32_t cis_established;           /**< CIS streams established */
    uint32_t cis_disconnected;          /**< CIS streams disconnected */
    uint32_t tx_frames;                 /**< Frames transmitted */
    uint32_t rx_frames;                 /**< Frames received */
    uint32_t tx_bytes;                  /**< Bytes transmitted */
    uint32_t rx_bytes;                  /**< Bytes received */
    uint32_t tx_errors;                 /**< TX errors */
    uint32_t rx_errors;                 /**< RX errors */
} bap_unicast_stats_t;

/*******************************************************************************
 * Default Configurations
 ******************************************************************************/

/** Default LC3 codec config (48kHz, 10ms, 100 octets) */
#define BAP_UNICAST_LC3_CONFIG_48_10_100 {  \
    .sampling_frequency = 0x08,              \
    .frame_duration = 0x01,                  \
    .audio_channel_allocation = 0x00000001,  \
    .octets_per_codec_frame = 100,           \
    .codec_frames_per_sdu = 1                \
}

/** Default QoS config for low latency */
#define BAP_UNICAST_QOS_CONFIG_LOW_LATENCY { \
    .cig_id = 0,                             \
    .cis_id = 0,                             \
    .sdu_interval = 10000,                   \
    .framing = 0,                            \
    .phy = 0x02,                             \
    .max_sdu = 100,                          \
    .retransmission_number = 2,              \
    .max_transport_latency = 20,             \
    .presentation_delay = 20000              \
}

/** Default QoS config for high reliability */
#define BAP_UNICAST_QOS_CONFIG_HIGH_RELIABILITY { \
    .cig_id = 0,                             \
    .cis_id = 0,                             \
    .sdu_interval = 10000,                   \
    .framing = 0,                            \
    .phy = 0x02,                             \
    .max_sdu = 100,                          \
    .retransmission_number = 5,              \
    .max_transport_latency = 60,             \
    .presentation_delay = 40000              \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize BAP Unicast
 *
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_init(void);

/**
 * @brief Deinitialize BAP Unicast
 */
void bap_unicast_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void bap_unicast_register_callback(bap_unicast_callback_t callback, void *user_data);

/*******************************************************************************
 * API Functions - Client Role
 ******************************************************************************/

/**
 * @brief Discover ASCS on connected device
 *
 * Discovers the Audio Stream Control Service and ASE characteristics.
 *
 * @param conn_handle ACL connection handle
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_discover(uint16_t conn_handle);

/**
 * @brief Configure codec on remote ASE
 *
 * @param conn_handle ACL connection handle
 * @param request Codec configuration request
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_config_codec(uint16_t conn_handle, const bap_codec_config_request_t *request);

/**
 * @brief Configure QoS on remote ASE
 *
 * @param conn_handle ACL connection handle
 * @param request QoS configuration request
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_config_qos(uint16_t conn_handle, const bap_qos_config_request_t *request);

/**
 * @brief Enable ASE for streaming
 *
 * @param conn_handle ACL connection handle
 * @param request Enable request
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_enable(uint16_t conn_handle, const bap_enable_request_t *request);

/**
 * @brief Signal receiver ready (for Source ASEs)
 *
 * @param conn_handle ACL connection handle
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_receiver_start_ready(uint16_t conn_handle, uint8_t ase_id);

/**
 * @brief Disable ASE
 *
 * @param conn_handle ACL connection handle
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_disable(uint16_t conn_handle, uint8_t ase_id);

/**
 * @brief Signal receiver stop ready
 *
 * @param conn_handle ACL connection handle
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_receiver_stop_ready(uint16_t conn_handle, uint8_t ase_id);

/**
 * @brief Update ASE metadata
 *
 * @param conn_handle ACL connection handle
 * @param ase_id ASE identifier
 * @param metadata Metadata bytes
 * @param metadata_len Metadata length
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_update_metadata(uint16_t conn_handle, uint8_t ase_id,
                                 const uint8_t *metadata, uint8_t metadata_len);

/**
 * @brief Release ASE
 *
 * @param conn_handle ACL connection handle
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_release(uint16_t conn_handle, uint8_t ase_id);

/*******************************************************************************
 * API Functions - Server Role
 ******************************************************************************/

/**
 * @brief Register local ASE
 *
 * Registers a local Audio Stream Endpoint for the server role.
 *
 * @param direction Sink or Source
 * @param ase_id Pointer to receive assigned ASE ID
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_register_ase(bap_ase_direction_t direction, uint8_t *ase_id);

/**
 * @brief Unregister local ASE
 *
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_unregister_ase(uint8_t ase_id);

/**
 * @brief Accept codec configuration from client
 *
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_accept_codec(uint8_t ase_id);

/**
 * @brief Reject codec configuration from client
 *
 * @param ase_id ASE identifier
 * @param reason Rejection reason
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_reject_codec(uint8_t ase_id, bap_ase_response_t reason);

/**
 * @brief Accept QoS configuration from client
 *
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_accept_qos(uint8_t ase_id);

/**
 * @brief Reject QoS configuration from client
 *
 * @param ase_id ASE identifier
 * @param reason Rejection reason
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_reject_qos(uint8_t ase_id, bap_ase_response_t reason);

/*******************************************************************************
 * API Functions - CIS Management
 ******************************************************************************/

/**
 * @brief Create CIS for ASE
 *
 * Creates the Connected Isochronous Stream for an enabled ASE.
 *
 * @param conn_handle ACL connection handle
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_create_cis(uint16_t conn_handle, uint8_t ase_id);

/**
 * @brief Setup ISO data path for ASE
 *
 * Configures the ISO data path for streaming.
 *
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_setup_data_path(uint8_t ase_id);

/*******************************************************************************
 * API Functions - Audio Data
 ******************************************************************************/

/**
 * @brief Send audio data on ASE
 *
 * Sends an LC3 encoded frame on the specified ASE.
 *
 * @param ase_id ASE identifier
 * @param data LC3 encoded frame
 * @param length Frame length
 * @param timestamp Timestamp (microseconds)
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_send(uint8_t ase_id, const uint8_t *data, uint16_t length,
                      uint32_t timestamp);

/**
 * @brief Send audio data on CIS handle
 *
 * @param cis_handle CIS handle
 * @param data LC3 encoded frame
 * @param length Frame length
 * @param timestamp Timestamp
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_send_on_cis(uint16_t cis_handle, const uint8_t *data,
                             uint16_t length, uint32_t timestamp);

/*******************************************************************************
 * API Functions - Query
 ******************************************************************************/

/**
 * @brief Get ASE information
 *
 * @param ase_id ASE identifier
 * @param ase Pointer to structure to fill
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_get_ase(uint8_t ase_id, bap_ase_t *ase);

/**
 * @brief Get connection information
 *
 * @param conn_handle ACL connection handle
 * @param connection Pointer to structure to fill
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_get_connection(uint16_t conn_handle, bap_unicast_connection_t *connection);

/**
 * @brief Get number of streaming ASEs
 *
 * @return Number of ASEs in streaming state
 */
uint8_t bap_unicast_get_streaming_count(void);

/**
 * @brief Get ASE by CIS handle
 *
 * @param cis_handle CIS handle
 * @param ase Pointer to structure to fill
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_get_ase_by_cis(uint16_t cis_handle, bap_ase_t *ase);

/*******************************************************************************
 * API Functions - Statistics
 ******************************************************************************/

/**
 * @brief Get unicast statistics
 *
 * @param stats Pointer to statistics structure
 */
void bap_unicast_get_stats(bap_unicast_stats_t *stats);

/**
 * @brief Reset unicast statistics
 */
void bap_unicast_reset_stats(void);

/*******************************************************************************
 * API Functions - Convenience
 ******************************************************************************/

/**
 * @brief Start streaming to sink ASE (simplified)
 *
 * Performs codec config, QoS config, enable, and CIS creation.
 *
 * @param conn_handle ACL connection handle
 * @param ase_id Remote sink ASE ID
 * @param lc3_config LC3 codec configuration
 * @param qos_config QoS configuration
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_start_stream(uint16_t conn_handle, uint8_t ase_id,
                              const bap_lc3_codec_config_t *lc3_config,
                              const bap_qos_config_t *qos_config);

/**
 * @brief Stop streaming on ASE
 *
 * @param ase_id ASE identifier
 * @return BAP_UNICAST_OK on success, negative error code on failure
 */
int bap_unicast_stop_stream(uint8_t ase_id);

#ifdef __cplusplus
}
#endif

#endif /* BAP_UNICAST_H */
