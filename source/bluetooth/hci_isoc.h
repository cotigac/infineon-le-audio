/**
 * @file hci_isoc.h
 * @brief HCI Isochronous Channels API
 *
 * This module provides management of HCI isochronous channels for
 * LE Audio streaming, including:
 * - CIG/CIS for unicast audio (Connected Isochronous)
 * - BIG/BIS for broadcast audio (Broadcast Isochronous / Auracast)
 *
 * Based on Bluetooth Core Specification 5.4, Volume 4, Part E
 * (HCI Commands for LE Isochronous Channels)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HCI_ISOC_H
#define HCI_ISOC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum CIG (Connected Isochronous Groups) */
#define HCI_ISOC_MAX_CIG            2

/** Maximum CIS per CIG */
#define HCI_ISOC_MAX_CIS_PER_CIG    4

/** Maximum BIG (Broadcast Isochronous Groups) */
#define HCI_ISOC_MAX_BIG            2

/** Maximum BIS per BIG */
#define HCI_ISOC_MAX_BIS_PER_BIG    4

/** Maximum ISO data packet size */
#define HCI_ISOC_MAX_SDU_SIZE       251

/** ISO data packet header size */
#define HCI_ISOC_HEADER_SIZE        4

/** Invalid handle */
#define HCI_ISOC_INVALID_HANDLE     0xFFFF

/** PHY definitions */
#define HCI_ISOC_PHY_1M             0x01
#define HCI_ISOC_PHY_2M             0x02
#define HCI_ISOC_PHY_CODED          0x04

/** Framing modes */
#define HCI_ISOC_FRAMING_UNFRAMED   0x00
#define HCI_ISOC_FRAMING_FRAMED     0x01

/** Packing modes */
#define HCI_ISOC_PACKING_SEQUENTIAL 0x00
#define HCI_ISOC_PACKING_INTERLEAVED 0x01

/** ISO Data Path directions */
#define HCI_ISOC_DATA_PATH_INPUT    0x00    /**< Host to Controller */
#define HCI_ISOC_DATA_PATH_OUTPUT   0x01    /**< Controller to Host */

/** ISO Data Path IDs */
#define HCI_ISOC_DATA_PATH_HCI      0x00    /**< Data via HCI */
#define HCI_ISOC_DATA_PATH_DISABLED 0xFF    /**< Data path disabled */

/** Broadcast encryption modes */
#define HCI_ISOC_ENCRYPT_DISABLED   0x00
#define HCI_ISOC_ENCRYPT_ENABLED    0x01

/*******************************************************************************
 * Error Codes
 ******************************************************************************/

typedef enum {
    HCI_ISOC_OK = 0,
    HCI_ISOC_ERROR_INVALID_PARAM = -1,
    HCI_ISOC_ERROR_NOT_INITIALIZED = -2,
    HCI_ISOC_ERROR_NO_RESOURCES = -3,
    HCI_ISOC_ERROR_INVALID_STATE = -4,
    HCI_ISOC_ERROR_CIG_EXISTS = -5,
    HCI_ISOC_ERROR_CIG_NOT_FOUND = -6,
    HCI_ISOC_ERROR_CIS_NOT_FOUND = -7,
    HCI_ISOC_ERROR_BIG_EXISTS = -8,
    HCI_ISOC_ERROR_BIG_NOT_FOUND = -9,
    HCI_ISOC_ERROR_BIS_NOT_FOUND = -10,
    HCI_ISOC_ERROR_COMMAND_FAILED = -11,
    HCI_ISOC_ERROR_TIMEOUT = -12,
    HCI_ISOC_ERROR_NOT_SUPPORTED = -13
} hci_isoc_error_t;

/*******************************************************************************
 * Types - CIG/CIS (Unicast)
 ******************************************************************************/

/** CIS state */
typedef enum {
    CIS_STATE_IDLE = 0,
    CIS_STATE_CONFIGURED,
    CIS_STATE_CONNECTING,
    CIS_STATE_ESTABLISHED,
    CIS_STATE_DISCONNECTING
} cis_state_t;

