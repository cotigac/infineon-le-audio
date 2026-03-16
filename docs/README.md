# Infineon LE Audio - Project Plan & Analysis

This document contains the complete implementation plan for the Infineon LE Audio demo project, including analysis of different approaches and technical decisions.

## Table of Contents

1. [Project Overview](#project-overview)
2. [Implementation Status & Gap Analysis](#implementation-status--gap-analysis)
3. [Remaining TODOs by Data Path](#remaining-todos-by-data-path)
4. [Technology Analysis: Zephyr vs ModusToolbox](#technology-analysis-zephyr-vs-modustoolbox)
5. [Implementation Plan](#implementation-plan)
6. [Key Infineon Repositories](#key-infineon-repositories)
7. [Risks & Mitigations](#risks--mitigations)
8. [References](#references)

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

| Feature | Description | Status |
|---------|-------------|--------|
| **LE Audio Unicast** | Full-duplex audio streaming via CIS | 🟡 Scaffolded |
| **LE Audio Broadcast (Auracast)** | One-to-many audio broadcast via BIS | 🟡 Scaffolded |
| **LC3 Codec** | Host-side implementation using Google liblc3 | 🟡 Scaffolded |
| **BLE MIDI** | MIDI over Bluetooth Low Energy GATT service | 🟡 Scaffolded |
| **USB MIDI** | USB High-Speed MIDI class device | 🟡 Scaffolded |
| **I2S Streaming** | DMA-based bidirectional audio | 🟡 Scaffolded |
| **Wi-Fi Bridge** | USB HS → SDIO → CYW55512 WLAN | 🟡 Scaffolded |

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
- ✅ **wifi_sdio.c** - SDIO HAL with `cyhal_sdio_*` APIs (CMD52, CMD53, async DMA)
- ✅ **wifi_bridge.c** - USB-Wi-Fi bridge with WHD and emUSB-Device bulk endpoints
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
| `wifi_bridge.c` | 0 | ✅ Complete | WHD + emUSB-Device bulk integration |
| `wifi_sdio.c` | 0 | ✅ Complete | `cyhal_sdio_*` HAL APIs |
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
| `bap_broadcast.c` | ✅ BAP Broadcast (Auracast) complete |
| `le_audio_manager.h` | ✅ Header definitions complete |
| `i2s_stream.c` | ✅ I2S streaming complete |
| `midi_ble_service.c` | ✅ BLE MIDI service complete |

---

## Remaining TODOs by Data Path

### Path 1: LE Audio TX (I2S RX → LC3 Encode → HCI ISOC TX) ✅ COMPLETE

```
Main Controller → I2S RX → Audio Buffers → LC3 Encode → ISOC Handler → HCI → CYW55512
     PCM           DMA      Ring Buffer     liblc3       HCI ISOC      UART    Radio
```

| Step | File | TODOs | Status |
|------|------|-------|--------|
| I2S RX DMA | `i2s_stream.c` | 0 | ✅ Ring buffer with critical sections |
| LC3 Encode | `audio_task.c` | 0 | ✅ Wired to `lc3_encode()`, sends to ISOC |
| ISOC TX | `isoc_handler.c` | 0 | ✅ `isoc_handler_tx_frame()` with stream lookup |
| HCI Send | `hci_isoc.c` | 0 | ✅ `wiced_bt_isoc_write()` integration |

**Data Path Wiring (Fixed):**
```c
// audio_task.c - process_tx_path()
// Finds ISOC stream by handle, then calls correct API
int isoc_stream_id = isoc_handler_find_by_iso_handle(stream->info.cis_handle);
isoc_handler_tx_frame((uint8_t)isoc_stream_id, lc3_buffer, lc3_len, timestamp);
```

### Path 2: LE Audio RX (HCI ISOC RX → LC3 Decode → I2S TX) ✅ COMPLETE

```
CYW55512 → HCI → ISOC Handler → LC3 Decode → Audio Buffers → I2S TX → Main Controller
  Radio    UART    HCI ISOC       liblc3      Ring Buffer     DMA         PCM
```

| Step | File | TODOs | Status |
|------|------|-------|--------|
| HCI RX | `hci_isoc.c` | 0 | ✅ BTSTACK ISOC data callback registered |
| ISOC RX | `isoc_handler.c` | 0 | ✅ Process incoming SDUs with timestamps |
| LC3 Decode | `audio_task.c` | 0 | ✅ Reads from ISOC handler, decodes with PLC |
| I2S TX DMA | `i2s_stream.c` | 0 | ✅ Thread-safe ring buffer writes |

**Data Path Wiring (Fixed):**
```c
// audio_task.c - process_rx_path()
// Reads directly from ISOC handler's RX buffer (not audio_task's lc3_buffer)
int isoc_stream_id = isoc_handler_find_by_iso_handle(stream->info.cis_handle);
int result = isoc_handler_rx_frame((uint8_t)isoc_stream_id,
                                   lc3_decode_buffer, MAX_LC3_BYTES_PER_FRAME,
                                   &lc3_len, &rx_timestamp);
// Then decode: decode_lc3_to_pcm(stream, lc3_decode_buffer, lc3_len, pcm_tx_buffer);
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
| SDIO Driver | `wifi_sdio.c` | 0 | ✅ `cyhal_sdio_*` HAL APIs |
| WHD Init | `wifi_bridge.c` | 0 | ✅ `whd_init()`, `whd_wifi_on()` |
| USB Bulk | `wifi_bridge.c` | 0 | ✅ emUSB-Device bulk endpoints |
| Bridge Logic | `wifi_bridge.c` | 0 | ✅ WHD packet callbacks |

**SDIO Implementation:**
```c
// wifi_sdio.c - SDIO HAL with cyhal_sdio_* APIs
cyhal_sdio_init(&sdio_obj, cmd, clk, d0, d1, d2, d3);
cyhal_sdio_send_cmd(&sdio_obj, CYHAL_SDIO_CMD_IO_RW_DIRECT, arg, &response);
cyhal_sdio_bulk_transfer(&sdio_obj, direction, addr, data, len, &response);
cyhal_sdio_register_callback(&sdio_obj, sdio_irq_handler, NULL);
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
| BAP Broadcast | `bap_broadcast.c` | 0 | ✅ Auracast with periodic advertising |
| PACS | `pacs.c` | 0 | ✅ Published Audio Capabilities |
| LE Audio Mgr | `le_audio_manager.c` | 0 | ✅ Timeout with FreeRTOS ticks |

### Path Summary

| Path | Description | TODOs | Status |
|------|-------------|-------|--------|
| 1 | LE Audio TX | 0 | ✅ Complete |
| 2 | LE Audio RX | 0 | ✅ Complete |
| 3 | BLE MIDI | 0 | ✅ Complete |
| 4 | Wi-Fi Bridge | 0 | ✅ Complete |
| 5 | Bluetooth HCI | 0 | ✅ Complete |
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

#### Gap 4: Wi-Fi SDIO HAL - RESOLVED

**Status**: Fully implemented with `cyhal_sdio_*` HAL APIs.

**Implementation:**
```c
// wifi_sdio.c
cyhal_sdio_init(&ctx->sdio_obj, ctx->config.cmd_pin, ctx->config.clk_pin,
                ctx->config.data_pins[0], ctx->config.data_pins[1],
                ctx->config.data_pins[2], ctx->config.data_pins[3]);
cyhal_sdio_send_cmd(&ctx->sdio_obj, CYHAL_SDIO_CMD_IO_RW_DIRECT, arg, &response);
cyhal_sdio_bulk_transfer(&ctx->sdio_obj, direction, address, data, length, &response);
cyhal_sdio_register_callback(&ctx->sdio_obj, sdio_irq_handler, NULL);
```

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

### Phase A: Foundation (Hardware Bring-Up)

**Target**: Get basic hardware working with debug output.

| Task | File | Description |
|------|------|-------------|
| A.1 | `main.c` | Verify FreeRTOS scheduler starts |
| A.2 | `bt_init.c` | Initialize BTSTACK, see HCI events |
| A.3 | `i2s_stream.c` | Configure I2S, verify DMA interrupts |

**Checkpoint**: Console shows "Audio task started", "BLE task started", HCI Reset Complete event.

### Phase B: Bluetooth Stack Integration

**Target**: Complete BTSTACK integration for basic BLE.

| Task | File | Description |
|------|------|-------------|
| B.1 | `bt_init.c` | `wiced_bt_stack_init()` with config |
| B.2 | `gatt_db.c` | Register GATT database |
| B.3 | `gap_config.c` | Wire advertising functions |
| B.4 | `gap_config.c` | Wire scanning functions |
| B.5 | `gap_config.c` | Wire connection functions |

**Checkpoint**: Device advertises, visible in nRF Connect app.

### Phase C: LE Audio ISOC

**Target**: Get HCI ISOC data path working.

| Task | File | Description |
|------|------|-------------|
| C.1 | `hci_isoc.c` | Implement `hci_isoc_create_cig()` |
| C.2 | `hci_isoc.c` | Implement `hci_isoc_create_cis()` |
| C.3 | `hci_isoc.c` | Implement `hci_isoc_send_data()` |
| C.4 | `isoc_handler.c` | Wire ISOC data callbacks |
| C.5 | `le_audio_manager.c` | Wire to audio task |

**Checkpoint**: LC3 frames sent over CIS, visible in Bluetooth sniffer.

### Phase D: Audio Pipeline

**Target**: Full audio loopback working.

| Task | File | Description |
|------|------|-------------|
| D.1 | `i2s_stream.c` | `cyhal_i2s_init()` with DMA |
| D.2 | `audio_task.c` | Wire I2S RX to LC3 encoder |
| D.3 | `audio_task.c` | Wire LC3 decoder to I2S TX |
| D.4 | `audio_task.c` | Wire to ISOC handler |

**Checkpoint**: Audio loopback through LC3 codec.

### Phase E: MIDI Integration

**Target**: MIDI over USB and BLE working.

| Task | File | Description |
|------|------|-------------|
| E.1 | `midi_usb.c` | emUSB-Device initialization |
| E.2 | `midi_usb.c` | USB descriptors and endpoints |
| E.3 | `midi_ble_service.c` | GATT service registration |
| E.4 | `midi_router.c` | Wire all ports together |

**Checkpoint**: MIDI events flow USB ↔ BLE.

### Phase F: Wi-Fi Bridge

**Target**: USB-to-Wi-Fi data bridge working.

| Task | File | Description |
|------|------|-------------|
| F.1 | `wifi_sdio.c` | `cyhal_sdio_init()` |
| F.2 | `wifi_sdio.c` | CMD52/CMD53 implementation |
| F.3 | `wifi_bridge.c` | WHD initialization |
| F.4 | `wifi_bridge.c` | USB bulk endpoints |
| F.5 | `wifi_bridge.c` | Bidirectional packet bridge |

**Checkpoint**: Data flows USB HS → SDIO → Wi-Fi.

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
