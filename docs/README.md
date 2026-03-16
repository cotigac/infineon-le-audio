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

The project has **complete scaffolding** for all major modules with **143 TODOs** remaining that require HAL/middleware integration.

**What's Complete:**
- ✅ Full project structure with all source files
- ✅ FreeRTOS task architecture with proper priorities
- ✅ FreeRTOS synchronization (mutexes, queues, semaphores) in all modules
- ✅ Data structures and state machines for all features
- ✅ Ring buffer management for audio and MIDI
- ✅ LC3 wrapper API (calls to liblc3)
- ✅ CMakeLists.txt with all dependencies
- ✅ Library submodules (btstack, btstack-integration, liblc3, wifi-host-driver, emusb-device)

**What's Remaining (143 TODOs):**
- HAL calls (cyhal_i2s, cyhal_sdio, cyhal_uart)
- BTSTACK API calls (wiced_bt_*, HCI commands)
- USB middleware integration (emUSB-Device)
- WHD (Wi-Fi Host Driver) integration

### TODO Distribution by File

| File | TODOs | Category | Priority |
|------|-------|----------|----------|
| `gap_config.c` | 37 | Bluetooth GAP/HCI | HIGH |
| `le_audio_manager.c` | 18 | LE Audio Profiles | HIGH |
| `wifi_bridge.c` | 17 | Wi-Fi Data Path | MEDIUM |
| `pacs.c` | 11 | LE Audio PACS | HIGH |
| `wifi_sdio.c` | 9 | SDIO HAL | MEDIUM |
| `midi_ble_service.c` | 9 | BLE MIDI | MEDIUM |
| `bap_broadcast.c` | 7 | Auracast | HIGH |
| `midi_usb.c` | 7 | USB MIDI | MEDIUM |
| `audio_task.c` | 6 | Audio Processing | HIGH |
| `isoc_handler.c` | 4 | HCI ISOC | CRITICAL |
| `midi_router.c` | 4 | MIDI Routing | LOW |
| `bt_init.c` | 3 | BT Stack Init | CRITICAL |
| `hci_isoc.c` | 3 | HCI Commands | CRITICAL |
| `gatt_db.c` | 3 | GATT Database | MEDIUM |
| `bap_unicast.c` | 3 | BAP Unicast | HIGH |
| `i2s_stream.c` | 2 | I2S Audio | HIGH |
| **Total** | **143** | | |

---

## Remaining TODOs by Data Path

### Path 1: LE Audio TX (I2S RX → LC3 Encode → HCI ISOC TX)

```
Main Controller → I2S RX → Audio Buffers → LC3 Encode → ISOC Handler → HCI → CYW55512
     PCM           DMA      Ring Buffer     liblc3       HCI ISOC      UART    Radio
```

| Step | File | TODOs | What's Needed |
|------|------|-------|---------------|
| I2S RX DMA | `i2s_stream.c` | 2 | `cyhal_i2s_init()`, `cyhal_i2s_read_async()` |
| LC3 Encode | `audio_task.c` | 2 | Wire `lc3_encode()` output to ISOC handler |
| ISOC TX | `isoc_handler.c` | 2 | `hci_isoc_send_data()` implementation |
| HCI Send | `hci_isoc.c` | 2 | Wire to BTSTACK HCI transport |

**Critical Integration Point:**
```c
// In audio_task.c:996 - Currently commented out:
/* TODO: Send to ISOC handler */
/* isoc_handler_send_sdu(stream->info.isoc_stream_id,
                         g_audio_task.lc3_encode_buffer, lc3_len,
                         meta.timestamp); */
```

### Path 2: LE Audio RX (HCI ISOC RX → LC3 Decode → I2S TX)

```
CYW55512 → HCI → ISOC Handler → LC3 Decode → Audio Buffers → I2S TX → Main Controller
  Radio    UART    HCI ISOC       liblc3      Ring Buffer     DMA         PCM
```

| Step | File | TODOs | What's Needed |
|------|------|-------|---------------|
| HCI RX | `hci_isoc.c` | 1 | BTSTACK ISOC data callback |
| ISOC RX | `isoc_handler.c` | 2 | Process incoming SDUs |
| LC3 Decode | `audio_task.c` | 2 | Already wired to `lc3_decode()` |
| I2S TX DMA | `i2s_stream.c` | 0 | `cyhal_i2s_write_async()` |

### Path 3: BLE MIDI (USB ↔ GATT ↔ Controller)

```
USB Host ↔ USB MIDI Class ↔ MIDI Router ↔ BLE MIDI Service ↔ HCI ↔ CYW55512
           emUSB-Device       FreeRTOS       BTSTACK GATT      UART    Radio
                                  ↓
                          Controller UART
```

| Component | File | TODOs | What's Needed |
|-----------|------|-------|---------------|
| USB MIDI | `midi_usb.c` | 7 | emUSB-Device initialization, endpoint callbacks |
| BLE MIDI | `midi_ble_service.c` | 9 | BTSTACK GATT service registration, notifications |
| Router | `midi_router.c` | 4 | Controller UART init, timestamp generation |