/** CIS configuration (per stream) */
typedef struct {
    uint8_t cis_id;                 /**< CIS identifier (0x00-0xEF) */
    uint16_t max_sdu_c_to_p;        /**< Max SDU Central to Peripheral */
    uint16_t max_sdu_p_to_c;        /**< Max SDU Peripheral to Central */
    uint8_t phy_c_to_p;             /**< PHY Central to Peripheral */
    uint8_t phy_p_to_c;             /**< PHY Peripheral to Central */
    uint8_t rtn_c_to_p;             /**< Retransmission number C to P */
    uint8_t rtn_p_to_c;             /**< Retransmission number P to C */
} cis_config_t;

/** CIG configuration (group of CIS streams) */
typedef struct {
    uint8_t cig_id;                 /**< CIG identifier (0x00-0xEF) */
    uint32_t sdu_interval_c_to_p;   /**< SDU interval C to P (microseconds) */
    uint32_t sdu_interval_p_to_c;   /**< SDU interval P to C (microseconds) */
    uint8_t sca;                    /**< Sleep clock accuracy */
    uint8_t packing;                /**< Packing mode */
    uint8_t framing;                /**< Framing mode */
    uint16_t max_transport_latency_c_to_p;  /**< Max transport latency C to P (ms) */
    uint16_t max_transport_latency_p_to_c;  /**< Max transport latency P to C (ms) */
    uint8_t num_cis;                /**< Number of CIS in this CIG */
    cis_config_t cis[HCI_ISOC_MAX_CIS_PER_CIG];  /**< CIS configurations */
} cig_config_t;

/** CIS runtime info */
typedef struct {
    uint8_t cig_id;                 /**< Parent CIG ID */
    uint8_t cis_id;                 /**< CIS ID */
    uint16_t cis_handle;            /**< CIS connection handle */
    uint16_t acl_handle;            /**< Associated ACL connection handle */
    cis_state_t state;              /**< Current state */
    uint16_t max_pdu_c_to_p;        /**< Max PDU C to P */
    uint16_t max_pdu_p_to_c;        /**< Max PDU P to C */
    uint32_t transport_latency_c_to_p;  /**< Actual transport latency C to P (us) */
    uint32_t transport_latency_p_to_c;  /**< Actual transport latency P to C (us) */
    uint8_t phy_c_to_p;             /**< Actual PHY C to P */
    uint8_t phy_p_to_c;             /**< Actual PHY P to C */
    uint8_t nse;                    /**< Number of subevents */
    uint8_t bn_c_to_p;              /**< Burst number C to P */
    uint8_t bn_p_to_c;              /**< Burst number P to C */
    uint8_t ft_c_to_p;              /**< Flush timeout C to P */
    uint8_t ft_p_to_c;              /**< Flush timeout P to C */
    uint16_t iso_interval;          /**< ISO interval (1.25ms units) */
} cis_info_t;

/*******************************************************************************
 * Types - BIG/BIS (Broadcast)
 ******************************************************************************/

/** BIG state */
typedef enum {
    BIG_STATE_IDLE = 0,
    BIG_STATE_CREATING,
    BIG_STATE_ACTIVE,
    BIG_STATE_TERMINATING
} big_state_t;

/** BIG configuration (for broadcast source) */
typedef struct {
    uint8_t big_handle;             /**< BIG handle (0x00-0xEF) */
    uint8_t adv_handle;             /**< Advertising handle for periodic adv */
    uint8_t num_bis;                /**< Number of BIS streams */
    uint32_t sdu_interval;          /**< SDU interval (microseconds) */
    uint16_t max_sdu;               /**< Maximum SDU size */
    uint16_t max_transport_latency; /**< Max transport latency (ms) */
    uint8_t rtn;                    /**< Retransmission number */
    uint8_t phy;                    /**< PHY for transmission */
    uint8_t packing;                /**< Packing mode */
    uint8_t framing;                /**< Framing mode */
    uint8_t encryption;             /**< Encryption enabled */
    uint8_t broadcast_code[16];     /**< Broadcast code (if encrypted) */
} big_config_t;

