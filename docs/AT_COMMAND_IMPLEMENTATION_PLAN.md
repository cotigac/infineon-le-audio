# USB CDC/ACM AT Command Interface Implementation Plan

## Executive Summary

This document describes the implementation of a USB CDC/ACM virtual serial port for the infineon-le-audio project. The interface exposes AT-style commands enabling an external host processor to configure and control Bluetooth and Wi-Fi functionality on the CYW55512 combo IC via the PSoC Edge E82/E84 MCU.

---

## 1. Architecture Overview

### 1.1 System Block Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Main Application Processor                           │
│                              (Host Controller)                               │
└─────────────────────────────────┬───────────────────────────────────────────┘
                                  │ USB High-Speed (480 Mbps)
                                  │
┌─────────────────────────────────▼───────────────────────────────────────────┐
│                         USB Composite Device                                 │
│                           (emUSB-Device)                                     │
├─────────────────┬─────────────────────────────┬─────────────────────────────┤
│ Interface 0-1:  │    Interface 2-3: CDC/ACM   │    Interface 4: Wi-Fi       │
│ MIDI (Audio)    │    (Comm Class 0x02)        │    Bridge (Vendor 0xFF)     │
│ EP 0x81, 0x01   │    EP 0x82, 0x02, 0x83      │    EP 0x84, 0x04            │
└─────────────────┴─────────────────────────────┴─────────────────────────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │                           │
           ┌────────▼────────┐        ┌─────────▼─────────┐
           │  midi_usb.c     │        │  cdc_acm.c        │
           │  (existing)     │        │  (new)            │
           └─────────────────┘        └─────────┬─────────┘
                                                │
                                      ┌─────────▼─────────┐
                                      │   at_parser.c     │
                                      │   AT Command      │
                                      │   Parser          │
                                      └─────────┬─────────┘
                                                │
                    ┌───────────────────────────┼───────────────────────────┐
                    │                           │                           │
           ┌────────▼────────┐        ┌─────────▼─────────┐       ┌─────────▼─────────┐
           │ at_bt_cmds.c    │        │ at_wifi_cmds.c    │       │ at_system_cmds.c  │
           │ Bluetooth       │        │ Wi-Fi             │       │ System            │
           │ Commands        │        │ Commands          │       │ Commands          │
           └────────┬────────┘        └─────────┬─────────┘       └───────────────────┘
                    │                           │
           ┌────────▼────────┐        ┌─────────▼─────────┐
           │ BTSTACK APIs    │        │ WHD APIs          │
           │ bt_init.h       │        │ whd_wifi_api.h    │
           │ gap_config.h    │        │                   │
           │ le_audio_mgr.h  │        │                   │
           └────────┬────────┘        └─────────┬─────────┘
                    │                           │
                    │ HCI UART (3 Mbps)         │ SDIO 3.0 (208 MHz)
                    │                           │
           ┌────────▼───────────────────────────▼─────────┐
           │              CYW55512 Combo IC               │
           │  ┌─────────────────┐  ┌────────────────────┐ │
           │  │  Bluetooth 6.0  │  │  Wi-Fi 6 (802.11ax)│ │
           │  │  LE Audio       │  │                    │ │
           │  └─────────────────┘  └────────────────────┘ │
           └──────────────────────────────────────────────┘
```

### 1.2 Data Flow

```
Host TX: Host App → USB CDC OUT → cdc_acm_read() → at_parser → cmd_handler → BT/WiFi API
Host RX: BT/WiFi callback → URC queue → cdc_acm_write() → USB CDC IN → Host App
```

---

## 2. File Structure

### 2.1 New Files to Create

```
source/
└── cdc/
    ├── cdc_acm.h           # CDC/ACM public API
    ├── cdc_acm.c           # CDC implementation (~400 lines)
    ├── at_parser.h         # AT parser public API
    ├── at_parser.c         # Parser implementation (~300 lines)
    ├── at_commands.h       # Command definitions and error codes
    ├── at_bt_cmds.h        # Bluetooth command declarations
    ├── at_bt_cmds.c        # Bluetooth handlers (~500 lines)
    ├── at_wifi_cmds.h      # Wi-Fi command declarations
    ├── at_wifi_cmds.c      # Wi-Fi handlers (~400 lines)
    ├── at_leaudio_cmds.h   # LE Audio command declarations
    ├── at_leaudio_cmds.c   # LE Audio handlers (~300 lines)
    ├── at_system_cmds.h    # System command declarations
    └── at_system_cmds.c    # System handlers (~150 lines)
```

### 2.2 Files to Modify

| File | Modification |
|------|--------------|
| `source/midi/midi_usb.c` | Refactor `usb_device_init()` for composite device |
| `source/main_non_mtb.c` | Add `cdc_acm_init()` and CDC task creation |
| `mtb/le-audio/proj_cm33_ns/source/main.c` | Same for MTB build |
| `CMakeLists.txt` | Add `ENABLE_CDC` option and source files |
| `mtb/le-audio/proj_cm33_ns/Makefile` | Add CDC sources for MTB build |

---

## 3. Module Specifications

### 3.1 CDC/ACM Module (`cdc_acm.h/c`)

#### Public API

```c
/**
 * @brief CDC/ACM configuration
 */
typedef struct {
    uint16_t rx_buffer_size;    // Default: 256
    uint16_t tx_buffer_size;    // Default: 512
    uint8_t  rx_queue_depth;    // Default: 4
    uint8_t  tx_queue_depth;    // Default: 8
} cdc_acm_config_t;