**USB MIDI Critical TODOs:**
```c
// midi_usb.c:538 - USB device initialization
/* TODO: Initialize USB device with MIDI descriptors */
// Need: Cy_USB_Dev_Init(), Cy_USB_Dev_RegisterCallback()

// midi_usb.c:586 - Start receiving
/* TODO: Start async receive on OUT endpoint */
// Need: Cy_USB_Dev_ReadOutEndpoint()

// midi_usb.c:611 - Send data
/* TODO: Send data on IN endpoint */
// Need: Cy_USB_Dev_WriteInEndpoint()
```

**BLE MIDI Critical TODOs:**
```c
// midi_ble_service.c:628 - GATT registration
/* TODO: Register GATT database with MIDI service */
// Need: wiced_bt_gatt_db_init(), wiced_bt_gatt_register()

// midi_ble_service.c:730 - Send notification
/* TODO: Send GATT notification */
// Need: wiced_bt_gatt_server_send_notification()
```

### Path 4: Wi-Fi Bridge (USB HS → SDIO → CYW55512 WLAN)

```
App Processor → USB HS Bulk → Bridge Queues → SDIO Driver → WHD → CYW55512
   Network       emUSB-Device    FreeRTOS      cyhal_sdio    WHD    WLAN
```

| Component | File | TODOs | What's Needed |
|-----------|------|-------|---------------|
| USB Bulk | `wifi_bridge.c` | 6 | emUSB-Device bulk endpoint init |
| Bridge | `wifi_bridge.c` | 5 | WHD packet callbacks |
| SDIO | `wifi_sdio.c` | 9 | `cyhal_sdio_init()`, CMD52/CMD53 |
| WHD | `wifi_bridge.c` | 6 | `whd_init()`, `whd_wifi_on()` |

**SDIO Critical TODOs:**
```c
// wifi_sdio.c:104 - SDIO initialization
/* TODO: Initialize SDIO HAL */
// Need: cyhal_sdio_init(), cyhal_sdio_configure()

// wifi_sdio.c:159 - CMD52 (single byte)
/* TODO: Implement CMD52 using HAL */
// Need: cyhal_sdio_send_cmd()

// wifi_sdio.c:196 - CMD53 (block transfer)
/* TODO: Implement CMD53 using HAL */
// Need: cyhal_sdio_bulk_transfer()
```

### Path 5: Bluetooth HCI (All BLE Operations)

```
BTSTACK → HCI Commands → UART → CYW55512 Controller
         gap_config.c    3Mbps    BLE Radio
```

| Component | File | TODOs | What's Needed |
|-----------|------|-------|---------------|
| Stack Init | `bt_init.c` | 3 | `wiced_bt_stack_init()`, power management |
| GAP | `gap_config.c` | 37 | All HCI LE commands via BTSTACK |
| GATT | `gatt_db.c` | 3 | `wiced_bt_gatt_db_init()` |
| ISOC | `hci_isoc.c` | 3 | `wiced_bt_isoc_*` APIs |

**Note:** `gap_config.c` has 37 TODOs because every GAP operation (advertising, scanning, connection, etc.) needs its corresponding HCI command wired to BTSTACK. The scaffolding stores parameters locally but doesn't call the actual HCI functions.

---

## Critical Architectural Gaps

### Gap 1: HCI ISOC Data Path (CRITICAL)

**Problem**: The entire LE Audio data path depends on HCI ISOC working.

**Files Affected:**
- `hci_isoc.c` - Send/receive ISOC data packets
- `isoc_handler.c` - SDU queuing and timing

**Required Integration:**
```c
// Need to implement in hci_isoc.c:
int hci_isoc_send_data(uint16_t handle, uint32_t timestamp,
                       uint16_t seq_num, const uint8_t *data, uint16_t len)
{
    // Build HCI ISO Data packet header
    // Send via wiced_bt_isoc_write()
}

// Need to register callback:
wiced_bt_isoc_register_data_cb(hci_isoc_data_callback);
```

### Gap 2: USB High-Speed Middleware

**Problem**: `emusb-device` library needs proper initialization for USB HS.

**Files Affected:**
- `midi_usb.c` - USB MIDI class
- `wifi_bridge.c` - USB bulk data

**Required Integration:**
```c
// Need to implement USB device init with proper descriptors
// PSoC Edge E82 uses different USB IP than PSoC 6
// May require USBD_Init() from Segger emUSB-Device
```

### Gap 3: BTSTACK Integration

**Problem**: All 37 TODOs in `gap_config.c` need BTSTACK API calls.

**Example Pattern:**
```c
// Current (scaffolded):
int gap_start_advertising(void) {
    /* TODO: Enable advertising via HCI */
    return GAP_OK;
}

// Needed:
int gap_start_advertising(void) {
    wiced_result_t result = wiced_bt_start_advertisements(
        BTM_BLE_ADVERT_UNDIRECTED_HIGH,
        BLE_ADDR_PUBLIC,
        NULL
    );
    return (result == WICED_BT_SUCCESS) ? GAP_OK : GAP_ERROR;
}
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
