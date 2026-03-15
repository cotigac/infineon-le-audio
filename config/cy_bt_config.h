/**
 * @file cy_bt_config.h
 * @brief Bluetooth Configuration for CYW55511 + PSoC Edge E81
 *
 * This configuration file defines hardware-specific settings for the
 * CYW55511 Bluetooth 6.0 controller connected to PSoC Edge E81.
 *
 * Hardware Connection:
 * ┌─────────────────────────────────────────────────────────────────┐
 * │                      PSoC Edge E81                              │
 * │  ┌──────────────────────────────────────────────────────────┐  │
 * │  │  HCI UART (SCB)                                          │  │
 * │  │    TX ───────────────────────────────────────► RX        │  │
 * │  │    RX ◄─────────────────────────────────────── TX        │  │
 * │  │    RTS ──────────────────────────────────────► CTS       │  │
 * │  │    CTS ◄─────────────────────────────────────── RTS      │  │
 * │  └──────────────────────────────────────────────────────────┘  │
 * │                                                                 │
 * │  Control GPIOs:                                                 │
 * │    BT_REG_ON ────────────────────────────────────► REG_ON       │
 * │    BT_HOST_WAKE ◄──────────────────────────────── HOST_WAKE     │
 * │    BT_DEV_WAKE ──────────────────────────────────► DEV_WAKE     │
 * └─────────────────────────────────────────────────────────────────┘
 *                                │
 *                                │ HCI over UART (up to 4Mbps)
 *                                ▼
 * ┌─────────────────────────────────────────────────────────────────┐
 * │                        CYW55511                                  │
 * │               Bluetooth 6.0 LE Controller                        │
 * │                                                                  │
 * │  Features:                                                       │
 * │    - Bluetooth 6.0 with LE Audio                                │
 * │    - HCI Isochronous Channels (CIS/BIS)                         │
 * │    - Extended & Periodic Advertising                            │
 * │    - 2M PHY, Coded PHY                                          │
 * │    - Direction Finding (AoA/AoD)                                │
 * │    - Power Class 1.5 (+10 dBm TX)                               │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CY_BT_CONFIG_H
#define CY_BT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Controller Selection
 ******************************************************************************/

/**
 * @brief Bluetooth controller part number
 *
 * Select the CYW555xx variant being used:
 * - CYW55511: Bluetooth 6.0, LE Audio, 1x1 MIMO
 * - CYW55512: Bluetooth 6.0, LE Audio, 1x1 MIMO + enhanced features
 * - CYW55513: Bluetooth 6.0, LE Audio, 2x2 MIMO
 */
#define CY_BT_CONTROLLER_CYW55511           0
#define CY_BT_CONTROLLER_CYW55512           1
#define CY_BT_CONTROLLER_CYW55513           2

#ifndef CY_BT_CONTROLLER
#define CY_BT_CONTROLLER                    CY_BT_CONTROLLER_CYW55511
#endif

/*******************************************************************************
 * HCI UART Configuration
 ******************************************************************************/

/**
 * @brief HCI UART baud rate
 *
 * CYW55511 supports up to 4 Mbps HCI UART
 * Common values: 115200, 921600, 1000000, 2000000, 3000000, 4000000
 *
 * Recommended: 3000000 (3 Mbps) for LE Audio to handle ISOC data
 */
#ifndef CY_BT_HCI_UART_BAUD
#define CY_BT_HCI_UART_BAUD                 3000000
#endif

/**
 * @brief Initial baud rate for firmware download
 *
 * Controller starts at 115200, then switches to target baud
 */
#define CY_BT_HCI_UART_DOWNLOAD_BAUD        115200

/**
 * @brief Enable hardware flow control (RTS/CTS)
 *
 * REQUIRED for reliable operation at high baud rates
 */
#ifndef CY_BT_HCI_UART_FLOW_CONTROL
#define CY_BT_HCI_UART_FLOW_CONTROL         1
#endif

/**
 * @brief SCB block number for HCI UART
 *
 * PSoC Edge E81 SCB selection (0-7 depending on pin configuration)
 */
#ifndef CY_BT_HCI_UART_SCB
#define CY_BT_HCI_UART_SCB                  2
#endif

/**
 * @brief HCI UART data bits
 */
#define CY_BT_HCI_UART_DATA_BITS            8