/** BIG runtime info */
typedef struct {
    uint8_t big_handle;             /**< BIG handle */
    big_state_t state;              /**< Current state */
    uint8_t num_bis;                /**< Number of BIS */
    uint16_t bis_handles[HCI_ISOC_MAX_BIS_PER_BIG];  /**< BIS handles */
    uint32_t big_sync_delay;        /**< BIG sync delay (us) */
    uint32_t transport_latency;     /**< Transport latency (us) */
    uint8_t phy;                    /**< PHY used */
    uint8_t nse;                    /**< Number of subevents */
    uint8_t bn;                     /**< Burst number */
    uint8_t pto;                    /**< Pre-transmission offset */
    uint8_t irc;                    /**< Immediate repetition count */
    uint16_t max_pdu;               /**< Max PDU size */
    uint16_t iso_interval;          /**< ISO interval (1.25ms units) */
} big_info_t;

/** BIG Sync configuration (for broadcast sink) */
typedef struct {
    uint8_t big_handle;             /**< BIG handle to assign */
    uint16_t sync_handle;           /**< Periodic advertising sync handle */
    uint8_t encryption;             /**< Encryption enabled */
    uint8_t broadcast_code[16];     /**< Broadcast code (if encrypted) */
    uint8_t mse;                    /**< Max subevents to receive */
    uint16_t big_sync_timeout;      /**< Sync timeout (10ms units) */
    uint8_t num_bis;                /**< Number of BIS to sync */
    uint8_t bis_indices[HCI_ISOC_MAX_BIS_PER_BIG];  /**< BIS indices to sync */
} big_sync_config_t;

/*******************************************************************************
 * Types - ISO Data
 ******************************************************************************/

/** ISO data packet flags */
typedef enum {
    ISO_DATA_FLAG_COMPLETE = 0x00,  /**< Complete SDU */
    ISO_DATA_FLAG_START = 0x01,     /**< First fragment */
    ISO_DATA_FLAG_CONTINUE = 0x02,  /**< Continuation fragment */
    ISO_DATA_FLAG_END = 0x03        /**< Last fragment */
} iso_data_flag_t;

/** ISO data packet */
typedef struct {
    uint16_t handle;                /**< CIS/BIS handle */
    uint8_t pb_flag;                /**< Packet boundary flag */
    uint8_t ts_flag;                /**< Timestamp flag */
    uint32_t timestamp;             /**< Timestamp (if ts_flag set) */
    uint16_t packet_seq_num;        /**< Packet sequence number */
    uint16_t sdu_length;            /**< SDU length */
    uint8_t *data;                  /**< SDU data pointer */
} iso_data_packet_t;

/*******************************************************************************
 * Types - Events and Callbacks
 ******************************************************************************/

/** ISOC event types */
typedef enum {
    HCI_ISOC_EVENT_CIG_CREATED,     /**< CIG created */
    HCI_ISOC_EVENT_CIG_REMOVED,     /**< CIG removed */
    HCI_ISOC_EVENT_CIS_ESTABLISHED, /**< CIS established */
    HCI_ISOC_EVENT_CIS_DISCONNECTED,/**< CIS disconnected */
    HCI_ISOC_EVENT_CIS_REQUEST,     /**< CIS request from peer */
    HCI_ISOC_EVENT_BIG_CREATED,     /**< BIG created */
    HCI_ISOC_EVENT_BIG_TERMINATED,  /**< BIG terminated */
    HCI_ISOC_EVENT_BIG_SYNC_ESTABLISHED,  /**< BIG sync established */
    HCI_ISOC_EVENT_BIG_SYNC_LOST,   /**< BIG sync lost */
    HCI_ISOC_EVENT_DATA_PATH_SETUP, /**< ISO data path configured */
    HCI_ISOC_EVENT_TX_COMPLETE,     /**< ISO TX complete */
    HCI_ISOC_EVENT_RX_DATA,         /**< ISO data received */
    HCI_ISOC_EVENT_ERROR            /**< Error occurred */
} hci_isoc_event_type_t;