/**
 * @brief Initialize CDC/ACM interface
 * @param config Configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int cdc_acm_init(const cdc_acm_config_t *config);

/**
 * @brief Deinitialize CDC/ACM interface
 */
void cdc_acm_deinit(void);

/**
 * @brief Process CDC events (call from task loop)
 */
void cdc_acm_process(void);

/**
 * @brief Check if host is connected
 * @return true if CDC port is open
 */
bool cdc_acm_is_connected(void);

/**
 * @brief Read data from host
 * @param buffer Buffer to store data
 * @param max_len Maximum bytes to read
 * @return Number of bytes read, 0 if none available
 */
int cdc_acm_read(char *buffer, size_t max_len);

/**
 * @brief Write data to host
 * @param data Data to send
 * @param len Number of bytes
 * @return Number of bytes written
 */
int cdc_acm_write(const char *data, size_t len);

/**
 * @brief Printf-style output to host
 */
int cdc_acm_printf(const char *fmt, ...);

/**
 * @brief Send standard AT responses
 */
void cdc_acm_send_ok(void);
void cdc_acm_send_error(int code);
void cdc_acm_send_response(const char *prefix, const char *data);

/**
 * @brief Send unsolicited result code (async event)
 * @param event Event name (without +)
 * @param data Event data (can be NULL)
 */
void cdc_acm_send_urc(const char *event, const char *data);
```

#### Implementation Notes

- Uses `USBD_CDC_Add()`, `USBD_CDC_Read()`, `USBD_CDC_Write()` from emUSB-Device
- TX protected by FreeRTOS mutex for thread safety
- RX/TX use FreeRTOS queues for buffering
- Line coding callback for baud rate changes (informational only)

### 3.2 AT Parser Module (`at_parser.h/c`)

#### Command Handler Types

```c
/**
 * @brief Command handler function signature
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return 0 on success, error code on failure
 */
typedef int (*at_cmd_handler_t)(int argc, const char *argv[]);

/**
 * @brief Command table entry
 */
typedef struct {
    const char         *name;       // Command name (without AT+)
    at_cmd_handler_t    exec;       // AT+CMD or AT+CMD=params
    at_cmd_handler_t    query;      // AT+CMD?
    at_cmd_handler_t    test;       // AT+CMD=?
    const char         *help;       // Help text
    uint8_t             min_args;   // Minimum arguments
    uint8_t             max_args;   // Maximum arguments
} at_cmd_entry_t;
```

#### Public API

```c
/**
 * @brief Initialize AT parser
 * @return 0 on success
 */
int at_parser_init(void);

/**
 * @brief Register a command table
 * @param table Array of command entries
 * @param count Number of entries
 * @return 0 on success
 */
int at_parser_register_commands(const at_cmd_entry_t *table, size_t count);

/**
 * @brief Process a single character (for streaming input)
 * @param c Character received
 */
void at_parser_process_char(char c);

/**
 * @brief Process a complete line
 * @param line Null-terminated line (without CR/LF)
 */
void at_parser_process_line(const char *line);

/**
 * @brief Enable/disable command echo
 * @param enabled true to echo characters back
 */
void at_parser_set_echo(bool enabled);
```

#### Parsing Rules

1. Commands start with `AT` (case insensitive)
2. `AT` alone returns OK
3. `AT+CMD` executes command with no arguments
4. `AT+CMD=arg1,arg2` executes with arguments
5. `AT+CMD?` queries current value
6. `AT+CMD=?` queries supported values
7. String arguments can be quoted: `"my string"`
8. Hex data prefixed with `0x`: `0xAABBCC`
9. Line terminated by CR, LF, or CR+LF

---

## 4. AT Command Reference

### 4.1 Response Format

```
Successful response (no data):
<CR><LF>OK<CR><LF>

Successful response (with data):
<CR><LF>+RESPONSE: data<CR><LF>
<CR><LF>OK<CR><LF>

Error response:
<CR><LF>ERROR<CR><LF>

Error response (with code):
<CR><LF>+CME ERROR: <code><CR><LF>

Unsolicited Result Code (async event):
<CR><LF>+EVENT: data<CR><LF>
```

### 4.2 Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | SUCCESS | Operation successful |
| 1 | FAILURE | Generic failure |
| 2 | NOT_ALLOWED | Operation not allowed in current state |
| 3 | NOT_SUPPORTED | Command not supported |
| 4 | INVALID_PARAM | Invalid parameter |
| 5 | NOT_FOUND | Resource not found |
| 6 | NO_MEMORY | Out of memory |
| 7 | BUSY | Resource busy |
| 8 | TIMEOUT | Operation timeout |
| 10 | BT_NOT_INIT | Bluetooth not initialized |
| 11 | BT_ALREADY_INIT | Bluetooth already initialized |
| 12 | BT_HCI_ERROR | HCI transport error |
| 20 | WIFI_NOT_INIT | Wi-Fi not initialized |
| 21 | WIFI_NOT_CONNECTED | Wi-Fi not connected |
| 22 | WIFI_AUTH_FAILED | Authentication failed |
| 30 | LEA_NOT_INIT | LE Audio not initialized |
| 31 | LEA_CODEC_ERROR | Codec error |

### 4.3 System Commands

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT` | Test communication | None | OK |
| `ATI` | Device information | None | +INFO: <model>,<fw_ver> |
| `AT+VERSION?` | Query firmware version | None | +VERSION: <version> |
| `AT+RST` | Soft reset | None | OK (then resets) |
| `AT+ECHO=<n>` | Enable/disable echo | 0=off, 1=on | OK |
| `AT+ECHO?` | Query echo state | None | +ECHO: 0\|1 |
| `AT+SAVE` | Save config to flash | None | OK |
| `AT+RESTORE` | Restore factory defaults | None | OK |
| `AT+HELP` | List commands | None | Command list |

