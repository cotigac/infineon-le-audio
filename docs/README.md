# Infineon LE Audio - Project Plan & Analysis

This document contains the complete implementation plan for the Infineon LE Audio demo project, including analysis of different approaches and technical decisions.

## Table of Contents

1. [Project Overview](#project-overview)
2. [Dual-Core Architecture](#dual-core-architecture)
3. [Implementation Status & Gap Analysis](#implementation-status--gap-analysis)
4. [Remaining TODOs by Data Path](#remaining-todos-by-data-path)
5. [Technology Analysis: Zephyr vs ModusToolbox](#technology-analysis-zephyr-vs-modustoolbox)
6. [Implementation Plan](#implementation-plan)
7. [Key Infineon Repositories](#key-infineon-repositories)
8. [Risks & Mitigations](#risks--mitigations)
9. [References](#references)

---

## Project Overview

### Target Application

A musical instrument (synthesizer, digital piano, guitar processor, etc.) that needs to:
- Stream high-quality audio wirelessly to LE Audio headphones/speakers
- Broadcast audio to multiple receivers simultaneously (Auracast)
- Send/receive MIDI over Bluetooth LE and USB
- Interface with a main application controller via I2S

### Hardware Platform

| Component | Part Number | Description |
|-----------|-------------|-------------|
| **MCU** | PSE823GOS4DBZQ3 | PSoC Edge E82, Cortex-M55 @ 400MHz + Cortex-M33 |
| **Wireless** | CYW55512IUBGT | AIROC Wi-Fi 6 + Bluetooth 6.0 combo IC |
| **Eval Kit** | KIT_PSE84_EVAL | PSoC Edge E84 Evaluation Kit (USB HS, SDIO) |
| **Alt Eval** | CYW9RPI55513-EVK | CYW55512/55513 evaluation kit for Raspberry Pi |

### Feature Requirements

| Feature | Core | Description | Status |
|---------|------|-------------|--------|
| **LE Audio Unicast** | CM33+CM55 | Full-duplex audio streaming via CIS | ✅ Implemented |
| **LE Audio Broadcast Source** | CM33+CM55 | One-to-many audio broadcast via BIS (Auracast TX) | ✅ Implemented |
| **LE Audio Broadcast Sink** | CM33+CM55 | Receive Auracast broadcasts (Auracast RX) | ✅ Implemented |
| **LC3 Codec** | CM55 | Host-side implementation using Google liblc3 (Helium DSP) | ✅ Implemented |
| **BLE MIDI** | CM33 | MIDI over Bluetooth Low Energy GATT service | ✅ Implemented |
| **USB MIDI** | CM33 | USB High-Speed MIDI class device | ✅ Implemented |
| **I2S Streaming** | CM55 | DMA-based bidirectional audio | ✅ Implemented |
| **Wi-Fi Bridge** | CM33 | USB HS → SDIO → CYW55512 WLAN | ✅ Implemented |
| **USB CDC/ACM** | CM33 | AT command interface for BT/WiFi/LE Audio config | ✅ Complete |

---

## Dual-Core Architecture

The firmware uses PSoC Edge's dual-core architecture to optimize performance:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            PSoC Edge E82/E84                                 │
├──────────────────────────────────┬──────────────────────────────────────────┤
│        Cortex-M33 Core           │            Cortex-M55 Core               │
│       (Control Plane)            │            (Audio DSP)                   │
│                                  │                                          │
│  - BTSTACK + LE Audio control    │  - LC3 Codec (liblc3 + Helium DSP)      │
│  - USB High-Speed (emUSB)        │  - I2S DMA streaming                    │
│  - Wi-Fi WHD + SDIO              │  - Audio ring buffers                   │
│  - MIDI routing                  │  - Frame synchronization                │
│                                  │                                          │
│  Tasks: BLE(5), USB(4),          │  Tasks: Audio(Highest), IPC(High)       │
│         WiFi(3), MIDI(2)         │                                          │
├──────────────────────────────────┴──────────────────────────────────────────┤
│                    IPC (Inter-Processor Communication)                       │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ TX: CM55 (I2S RX → LC3 Encode) → [Queue] → CM33 (ISOC TX)            │  │
│  │ RX: CM33 (ISOC RX) → [Queue] → CM55 (LC3 Decode → I2S TX)            │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Why Dual-Core?

| Benefit | Description |
|---------|-------------|
| **Performance** | LC3 codec benefits from CM55's Helium DSP (up to 8x faster) |
| **Concurrency** | Control plane runs concurrently with audio processing |
| **Determinism** | I2S DMA callbacks have deterministic timing on dedicated core |
| **No Contention** | BT stack callbacks don't compete with codec execution |

### Core Responsibilities

| Core | FreeRTOS Tasks | Source Files |
|------|----------------|--------------|
| **CM33** | BLE, USB, Wi-Fi, MIDI | `le_audio/`, `bluetooth/`, `midi/`, `wifi/` |
| **CM55** | Audio, IPC | `audio/` |

### ModusToolbox Project Structure

```
mtb/le-audio/
├── proj_cm33_s/          # CM33 Secure core (TrustZone bootstrap)
│   └── main.c            # Minimal secure bootstrap
├── proj_cm33_ns/         # CM33 Non-Secure (Control Plane)
│   ├── source/
│   │   ├── main.c        # CM33 entry: BT, USB, WiFi, MIDI tasks
│   │   └── app_le_audio.c# Bridge to LE Audio manager
│   └── Makefile          # CM33 build: BTSTACK, emUSB, WHD
└── proj_cm55/            # CM55 Core (Audio DSP)
    ├── main.c            # CM55 entry: LC3, I2S, IPC tasks
    └── Makefile          # CM55 build: liblc3, audio
```

---

## Implementation Status & Gap Analysis

### Current Implementation Summary

The project has **complete implementation** of all Bluetooth, LE Audio, MIDI, and Wi-Fi modules. All **28 original TODOs have been implemented**, and critical data path issues identified during end-to-end analysis have been fixed.

**What's Complete (✅ All Modules Fully Implemented):**
- ✅ Full project structure with all source files
- ✅ FreeRTOS task architecture with proper priorities
- ✅ FreeRTOS synchronization (mutexes, queues, semaphores) in all modules
- ✅ **bt_init.c** - `wiced_bt_stack_init()` with management callback
- ✅ **gatt_db.c** - GATT database registration with 128-bit UUIDs
- ✅ **gap_config.c** - All GAP operations (advertising, scanning, connections)
- ✅ **hci_isoc.c** - HCI ISOC commands (CIG/CIS/BIG creation, data paths)
- ✅ **isoc_handler.c** - ISOC data handling with timestamps
- ✅ **pacs.c** - PACS service with GATT client/server operations
- ✅ **bap_unicast.c** - BAP Unicast with ASE Control Point writes
- ✅ **bap_broadcast.c** - BAP Broadcast (Auracast) with periodic advertising
- ✅ **le_audio_manager.c** - Full unicast/broadcast state machines with timeout handling
- ✅ **audio_task.c** - LC3 encode/decode pipeline, ISOC transmission, I2S silence handling
- ✅ **i2s_stream.c** - DMA-based I2S with thread-safe ring buffers
- ✅ **midi_ble_service.c** - BLE MIDI GATT service with notifications
- ✅ **midi_usb.c** - USB MIDI class with emUSB-Device (TX and RX)
- ✅ **midi_router.c** - MIDI routing with timestamps using FreeRTOS ticks
- ✅ **wifi_bridge.c** - USB-Wi-Fi bridge with WHD (uses cyhal_sdio internally) and emUSB-Device bulk endpoints
- ✅ LC3 wrapper API (calls to liblc3)
- ✅ CMakeLists.txt with all dependencies
- ✅ Library submodules (btstack, btstack-integration, liblc3, wifi-host-driver, emusb-device)

**Critical Data Path Fixes (Applied after end-to-end analysis):**
- ✅ **LE Audio TX**: Fixed `isoc_handler_send_sdu()` → `isoc_handler_tx_frame()` with proper stream_id lookup
- ✅ **LE Audio RX**: Fixed disconnected buffers - now calls `isoc_handler_rx_frame()` directly
- ✅ **USB MIDI RX**: Added `midi_usb_receive()` and `midi_usb_rx_available()` public APIs

### TODO Distribution by File

| File | TODOs | Status | Notes |
|------|-------|--------|-------|
| `wifi_bridge.c` | 0 | ✅ Complete | WHD (cyhal_sdio) + emUSB-Device bulk |
| `midi_usb.c` | 0 | ✅ Complete | emUSB-Device MIDI class |
| `midi_router.c` | 0 | ✅ Complete | FreeRTOS tick timestamps |
| `audio_task.c` | 0 | ✅ Complete | I2S silence + ISOC TX/RX wiring |
| `le_audio_manager.c` | 0 | ✅ Complete | Timeout with tick polling |
| **Total** | **0** | ✅ | All TODOs implemented |

### Completed Modules (0 TODOs)

| File | Description |
|------|-------------|
| `bt_init.c` | ✅ BTSTACK initialization complete |
| `gatt_db.c` | ✅ GATT database registration complete |
| `gap_config.c` | ✅ All 37 GAP operations implemented |
| `hci_isoc.c` | ✅ HCI ISOC commands complete |
| `isoc_handler.c` | ✅ ISOC data handling complete |
| `pacs.c` | ✅ PACS service complete |
| `bap_unicast.c` | ✅ BAP Unicast complete |
| `bap_broadcast.c` | ✅ BAP Broadcast Source (Auracast TX) complete |
| `bap_broadcast_sink.c` | ✅ BAP Broadcast Sink (Auracast RX) complete |
| `le_audio_manager.h` | ✅ Header definitions complete |
| `i2s_stream.c` | ✅ I2S streaming complete |
| `midi_ble_service.c` | ✅ BLE MIDI service complete |
| `cdc_acm.c` | ✅ USB CDC/ACM virtual serial port |
| `at_parser.c` | ✅ AT command parser and dispatcher |
| `at_system_cmds.c` | ✅ System AT commands (AT, ATI, VERSION) |
| `at_bt_cmds.c` | ✅ Bluetooth AT commands |
| `at_leaudio_cmds.c` | ✅ LE Audio AT commands |
| `at_wifi_cmds.c` | ✅ Wi-Fi AT commands |
| `usb_composite.c` | ✅ USB composite device (MIDI + CDC) |

---

## Remaining TODOs by Data Path

### Path 1: LE Audio TX (I2S RX → LC3 Encode → HCI ISOC TX) ✅ COMPLETE

Supports both Unicast (CIS) and Broadcast Source (BIS/Auracast TX).

**Dual-Core Data Flow:**
```
Main Controller → I2S RX → LC3 Encode → IPC Queue → ISOC Handler → HCI → CYW55512
     PCM          [CM55]     [CM55]       TX        [CM33]       UART    Radio
                   DMA      Helium DSP   Shared Mem  HCI ISOC   CIS/BIS
```

| Step | Core | File | TODOs | Status |
|------|------|------|-------|--------|
| I2S RX DMA | CM55 | `i2s_stream.c` | 0 | ✅ Ring buffer with critical sections |
| LC3 Encode | CM55 | `audio_task.c` | 0 | ✅ Helium DSP, posts to IPC queue |
| IPC TX | Both | `audio_ipc.c` | 0 | ✅ mtb-ipc queue CM55 → CM33 |
| ISOC TX | CM33 | `isoc_handler.c` | 0 | ✅ `isoc_handler_tx_frame()` with stream lookup |
| HCI Send | CM33 | `hci_isoc.c` | 0 | ✅ `wiced_bt_isoc_write()` integration (CIS + BIS) |
| CIG/CIS Create | CM33 | `bap_unicast.c` | 0 | ✅ Unicast: ASE config, CIG/CIS setup |
| BIG Create | CM33 | `bap_broadcast.c` | 0 | ✅ Auracast TX: ext adv, PA, BASE, BIG |

**Data Path Wiring (Dual-Core via mtb-ipc):**
```c
// CM55: audio_task.c - process_tx_path()
// Encode PCM to LC3, send to CM33 via mtb-ipc
lc3_encode(pcm_buffer, lc3_frame.data, &lc3_frame.length);
audio_ipc_send_encoded_frame(&lc3_frame);  // CM55 → CM33

// CM33: isoc_handler.c - polls mtb-ipc queue
audio_ipc_receive_from_encoder(&lc3_frame);
isoc_handler_tx_frame(stream_id, lc3_frame.data, lc3_frame.length, timestamp);
```

**Broadcast Source (Auracast TX) Flow:**
```c
// bap_broadcast.c - configure → ext adv → periodic adv → BIG create → streaming
bap_broadcast_configure(&config);     // Set codec params, build BASE
bap_broadcast_start();                // Start extended + periodic advertising
// BIG is created automatically, LC3 frames sent on BIS
bap_broadcast_stop();                 // Terminate BIG, stop advertising
```

### Path 2: LE Audio RX (HCI ISOC RX → LC3 Decode → I2S TX) ✅ COMPLETE

Supports both Unicast (CIS) and Broadcast Sink (BIS/Auracast RX).

**Dual-Core Data Flow:**
```
CYW55512 → HCI → ISOC Handler → IPC Queue → LC3 Decode → I2S TX → Main Controller
  Radio    UART   [CM33]         RX         [CM55]       [CM55]       PCM
 CIS/BIS         HCI ISOC     Shared Mem  Helium DSP     DMA
```

| Step | Core | File | TODOs | Status |
|------|------|------|-------|--------|
| HCI RX | CM33 | `hci_isoc.c` | 0 | ✅ BTSTACK ISOC data callback registered (CIS + BIS) |
| ISOC RX | CM33 | `isoc_handler.c` | 0 | ✅ Posts to IPC queue (multi-callback registry) |
| BIG Sync | CM33 | `bap_broadcast_sink.c` | 0 | ✅ Auracast RX: scan, PA sync, BIG sync |
| IPC RX | Both | `audio_ipc.c` | 0 | ✅ mtb-ipc queue CM33 → CM55 |
| LC3 Decode | CM55 | `audio_task.c` | 0 | ✅ Helium DSP, PLC for packet loss |
| I2S TX DMA | CM55 | `i2s_stream.c` | 0 | ✅ Thread-safe ring buffer writes |

**Data Path Wiring (Dual-Core via mtb-ipc):**
```c
// CM33: isoc_handler.c - on ISOC RX callback (CIS or BIS)
audio_ipc_send_to_decoder(&lc3_frame);  // CM33 → CM55

// CM55: audio_task.c - process_rx_path()
audio_ipc_receive_for_decode(&lc3_frame);
lc3_decode(lc3_frame.data, lc3_frame.length, pcm_buffer);
i2s_stream_write(pcm_buffer, pcm_len);
```

**Broadcast Sink (Auracast RX) Flow:**
```c
// bap_broadcast_sink.c - scan → PA sync → BIG sync → streaming
bap_broadcast_sink_start_scan();      // Extended scanning for broadcasts
bap_broadcast_sink_sync_to_pa(addr);  // Sync to periodic advertising (BASE)
bap_broadcast_sink_sync_to_big(...);  // Sync to BIG, receive LC3 on BIS
```

### Path 3: BLE MIDI (USB ↔ GATT ↔ Controller) ✅ COMPLETE

```
USB Host ↔ USB MIDI Class ↔ MIDI Router ↔ BLE MIDI Service ↔ HCI ↔ CYW55512
           emUSB-Device       FreeRTOS       BTSTACK GATT      UART    Radio
                                  ↓
                          Controller UART
```

| Component | File | TODOs | Status |
|-----------|------|-------|--------|
| USB MIDI | `midi_usb.c` | 0 | ✅ emUSB-Device MIDI class with TX/RX |
| BLE MIDI | `midi_ble_service.c` | 0 | ✅ GATT service with notifications |
| Router | `midi_router.c` | 0 | ✅ Timestamps using FreeRTOS ticks |

**USB MIDI Implementation:**
```c
// midi_usb.c - emUSB-Device MIDI class
USBD_MIDI_Add(&midi_init_data);           // Add MIDI class
USBD_MIDI_Receive(handle, buffer, len, 0); // Non-blocking RX
USBD_MIDI_Write(handle, data, len, 0);     // Non-blocking TX
midi_usb_receive(&event);                  // Public API for RX queue
```

**MIDI Router Timestamps:**
```c
// midi_router.c - FreeRTOS tick-based timestamps
static uint32_t get_system_timestamp_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * (1000U / configTICK_RATE_HZ));
}
```

### Path 4: Wi-Fi Bridge (USB HS → SDIO → CYW55512 WLAN) ✅ COMPLETE

```
App Processor → USB HS Bulk → Bridge Queues → SDIO Driver → WHD → CYW55512
   Network       emUSB-Device    FreeRTOS      cyhal_sdio    WHD    WLAN
```

| Component | File | TODOs | Status |
|-----------|------|-------|--------|
| WHD + SDIO | `wifi_bridge.c` | 0 | ✅ WHD uses `cyhal_sdio_*` internally |
| USB Bulk | `wifi_bridge.c` | 0 | ✅ emUSB-Device bulk endpoints |
| Bridge Logic | `wifi_bridge.c` | 0 | ✅ WHD packet callbacks |

**WHD SDIO (internal to wifi-host-driver):**
```c
// WHD library handles SDIO via cyhal_sdio internally
// No custom SDIO driver needed - WHD abstracts this
whd_init(&whd_driver, &whd_init_config, &resource_ops, &buffer_ops, &netif_funcs);
whd_wifi_on(whd_driver, interface);
```

**WHD Implementation:**
```c
// wifi_bridge.c - Wi-Fi Host Driver integration
#include "whd.h"
#include "whd_wifi_api.h"
#include "whd_network_types.h"

whd_init(&whd_config, &ctx->whd_driver, &buffer_ops, &netif_ops, &resource_ops);
whd_wifi_on(ctx->whd_driver, &ctx->whd_interface);
whd_network_register_link_callback(ctx->whd_interface, whd_link_state_callback, ctx);
```

### Path 5: Bluetooth HCI (All BLE Operations) ✅ COMPLETE

```
BTSTACK → HCI Commands → UART → CYW55512 Controller
         gap_config.c    3Mbps    BLE Radio
```

| Component | File | TODOs | Status |
|-----------|------|-------|--------|
| Stack Init | `bt_init.c` | 0 | ✅ `wiced_bt_stack_init()` with callbacks |
| GAP | `gap_config.c` | 0 | ✅ All GAP operations implemented |
| GATT | `gatt_db.c` | 0 | ✅ `wiced_bt_gatt_db_init()` complete |
| ISOC | `hci_isoc.c` | 0 | ✅ `wiced_bt_isoc_*` APIs integrated |
| BAP Unicast | `bap_unicast.c` | 0 | ✅ ASE Control Point operations |
| BAP Broadcast Source | `bap_broadcast.c` | 0 | ✅ Auracast TX with periodic advertising |
| BAP Broadcast Sink | `bap_broadcast_sink.c` | 0 | ✅ Auracast RX: scan, PA sync, BIG sync |
| PACS | `pacs.c` | 0 | ✅ Published Audio Capabilities |
| LE Audio Mgr | `le_audio_manager.c` | 0 | ✅ Timeout with FreeRTOS ticks |

### Path 6: USB CDC/ACM AT Command Interface ✅ COMPLETE

AT-style command interface over USB virtual serial port for configuring Bluetooth, Wi-Fi, and LE Audio.

```
Host PC → USB CDC/ACM → AT Parser → Command Handlers → BT/WiFi/LE Audio APIs
Terminal   emUSB-Device   Tokenizer    at_*_cmds.c       BTSTACK/WHD/etc.
```

| Component | File | TODOs | Status |
|-----------|------|-------|--------|
| USB CDC Class | `cdc_acm.c` | 0 | ✅ emUSB-Device CDC/ACM |
| AT Parser | `at_parser.c` | 0 | ✅ Line buffer, tokenizer, dispatcher |
| System Commands | `at_system_cmds.c` | 0 | ✅ AT, ATI, VERSION, RST, ECHO |
| Bluetooth Commands | `at_bt_cmds.c` | 0 | ✅ BTINIT, BTSTATE, GAP operations |
| LE Audio Commands | `at_leaudio_cmds.c` | 0 | ✅ LEAINIT, BROADCAST, UNICAST |
| Wi-Fi Commands | `at_wifi_cmds.c` | 0 | ✅ WIFIINIT, SCAN, JOIN, BRIDGE |
| USB Composite | `usb_composite.c` | 0 | ✅ MIDI + CDC composite device |

**AT Command Summary:**

| Category | Commands |
|----------|----------|
| System | `AT`, `ATI`, `AT+VERSION?`, `AT+RST`, `AT+ECHO=0/1` |
| Bluetooth | `AT+BTINIT`, `AT+BTSTATE?`, `AT+BTNAME=<name>`, `AT+GAPADVSTART`, `AT+GAPSCAN=1`, `AT+GAPCONN=<addr>` |
| LE Audio | `AT+LEAINIT`, `AT+LEASTATE?`, `AT+LEABROADCAST=1`, `AT+LEAUNICAST=1,<handle>`, `AT+LEACODEC`, `AT+LEASCAN`, `AT+LEASYNC`, `AT+LEADEMO` |
| Wi-Fi | `AT+WIFIINIT`, `AT+WIFISTATE?`, `AT+WIFISCAN`, `AT+WIFIJOIN=<ssid>,<pwd>`, `AT+WIFIBRIDGE=1` |

### Path Summary

| Path | Description | TODOs | Status |
|------|-------------|-------|--------|
| 1 | LE Audio TX (Unicast CIS + Broadcast Source/Auracast TX) | 0 | ✅ Complete |
| 2 | LE Audio RX (Unicast CIS + Broadcast Sink/Auracast RX) | 0 | ✅ Complete |
| 3 | BLE MIDI | 0 | ✅ Complete |
| 4 | Wi-Fi Bridge | 0 | ✅ Complete |
| 5 | Bluetooth HCI | 0 | ✅ Complete |
| 6 | USB CDC/ACM AT | 0 | ✅ Complete |
| **Total** | | **0** | ✅ All paths complete |

---

## Architectural Status

### ✅ Closed Gaps

#### Gap 1: HCI ISOC Data Path - RESOLVED

**Status**: Fully implemented with Infineon BTSTACK APIs.

**Implementation:**
```c
// hci_isoc.c - Sending ISOC data
int hci_isoc_send_data(uint16_t handle, uint32_t timestamp,
                       uint16_t seq_num, const uint8_t *data, uint16_t len)
{
    wiced_bt_isoc_sdu_t sdu = {0};
    sdu.p_data = (uint8_t *)data;
    sdu.length = len;
    sdu.timestamp = timestamp;
    sdu.sequence_number = seq_num;
    return wiced_bt_isoc_write(handle, &sdu);
}

// Callback registration in bt_init.c
wiced_bt_isoc_register_data_cb(hci_isoc_data_callback);
```

#### Gap 2: BTSTACK Integration - RESOLVED

**Status**: All GAP operations implemented with proper `wiced_bt_*` API calls.

**Example Implementation (gap_config.c):**
```c
int gap_start_advertising(void) {
    wiced_result_t result = wiced_bt_start_advertisements(
        BTM_BLE_ADVERT_UNDIRECTED_HIGH,
        BLE_ADDR_PUBLIC,
        NULL
    );
    return (result == WICED_BT_SUCCESS) ? GAP_OK : GAP_ERROR;
}
```

### ✅ All Gaps Closed

#### Gap 3: USB High-Speed Middleware - RESOLVED

**Status**: Fully implemented with Segger emUSB-Device.

**Implementation:**
```c
// midi_usb.c - USB MIDI class
USBD_AddEP(USB_DIR_IN, USB_TRANSFER_TYPE_BULK, 0, buffer, size);
USBD_MIDI_Add(&midi_init_data);
USBD_MIDI_Receive(handle, buffer, len, 0);  // Non-blocking RX
USBD_MIDI_Write(handle, data, len, 0);      // Non-blocking TX

// wifi_bridge.c - USB bulk endpoints
USBD_AddEP(USB_DIR_OUT, USB_TRANSFER_TYPE_BULK, 0, buffer, size);
USBD_BULK_Add(&bulk_init_data);
```

#### Gap 4: Wi-Fi SDIO HAL - RESOLVED (via WHD)

**Status**: Handled internally by WHD library using `cyhal_sdio_*` APIs.

**Note:** No custom SDIO driver needed. WHD (Wi-Fi Host Driver) manages SDIO
communication with CYW55512 internally using the cyhal_sdio interface.

#### Gap 5: Wi-Fi Host Driver (WHD) - RESOLVED

**Status**: Fully implemented with WHD initialization and callbacks.

**Implementation:**
```c
// wifi_bridge.c
#include "whd.h"
#include "whd_wifi_api.h"
#include "whd_network_types.h"

whd_init(&whd_config, &ctx->whd_driver, &buffer_ops, &netif_ops, &resource_ops);
whd_wifi_on(ctx->whd_driver, &ctx->whd_interface);
whd_network_register_link_callback(ctx->whd_interface, whd_link_state_callback, ctx);
```

### Data Path Fixes (Critical Issues Resolved)

During end-to-end analysis, three critical data path issues were identified and fixed:

#### Fix 1: LE Audio TX - Incorrect Function Call

**Problem**: `audio_task.c` called `isoc_handler_send_sdu()` which doesn't exist.

**Solution**: Changed to `isoc_handler_tx_frame()` with proper stream ID lookup:
```c
// Before (broken):
isoc_handler_send_sdu(stream->info.cis_handle, buffer, len, timestamp);

// After (fixed):
int isoc_stream_id = isoc_handler_find_by_iso_handle(stream->info.cis_handle);
isoc_handler_tx_frame((uint8_t)isoc_stream_id, buffer, len, timestamp);
```

#### Fix 2: LE Audio RX - Disconnected Buffers

**Problem**: `process_rx_path()` read from `stream->lc3_buffer` (audio_task's buffer), but ISOC data was stored in `isoc_handler`'s `rx_buffer` - different buffers with no connection.

**Solution**: Changed to read directly from ISOC handler:
```c
// Before (broken):
audio_ring_buffer_read_frame(stream->lc3_buffer, decode_buffer, &meta);

// After (fixed):
int isoc_stream_id = isoc_handler_find_by_iso_handle(stream->info.cis_handle);
isoc_handler_rx_frame((uint8_t)isoc_stream_id, decode_buffer, max_len, &len, &timestamp);
```

#### Fix 3: USB MIDI RX - No Public API

**Problem**: `rx_queue_push()` stored received MIDI events, but `rx_queue_pop()` was private with no public API to drain the queue.

**Solution**: Added public APIs in `midi_usb.h`:
```c
int midi_usb_receive(midi_usb_event_t *event);  // Pop from RX queue
uint16_t midi_usb_rx_available(void);           // Check queue level
```

---

## Implementation Roadmap

### Phase 1: Dual-Core Foundation (Hardware Bring-Up)

**Target**: Get both cores booting with FreeRTOS and debug output.

| Task | Core | File | Description | Status |
|------|------|------|-------------|--------|
| 1.1 | CM33 | `proj_cm33_ns/main.c` | BSP init, retarget-io, FreeRTOS | ✅ Done |
| 1.2 | CM33 | `proj_cm33_ns/main.c` | Boot CM55 via `Cy_SysEnableCM55()` | ✅ Done |
| 1.3 | CM55 | `proj_cm55/main.c` | FreeRTOS scheduler, debug output | ✅ Done |
| 1.4 | Both | Makefiles | Build configuration for each core | ✅ Done |

**Checkpoint**: Both cores show "Task started" messages on debug UART.

### Phase 2: CM33 Bluetooth Stack Integration

**Target**: Complete BTSTACK integration on CM33.

| Task | Core | File | Description | Status |
|------|------|------|-------------|--------|
| 2.1 | CM33 | `bt_init.c` | `wiced_bt_stack_init()` with config | ✅ Done |
| 2.2 | CM33 | `gatt_db.c` | Register GATT database | ✅ Done |
| 2.3 | CM33 | `gap_config.c` | Wire advertising functions | ✅ Done |
| 2.4 | CM33 | `gap_config.c` | Wire scanning/connection | ✅ Done |

**Checkpoint**: Device advertises, visible in nRF Connect app.

### Phase 3: CM55 Audio Initialization

**Target**: I2S DMA and LC3 codec working on CM55.

| Task | Core | File | Description | Status |
|------|------|------|-------------|--------|
| 3.1 | CM55 | `i2s_stream.c` | `cyhal_i2s_init()` with DMA | ✅ Done |
| 3.2 | CM55 | `lc3_wrapper.c` | liblc3 encoder/decoder init | ✅ Done |
| 3.3 | CM55 | `audio_buffers.c` | Ring buffers with metadata | ✅ Done |
| 3.4 | CM55 | `audio_task.c` | Audio processing loop | ✅ Done |

**Checkpoint**: I2S loopback works (no LC3, just passthrough).

### Phase 4: IPC (Inter-Processor Communication)

**Target**: Shared memory queues between CM33 and CM55.

| Task | Core | File | Description | Status |
|------|------|------|-------------|--------|
| 4.1 | Both | `source/ipc/audio_ipc.c` | Define IPC queue structures | ✅ Done |
| 4.2 | CM55 | `proj_cm55/main.c` | TX queue (LC3 frames → CM33) | ✅ Done |
| 4.3 | CM33 | `isoc_handler.c` | RX queue (ISOC → CM55) | ✅ Done |
| 4.4 | Both | `proj_cm55/main.c` | IPC task with monitoring | ✅ Done |

**Checkpoint**: CM55 can send test data to CM33 via IPC.

### Phase 5: LE Audio ISOC (CM33 + IPC)

**Target**: HCI ISOC data path working with IPC.

| Task | Core | File | Description | Status |
|------|------|------|-------------|--------|
| 5.1 | CM33 | `hci_isoc.c` | CIG/CIS creation | ✅ Done |
| 5.2 | CM33 | `isoc_handler.c` | ISOC TX from IPC queue | ✅ Done |
| 5.3 | CM33 | `isoc_handler.c` | ISOC RX to IPC queue | ✅ Done |
| 5.4 | CM55 | `audio_task.c` | Wire LC3 encode → IPC TX | ✅ Done |
| 5.5 | CM55 | `audio_task.c` | Wire IPC RX → LC3 decode | ✅ Done |

**Checkpoint**: LC3 frames flow I2S → LC3 → ISOC → BLE.

### Phase 6: CM33 MIDI Integration

**Target**: MIDI over USB and BLE working on CM33.

| Task | Core | File | Description | Status |
|------|------|------|-------------|--------|
| 6.1 | CM33 | `midi_usb.c` | emUSB-Device initialization | ✅ Done |
| 6.2 | CM33 | `midi_ble_service.c` | GATT service registration | ✅ Done |
| 6.3 | CM33 | `midi_router.c` | Wire all ports together | ✅ Done |

**Checkpoint**: MIDI events flow USB ↔ BLE.

### Phase 7: CM33 Wi-Fi Bridge

**Target**: USB-to-Wi-Fi data bridge working on CM33.

| Task | Core | File | Description | Status |
|------|------|------|-------------|--------|
| 7.1 | CM33 | `wifi_bridge.c` | WHD initialization (uses cyhal_sdio internally) | ✅ Done |
| 7.2 | CM33 | `wifi_bridge.c` | USB bulk endpoints | ✅ Done |
| 7.3 | CM33 | `wifi_bridge.c` | Bidirectional packet bridge | ✅ Done |

**Checkpoint**: Data flows USB HS → SDIO → Wi-Fi.

### Implementation Summary

| Phase | Description | Core(s) | Status |
|-------|-------------|---------|--------|
| 1 | Dual-Core Foundation | CM33+CM55 | ✅ Complete |
| 2 | Bluetooth Stack | CM33 | ✅ Complete |
| 3 | Audio Initialization | CM55 | ✅ Complete |
| 4 | IPC Queues | CM33+CM55 | ✅ Complete |
| 5 | LE Audio ISOC | CM33+CM55 | ✅ Complete |
| 6 | MIDI Integration | CM33 | ✅ Complete |
| 7 | Wi-Fi Bridge | CM33 | ✅ Complete |

---

## Technology Analysis: Zephyr vs ModusToolbox

### Executive Summary

| Criteria | Zephyr RTOS | ModusToolbox + FreeRTOS |
|----------|-------------|-------------------------|
| **LE Audio / Auracast** | Native, complete | Unicast only; Auracast needs porting |
| **CYW55512 Support** | NOT AVAILABLE | Full support with on-chip LC3 |
| **PSoC Edge E82 Support** | E84 supported; E82 unclear | Full support via PDL |
| **USB MIDI** | Native USB MIDI 2.0 | Native USB MIDI 1.0 |
| **I2S/Audio** | I2S driver available | Mature I2S + TDM support |
| **Recommendation** | Hybrid approach required | **Primary recommendation** |

**CRITICAL FINDING**: Zephyr does **NOT** have a driver for CYW55512 (only CYW43xxx series). This makes a pure Zephyr approach **non-viable** for this hardware.

### Final Decision

**For PSoC Edge E82 + CYW55512: Use ModusToolbox + FreeRTOS**

The lack of CYW55512 driver support in Zephyr is a blocking issue.

---

## Key Infineon Repositories

### Core Stack & Middleware

| Repository | Purpose |
|------------|---------|
| [Infineon/btstack](https://github.com/Infineon/btstack) | Bluetooth Host Stack (BR/EDR + BLE) |
| [Infineon/btstack-integration](https://github.com/Infineon/btstack-integration) | Platform adaptation layer |
| [Infineon/emusb-device](https://github.com/Infineon/emusb-device) | USB Device Middleware |
| [Infineon/wifi-host-driver](https://github.com/Infineon/wifi-host-driver) | Wi-Fi Host Driver for CYW55512 |

### Audio Examples

| Repository | Purpose |
|------------|---------|
| [mtb-example-btstack-threadx-audio-watch](https://github.com/Infineon/mtb-example-btstack-threadx-audio-watch) | A2DP/HFP audio streaming reference |
| [mtb-example-psoc6-i2s](https://github.com/Infineon/mtb-example-psoc6-i2s) | I2S interface with audio codec |

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **HCI ISOC not working** | CRITICAL | Test with Bluetooth sniffer first |
| **USB HS middleware issues** | HIGH | May need Segger support for PSoC Edge |
| **Auracast porting complexity** | HIGH | Port minimal BAP broadcast from Zephyr |
| **LC3 CPU load** | MEDIUM | Profile on hardware, optimize if needed |
| **WHD integration** | MEDIUM | Follow Infineon Wi-Fi examples |

---

## References

### Infineon Documentation
- [CYW55513 Documentation](https://documentation.infineon.com/cyw55513/docs/nhu1755169548502)
- [PSoC Edge Documentation](https://documentation.infineon.com/psocedge/docs/bwb1750411526047)
- [BTSTACK API Reference](https://infineon.github.io/btstack/dual_mode/api_reference_manual/html/modules.html)

### Bluetooth SIG Specifications
- [Bluetooth Core 6.0 Specification](https://www.bluetooth.com/specifications/specs/)
- [BAP (Basic Audio Profile) Specification](https://www.bluetooth.com/specifications/specs/basic-audio-profile-1-0-1/)

### Open Source
- [Google liblc3](https://github.com/google/liblc3)
- [Zephyr LE Audio](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html)