/** CIS request event data */
typedef struct {
    uint16_t acl_handle;            /**< ACL connection handle */
    uint8_t cig_id;                 /**< CIG ID */
    uint8_t cis_id;                 /**< CIS ID */
    uint16_t cis_handle;            /**< CIS handle */
} cis_request_t;

/** ISOC event data */
typedef struct {
    hci_isoc_event_type_t type;
    union {
        cis_info_t cis_info;        /**< For CIS events */
        big_info_t big_info;        /**< For BIG events */
        cis_request_t cis_request;  /**< For CIS_REQUEST */
        iso_data_packet_t rx_data;  /**< For RX_DATA */
        uint16_t handle;            /**< For simple handle events */
        int error_code;             /**< For ERROR */
    } data;
} hci_isoc_event_t;

/** ISOC event callback */
typedef void (*hci_isoc_callback_t)(const hci_isoc_event_t *event, void *user_data);

/*******************************************************************************
 * Types - Statistics
 ******************************************************************************/

/** ISOC statistics */
typedef struct {
    uint32_t cis_established;       /**< CIS streams established */
    uint32_t cis_disconnected;      /**< CIS streams disconnected */
    uint32_t big_created;           /**< BIG groups created */
    uint32_t big_terminated;        /**< BIG groups terminated */
    uint32_t iso_tx_packets;        /**< ISO packets transmitted */
    uint32_t iso_rx_packets;        /**< ISO packets received */
    uint32_t iso_tx_bytes;          /**< ISO bytes transmitted */
    uint32_t iso_rx_bytes;          /**< ISO bytes received */
    uint32_t iso_tx_failed;         /**< ISO TX failures */
    uint32_t iso_rx_missed;         /**< ISO RX missed packets */
} hci_isoc_stats_t;

/*******************************************************************************
 * Default Configurations
 ******************************************************************************/

/** Default CIG configuration for LE Audio (48kHz, 10ms, mono) */
#define CIG_CONFIG_LE_AUDIO_DEFAULT {       \
    .cig_id = 0,                            \
    .sdu_interval_c_to_p = 10000,           \
    .sdu_interval_p_to_c = 10000,           \
    .sca = 0,                               \
    .packing = HCI_ISOC_PACKING_SEQUENTIAL, \
    .framing = HCI_ISOC_FRAMING_UNFRAMED,   \
    .max_transport_latency_c_to_p = 40,     \
    .max_transport_latency_p_to_c = 40,     \
    .num_cis = 1,                           \
    .cis = {{                               \
        .cis_id = 0,                        \
        .max_sdu_c_to_p = 100,              \
        .max_sdu_p_to_c = 100,              \
        .phy_c_to_p = HCI_ISOC_PHY_2M,      \
        .phy_p_to_c = HCI_ISOC_PHY_2M,      \
        .rtn_c_to_p = 2,                    \
        .rtn_p_to_c = 2                     \
    }}                                      \
}

/** Default BIG configuration for Auracast (48kHz, 10ms) */
#define BIG_CONFIG_AURACAST_DEFAULT {       \
    .big_handle = 0,                        \
    .adv_handle = 0,                        \
    .num_bis = 1,                           \
    .sdu_interval = 10000,                  \
    .max_sdu = 100,                         \
    .max_transport_latency = 40,            \
    .rtn = 2,                               \
    .phy = HCI_ISOC_PHY_2M,                 \
    .packing = HCI_ISOC_PACKING_SEQUENTIAL, \
    .framing = HCI_ISOC_FRAMING_UNFRAMED,   \
    .encryption = HCI_ISOC_ENCRYPT_DISABLED,\
    .broadcast_code = {0}                   \
}

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

/**
 * @brief Initialize HCI ISOC module
 *
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_init(void);

/**
 * @brief Deinitialize HCI ISOC module
 */
void hci_isoc_deinit(void);

/**
 * @brief Register event callback
 *
 * @param callback Function to call on events
 * @param user_data User data passed to callback
 */
void hci_isoc_register_callback(hci_isoc_callback_t callback, void *user_data);