### 4.4 Bluetooth Commands

#### Initialization & Status

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+BTINIT` | Initialize BT stack | None | OK |
| `AT+BTDEINIT` | Deinitialize BT stack | None | OK |
| `AT+BTSTATE?` | Query BT state | None | +BTSTATE: OFF\|INIT\|READY\|ADV\|CONN\|STREAM |
| `AT+BTINFO?` | Query controller info | None | +BTINFO: <ver>,<manufacturer>,<features> |
| `AT+BTADDR?` | Query BT address | None | +BTADDR: AA:BB:CC:DD:EE:FF |
| `AT+BTADDR=<addr>` | Set BT address | MAC address | OK |
| `AT+BTNAME=<name>` | Set device name | String (max 32 chars) | OK |
| `AT+BTNAME?` | Query device name | None | +BTNAME: <name> |
| `AT+BTPWR=<dbm>` | Set TX power | Power in dBm | OK |
| `AT+BTPWR?` | Query TX power | None | +BTPWR: <dbm> |

#### GAP Advertising

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+GAPADVSTART` | Start advertising | [<connectable>,<interval_ms>] | OK |
| `AT+GAPADVSTOP` | Stop advertising | None | OK |
| `AT+GAPADVDATA=<hex>` | Set advertising data | Hex bytes (max 31) | OK |
| `AT+GAPADVDATA?` | Query advertising data | None | +GAPADVDATA: <hex> |
| `AT+GAPSCANRSP=<hex>` | Set scan response | Hex bytes (max 31) | OK |

#### GAP Scanning

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+GAPSCAN=<n>` | Start/stop scanning | 0=stop, 1=start | OK |
| `AT+GAPSCAN=1,<ms>` | Scan with timeout | Timeout in ms | OK |
| `AT+GAPSCANPARAM=<int>,<win>` | Set scan params | Interval, window (ms) | OK |

**Unsolicited Result Codes during scan:**
```
+SCANRESULT: "<name>",AA:BB:CC:DD:EE:FF,<rssi>,<adv_type>
+SCANDONE
```

#### GAP Connection

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+GAPCONN=<addr>` | Connect to device | MAC address | OK |
| `AT+GAPCONN=<addr>,<type>` | Connect with addr type | MAC, 0=public/1=random | OK |
| `AT+GAPDISCONN` | Disconnect current | None | OK |
| `AT+GAPDISCONN=<handle>` | Disconnect specific | Connection handle | OK |
| `AT+GAPCONNLIST?` | List connections | None | +GAPCONN: <handle>,<addr>,<role> |
| `AT+GAPCONNPARAM=<h>,<int>,<lat>,<to>` | Update params | handle, interval, latency, timeout | OK |

**Unsolicited Result Codes:**
```
+GAPCONNECTED: <handle>,AA:BB:CC:DD:EE:FF
+GAPDISCONNECTED: <handle>,<reason>
```

#### Pairing & Security

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+BTPAIR` | Enter pairing mode | None | OK |
| `AT+BTPAIR=<addr>` | Initiate pairing | MAC address | OK |
| `AT+BTBOND?` | List bonded devices | None | +BTBOND: <addr>,<name> (multiple) |
| `AT+BTBONDCLEAR` | Clear all bonds | None | OK |
| `AT+BTBONDCLEAR=<addr>` | Clear specific bond | MAC address | OK |

### 4.5 LE Audio Commands

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+LEAINIT` | Initialize LE Audio | [<sample_rate>,<frame_dur>] | OK |
| `AT+LEADEINIT` | Deinitialize | None | OK |
| `AT+LEASTATE?` | Query state | None | +LEASTATE: IDLE\|CONFIG\|STREAM |
| `AT+LEAMODE?` | Query mode | None | +LEAMODE: IDLE\|UNICAST\|BROADCAST |
| `AT+LEABROADCAST=1` | Start broadcast | [,"<name>",<context>] | OK |
| `AT+LEABROADCAST=0` | Stop broadcast | None | OK |
| `AT+LEAUNICAST=1,<h>` | Start unicast | Connection handle | OK |
| `AT+LEAUNICAST=0` | Stop unicast | None | OK |
| `AT+LEACODEC?` | Query codec config | None | +LEACODEC: <rate>,<frame>,<octets> |
| `AT+LEACODEC=<rate>,<dur>` | Set codec config | Sample rate, frame duration | OK |

**Unsolicited Result Codes:**
```
+LEASTREAM: STARTED,<type>
+LEASTREAM: STOPPED,<reason>
```

### 4.6 Wi-Fi Commands

