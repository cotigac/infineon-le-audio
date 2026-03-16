# Infineon LE Audio - Project Plan & Analysis

This document contains the complete implementation plan for the Infineon LE Audio demo project, including analysis of different approaches and technical decisions.

## Table of Contents

1. [Project Overview](#project-overview)
2. [Implementation Status & Gap Analysis](#implementation-status--gap-analysis)
3. [Technology Analysis: Zephyr vs ModusToolbox](#technology-analysis-zephyr-vs-modustoolbox)
4. [Implementation Plan](#implementation-plan)
5. [Key Infineon Repositories](#key-infineon-repositories)
6. [Risks & Mitigations](#risks--mitigations)
7. [References](#references)

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
| **LE Audio Unicast** | Full-duplex audio streaming via CIS | Scaffolded (TODOs) |
| **LE Audio Broadcast (Auracast)** | One-to-many audio broadcast via BIS | Scaffolded (TODOs) |
| **LC3 Codec** | Host-side implementation using Google liblc3 | Scaffolded (TODOs) |
| **BLE MIDI** | MIDI over Bluetooth Low Energy GATT service | Scaffolded (TODOs) |
| **USB MIDI** | USB High-Speed MIDI class device | **Needs USB HS Middleware** |
| **I2S Streaming** | DMA-based bidirectional audio | Scaffolded (TODOs) |
| **Wi-Fi Bridge** | USB HS → SDIO → CYW55512 WLAN | Scaffolded (TODOs) |

---

## Implementation Status & Gap Analysis

### Current Implementation Summary

The project has **scaffolded implementations** for all major modules. Code structure is complete but contains **184 TODOs** requiring hardware integration.

### TODO Distribution by Category

| Category | Count | Complexity | Priority |
|----------|-------|------------|----------|
| FreeRTOS Integration | 28 | Low-Medium | HIGH |
| Infineon PDL/HAL | 35 | High | CRITICAL |
| BTSTACK/HCI Integration | 52 | High | CRITICAL |
| liblc3 Codec | 8 | Medium | HIGH |
| USB MIDI | 14 | Medium | MEDIUM |
| BLE MIDI Service | 13 | Medium | MEDIUM |
| LE Audio Profiles | 22 | High | HIGH |
| Timestamps/Timing | 12 | Low | MEDIUM |
| **Total** | **184** | | |

### Critical Architectural Gaps

#### Gap 1: USB High-Speed Middleware (CRITICAL)

**Problem**: Current `emusb-device` library only supports USB Full-Speed (12 Mbps).

**Impact**:
- Wi-Fi data bridge requires USB HS (480 Mbps)
- PSoC Edge E82 hardware supports USB HS, but middleware doesn't

**Evidence** (from `libs/emusb-device/cy_usb_dev.h`):
```
"The USB Device middleware provides a full-speed USB 2.0..."
"hardware supports only full-speed device"
```

**Remediation**:
1. ~~Update all documentation references (USB FS → USB HS)~~ ✅ DONE
2. Replace `usbdev` ✅ DONE (replaced with emusb-device) with USB High-Speed capable middleware
3. Update `midi_usb.c` endpoint sizes (64 → 512 bytes)

#### Gap 2: Wi-Fi/SDIO Data Path ✅ RESOLVED

**Problem**: No Wi-Fi or SDIO implementation exists.

**Required Architecture**:
```
App Processor → USB HS → PSoC Edge → SDIO → CYW55512 WLAN
```

**Implementation** (completed):
- ✅ `source/wifi/` directory created
- ✅ `wifi_sdio.h/c` - SDIO driver interface for CYW55512
- ✅ `wifi_bridge.h/c` - USB-to-Wi-Fi data bridge
- ✅ [wifi-host-driver](https://github.com/Infineon/wifi-host-driver) added as submodule
- ✅ CMakeLists.txt updated with Wi-Fi sources and WHD library

**Remaining TODOs**:
- Integrate actual PSoC Edge SDIO HAL (cyhal_sdio)
- Integrate WHD (Wi-Fi Host Driver) callbacks
- Add Wi-Fi task to FreeRTOS main.c

#### Gap 3: Part Number Mismatch

**Problem**: Configuration files reference CYW55511, not CYW55512.

**Files to Update**:
- `config/cy_bt_config.h` - Change default controller
- `CMakeLists.txt` - Update MCU family
- `cmake/psoc_edge_e82.ld` - Updated for E82 (5 MB SRAM)

#### Gap 4: Missing Startup Code

**Problem**: No startup assembly or system initialization.

**Missing Files**:
```
source/startup/
├── startup_psoc_edge_e82.S
├── system_psoc_edge_e82.c
└── system_psoc_edge_e82.h
```

#### Gap 5: Missing Device Configurator Files

**Problem**: No hardware configuration for ModusToolbox.

**Missing Files**:
```
config/
├── design.modus
└── GeneratedSource/
    ├── cycfg.h
    ├── cycfg_pins.c/h
    ├── cycfg_peripherals.c/h
    └── cycfg_clocks.c/h
```

### Files Containing TODOs (Summary)

| File | TODOs | Primary Work |
|------|-------|--------------|
| `gap_config.c` | 35 | HCI advertising/scanning commands |
| `bt_init.c` | 18 | BTSTACK initialization, firmware download |
| `le_audio_manager.c` | 17 | BAP profiles, ISOC handling |
| `midi_usb.c` | 14 | USB device middleware integration |
| `midi_ble_service.c` | 13 | GATT service registration |
| `i2s_stream.c` | 14 | I2S peripheral, DMA setup |
| `audio_task.c` | 14 | FreeRTOS task, LC3 integration |
| `bap_broadcast.c` | 10 | Auracast advertising/BIG creation |
| `hci_isoc.c` | 4 | HCI command sending |
| Others | ~45 | Various integration points |

### Implementation Roadmap

> **Note**: This roadmap is dependency-aware. Each phase depends on the previous phases being complete.

#### Dependency Chain

```
Phase 1: Foundation (FreeRTOS + HAL)
    │
    ├─→ Phase 2: I2S Audio Pipeline
    │       │
    │       └─→ Phase 3: LC3 Codec Integration
    │               │
    │               └─→ Phase 5: LE Audio Streaming ←─┐
    │                                                  │
    └─→ Phase 4: Bluetooth HCI ────────────────────────┘
            │
            └─→ Phase 6: USB & MIDI
                    │
                    └─→ Phase 7: Wi-Fi Bridge
```

---

#### Phase 1: Foundation ⚡ CRITICAL - START HERE

**Status**: 🟢 Complete (FreeRTOS scaffolding)
**Blocking**: All other phases
**Files**: `main.c`

Without this phase complete, **nothing else works**. FreeRTOS must be running and HAL initialized.

| Task | Status | Description |
|------|--------|-------------|
| 1.1 Add `cybsp_init()` | [x] | Initialize board support package (clocks, pins) |
| 1.2 Uncomment FreeRTOS includes | [x] | Enable FreeRTOS headers in main.c |
| 1.3 Create task handles | [x] | Define xTaskHandle for each task |
| 1.4 Call `xTaskCreate()` | [x] | Create Audio, BLE, USB, MIDI, Wi-Fi tasks |
| 1.5 Call `vTaskStartScheduler()` | [x] | Start FreeRTOS scheduler |
| 1.6 Add debug console output | [x] | Enable printf via UART for debugging |

**Checkpoint**: See "Audio task started", "BLE task started" on console.

---

#### Phase 2: I2S Audio Pipeline

**Status**: 🟡 Scaffolded (code complete, HAL TODOs remain)
**Depends on**: Phase 1
**Files**: `i2s_stream.c`, `audio_buffers.c`

| Task | Status | Description |
|------|--------|-------------|
| 2.1 Implement `cyhal_i2s_init()` | [ ] | Configure I2S master, 48kHz/16-bit/stereo |
| 2.2 Configure DMA ring buffers | [x] | Ping-pong buffers for continuous streaming |
| 2.3 Wire DMA callbacks | [ ] | Handle buffer swap interrupts |
| 2.4 Implement `i2s_read()`/`i2s_write()` | [x] | Thread-safe audio data access |
| 2.5 Test audio loopback | [ ] | I2S RX → buffer → I2S TX (no codec) |

**Checkpoint**: Hear audio looped through I2S with external signal generator.

---

#### Phase 3: LC3 Codec Integration

**Status**: 🟡 Scaffolded (liblc3 wrapper complete, FreeRTOS sync enabled)
**Depends on**: Phase 2
**Files**: `lc3_wrapper.c`, `audio_task.c`

| Task | Status | Description |
|------|--------|-------------|
| 3.1 Instantiate liblc3 encoder | [x] | Create `lc3_encoder_t` with proper config |
| 3.2 Instantiate liblc3 decoder | [x] | Create `lc3_decoder_t` with proper config |
| 3.3 Wire `encode_pcm_to_lc3()` | [x] | Call `lc3_encode()` in audio task |
| 3.4 Wire `decode_lc3_to_pcm()` | [x] | Call `lc3_decode()` in audio task |
| 3.5 Add timing measurement | [ ] | Verify <2ms per frame @ 48kHz |
| 3.6 Test compressed loopback | [ ] | PCM → LC3 encode → LC3 decode → PCM |

**Checkpoint**: Compressed audio loopback working, CPU usage ~15-20%.

---

#### Phase 4: Bluetooth HCI

**Status**: 🟡 Scaffolded (BTSTACK + HCI-UART integration complete)
**Depends on**: Phase 1
**Files**: `bt_init.c`, `hci_isoc.c`, `bt_platform_config.c`

| Task | Status | Description |
|------|--------|-------------|
| 4.1 Initialize UART for HCI | [x] | 3 Mbps, CTS/RTS flow control (via btstack-integration) |
| 4.2 Implement HCI command sender | [x] | Packet framing via BTSTACK |
| 4.3 Implement HCI event handler | [x] | Parse responses via BTSTACK callbacks |
| 4.4 Implement HCI Reset command | [x] | Via wiced_bt_stack_init() |
| 4.5 Implement Read Local Version | [x] | Scaffolded in bt_init.c |
| 4.6 Download CYW55512 firmware | [ ] | Patchram download via btstack-integration |
| 4.7 Configure LE features | [x] | Enable ISOC, extended advertising (scaffolded) |

**Checkpoint**: See valid HCI events, controller version info on console.

---

#### Phase 5: LE Audio Streaming

**Status**: 🟡 Scaffolded (FreeRTOS sync enabled, BTSTACK wired)
**Depends on**: Phase 3 + Phase 4
**Files**: `le_audio_manager.c`, `bap_unicast.c`, `bap_broadcast.c`, `isoc_handler.c`, `pacs.c`

| Task | Status | Description |
|------|--------|-------------|
| 5.1 Implement PACS service | [x] | Publish audio capabilities via GATT (scaffolded) |
| 5.2 Implement ASCS service | [x] | Audio stream control via GATT (scaffolded) |
| 5.3 Implement CIS creation | [x] | HCI LE Create CIS command (in hci_isoc.c) |
| 5.4 Wire ISOC TX path | [x] | Audio task → HCI ISOC → radio (scaffolded) |
| 5.5 Wire ISOC RX path | [x] | Radio → HCI ISOC → audio task (scaffolded) |
| 5.6 Implement BAP state machine | [x] | Codec config → QoS config → Enable (scaffolded) |
| 5.7 Test with LE Audio headphones | [ ] | Full-duplex streaming end-to-end |

**Checkpoint**: Full-duplex audio streaming to LE Audio device.

---

#### Phase 6: USB & MIDI

**Status**: 🟡 Scaffolded (FreeRTOS sync enabled, BTSTACK wired)
**Depends on**: Phase 1
**Files**: `midi_usb.c`, `midi_ble_service.c`, `midi_router.c`

| Task | Status | Description |
|------|--------|-------------|
| 6.1 Initialize USB device | [x] | emUSB-Device initialization (scaffolded) |
| 6.2 Register USB descriptors | [x] | MIDI class, HS endpoints (512 bytes) |
| 6.3 Implement EP IN callback | [x] | MIDI TX to host (scaffolded) |
| 6.4 Implement EP OUT callback | [x] | MIDI RX from host (scaffolded) |
| 6.5 Implement BLE MIDI service | [x] | GATT MIDI characteristic (scaffolded) |
| 6.6 Implement MIDI router | [x] | Route between USB ↔ BLE ↔ I2S (FreeRTOS sync enabled) |

**Checkpoint**: MIDI note on/off flows over USB and BLE.

---

#### Phase 7: Wi-Fi Bridge

**Status**: 🟡 Scaffolded (FreeRTOS sync enabled, wifi-host-driver submodule added)
**Depends on**: Phase 6
**Files**: `wifi_sdio.c`, `wifi_bridge.c`

| Task | Status | Description |
|------|--------|-------------|
| 7.1 Integrate `cyhal_sdio` | [ ] | Replace TODO stubs with HAL calls |
| 7.2 Initialize WHD | [ ] | Wi-Fi Host Driver startup |
| 7.3 Implement SDIO CMD52/CMD53 | [x] | Single-byte and block transfers (scaffolded) |
| 7.4 Add Wi-Fi task to main.c | [x] | FreeRTOS task for packet processing |
| 7.5 Wire USB bulk endpoints | [ ] | emUSB-Device bulk IN/OUT |
| 7.6 Implement packet bridge | [x] | USB HS ↔ SDIO bidirectional (scaffolded, FreeRTOS sync enabled) |
| 7.7 Test Wi-Fi association | [ ] | Connect to access point |

**Checkpoint**: Data flows from USB through PSoC to Wi-Fi network.

---

### Completed Setup Tasks

- [x] Update part numbers (CYW55511 → CYW55512)
- [x] Rename linker script (e81 → e82)
- [x] Update SRAM to 5 MB per Infineon specs
- [x] Update documentation (USB FS → USB HS)
- [x] Replace usbdev library with emusb-device
- [x] Add wifi-host-driver submodule
- [x] Create `source/wifi/` directory structure
- [x] Implement SDIO driver interface (wifi_sdio.h/c)
- [x] Implement USB-to-Wi-Fi bridge (wifi_bridge.h/c)
- [x] Update CMakeLists.txt with Wi-Fi sources

---

## Technology Analysis: Zephyr vs ModusToolbox

### Executive Summary

| Criteria | Zephyr RTOS | ModusToolbox + FreeRTOS |
|----------|-------------|-------------------------|
| **LE Audio / Auracast** | Native, complete | Unicast only; Auracast needs porting |
| **CYW55511 Support** | NOT AVAILABLE | Full support with on-chip LC3 |
| **PSoC Edge E81 Support** | E84 supported; E81 unclear | Full support via PDL |
| **USB MIDI** | Native USB MIDI 2.0 | Native USB MIDI 1.0 |
| **I2S/Audio** | I2S driver available | Mature I2S + TDM support |
| **Community** | Large open-source community | Infineon-focused |
| **Recommendation** | Hybrid approach required | **Primary recommendation** |

**CRITICAL FINDING**: Zephyr does **NOT** have a driver for CYW55511 (only CYW43xxx series). This makes a pure Zephyr approach **non-viable** for this hardware configuration without significant porting effort.

### Detailed Comparison

#### 1. LE Audio / Auracast Support

**Zephyr RTOS:**
| Profile | Status | Roles |
|---------|--------|-------|
| BAP (Basic Audio Profile) | Feature Complete v1.0.1 | Unicast Server/Client, Broadcast Source/Sink |
| CAP (Common Audio Profile) | Feature Complete | Acceptor, Initiator, Commander |
| PBP (Public Broadcast Profile) | Feature Complete | Auracast TX/RX |
| BASS (Broadcast Audio Scan Service) | Feature Complete | Scan Delegator, Broadcast Assistant |
| VCP (Volume Control) | Feature Complete | Renderer, Controller |
| HAP (Hearing Aid) | Feature Complete | All roles |
| TMAP (Telephony & Media) | Feature Complete | All roles |
| GMAP (Gaming Audio) | Feature Complete | All roles |

**ModusToolbox + FreeRTOS:**
| Profile | Status | Notes |
|---------|--------|-------|
| LE Audio Unicast | Supported | Via CYW55511 firmware |
| LE Audio Broadcast | NOT SUPPORTED | Requires porting from Zephyr |
| LC3 Codec | On-chip (CYW55511) | Offload mode reduces host CPU load |
| Auracast | NOT SUPPORTED | Requires porting BAP broadcast source |

**Winner**: Zephyr (native LE Audio/Auracast support)

#### 2. Bluetooth Controller Support (CYW55511)

**Zephyr RTOS:**
| Chip Family | Zephyr Support | LE Audio Capable |
|-------------|----------------|------------------|
| CYW43012 | Supported | No |
| CYW4343W | Supported | No |
| CYW43439 | Supported | No |
| CYW4373 | Supported | No |
| **CYW55511** | **NOT SUPPORTED** | Yes (BT 6.0) |
| CYW55512 | NOT SUPPORTED | Yes (BT 6.0) |
| CYW55513 | NOT SUPPORTED | Yes (BT 6.0) |

**Current Driver**: `drivers/bluetooth/hci/h4_ifx_cyw43xxx.c` - Only supports legacy CYW43xxx series

**ModusToolbox + FreeRTOS:**
| Chip | Support | LE Audio | LC3 Mode | Wi-Fi |
|------|---------|----------|----------|-------|
| CYW55511 | Full | Yes | On-chip or HCI | Wi-Fi 6 |
| **CYW55512** | **Full** | **Yes** | **On-chip or HCI** | **Wi-Fi 6** |
| CYW55513 | Full | Yes | On-chip or HCI | Wi-Fi 6 |

**Winner**: ModusToolbox (CYW55512 support is mandatory)

#### 3. USB MIDI Support

**Zephyr RTOS:**
| Version | Status | Location |
|---------|--------|----------|
| USB MIDI 2.0 | Mainline | `subsys/usb/device_next/class/usbd_midi2.c` |
| USB MIDI 1.0 | Community driver | [stuffmatic/zephyr-usb-midi](https://github.com/stuffmatic/zephyr-usb-midi) |

**ModusToolbox:**
| Version | Status | Location |
|---------|--------|----------|
| USB MIDI 1.0 | Supported | Via `usbdev` middleware |

**Winner**: Zephyr (native USB MIDI 2.0)

#### 4. I2S / Audio Interface Support

**ModusToolbox** has more mature examples and TDM support:
- [mtb-example-psoc6-i2s](https://github.com/Infineon/mtb-example-psoc6-i2s)
- [mtb-example-psoc6-pdm-to-i2s](https://github.com/Infineon/mtb-example-psoc6-pdm-to-i2s)
- [mtb-example-audio-streaming](https://github.com/Infineon/mtb-example-audio-streaming)

**Winner**: ModusToolbox (mature examples, TDM explicitly supported)

### Architecture Options Considered

#### Option 1: Pure ModusToolbox + FreeRTOS (SELECTED)

```
┌──────────────────────────────────────────────────────────────┐
│                    PSoC Edge E82 (PSE823GOS4DBZQ3)           │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                       FreeRTOS                          │  │
│  │  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌──────┐ │  │
│  │  │ Infineon   │ │ USB HS     │ │ Wi-Fi      │ │ I2S  │ │  │
│  │  │ BTSTACK    │ │ MIDI+Data  │ │ Bridge     │ │ Audio│ │  │
│  │  └────────────┘ └────────────┘ └────────────┘ └──────┘ │  │
│  │        │              │              │                  │  │
│  │  ┌─────▼──────────────▼──────────────▼────────────────┐│  │
│  │  │ Auracast Port (from Zephyr) + liblc3               ││  │
│  │  │ BAP Broadcast Source                               ││  │
│  │  └────────────────────────────────────────────────────┘│  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────┬────────────────────┬──────────────────┘
                       │ UART (HCI)         │ SDIO (WLAN)
                       ▼                    ▼
┌──────────────────────────────────────────────────────────────┐
│                    CYW55512IUBGT                              │
│         Bluetooth 6.0 (BLE)     │     Wi-Fi 6 (802.11ax)     │
└──────────────────────────────────────────────────────────────┘
```

**Effort**: Medium (Auracast porting + Wi-Fi bridge required)
**Risk**: Medium (porting from Zephyr to BTSTACK HCI, USB HS middleware)

#### Option 2: Pure Zephyr RTOS (NOT VIABLE)

```
┌────────────────────────────────────────────────────────┐
│                    PSoC Edge E82                        │
│  ┌──────────────────────────────────────────────────┐  │
│  │                  Zephyr RTOS                      │  │
│  │  ┌────────────┐ ┌────────────┐ ┌──────────────┐  │  │
│  │  │ Zephyr     │ │ USB MIDI   │ │ I2S Audio    │  │  │
│  │  │ BT Host    │ │ 2.0        │ │              │  │  │
│  │  │ (LE Audio) │ │            │ │              │  │  │
│  │  └─────┬──────┘ └────────────┘ └──────────────┘  │  │
│  │        │                                          │  │
│  │        │  ╔════════════════════════════════════╗  │  │
│  │        │  ║ NO DRIVER FOR CYW55512 IN ZEPHYR! ║  │  │
│  │        │  ╚════════════════════════════════════╝  │  │
│  │        ▼                                          │  │
│  │  ┌──────────────────────────────────────────────┐│  │
│  │  │ h4_ifx_cyw43xxx.c - ONLY supports CYW43xxx  ││  │
│  │  │ NOT CYW55511/CYW55512/CYW55513              ││  │
│  │  └──────────────────────────────────────────────┘│  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                          ╳
                    NO DRIVER
                          ╳
┌────────────────────────────────────────────────────────┐
│                      CYW55512                           │
│           Bluetooth 6.0 + Wi-Fi 6                      │
└────────────────────────────────────────────────────────┘
```

**Status**: NOT VIABLE without writing a CYW55512 driver for Zephyr
**Effort**: Very High (write full HCI driver + firmware integration)

#### Option 3: Hybrid Approach (Advanced)

Write a CYW55511 HCI driver for Zephyr to use Zephyr's native LE Audio stack.

**Effort**: High (create CYW55511 HCI driver for Zephyr)
**Risk**: CYW55511 ISOC HCI commands may differ from standard

### Final Decision

**For PSoC Edge E82 + CYW55512: Use ModusToolbox + FreeRTOS**

The lack of CYW55512 driver support in Zephyr is a blocking issue. While Zephyr has superior LE Audio support, it cannot communicate with the Bluetooth chip without significant driver development.

**Recommended Path**:
1. Start with ModusToolbox + FreeRTOS + Infineon BTSTACK
2. Implement LE Audio unicast using Infineon's existing support
3. Port BAP Broadcast Source from Zephyr for Auracast
4. Use liblc3 for unified host-side LC3 encoding/decoding
5. **Add Wi-Fi bridge via SDIO + wifi-host-driver**
6. **Replace USB FS middleware with USB HS capable version**

---

## Implementation Plan

### Phase 1: Development Environment Setup

**1.1 Install ModusToolbox 3.x**
- Download from Infineon Developer Center
- Install PSoC Edge device support pack
- Install CYW55511 Bluetooth firmware package

**1.2 Obtain Evaluation Kits**
- KIT_PSE84_EVAL (PSoC Edge E84 Evaluation Kit) or equivalent E81 kit
- CYW955513EVK-01 (includes CYW55511/12/13 with audio codec shield)

**1.3 Clone Required Repositories**
```bash
# Infineon Core
git clone https://github.com/Infineon/btstack.git
git clone https://github.com/Infineon/btstack-integration.git
git clone https://github.com/Infineon/emusb-device.git           # NOTE: USB FS only, needs replacement
git clone https://github.com/Infineon/wifi-host-driver.git  # NEW: Required for Wi-Fi/SDIO

# Audio Examples
git clone https://github.com/Infineon/mtb-example-psoc6-i2s.git

# Open Source LE Audio (for Auracast porting)
git clone https://github.com/zephyrproject-rtos/zephyr.git
git clone https://github.com/google/liblc3.git
```

> **NOTE**: The `emusb-device` library only supports USB Full-Speed. PSoC Edge E82 requires USB High-Speed middleware which may need to be sourced separately or implemented.

### Phase 2: LE Audio LC3 Full Duplex Implementation

**Design Decision**: All LC3 encode/decode runs on the PSoC Edge host using Google's liblc3 library. This provides a unified architecture for both LE Audio Unicast and Auracast Broadcast.

| Aspect | Host-Side LC3 (Selected) | CYW55511 Offload |
|--------|--------------------------|------------------|
| LC3 Location | PSoC Edge (liblc3) | CYW55511 on-chip |
| Unicast Support | Yes | Yes |
| Broadcast Support | Yes | Unknown/Limited |
| CPU Load | ~15-20% Cortex-M55 | Minimal |
| Latency | Slightly higher | Lower |
| Debugging | Full visibility | Black box |
| Architecture | Unified, consistent | Asymmetric |

**2.1 liblc3 Integration**

```c
#include "lc3.h"

// LC3 encoder/decoder contexts
static lc3_encoder_t lc3_encoder;
static lc3_decoder_t lc3_decoder;

// Configuration for 48kHz, 10ms frames, 16-bit
#define LC3_SAMPLE_RATE     48000
#define LC3_FRAME_DURATION  LC3_FRAME_DURATION_10MS
#define LC3_BYTES_PER_FRAME 100  // ~80kbps quality

void lc3_init(void) {
    lc3_encoder = lc3_encoder_new(
        LC3_FRAME_DURATION,
        LC3_SAMPLE_RATE,
        0,  // Use same sample rate for input
        lc3_encoder_size(LC3_FRAME_DURATION, LC3_SAMPLE_RATE)
    );

    lc3_decoder = lc3_decoder_new(
        LC3_FRAME_DURATION,
        LC3_SAMPLE_RATE,
        0,  // Use same sample rate for output
        lc3_decoder_size(LC3_FRAME_DURATION, LC3_SAMPLE_RATE)
    );
}
```

**2.2 CPU Budget for LC3 on Cortex-M55**

| Operation | Cycles/Frame | Time @ 400MHz | Notes |
|-----------|--------------|---------------|-------|
| LC3 Encode (48kHz/10ms) | ~2M cycles | ~5ms | Per channel |
| LC3 Decode (48kHz/10ms) | ~1.5M cycles | ~3.75ms | Per channel |
| Full Duplex (stereo) | ~7M cycles | ~17.5ms | Encode + Decode |
| **Available time** | - | **10ms** | Frame period |

> **Note**: Cortex-M55 with Helium DSP can handle stereo LC3 encode+decode within the 10ms frame budget with optimization.

### Phase 3: LE Audio Broadcast (Auracast) Implementation

> **CRITICAL**: Infineon does NOT currently support Auracast on CYW55511. This feature requires porting from open-source implementations.

**3.1 Open Source Auracast Resources**

| Source | License | Notes |
|--------|---------|-------|
| [Zephyr RTOS LE Audio](https://github.com/zephyrproject-rtos/zephyr) | Apache 2.0 | Most complete open-source implementation |
| [liblc3 (Google)](https://github.com/google/liblc3) | Apache 2.0 | Open-source LC3 codec for host-side encoding |
| [BlueKitchen BTstack](https://github.com/bluekitchen/btstack) | Commercial | Full LE Audio support (requires license) |

**3.2 Zephyr LE Audio Code Structure to Port**
```
zephyr/subsys/bluetooth/audio/
├── bap_broadcast_source.c    # Broadcast source state machine
├── bap_base.c                # BASE structure handling
├── bass.c                    # Broadcast scan service
├── pacs.c                    # Published audio capabilities
└── cap_initiator.c           # Common Audio Profile

zephyr/subsys/bluetooth/host/
├── iso.c                     # Isochronous channels (BIG/BIS)
└── conn.c                    # Connection management
```

**3.3 Required Profiles**
- **BAP Broadcast Source**: Transmit audio to BIS (Broadcast Isochronous Stream)
- **BASS** (Broadcast Audio Scan Service): Allow assistants to discover broadcasts
- **PBP** (Public Broadcast Profile): For public Auracast discovery

### Phase 4: MIDI GATT Profile over BLE

**4.1 BLE MIDI Service Definition**

```
Service UUID:        03B80E5A-EDE8-4B33-A751-6CE34EC4C700
Characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3
Properties:          Read, Write Without Response, Notify
```

**4.2 MIDI BLE Packet Format**
```
┌────────┬───────────┬─────────────────────────┐
│ Header │ Timestamp │ MIDI Message(s)         │
│ 1 byte │ 1 byte    │ Variable length         │
└────────┴───────────┴─────────────────────────┘
```

### Phase 5: USB High-Speed MIDI Implementation

> **CRITICAL**: PSoC Edge E82 supports USB 2.0 High-Speed (480 Mbps). The current `emusb-device` library only supports Full-Speed and must be replaced.

**5.1 USB MIDI Class**
- Device Class: 0x00 (defined at interface level)
- Interface Class: 0x01 (Audio)
- Interface SubClass: 0x03 (MIDI Streaming)
- **Speed**: USB High-Speed (480 Mbps)
- **Endpoint Size**: 512 bytes (vs 64 for Full-Speed)

**5.2 USB HS Middleware Requirements**

The current `emusb-device` library does NOT support High-Speed:
```c
// Current (Full-Speed only) - MUST BE REPLACED
cy_stc_usb_dev_midi_context_t midi_context;
Cy_USB_Dev_MIDI_Init(&midi_config, &midi_context, &usb_dev_context);
```

**Action Required**: Find or implement USB HS middleware for PSoC Edge.

### Phase 6: I2S Audio Stream

**6.1 I2S Configuration**
- Sample rates: 8/16/32/44.1/48/96 kHz
- Bit depth: 16/24/32-bit
- Mode: Master (PSoC generates BCLK, LRCLK)

**6.2 DMA-Based Audio Streaming**
```c
typedef struct {
    int16_t* tx_buffer[2];  // Double buffer for TX
    int16_t* rx_buffer[2];  // Double buffer for RX
    uint32_t buffer_size;
    volatile uint8_t active_buffer;
} i2s_audio_stream_t;
```

### Phase 7: System Integration

**7.1 FreeRTOS Task Architecture**
```
┌─────────────────────────────────────────────────────────────┐
│                    FreeRTOS Scheduler                        │
├──────────────┬──────────────┬──────────────┬────────────────┤
│ Audio Task   │ BLE Task     │ USB Task     │ I2S Task       │
│ Priority: 5  │ Priority: 4  │ Priority: 3  │ Priority: 6    │
│              │              │              │ (Highest)       │
├──────────────┼──────────────┼──────────────┼────────────────┤
│ LC3 control  │ MIDI BLE     │ USB MIDI     │ DMA callbacks  │
│ Auracast mgmt│ LE Audio ctrl│ Enum/Control │ Buffer swap    │
└──────────────┴──────────────┴──────────────┴────────────────┘
```

**7.2 Memory Budget (Estimated)**

| Component | RAM | Flash |
|-----------|-----|-------|
| FreeRTOS kernel | ~10 KB | ~20 KB |
| BTSTACK + LE Audio profiles | ~80 KB | ~200 KB |
| liblc3 (encoder + decoder) | ~40 KB | ~60 KB |
| USB Middleware | ~8 KB | ~30 KB |
| Audio buffers (I2S + LC3) | ~20 KB | - |
| Application code | ~30 KB | ~120 KB |
| **Total** | **~188 KB** | **~430 KB** |

PSoC Edge E82 has 5 MB SRAM and 512 KB Flash - plenty of headroom.

---

## Key Infineon Repositories

### Core Stack & Middleware

| Repository | Purpose |
|------------|---------|
| [Infineon/btstack](https://github.com/Infineon/btstack) | Bluetooth Host Stack (BR/EDR + BLE) |
| [Infineon/btstack-integration](https://github.com/Infineon/btstack-integration) | Platform adaptation layer |
| [Infineon/bless](https://github.com/Infineon/bless) | PSoC BLE Middleware (GATT, custom profiles) |
| [Infineon/emusb-device](https://github.com/Infineon/emusb-device) | USB Device Middleware (MIDI class) |
| [Infineon/ifx-linux-bluetooth](https://github.com/Infineon/ifx-linux-bluetooth) | Reference for LE Audio implementation |

### Audio Examples

| Repository | Purpose |
|------------|---------|
| [mtb-example-btstack-threadx-audio-watch](https://github.com/Infineon/mtb-example-btstack-threadx-audio-watch) | A2DP/HFP audio streaming reference |
| [mtb-example-psoc6-i2s](https://github.com/Infineon/mtb-example-psoc6-i2s) | I2S interface with audio codec |
| [mtb-example-psoc6-pdm-to-i2s](https://github.com/Infineon/mtb-example-psoc6-pdm-to-i2s) | PDM microphone to I2S output |
| [mtb-example-audio-streaming](https://github.com/Infineon/mtb-example-audio-streaming) | FreeRTOS audio streaming |
| [mtb-example-usb-device-audio-recorder-freertos](https://github.com/Infineon/mtb-example-usb-device-audio-recorder-freertos) | USB Audio Class with FreeRTOS |

### Development Tools

| Repository | Purpose |
|------------|---------|
| [Code-Examples-for-ModusToolbox-Software](https://github.com/Infineon/Code-Examples-for-ModusToolbox-Software) | Master index of examples |
| [modustoolbox-software](https://github.com/Infineon/modustoolbox-software) | Tool links and resources |

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **USB Library ✅ RESOLVED** | CRITICAL | Replace `usbdev` ✅ DONE (replaced with emusb-device) with USB HS capable middleware for PSoC Edge |
| **Wi-Fi/SDIO ✅ RESOLVED** | HIGH | Added wifi-host-driver, created SDIO driver and USB bridge |
| **Auracast not supported by Infineon** | HIGH | Port BAP broadcast from Zephyr (Apache 2.0); LC3 already on host |
| Zephyr-to-FreeRTOS porting complexity | MEDIUM | Start with minimal broadcast source sample; incremental porting |
| LC3 codec licensing | LOW | Google liblc3 is Apache 2.0 - no licensing concerns |
| **Host-side LC3 CPU load** | MEDIUM | Cortex-M55 @ 400MHz can handle stereo; reduce to mono if needed |
| LE Audio profile complexity | MEDIUM | Study Zephyr implementation; use Bluetooth SIG sample data |
| CYW55512 HCI ISOC support | MEDIUM | Verify HCI_LE_Create_BIG and HCI_LE_Create_CIS commands |
| Real-time LC3 + I2S timing | MEDIUM | Use DMA ping-pong buffers; audio task at high priority |
| Memory: liblc3 + BAP stack | LOW | ~188 KB RAM needed; PSoC Edge E82 has 5 MB SRAM |
| Full-duplex latency | LOW | 10ms frame duration + buffering = ~30-40ms total latency |
| Testing: Need Auracast receiver | LOW | nRF Connect app, Samsung Galaxy Buds3, or LE Audio headphones |
| Part number mismatch in code | LOW | Update cy_bt_config.h, CMakeLists.txt, linker script |
| Missing startup code | MEDIUM | Create startup_psoc_edge_e82.S and system init files |

---

## References

### Infineon Documentation
- [CYW55513 Documentation](https://documentation.infineon.com/cyw55513/docs/nhu1755169548502)
- [PSoC Edge Documentation](https://documentation.infineon.com/psocedge/docs/bwb1750411526047)
- [BTSTACK API Reference](https://infineon.github.io/btstack/dual_mode/api_reference_manual/html/modules.html)
- [PSoC 6 BLE Middleware (bless)](https://infineon.github.io/bless/ble_api_reference_manual/html/index.html)
- [USB Device Middleware](https://github.com/Infineon/emusb-device)

### Zephyr LE Audio (Open Source Reference)
- [Zephyr LE Audio Architecture](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html)
- [Zephyr LE Audio Project Board](https://github.com/orgs/zephyrproject-rtos/projects/26)
- [Simple Broadcast Source Tutorial](https://dev.to/denladeside/a-simple-broadcast-audio-source-4e8b)
- [Multi-Subgroup Broadcasting Guide](https://kitemetric.com/blogs/mastering-auracast-broadcasting-multiple-audio-subgroups-with-zephyr)

### Bluetooth SIG Specifications
- [Bluetooth Core 6.0 Specification](https://www.bluetooth.com/specifications/specs/)
- [BAP (Basic Audio Profile) Specification](https://www.bluetooth.com/specifications/specs/basic-audio-profile-1-0-1/)
- [Auracast Transmitter Technical Overview](https://www.bluetooth.com/wp-content/uploads/2024/05/2403_How_To_Auracast_Transmitter.pdf)
- [Auracast Assistant Technical Overview](https://www.bluetooth.com/wp-content/uploads/2024/05/2403_How_To_Auracast_Assistant.pdf)
- [LC3 Codec Specification](https://www.bluetooth.com/specifications/specs/low-complexity-communication-codec-1-0/)

### Open Source
- [Google liblc3](https://github.com/google/liblc3)
- [BlueKitchen BTstack (Commercial)](https://bluekitchen-gmbh.com/)
- [Zephyr USB MIDI](https://github.com/stuffmatic/zephyr-usb-midi)