/**
 * @brief HCI UART stop bits
 */
#define CY_BT_HCI_UART_STOP_BITS            1

/**
 * @brief HCI UART parity
 */
#define CY_BT_HCI_UART_PARITY_NONE          0

/*******************************************************************************
 * HCI UART Pin Configuration (PSoC Edge E81)
 *
 * These pins should match your hardware design.
 * Format: Port * 8 + Pin (e.g., P5_0 = 5*8+0 = 40)
 ******************************************************************************/

/**
 * @brief HCI UART TX pin (PSoC to CYW55511)
 */
#ifndef CY_BT_HCI_UART_TX_PIN
#define CY_BT_HCI_UART_TX_PORT              5
#define CY_BT_HCI_UART_TX_PIN               0
#endif

/**
 * @brief HCI UART RX pin (CYW55511 to PSoC)
 */
#ifndef CY_BT_HCI_UART_RX_PIN
#define CY_BT_HCI_UART_RX_PORT              5
#define CY_BT_HCI_UART_RX_PIN               1
#endif

/**
 * @brief HCI UART RTS pin (Request To Send, PSoC output)
 */
#ifndef CY_BT_HCI_UART_RTS_PIN
#define CY_BT_HCI_UART_RTS_PORT             5
#define CY_BT_HCI_UART_RTS_PIN              2
#endif

/**
 * @brief HCI UART CTS pin (Clear To Send, PSoC input)
 */
#ifndef CY_BT_HCI_UART_CTS_PIN
#define CY_BT_HCI_UART_CTS_PORT             5
#define CY_BT_HCI_UART_CTS_PIN              3
#endif

/*******************************************************************************
 * Bluetooth Control Pins
 ******************************************************************************/

/**
 * @brief BT_REG_ON pin - Power enable for CYW55511
 *
 * Active high: Pull high to enable Bluetooth controller
 */
#ifndef CY_BT_POWER_PIN
#define CY_BT_POWER_PORT                    6
#define CY_BT_POWER_PIN                     0
#endif

/**
 * @brief BT_HOST_WAKE pin - Wake signal from CYW55511 to PSoC
 *
 * Active low: CYW55511 pulls low to wake host
 */
#ifndef CY_BT_HOST_WAKE_PIN
#define CY_BT_HOST_WAKE_PORT                6
#define CY_BT_HOST_WAKE_PIN                 1
#endif

/**
 * @brief BT_DEV_WAKE pin - Wake signal from PSoC to CYW55511
 *
 * Active low: Host pulls low to wake controller
 */
#ifndef CY_BT_DEV_WAKE_PIN
#define CY_BT_DEV_WAKE_PORT                 6
#define CY_BT_DEV_WAKE_PIN                  2
#endif

/**
 * @brief Enable sleep mode (BT_HOST_WAKE/BT_DEV_WAKE handshaking)
 */
#ifndef CY_BT_SLEEP_MODE_ENABLED
#define CY_BT_SLEEP_MODE_ENABLED            0
#endif

/*******************************************************************************
 * Firmware Configuration
 ******************************************************************************/

/**
 * @brief Download firmware on initialization
 *
 * CYW55511 requires firmware download via HCI after power-up
 */
#ifndef CY_BT_DOWNLOAD_FIRMWARE
#define CY_BT_DOWNLOAD_FIRMWARE             1
#endif

/**
 * @brief Firmware binary included in flash
 *
 * If 1, firmware is linked as const array cy_bt_firmware[]
 * If 0, firmware is loaded from filesystem
 */
#ifndef CY_BT_FIRMWARE_IN_FLASH
#define CY_BT_FIRMWARE_IN_FLASH             1
#endif

/**
 * @brief Firmware file path (if loading from filesystem)
 */
#ifndef CY_BT_FIRMWARE_PATH
#define CY_BT_FIRMWARE_PATH                 "/firmware/cyw55511_bt.hcd"
#endif

/**
 * @brief Firmware download timeout (ms)
 */
#define CY_BT_FIRMWARE_DOWNLOAD_TIMEOUT     5000

/**
 * @brief Post-download stabilization delay (ms)
 */
#define CY_BT_FIRMWARE_STABILIZE_DELAY      100

/*******************************************************************************
 * Device Identity
 ******************************************************************************/