/*******************************************************************************
 * API Functions - CIG Management (Unicast)
 ******************************************************************************/

/**
 * @brief Set CIG parameters
 *
 * Configures a Connected Isochronous Group (CIG) with specified parameters.
 * Call before creating CIS streams.
 *
 * @param config Pointer to CIG configuration
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_set_cig_params(const cig_config_t *config);

/**
 * @brief Create CIS connections
 *
 * Creates CIS streams to connected peers. CIG must be configured first.
 *
 * @param cig_id CIG identifier
 * @param num_cis Number of CIS to create
 * @param cis_handles Array of CIS handles (from set_cig_params)
 * @param acl_handles Array of ACL connection handles
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_create_cis(uint8_t cig_id, uint8_t num_cis,
                        const uint16_t *cis_handles,
                        const uint16_t *acl_handles);

/**
 * @brief Accept incoming CIS request
 *
 * Accepts a CIS request from a peer device.
 *
 * @param cis_handle CIS handle from CIS_REQUEST event
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_accept_cis(uint16_t cis_handle);

/**
 * @brief Reject incoming CIS request
 *
 * @param cis_handle CIS handle from CIS_REQUEST event
 * @param reason HCI reject reason
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_reject_cis(uint16_t cis_handle, uint8_t reason);

/**
 * @brief Disconnect CIS
 *
 * @param cis_handle CIS handle
 * @param reason HCI disconnect reason
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_disconnect_cis(uint16_t cis_handle, uint8_t reason);

/**
 * @brief Remove CIG
 *
 * Removes a CIG and all associated CIS. All CIS must be disconnected first.
 *
 * @param cig_id CIG identifier
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_remove_cig(uint8_t cig_id);

/**
 * @brief Get CIS info
 *
 * @param cis_handle CIS handle
 * @param info Pointer to structure to fill
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_get_cis_info(uint16_t cis_handle, cis_info_t *info);

/*******************************************************************************
 * API Functions - BIG Management (Broadcast)
 ******************************************************************************/

/**
 * @brief Create BIG (Broadcast Isochronous Group)
 *
 * Creates a BIG for Auracast/broadcast transmission.
 * Requires periodic advertising to be active.
 *
 * @param config Pointer to BIG configuration
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_create_big(const big_config_t *config);

/**
 * @brief Terminate BIG
 *
 * Stops broadcast transmission and removes BIG.
 *
 * @param big_handle BIG handle
 * @param reason Termination reason
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_terminate_big(uint8_t big_handle, uint8_t reason);

/**
 * @brief Sync to BIG (as broadcast sink)
 *
 * Synchronizes to a broadcast source.
 *
 * @param config Pointer to BIG sync configuration
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_big_create_sync(const big_sync_config_t *config);

/**
 * @brief Terminate BIG sync
 *
 * @param big_handle BIG handle
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_big_terminate_sync(uint8_t big_handle);

/**
 * @brief Get BIG info
 *
 * @param big_handle BIG handle
 * @param info Pointer to structure to fill
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_get_big_info(uint8_t big_handle, big_info_t *info);

/*******************************************************************************
 * API Functions - ISO Data Path
 ******************************************************************************/

/**
 * @brief Setup ISO data path
 *
 * Configures the data path for an ISO stream (CIS or BIS).
 * Use HCI_ISOC_DATA_PATH_HCI for host-side codec (LC3 on PSoC).
 *
 * @param handle CIS or BIS handle
 * @param direction Data path direction (INPUT or OUTPUT)
 * @param data_path_id Data path ID (0x00 = HCI)
 * @param codec_id Codec ID (5 bytes, or NULL for transparent)
 * @param controller_delay Controller delay (microseconds)
 * @param codec_config Codec configuration (or NULL)
 * @param codec_config_len Codec config length
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_setup_data_path(uint16_t handle, uint8_t direction,
                              uint8_t data_path_id, const uint8_t *codec_id,
                              uint32_t controller_delay,
                              const uint8_t *codec_config, uint8_t codec_config_len);

/**
 * @brief Remove ISO data path
 *
 * @param handle CIS or BIS handle
 * @param direction Data path direction
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_remove_data_path(uint16_t handle, uint8_t direction);

/*******************************************************************************
 * API Functions - ISO Data Transfer
 ******************************************************************************/