#### Initialization & Status

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+WIFIINIT` | Initialize Wi-Fi | None | OK |
| `AT+WIFIDEINIT` | Deinitialize | None | OK |
| `AT+WIFISTATE?` | Query state | None | +WIFISTATE: OFF\|READY\|SCANNING\|CONNECTED\|AP |
| `AT+WIFIMAC?` | Query MAC address | None | +WIFIMAC: AA:BB:CC:DD:EE:FF |
| `AT+WIFIVER?` | Query firmware version | None | +WIFIVER: <version> |

#### Wi-Fi Security Types

The CYW55512 supports comprehensive Wi-Fi security options via WHD (Wi-Fi Host Driver):

##### Personal (PSK) Security

| Security ID | AT Parameter | Description |
|-------------|--------------|-------------|
| 0 | `OPEN` | Open network (no security) |
| 1 | `WEP_PSK` | WEP with open authentication (legacy, insecure) |
| 2 | `WEP_SHARED` | WEP with shared authentication (legacy, insecure) |
| 3 | `WPA_TKIP` | WPA with TKIP encryption |
| 4 | `WPA_AES` | WPA with AES encryption |
| 5 | `WPA_MIXED` | WPA with AES + TKIP |
| 6 | `WPA2_AES` | WPA2 with AES (most common) |
| 7 | `WPA2_TKIP` | WPA2 with TKIP |
| 8 | `WPA2_MIXED` | WPA2 with AES + TKIP |
| 9 | `WPA2_SHA256` | WPA2 with AES + SHA256 |
| 10 | `WPA2_FBT` | WPA2 with Fast BSS Transition |
| 11 | `WPA3_SAE` | WPA3 with SAE (Simultaneous Authentication of Equals) |
| 12 | `WPA3_FBT` | WPA3 with Fast BSS Transition |
| 13 | `WPA3_WPA2` | WPA3/WPA2 transition mode |
| 14 | `WPA3_OWE` | WPA3 Enhanced Open (Opportunistic Wireless Encryption) |
| 15 | `WPA2_WPA` | WPA2/WPA mixed mode |

##### Enterprise (802.1X) Security

| Security ID | AT Parameter | Description |
|-------------|--------------|-------------|
| 20 | `WPA_ENT_TKIP` | WPA Enterprise with TKIP |
| 21 | `WPA_ENT_AES` | WPA Enterprise with AES |
| 22 | `WPA_ENT_MIXED` | WPA Enterprise with AES + TKIP |
| 23 | `WPA2_ENT_TKIP` | WPA2 Enterprise with TKIP |
| 24 | `WPA2_ENT_AES` | WPA2 Enterprise with AES |
| 25 | `WPA2_ENT_MIXED` | WPA2 Enterprise with AES + TKIP |
| 26 | `WPA2_ENT_FBT` | WPA2 Enterprise with FBT |
| 27 | `WPA3_ENT` | WPA3 Enterprise |
| 28 | `WPA3_ENT_192` | WPA3 Enterprise 192-bit (Suite B) |
| 29 | `WPA3_ENT_AES` | WPA3 Enterprise transition mode |

##### Security Type Mapping (Internal)

```c
// In at_wifi_cmds.c
typedef struct {
    const char *name;
    whd_security_t whd_type;
} wifi_security_map_t;

static const wifi_security_map_t g_security_map[] = {
    { "OPEN",           WHD_SECURITY_OPEN },
    { "WEP_PSK",        WHD_SECURITY_WEP_PSK },
    { "WEP_SHARED",     WHD_SECURITY_WEP_SHARED },
    { "WPA_TKIP",       WHD_SECURITY_WPA_TKIP_PSK },
    { "WPA_AES",        WHD_SECURITY_WPA_AES_PSK },
    { "WPA_MIXED",      WHD_SECURITY_WPA_MIXED_PSK },
    { "WPA2_AES",       WHD_SECURITY_WPA2_AES_PSK },
    { "WPA2_TKIP",      WHD_SECURITY_WPA2_TKIP_PSK },
    { "WPA2_MIXED",     WHD_SECURITY_WPA2_MIXED_PSK },
    { "WPA2_SHA256",    WHD_SECURITY_WPA2_AES_PSK_SHA256 },
    { "WPA2_FBT",       WHD_SECURITY_WPA2_FBT_PSK },
    { "WPA3_SAE",       WHD_SECURITY_WPA3_SAE },
    { "WPA3_FBT",       WHD_SECURITY_WPA3_FBT },
    { "WPA3_WPA2",      WHD_SECURITY_WPA3_WPA2_PSK },
    { "WPA3_OWE",       WHD_SECURITY_WPA3_OWE },
    { "WPA2_WPA",       WHD_SECURITY_WPA2_WPA_AES_PSK },
    // Enterprise
    { "WPA_ENT_TKIP",   WHD_SECURITY_WPA_TKIP_ENT },
    { "WPA_ENT_AES",    WHD_SECURITY_WPA_AES_ENT },
    { "WPA_ENT_MIXED",  WHD_SECURITY_WPA_MIXED_ENT },
    { "WPA2_ENT_TKIP",  WHD_SECURITY_WPA2_TKIP_ENT },
    { "WPA2_ENT_AES",   WHD_SECURITY_WPA2_AES_ENT },
    { "WPA2_ENT_MIXED", WHD_SECURITY_WPA2_MIXED_ENT },
    { "WPA2_ENT_FBT",   WHD_SECURITY_WPA2_FBT_ENT },
    { "WPA3_ENT",       WHD_SECURITY_WPA3_ENT },
    { "WPA3_ENT_192",   WHD_SECURITY_WPA3_192BIT_ENT },
    { "WPA3_ENT_AES",   WHD_SECURITY_WPA3_ENT_AES_CCMP },
};
```

#### Station Mode

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+WIFISCAN` | Start scan | None | OK (results via URC) |
| `AT+WIFISCAN=<ssid>` | Scan for specific SSID | SSID string | OK |
| `AT+WIFIJOIN=<ssid>,<pwd>` | Join network (auto-detect security) | SSID, password | OK |
| `AT+WIFIJOIN=<ssid>,<pwd>,<sec>` | Join with explicit security | SSID, pwd, security type | OK |
| `AT+WIFIJOIN=?` | List supported security types | None | +WIFIJOIN: OPEN,WPA2_AES,WPA3_SAE,... |
| `AT+WIFILEAVE` | Disconnect | None | OK |
| `AT+WIFIRSSI?` | Query signal strength | None | +WIFIRSSI: <rssi_dbm> |
| `AT+WIFIBSSID?` | Query connected AP | None | +WIFIBSSID: AA:BB:CC:DD:EE:FF |
| `AT+WIFICHAN?` | Query channel | None | +WIFICHAN: <channel> |
| `AT+WIFISEC?` | Query current security type | None | +WIFISEC: <security_type> |