/**
 * @brief Default Bluetooth device name
 */
#ifndef CY_BT_DEVICE_NAME
#define CY_BT_DEVICE_NAME                   "Infineon LE Audio Instrument"
#endif

/**
 * @brief Maximum device name length
 */
#define CY_BT_MAX_DEVICE_NAME_LEN           32

/**
 * @brief Use random static address
 *
 * If 1, generate random static address on first boot
 * If 0, use address from controller or CY_BT_DEVICE_ADDRESS
 */
#ifndef CY_BT_USE_RANDOM_ADDRESS
#define CY_BT_USE_RANDOM_ADDRESS            1
#endif

/**
 * @brief Fixed device address (if not using random)
 *
 * Set to non-zero to use a specific address
 * Format: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
 */
#ifndef CY_BT_DEVICE_ADDRESS
#define CY_BT_DEVICE_ADDRESS                {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#endif

/*******************************************************************************
 * LE Audio Configuration
 ******************************************************************************/

/**
 * @brief Enable LE Audio features
 */
#ifndef CY_BT_LE_AUDIO_ENABLED
#define CY_BT_LE_AUDIO_ENABLED              1
#endif

/**
 * @brief Enable Connected Isochronous Streams (CIS) - Unicast
 */
#ifndef CY_BT_LE_AUDIO_CIS_ENABLED
#define CY_BT_LE_AUDIO_CIS_ENABLED          1
#endif

/**
 * @brief Enable Broadcast Isochronous Streams (BIS) - Auracast
 */
#ifndef CY_BT_LE_AUDIO_BIS_ENABLED
#define CY_BT_LE_AUDIO_BIS_ENABLED          1
#endif

/**
 * @brief Maximum CIG (Connected Isochronous Groups)
 */
#ifndef CY_BT_MAX_CIG
#define CY_BT_MAX_CIG                       2
#endif

/**
 * @brief Maximum CIS per CIG
 */
#ifndef CY_BT_MAX_CIS_PER_CIG
#define CY_BT_MAX_CIS_PER_CIG               2
#endif

/**
 * @brief Maximum BIG (Broadcast Isochronous Groups)
 */
#ifndef CY_BT_MAX_BIG
#define CY_BT_MAX_BIG                       1
#endif

/**
 * @brief Maximum BIS per BIG
 */
#ifndef CY_BT_MAX_BIS_PER_BIG
#define CY_BT_MAX_BIS_PER_BIG               2
#endif

/**
 * @brief Maximum ISOC SDU size (bytes)
 *
 * 155 bytes covers max LC3 frame at 48kHz/10ms
 */
#ifndef CY_BT_ISOC_MAX_SDU_SIZE
#define CY_BT_ISOC_MAX_SDU_SIZE             155
#endif

/**
 * @brief ISOC SDU interval for 10ms frames (microseconds)
 */
#define CY_BT_ISOC_SDU_INTERVAL_10MS        10000

/**
 * @brief ISOC SDU interval for 7.5ms frames (microseconds)
 */
#define CY_BT_ISOC_SDU_INTERVAL_7_5MS       7500

/**
 * @brief Default presentation delay (microseconds)
 *
 * 40ms default for good sync margin
 */
#ifndef CY_BT_DEFAULT_PRESENTATION_DELAY
#define CY_BT_DEFAULT_PRESENTATION_DELAY    40000
#endif

/**
 * @brief Minimum presentation delay (microseconds)
 */
#define CY_BT_MIN_PRESENTATION_DELAY        20000

/**
 * @brief Maximum presentation delay (microseconds)
 */
#define CY_BT_MAX_PRESENTATION_DELAY        100000

/*******************************************************************************
 * PHY Configuration
 ******************************************************************************/

/**
 * @brief Enable 2M PHY
 */
#ifndef CY_BT_PHY_2M_ENABLED
#define CY_BT_PHY_2M_ENABLED                1
#endif

/**
 * @brief Enable Coded PHY (long range)
 */
#ifndef CY_BT_PHY_CODED_ENABLED
#define CY_BT_PHY_CODED_ENABLED             0
#endif

/**
 * @brief Preferred PHY for audio streaming
 *
 * 1 = 1M PHY, 2 = 2M PHY, 3 = Coded S8, 4 = Coded S2
 */