/**
 * @brief Send ISO data packet
 *
 * Sends an SDU over an established CIS or BIS.
 *
 * @param handle CIS or BIS handle
 * @param data SDU data
 * @param length SDU length
 * @param timestamp Timestamp (or 0 for no timestamp)
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_send_data(uint16_t handle, const uint8_t *data,
                       uint16_t length, uint32_t timestamp);

/**
 * @brief Send ISO data packet with sequence number
 *
 * @param handle CIS or BIS handle
 * @param data SDU data
 * @param length SDU length
 * @param timestamp Timestamp
 * @param seq_num Packet sequence number
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_send_data_ts(uint16_t handle, const uint8_t *data,
                          uint16_t length, uint32_t timestamp,
                          uint16_t seq_num);

/**
 * @brief Read ISO link quality
 *
 * @param handle CIS or BIS handle
 * @param tx_unacked_packets Output: unacknowledged packets
 * @param tx_flushed_packets Output: flushed packets
 * @param tx_last_subevent_packets Output: packets in last subevent
 * @param retransmitted_packets Output: retransmitted packets
 * @param crc_error_packets Output: CRC error packets
 * @param rx_unreceived_packets Output: unreceived packets
 * @param duplicate_packets Output: duplicate packets
 * @return HCI_ISOC_OK on success, negative error code on failure
 */
int hci_isoc_read_link_quality(uint16_t handle,
                                uint32_t *tx_unacked_packets,
                                uint32_t *tx_flushed_packets,
                                uint32_t *tx_last_subevent_packets,
                                uint32_t *retransmitted_packets,
                                uint32_t *crc_error_packets,
                                uint32_t *rx_unreceived_packets,
                                uint32_t *duplicate_packets);

/*******************************************************************************
 * API Functions - Utilities
 ******************************************************************************/

/**
 * @brief Check if handle is a CIS
 *
 * @param handle Connection handle
 * @return true if CIS handle
 */
bool hci_isoc_is_cis_handle(uint16_t handle);

/**
 * @brief Check if handle is a BIS
 *
 * @param handle Connection handle
 * @return true if BIS handle
 */
bool hci_isoc_is_bis_handle(uint16_t handle);

/**
 * @brief Get number of active CIS
 *
 * @return Number of established CIS
 */
uint8_t hci_isoc_get_active_cis_count(void);

/**
 * @brief Get number of active BIG
 *
 * @return Number of active BIG
 */
uint8_t hci_isoc_get_active_big_count(void);

/**
 * @brief Get ISOC statistics
 *
 * @param stats Pointer to statistics structure
 */
void hci_isoc_get_stats(hci_isoc_stats_t *stats);

/**
 * @brief Reset ISOC statistics
 */
void hci_isoc_reset_stats(void);

/*******************************************************************************
 * API Functions - HCI Event Processing
 ******************************************************************************/

/**
 * @brief Process LE Meta Event for ISOC
 *
 * This function should be called from the main BT event handler
 * when an LE Meta event related to ISOC is received.
 *
 * @param subevent LE Meta subevent code
 * @param data Event data
 * @param len Event data length
 */
void hci_isoc_process_le_meta_event(uint8_t subevent, const uint8_t *data, uint16_t len);

/**
 * @brief Process ISO Data received
 *
 * Called from HCI layer when ISO data packet is received.
 *
 * @param handle CIS or BIS handle
 * @param pb_flag Packet boundary flag
 * @param ts_flag Timestamp flag
 * @param timestamp Timestamp (if ts_flag set)
 * @param seq_num Packet sequence number
 * @param data SDU data
 * @param len SDU length
 */
void hci_isoc_process_rx_data(uint16_t handle, uint8_t pb_flag, uint8_t ts_flag,
                               uint32_t timestamp, uint16_t seq_num,
                               const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* HCI_ISOC_H */