**Station Mode Examples:**
```
# Join open network
AT+WIFIJOIN="GuestNet",""
OK

# Join WPA2 network (auto-detect)
AT+WIFIJOIN="HomeNetwork","MyPassword123"
OK

# Join WPA2 network (explicit security)
AT+WIFIJOIN="HomeNetwork","MyPassword123",WPA2_AES
OK

# Join WPA3 network
AT+WIFIJOIN="SecureNet","StrongPass!",WPA3_SAE
OK

# Join WPA3/WPA2 transition network
AT+WIFIJOIN="ModernNet","password",WPA3_WPA2
OK

# Join WPA3 Enhanced Open (no password, but encrypted)
AT+WIFIJOIN="CafeWiFi","",WPA3_OWE
OK

# Query supported security types
AT+WIFIJOIN=?
+WIFIJOIN: OPEN,WEP_PSK,WPA_AES,WPA2_AES,WPA2_MIXED,WPA3_SAE,WPA3_WPA2,WPA3_OWE
OK
```

**Unsolicited Result Codes during scan:**
```
+WIFISCAN: "<ssid>",<channel>,<rssi>,<security_type>
+WIFISCANEND
```

**Scan Result Security Mapping:**
The scan results report security type as detected by WHD:
```
+WIFISCAN: "HomeNetwork",6,-42,WPA2_AES
+WIFISCAN: "OfficeNet",11,-55,WPA3_SAE
+WIFISCAN: "GuestNet",1,-60,OPEN
+WIFISCAN: "LegacyNet",6,-70,WEP_PSK
+WIFISCANEND
```

**Connection events:**
```
+WIFICONNECTED: "<ssid>",<security_type>
+WIFIDISCONNECTED: <reason>
+WIFIGOTIP: <ip>,<mask>,<gateway>
```

#### Enterprise Mode (802.1X)

For enterprise networks, additional credentials are required:

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+WIFIEAP=<method>` | Set EAP method | PEAP\|TLS\|TTLS | OK |
| `AT+WIFIEAPID=<identity>` | Set EAP identity | Username string | OK |
| `AT+WIFIEAPPWD=<password>` | Set EAP password | Password string | OK |
| `AT+WIFIEAPCERT=<cert>` | Set client certificate | Base64 or file ref | OK |
| `AT+WIFIEAPKEY=<key>` | Set client private key | Base64 or file ref | OK |
| `AT+WIFIEAPCA=<ca>` | Set CA certificate | Base64 or file ref | OK |

**Enterprise Mode Example:**
```
# Configure EAP-PEAP
AT+WIFIEAP=PEAP
OK
AT+WIFIEAPID="user@company.com"
OK
AT+WIFIEAPPWD="SecretPassword"
OK

# Join enterprise network
AT+WIFIJOIN="CorpNetwork","",WPA2_ENT_AES
OK
+WIFICONNECTED: "CorpNetwork",WPA2_ENT_AES
+WIFIGOTIP: 10.0.1.50,255.255.255.0,10.0.1.1
```

#### Access Point Mode

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+WIFIAP=<ssid>,<pwd>,<ch>` | Configure AP (WPA2 default) | SSID, password, channel | OK |
| `AT+WIFIAP=<ssid>,<pwd>,<ch>,<sec>` | Configure AP with security | SSID, pwd, channel, security | OK |
| `AT+WIFIAPSTART` | Start AP | None | OK |
| `AT+WIFIAPSTOP` | Stop AP | None | OK |
| `AT+WIFIAPCLIENTS?` | List connected clients | None | +WIFIAPCLIENT: <mac>,<ip> |
| `AT+WIFIAPSEC?` | Query AP security type | None | +WIFIAPSEC: <security_type> |

**AP Mode Supported Security Types:**
- `OPEN` - Open network (not recommended)
- `WPA2_AES` - WPA2 Personal (recommended minimum)
- `WPA2_MIXED` - WPA2 with AES + TKIP
- `WPA3_SAE` - WPA3 Personal (most secure)
- `WPA3_WPA2` - WPA3/WPA2 transition mode

**AP Mode Examples:**
```
# Create WPA2 access point
AT+WIFIAP="MyHotspot","password123",6,WPA2_AES
OK
AT+WIFIAPSTART
OK

# Create WPA3 access point
AT+WIFIAP="SecureHotspot","StrongPass!",11,WPA3_SAE
OK
AT+WIFIAPSTART
OK

# Create open access point (use with caution)
AT+WIFIAP="OpenHotspot","",6,OPEN
OK
AT+WIFIAPSTART
OK
```

#### Power Management

| Command | Description | Parameters | Response |
|---------|-------------|------------|----------|
| `AT+WIFIPM=<mode>` | Set power mode | 0=off, 1=PS-Poll, 2=fast | OK |
| `AT+WIFIPM?` | Query power mode | None | +WIFIPM: <mode> |

---

## 5. USB Configuration

### 5.1 Endpoint Allocation