#ifndef CY_BT_PREFERRED_PHY
#define CY_BT_PREFERRED_PHY                 2   /* 2M PHY for lower latency */
#endif

/*******************************************************************************
 * Advertising Configuration
 ******************************************************************************/

/**
 * @brief Enable Extended Advertising
 */
#ifndef CY_BT_EXT_ADV_ENABLED
#define CY_BT_EXT_ADV_ENABLED               1
#endif

/**
 * @brief Enable Periodic Advertising (for Auracast)
 */
#ifndef CY_BT_PERIODIC_ADV_ENABLED
#define CY_BT_PERIODIC_ADV_ENABLED          1
#endif

/**
 * @brief Default advertising interval (ms)
 */
#ifndef CY_BT_ADV_INTERVAL_MS
#define CY_BT_ADV_INTERVAL_MS               100
#endif

/**
 * @brief Fast advertising interval for quick connection (ms)
 */
#define CY_BT_ADV_INTERVAL_FAST_MS          20

/**
 * @brief Slow advertising interval for power saving (ms)
 */
#define CY_BT_ADV_INTERVAL_SLOW_MS          1000

/**
 * @brief Advertising timeout (seconds, 0 = no timeout)
 */
#ifndef CY_BT_ADV_TIMEOUT_SEC
#define CY_BT_ADV_TIMEOUT_SEC               0
#endif

/**
 * @brief Maximum advertising data length
 *
 * Extended advertising supports up to 1650 bytes
 */
#define CY_BT_MAX_ADV_DATA_LEN              254
#define CY_BT_MAX_EXT_ADV_DATA_LEN          1650

/**
 * @brief Periodic advertising interval (1.25ms units)
 *
 * For Auracast BASE transmission
 */
#ifndef CY_BT_PERIODIC_ADV_INTERVAL
#define CY_BT_PERIODIC_ADV_INTERVAL         80  /* 100ms */
#endif

/*******************************************************************************
 * Connection Configuration
 ******************************************************************************/

/**
 * @brief Maximum simultaneous connections
 */
#ifndef CY_BT_MAX_CONNECTIONS
#define CY_BT_MAX_CONNECTIONS               2
#endif

/**
 * @brief Default connection interval (1.25ms units)
 *
 * 12 = 15ms, good for LE Audio
 */
#ifndef CY_BT_CONN_INTERVAL_DEFAULT
#define CY_BT_CONN_INTERVAL_DEFAULT         12
#endif

/**
 * @brief Minimum connection interval (1.25ms units)
 */
#define CY_BT_CONN_INTERVAL_MIN             6   /* 7.5ms */

/**
 * @brief Maximum connection interval (1.25ms units)
 */
#define CY_BT_CONN_INTERVAL_MAX             80  /* 100ms */

/**
 * @brief Default peripheral latency (connection events to skip)
 */
#ifndef CY_BT_CONN_LATENCY_DEFAULT
#define CY_BT_CONN_LATENCY_DEFAULT          0   /* No skipping for audio */
#endif

/**
 * @brief Supervision timeout (10ms units)
 *
 * 500 = 5 seconds
 */
#ifndef CY_BT_CONN_SUPERVISION_TIMEOUT
#define CY_BT_CONN_SUPERVISION_TIMEOUT      500
#endif

/**
 * @brief ATT MTU size
 *
 * 247 bytes for efficient GATT operations
 */
#ifndef CY_BT_ATT_MTU
#define CY_BT_ATT_MTU                       247
#endif

/**
 * @brief Maximum ATT MTU size
 */
#define CY_BT_ATT_MTU_MAX                   517

/*******************************************************************************
 * Security Configuration
 ******************************************************************************/

/**
 * @brief Enable bonding (store pairing info)
 */
#ifndef CY_BT_BONDING_ENABLED
#define CY_BT_BONDING_ENABLED               1
#endif

/**
 * @brief Maximum bonded devices to store
 */
#ifndef CY_BT_MAX_BONDED_DEVICES
#define CY_BT_MAX_BONDED_DEVICES            8
#endif

/**
 * @brief Security level required
 *
 * 0 = No security
 * 1 = Unauthenticated encryption (Just Works)
 * 2 = Authenticated encryption (PIN/passkey)
 * 3 = Authenticated LE Secure Connections
 */
#ifndef CY_BT_SECURITY_LEVEL
#define CY_BT_SECURITY_LEVEL                1
#endif

/**
 * @brief Enable Secure Connections (LE SC)
 */
#ifndef CY_BT_SECURE_CONNECTIONS_ENABLED
#define CY_BT_SECURE_CONNECTIONS_ENABLED    1
#endif

/**
 * @brief IO capability for pairing
 *
 * 0 = Display Only
 * 1 = Display + Yes/No
 * 2 = Keyboard Only
 * 3 = No Input No Output
 * 4 = Keyboard + Display
 */
#ifndef CY_BT_IO_CAPABILITY
#define CY_BT_IO_CAPABILITY                 3   /* No Input No Output */
#endif

/**
 * @brief MITM (Man-in-the-Middle) protection required
 */
#ifndef CY_BT_MITM_REQUIRED
#define CY_BT_MITM_REQUIRED                 0
#endif

/*******************************************************************************
 * Power Configuration
 ******************************************************************************/

/**
 * @brief Default TX power (dBm)
 *
 * CYW55511 range: -20 to +10 dBm
 */
#ifndef CY_BT_TX_POWER_DEFAULT
#define CY_BT_TX_POWER_DEFAULT              0
#endif

/**
 * @brief Maximum TX power (dBm)
 */
#define CY_BT_TX_POWER_MAX                  10

/**
 * @brief Minimum TX power (dBm)
 */
#define CY_BT_TX_POWER_MIN                  -20

/**
 * @brief Enable low power mode
 */
#ifndef CY_BT_LOW_POWER_ENABLED
#define CY_BT_LOW_POWER_ENABLED             0
#endif

/**
 * @brief Idle timeout before entering low power (ms)
 */
#define CY_BT_IDLE_TIMEOUT_MS               5000

/*******************************************************************************
 * Buffer Configuration
 ******************************************************************************/

/**
 * @brief Number of HCI ACL TX buffers
 */
#ifndef CY_BT_HCI_ACL_TX_BUFFERS
#define CY_BT_HCI_ACL_TX_BUFFERS            8
#endif

/**
 * @brief Number of HCI ACL RX buffers
 */
#ifndef CY_BT_HCI_ACL_RX_BUFFERS
#define CY_BT_HCI_ACL_RX_BUFFERS            8
#endif

/**
 * @brief HCI ACL buffer size
 */
#ifndef CY_BT_HCI_ACL_BUFFER_SIZE
#define CY_BT_HCI_ACL_BUFFER_SIZE           256
#endif

/**
 * @brief Number of HCI ISOC TX buffers
 */
#ifndef CY_BT_HCI_ISOC_TX_BUFFERS
#define CY_BT_HCI_ISOC_TX_BUFFERS           8
#endif

/**
 * @brief Number of HCI ISOC RX buffers
 */
#ifndef CY_BT_HCI_ISOC_RX_BUFFERS
#define CY_BT_HCI_ISOC_RX_BUFFERS           8
#endif

/**
 * @brief HCI ISOC buffer size
 */
#ifndef CY_BT_HCI_ISOC_BUFFER_SIZE
#define CY_BT_HCI_ISOC_BUFFER_SIZE          256
#endif

/*******************************************************************************
 * GATT Database Configuration
 ******************************************************************************/

/**
 * @brief Maximum GATT services
 */
#ifndef CY_BT_GATT_MAX_SERVICES
#define CY_BT_GATT_MAX_SERVICES             10
#endif

/**
 * @brief Maximum GATT characteristics
 */
#ifndef CY_BT_GATT_MAX_CHARACTERISTICS
#define CY_BT_GATT_MAX_CHARACTERISTICS      32
#endif

/**
 * @brief Maximum GATT descriptors
 */
#ifndef CY_BT_GATT_MAX_DESCRIPTORS
#define CY_BT_GATT_MAX_DESCRIPTORS          32
#endif

/**
 * @brief Maximum CCCD (Client Characteristic Configuration Descriptor) entries
 */
#ifndef CY_BT_GATT_MAX_CCCD
#define CY_BT_GATT_MAX_CCCD                 16
#endif

/*******************************************************************************
 * Debug Configuration
 ******************************************************************************/