| Endpoint | Direction | Type | Size | Interface | Usage |
|----------|-----------|------|------|-----------|-------|
| EP0 | IN/OUT | Control | 64 | - | USB Control |
| EP 0x81 | IN | Bulk | 512 | 1 | MIDI IN (High-Speed) |
| EP 0x01 | OUT | Bulk | 512 | 1 | MIDI OUT (High-Speed) |
| EP 0x82 | IN | Bulk | 64 | 3 | CDC Data IN |
| EP 0x02 | OUT | Bulk | 64 | 3 | CDC Data OUT |
| EP 0x83 | IN | Interrupt | 8 | 2 | CDC Notifications |
| EP 0x84 | IN | Bulk | 512 | 4 | Wi-Fi Bridge IN (High-Speed) |
| EP 0x04 | OUT | Bulk | 512 | 4 | Wi-Fi Bridge OUT (High-Speed) |

### 5.2 USB Descriptors

| Interface | Class | Subclass | Protocol | Description |
|-----------|-------|----------|----------|-------------|
| 0 | 0x01 (Audio) | 0x01 (Control) | 0x00 | Audio Control |
| 1 | 0x01 (Audio) | 0x03 (MIDI) | 0x00 | MIDI Streaming |
| 2 | 0x02 (CDC) | 0x02 (ACM) | 0x01 (AT) | CDC Control |
| 3 | 0x0A (Data) | 0x00 | 0x00 | CDC Data |
| 4 | 0xFF (Vendor) | 0x00 | 0x00 | Wi-Fi Bridge (Bulk Data) |

### 5.3 Composite Device Setup

```c
void usb_composite_init(void)
{
    // Initialize USB stack
    USBD_Init();
    USBD_SetDeviceInfo(&g_device_info);

    // Add MIDI interface (High-Speed bulk endpoints)
    USB_BULK_INIT_DATA midi_init = {
        .EPIn  = USBD_AddEP(USB_DIR_IN,  USB_TRANSFER_TYPE_BULK, 0, NULL, 512),
        .EPOut = USBD_AddEP(USB_DIR_OUT, USB_TRANSFER_TYPE_BULK, 0, NULL, 512),
    };
    g_midi_handle = USBD_BULK_Add(&midi_init);

    // Add CDC interface
    USB_CDC_INIT_DATA cdc_init = {
        .EPIn  = USBD_AddEP(USB_DIR_IN,  USB_TRANSFER_TYPE_BULK, 0, NULL, 64),
        .EPOut = USBD_AddEP(USB_DIR_OUT, USB_TRANSFER_TYPE_BULK, 0, NULL, 64),
        .EPInt = USBD_AddEP(USB_DIR_IN,  USB_TRANSFER_TYPE_INT,  0, NULL, 8),
    };
    g_cdc_handle = USBD_CDC_Add(&cdc_init);
    USBD_CDC_SetOnLineCoding(g_cdc_handle, on_line_coding);

    // Add Wi-Fi bridge interface (High-Speed bulk endpoints)
    USB_BULK_INIT_DATA wifi_init = {
        .EPIn  = USBD_AddEP(USB_DIR_IN,  USB_TRANSFER_TYPE_BULK, 0, NULL, 512),
        .EPOut = USBD_AddEP(USB_DIR_OUT, USB_TRANSFER_TYPE_BULK, 0, NULL, 512),
    };
    g_wifi_bridge_handle = USBD_BULK_Add(&wifi_init);

    // NOTE: Do NOT call USBD_Start() here!
    // Call wifi_bridge_init() first, then wifi_bridge_set_handle(g_wifi_bridge_handle),
    // then call usb_composite_start() which calls USBD_Start().
}

void usb_composite_start(void)
{
    // Start USB enumeration (must be called after all endpoints configured)
    USBD_Start();
}
```

**Initialization Order (Critical!):**
1. `usb_composite_init()` - Creates all USB endpoints for MIDI, CDC, and Wi-Fi bridge
2. `wifi_bridge_init()` - Initializes WHD/SDIO and FreeRTOS resources
3. `wifi_bridge_set_handle(usb_composite_get_wifi_bridge_handle())` - Connects Wi-Fi bridge to USB
4. `usb_composite_start()` - Starts USB enumeration (calls USBD_Start)
5. `wifi_bridge_start()` - Starts bridge data processing (called from Wi-Fi task)

---

## 6. FreeRTOS Integration

### 6.1 Task Configuration

| Task | Stack Size | Priority | Description |
|------|------------|----------|-------------|
| CDC/AT Task | 2048 words (8 KB) | 3 | CDC I/O + AT parsing |
| USB Task | 1024 words (4 KB) | 4 | USB/MIDI processing (existing) |
| BLE Task | 1024 words (4 KB) | 5 | Bluetooth events (existing) |
| Wi-Fi Task | 1024 words (4 KB) | 3 | Wi-Fi bridge (existing) |

### 6.2 Queue Configuration

| Queue | Item Size | Depth | Purpose |
|-------|-----------|-------|---------|
| CDC TX | 64 bytes | 8 | Response messages |
| CDC RX | 64 bytes | 4 | Incoming data chunks |
| URC | 128 bytes | 8 | Unsolicited result codes |

### 6.3 CDC/AT Task Implementation

```c
#define CDC_TASK_STACK_SIZE  2048
#define CDC_TASK_PRIORITY    3

static void cdc_at_task(void *pvParameters)
{
    char rx_buf[64];
    int rx_len;

    // Initialize modules
    at_parser_init();
    at_parser_register_commands(g_system_cmds, SYSTEM_CMD_COUNT);
    at_parser_register_commands(g_bt_cmds, BT_CMD_COUNT);
    at_parser_register_commands(g_wifi_cmds, WIFI_CMD_COUNT);
    at_parser_register_commands(g_leaudio_cmds, LEAUDIO_CMD_COUNT);

    while (1) {
        // Process CDC USB events
        cdc_acm_process();

        // Read and parse incoming data
        rx_len = cdc_acm_read(rx_buf, sizeof(rx_buf));
        for (int i = 0; i < rx_len; i++) {
            at_parser_process_char(rx_buf[i]);
        }

        // Process URC queue (async events)
        process_urc_queue();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// In main.c
xTaskCreate(cdc_at_task, "CDC/AT", CDC_TASK_STACK_SIZE,
            NULL, CDC_TASK_PRIORITY, &g_cdc_task_handle);
```

---

## 7. Async Event Handling

### 7.1 URC (Unsolicited Result Code) Pattern

For async operations like scanning, results are sent as URCs:

```c
// URC message structure
typedef struct {
    char event[16];     // Event name
    char data[112];     // Event data
} urc_message_t;

static QueueHandle_t g_urc_queue;

// Called from BT/Wi-Fi callbacks (may be ISR context)
void at_send_urc(const char *event, const char *fmt, ...)
{
    urc_message_t msg;
    va_list args;

    strncpy(msg.event, event, sizeof(msg.event) - 1);
    va_start(args, fmt);
    vsnprintf(msg.data, sizeof(msg.data), fmt, args);
    va_end(args);

    // Use ISR-safe queue send if in ISR
    BaseType_t higher_woken = pdFALSE;
    if (xPortIsInsideInterrupt()) {
        xQueueSendFromISR(g_urc_queue, &msg, &higher_woken);
        portYIELD_FROM_ISR(higher_woken);
    } else {
        xQueueSend(g_urc_queue, &msg, 0);
    }
}

// Called from CDC task
void process_urc_queue(void)
{
    urc_message_t msg;
    while (xQueueReceive(g_urc_queue, &msg, 0) == pdTRUE) {
        cdc_acm_send_urc(msg.event, msg.data);
    }
}
```

### 7.2 Scan Result Example

```c
// GAP scan callback
static void gap_scan_result_callback(const gap_scan_result_t *result, void *ctx)
{
    if (result == NULL) {
        // Scan complete
        at_send_urc("SCANDONE", NULL);
    } else {
        // Send result
        at_send_urc("SCANRESULT", "\"%s\",%02X:%02X:%02X:%02X:%02X:%02X,%d,%d",
                    result->name,
                    result->addr[0], result->addr[1], result->addr[2],
                    result->addr[3], result->addr[4], result->addr[5],
                    result->rssi, result->adv_type);
    }
}
```

---

## 8. Memory Budget

### 8.1 RAM Usage

| Component | Size | Notes |
|-----------|------|-------|
| CDC context | 128 B | Handles, state, mutex |
| CDC EP buffers | 192 B | 64 x 3 endpoints |
| AT parser | 320 B | Line buffer (256) + state |
| Command tables | - | In flash (const) |
| TX queue | 512 B | 8 x 64-byte items |
| RX queue | 256 B | 4 x 64-byte items |
| URC queue | 1024 B | 8 x 128-byte items |
| CDC task stack | 8192 B | 2048 words |
| **Total RAM** | **~10.5 KB** | |

### 8.2 Flash Usage

| Component | Size | Notes |
|-----------|------|-------|
| cdc_acm.c | ~2 KB | CDC implementation |
| at_parser.c | ~1.5 KB | Parser logic |
| at_bt_cmds.c | ~3 KB | BT command handlers |
| at_wifi_cmds.c | ~2.5 KB | Wi-Fi command handlers |
| at_leaudio_cmds.c | ~2 KB | LE Audio handlers |
| at_system_cmds.c | ~1 KB | System handlers |
| Command tables | ~2 KB | ~60 commands x 32B |
| **Total Flash** | **~14 KB** | |

---

## 9. Build Configuration

### 9.1 CMakeLists.txt Changes

```cmake
# Add option
option(ENABLE_CDC "Enable USB CDC/ACM AT command interface" ON)

# Add sources conditionally
if(ENABLE_CDC)
    list(APPEND PROJECT_SOURCES
        source/cdc/cdc_acm.c
        source/cdc/at_parser.c
        source/cdc/at_bt_cmds.c
        source/cdc/at_wifi_cmds.c
        source/cdc/at_leaudio_cmds.c
        source/cdc/at_system_cmds.c
    )
    list(APPEND PROJECT_INCLUDES source/cdc)
    add_definitions(-DENABLE_CDC=1)
endif()
```

### 9.2 MTB Makefile Changes

```makefile
# In proj_cm33_ns/Makefile
DEFINES+=ENABLE_CDC

SOURCES+=\
    $(wildcard ../../../source/cdc/*.c)

INCLUDES+=\
    ../../../source/cdc
```

---

## 10. Implementation Phases

### Phase 1: Foundation (Est. 2-3 days)
- [ ] Create `source/cdc/` directory structure
- [ ] Implement `cdc_acm.c` using emUSB-Device USB_CDC API
- [ ] Modify `midi_usb.c` for composite device support
- [ ] Implement basic `at_parser.c` with line buffering
- [ ] Add system commands (AT, ATI, AT+VERSION, AT+RST, AT+ECHO)
- [ ] Update CMakeLists.txt and main.c
- [ ] Test USB enumeration (both MIDI and CDC visible)
- [ ] Test basic AT commands via terminal