/**
 * @brief Enable HCI trace logging
 */
#ifndef CY_BT_HCI_TRACE_ENABLED
#define CY_BT_HCI_TRACE_ENABLED             0
#endif

/**
 * @brief Enable verbose Bluetooth logging
 */
#ifndef CY_BT_DEBUG_ENABLED
#define CY_BT_DEBUG_ENABLED                 0
#endif

/**
 * @brief HCI trace output method
 *
 * 0 = UART (separate debug UART)
 * 1 = RTT (Segger RTT)
 * 2 = Memory buffer
 */
#ifndef CY_BT_HCI_TRACE_OUTPUT
#define CY_BT_HCI_TRACE_OUTPUT              1   /* RTT */
#endif

/*******************************************************************************
 * LE Audio Service UUIDs (Bluetooth SIG assigned)
 ******************************************************************************/

/** Published Audio Capabilities Service */
#define CY_BT_UUID_PACS                     0x1850

/** Audio Stream Control Service */
#define CY_BT_UUID_ASCS                     0x184E

/** Broadcast Audio Scan Service */
#define CY_BT_UUID_BASS                     0x184F

/** Basic Audio Profile */
#define CY_BT_UUID_BAP                      0x1851

/** Common Audio Profile */
#define CY_BT_UUID_CAP                      0x1853

/** Hearing Access Service */
#define CY_BT_UUID_HAS                      0x1854

/** Telephony and Media Audio Profile */
#define CY_BT_UUID_TMAP                     0x1855

/** Volume Control Service */
#define CY_BT_UUID_VCS                      0x1844

/** Microphone Control Service */
#define CY_BT_UUID_MICS                     0x184D

/** Coordinated Set Identification Service */
#define CY_BT_UUID_CSIS                     0x1846

/*******************************************************************************
 * MIDI over BLE UUIDs
 ******************************************************************************/

/** MIDI Service UUID (Apple-defined, widely adopted) */
#define CY_BT_UUID_MIDI_SERVICE             {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, \
                                             0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, \
                                             0x5A, 0x0E, 0xB8, 0x03}

/** MIDI I/O Characteristic UUID */
#define CY_BT_UUID_MIDI_IO_CHAR             {0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, \
                                             0x11, 0x41, 0x68, 0x38, 0x68, 0xDB, \
                                             0x72, 0xE5, 0x77, 0x77}

/*******************************************************************************
 * Timing Constraints
 ******************************************************************************/

/**
 * @brief HCI command timeout (ms)
 */
#define CY_BT_HCI_CMD_TIMEOUT               2000

/**
 * @brief Connection timeout (ms)
 */
#define CY_BT_CONNECTION_TIMEOUT            30000

/**
 * @brief Pairing timeout (ms)
 */
#define CY_BT_PAIRING_TIMEOUT               30000

/**
 * @brief ISOC setup timeout (ms)
 */
#define CY_BT_ISOC_SETUP_TIMEOUT            5000

/*******************************************************************************
 * Compile-Time Validation
 ******************************************************************************/

/* Validate controller selection */
#if CY_BT_CONTROLLER > CY_BT_CONTROLLER_CYW55513
#error "Invalid CY_BT_CONTROLLER selection"
#endif

/* Validate HCI baud rate */
#if CY_BT_HCI_UART_BAUD > 4000000
#error "CY_BT_HCI_UART_BAUD exceeds maximum 4 Mbps"
#endif

/* Validate LE Audio settings */
#if CY_BT_LE_AUDIO_ENABLED
#if !CY_BT_EXT_ADV_ENABLED
#error "Extended Advertising required for LE Audio"
#endif
#if CY_BT_LE_AUDIO_BIS_ENABLED && !CY_BT_PERIODIC_ADV_ENABLED
#error "Periodic Advertising required for BIS (Auracast)"
#endif
#endif

/* Validate ISOC settings */
#if CY_BT_MAX_CIG < 1 || CY_BT_MAX_CIG > 4
#error "CY_BT_MAX_CIG must be 1-4"
#endif

#if CY_BT_MAX_BIG < 1 || CY_BT_MAX_BIG > 4
#error "CY_BT_MAX_BIG must be 1-4"
#endif

#ifdef __cplusplus
}
#endif

#endif /* CY_BT_CONFIG_H */