### Phase 2: Bluetooth Commands (Est. 2-3 days)
- [ ] Implement `at_bt_cmds.c`
- [ ] Add init/deinit/state commands
- [ ] Add GAP advertising commands
- [ ] Add GAP scanning with async URC results
- [ ] Add connection management
- [ ] Test BT command flow

### Phase 3: LE Audio Commands (Est. 1-2 days)
- [ ] Implement `at_leaudio_cmds.c`
- [ ] Add broadcast start/stop
- [ ] Add unicast start/stop
- [ ] Add codec query/config
- [ ] Test LE Audio command flow

### Phase 4: Wi-Fi Commands (Est. 2-3 days)
- [ ] Implement `at_wifi_cmds.c`
- [ ] Add init/deinit/state commands
- [ ] Add scan with async results
- [ ] Add join/leave commands
- [ ] Add AP mode commands
- [ ] Test Wi-Fi command flow

### Phase 5: Polish & Testing (Est. 1-2 days)
- [ ] Add AT+HELP command
- [ ] Add configuration save/restore
- [ ] Error handling improvements
- [ ] Integration testing
- [ ] Documentation updates

**Total Estimated Effort: 8-13 days**

---

## 11. Testing Strategy

### 11.1 Unit Tests
- AT parser tokenization
- Command table lookup
- Response formatting
- Parameter parsing (strings, hex, integers)

### 11.2 Integration Tests

| Test | Description |
|------|-------------|
| USB Enumeration | Both MIDI and CDC interfaces visible |
| Basic AT | AT, ATI, AT+VERSION respond correctly |
| BT Init | AT+BTINIT enables Bluetooth |
| BT Scan | AT+GAPSCAN=1 produces scan results |
| BT Connect | AT+GAPCONN connects to device |
| Wi-Fi Scan | AT+WIFISCAN produces network list |
| Wi-Fi Join | AT+WIFIJOIN connects to network |
| Concurrent | MIDI and AT commands work simultaneously |

### 11.3 Test Tools
- Serial terminal (Tera Term, PuTTY, minicom)
- USB analyzer (optional)
- Python test script for automation

---

## 12. Example Session

```
# Terminal connects at 115200 baud (any baud works for USB CDC)

AT
OK

ATI
+INFO: Infineon LE Audio,1.0.0
OK

# === Bluetooth Setup ===

AT+BTINIT
OK

AT+BTSTATE?
+BTSTATE: READY
OK

AT+BTNAME="My Speaker"
OK

AT+GAPSCAN=1,5000
OK
+SCANRESULT: "Phone",AA:BB:CC:DD:EE:FF,-45,0
+SCANRESULT: "Headphones",11:22:33:44:55:66,-62,0
+SCANDONE

AT+GAPCONN=AA:BB:CC:DD:EE:FF
OK
+GAPCONNECTED: 1,AA:BB:CC:DD:EE:FF

# === Wi-Fi Setup ===

AT+WIFIINIT
OK

# Query supported security types
AT+WIFIJOIN=?
+WIFIJOIN: OPEN,WEP_PSK,WPA_AES,WPA2_AES,WPA2_MIXED,WPA3_SAE,WPA3_WPA2,WPA3_OWE
OK

# Scan for networks (shows security type)
AT+WIFISCAN
OK
+WIFISCAN: "HomeNetwork",6,-42,WPA2_AES
+WIFISCAN: "OfficeWiFi",11,-55,WPA3_SAE
+WIFISCAN: "GuestNet",1,-65,OPEN
+WIFISCAN: "CafeWiFi",6,-70,WPA3_OWE
+WIFISCANEND

# Join WPA2 network (auto-detect security)
AT+WIFIJOIN="HomeNetwork","password123"
OK
+WIFICONNECTED: "HomeNetwork",WPA2_AES
+WIFIGOTIP: 192.168.1.100,255.255.255.0,192.168.1.1

# Check connection details
AT+WIFISEC?
+WIFISEC: WPA2_AES
OK

AT+WIFIRSSI?
+WIFIRSSI: -42
OK

# Disconnect and join WPA3 network
AT+WIFILEAVE
OK
+WIFIDISCONNECTED: 0

AT+WIFIJOIN="OfficeWiFi","SecurePass!",WPA3_SAE
OK
+WIFICONNECTED: "OfficeWiFi",WPA3_SAE
+WIFIGOTIP: 10.0.1.50,255.255.255.0,10.0.1.1

# === Start Access Point ===

# First disconnect from station mode
AT+WIFILEAVE
OK

# Create WPA3 hotspot
AT+WIFIAP="MyHotspot","HotspotPass123",6,WPA3_SAE
OK

AT+WIFIAPSTART
OK

# Check connected clients
AT+WIFIAPCLIENTS?
+WIFIAPCLIENT: AA:BB:CC:DD:EE:FF,192.168.4.2
+WIFIAPCLIENT: 11:22:33:44:55:66,192.168.4.3
OK

# === LE Audio Broadcast ===

AT+LEAINIT
OK

AT+LEABROADCAST=1,"Living Room Speaker"
OK
+LEASTREAM: STARTED,BROADCAST
```

---

## 13. References

- `libs/emusb-device/USBD/USB_CDC.h` - emUSB-Device CDC API
- `source/midi/midi_usb.c` - Existing USB implementation pattern
- `source/bluetooth/bt_init.h` - Bluetooth API
- `source/bluetooth/gap_config.h` - GAP API
- `source/le_audio/le_audio_manager.h` - LE Audio API
- `libs/wifi-host-driver/WHD/COMPONENT_WIFI6/inc/whd_wifi_api.h` - Wi-Fi API
- 3GPP TS 27.007 - AT command standard reference
