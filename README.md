# Infineon LE Audio Demo

PSoC Edge E82 + CYW55512 demo: Full-duplex LE Audio (LC3), Auracast broadcast, BLE/USB MIDI, Wi-Fi bridge, and I2S streaming for musical instruments.

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-PSoC%20Edge%20E82-green.svg)](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm/psoc-edge-e82)
[![Bluetooth](https://img.shields.io/badge/Bluetooth-6.0%20LE%20Audio-blue.svg)](https://www.bluetooth.com/learn-about-bluetooth/recent-enhancements/le-audio/)

## Overview

This project implements a comprehensive Bluetooth LE Audio solution for musical instruments on Infineon hardware. It provides a complete firmware implementation with full-duplex audio streaming, Auracast broadcast, MIDI over BLE/USB, and Wi-Fi data bridging.

### Target Application

A musical instrument (synthesizer, digital piano, guitar processor, etc.) that needs to:
- Stream high-quality audio wirelessly to LE Audio headphones/speakers
- Broadcast audio to multiple receivers simultaneously (Auracast)
- Send/receive MIDI over Bluetooth LE and USB
- Interface with a main application controller via I2S
- Bridge network data over Wi-Fi

### Key Features

| Feature | Core | Description | Status |
|---------|------|-------------|--------|
| **LE Audio Unicast** | CM33+CM55 | Full-duplex audio streaming via CIS (Connected Isochronous Stream) | Implemented |
| **LE Audio Broadcast Source** | CM33+CM55 | One-to-many audio broadcast via BIS (Auracast TX) | Implemented |
| **LE Audio Broadcast Sink** | CM33+CM55 | Receive Auracast broadcasts (Auracast RX) | Implemented |
| **LC3 Codec** | CM55 | Host-side LC3 encode/decode using Google liblc3 (Helium DSP) | Implemented |
| **BLE MIDI** | CM33 | MIDI over Bluetooth Low Energy GATT service | Implemented |
| **USB MIDI** | CM33 | USB High-Speed MIDI class device (480 Mbps) | Implemented |
| **I2S Streaming** | CM55 | DMA-based bidirectional audio with ping-pong buffers | Implemented |
| **Wi-Fi Bridge** | CM33 | USB HS to SDIO to CYW55512 WLAN data path | Implemented |
| **USB CDC/ACM** | CM33 | AT command interface for BT/Wi-Fi/LE Audio configuration | Implemented |

## Hardware

### Target Hardware

| Component | Part Number | Description |
|-----------|-------------|-------------|
| **MCU** | PSE823GOS4DBZQ3 | PSoC Edge E82, Cortex-M55 @ 400MHz + Cortex-M33 |
| **Wireless** | CYW55512IUBGT | AIROC Wi-Fi 6 + Bluetooth 6.0 combo IC |
| **Eval Kit (MCU)** | [KIT_PSE84_EVAL_EPC2](https://github.com/Infineon/TARGET_KIT_PSE84_EVAL_EPC2) | PSoC Edge E84 Evaluation Kit (USB HS, SDIO) |
| **Eval Kit (Wireless)** | [CYW955513EVK-01](https://github.com/Infineon/TARGET_CYW955513EVK-01) | CYW55513 Bluetooth/Wi-Fi Evaluation Kit |

### Hardware Architecture

```
+-----------------------------------------------------------------------------+
|                        Main Application Processor                            |
|                    (External Instrument / Host Device)                       |
+-------------------------------------+---------------------------------------+
                                      | USB High-Speed (480 Mbps)
                                      |   - USB MIDI Class
                                      |   - CDC/ACM (AT commands)
                                      |   - Wi-Fi Data (bridged)
                                      v
+-----------------------------------------------------------------------------+
|                      PSoC Edge E82 (PSE823GOS4DBZQ3)                        |
|                     Cortex-M55 @ 400MHz + Cortex-M33                        |
|                                                                              |
|  +--------------+  +--------------+  +--------------+  +------------------+ |
|  |   USB HS     |  |    I2S       |  |   UART       |  |     SDIO 3.0     | |
|  |  (480Mbps)   |  |   Master     |  |   (HCI)      |  |   (SDR50/DDR50)  | |
|  | MIDI+CDC+Data|  |   Audio      |  |   BT Host    |  |   WLAN Host      | |
|  +------+-------+  +------+-------+  +------+-------+  +--------+---------+ |
|         |                 |                 |                    |           |
|  +------v-------+  +------v-------+  +------v-------+  +--------v---------+ |
|  | emUSB-Device |  |  Audio DMA   |  |  BTSTACK +   |  |  wifi-host-drv   | |
|  | MIDI + CDC   |  |   Buffer     |  |  LE Audio    |  |  Wi-Fi Bridge    | |
|  | + AT Parser  |  |  (Ping-pong) |  |  Profiles    |  |                  | |
|  +--------------+  +------+-------+  +------^-------+  +------------------+ |
|                           |                 |                                |
|                    +------v-----------------+------+                        |
|                    |         liblc3 (Host-Side)    |                        |
|                    |    LC3 Encode/Decode on PSoC  |                        |
|                    |  Unified for Unicast+Broadcast|                        |
|                    +-------------------------------+                        |
|                                                                              |
|  FreeRTOS (Tasks: Audio/LC3, BLE, USB/CDC, Wi-Fi, MIDI)                     |
+------------------------------+----------------------+-----------------------+
                               |                      |
                               | UART (HCI+ISOC)      | SDIO (Wi-Fi Data)
                               | 3 Mbps               | Up to 208 MHz
                               v                      v
+-----------------------------------------------------------------------------+
|                          CYW55512IUBGT                                       |
|  +----------------------------------+  +----------------------------------+ |
|  |    Bluetooth 6.0 LE Controller   |  |       Wi-Fi 6 (802.11ax)         | |
|  |   ISOC Transport (BIS/CIS)       |  |    2.4 GHz + 5 GHz Dual-Band     | |
|  |   LC3 codec NOT used (HCI mode)  |  |    Up to 1.2 Gbps PHY Rate       | |
|  +----------------------------------+  +----------------------------------+ |
|                                                                              |
|  Handles: BLE advertising, scanning, connections, ISOC, Wi-Fi association   |
+-----------------------------------------------------------------------------+
```

### Audio Data Flow

```
TRANSMIT PATH (I2S RX -> LC3 Encode -> ISOC TX):
+--------------+    +--------------+    +--------------+    +--------------+
| Main Ctrl    |--->| PSoC Edge    |--->| liblc3       |--->| CYW55512     |
| I2S PCM      |    | I2S RX DMA   |    | LC3 Encode   |    | HCI ISOC TX  |
| 48kHz/16bit  |    | Audio Buffer |    | (host CPU)   |    | BLE Radio    |
+--------------+    +--------------+    +--------------+    +--------------+

RECEIVE PATH (ISOC RX -> LC3 Decode -> I2S TX):
+--------------+    +--------------+    +--------------+    +--------------+
| CYW55512     |--->| liblc3       |--->| PSoC Edge    |--->| Main Ctrl    |
| HCI ISOC RX  |    | LC3 Decode   |    | I2S TX DMA   |    | I2S PCM      |
| BLE Radio    |    | (host CPU)   |    | Audio Buffer |    | 48kHz/16bit  |
+--------------+    +--------------+    +--------------+    +--------------+
```

## Software

### Build Prerequisites

The following tools must be installed to build this project:

| Tool | Version | Purpose | Installation |
|------|---------|---------|--------------|
| **ModusToolbox** | 3.x | Infineon HAL/PDL, device support | [Download](https://softwaretools.infineon.com/tools/com.ifx.tb.tool.modustoolboxsetup) |
| **CMake** | 3.20+ | Build system | `winget install Kitware.CMake` |
| **Ninja** | 1.10+ | Build tool | `winget install Ninja-build.Ninja` |
| **ARM GNU Toolchain** | 14.x | arm-none-eabi-gcc cross-compiler | `winget install Arm.GnuArmEmbeddedToolchain` |
| **Git** | 2.x | Version control, submodules | `winget install Git.Git` |

#### Windows Installation (PowerShell as Administrator)

```powershell
# Install build tools via winget
winget install Kitware.CMake --accept-package-agreements
winget install Ninja-build.Ninja --accept-package-agreements
winget install Arm.GnuArmEmbeddedToolchain --accept-package-agreements
winget install Git.Git --accept-package-agreements

# Restart your terminal to refresh PATH

# Verify installations
cmake --version
ninja --version
arm-none-eabi-gcc --version
git --version
```

#### ModusToolbox Installation

ModusToolbox must be installed separately via the GUI installer:

1. Download from [Infineon Developer Center](https://softwaretools.infineon.com/tools/com.ifx.tb.tool.modustoolboxsetup)
2. Run the installer and select "ModusToolbox 3.x"
3. Install the **PSoC Edge Device Pack** when prompted
4. Default installation path: `C:\Users\<username>\ModusToolbox`

ModusToolbox provides:
- Infineon HAL (Hardware Abstraction Layer)
- PDL (Peripheral Driver Library)
- RTOS Abstraction (`cyabs_rtos.h`)
- Device configuration tools

### Runtime Requirements

| Component | Version | Purpose |
|-----------|---------|---------|
| PSoC Edge Device Pack | Latest | PSoC Edge E82/E84 support |
| CYW55512 Firmware | Latest | Bluetooth controller firmware |
| FreeRTOS | 10.x | Real-time operating system |

### Dependencies (Git Submodules)

#### Board Support Packages (BSPs)

| Library | License | Purpose |
|---------|---------|---------|
| [TARGET_KIT_PSE84_EVAL_EPC2](https://github.com/Infineon/TARGET_KIT_PSE84_EVAL_EPC2) | Apache 2.0 | PSoC Edge E84 Evaluation Kit BSP |
| [TARGET_CYW955513EVK-01](https://github.com/Infineon/TARGET_CYW955513EVK-01) | Apache 2.0 | CYW55513 Bluetooth/Wi-Fi EVK BSP |

#### HAL/PDL Libraries

| Library | License | Purpose |
|---------|---------|---------|
| [mtb-hal-cat1](https://github.com/Infineon/mtb-hal-cat1) | Apache 2.0 | Hardware Abstraction Layer for CAT1 devices |
| [mtb-pdl-cat1](https://github.com/Infineon/mtb-pdl-cat1) | Apache 2.0 | Peripheral Driver Library for CAT1 devices |
| [abstraction-rtos](https://github.com/Infineon/abstraction-rtos) | Apache 2.0 | RTOS abstraction layer |
| [core-lib](https://github.com/Infineon/core-lib) | Apache 2.0 | Infineon core library |

#### Middleware Libraries

| Library | License | Purpose |
|---------|---------|---------|
| [liblc3](https://github.com/google/liblc3) | Apache 2.0 | LC3 codec encode/decode |
| [btstack](https://github.com/Infineon/btstack) | Infineon | Bluetooth host stack |
| [btstack-integration](https://github.com/Infineon/btstack-integration) | Infineon | HCI UART porting layer |
| [freertos](https://github.com/Infineon/freertos) | MIT | Real-time operating system |
| [emusb-device](https://github.com/Infineon/emusb-device) | Infineon | USB High-Speed middleware |
| [wifi-host-driver](https://github.com/Infineon/wifi-host-driver) | Infineon | Wi-Fi Host Driver (WHD) |

### Project Structure

```
infineon-le-audio/
├── README.md                       # This file
├── LICENSE                         # Apache 2.0 license
├── CMakeLists.txt                  # CMake build system
├── .gitmodules                     # Git submodule definitions
│
├── cmake/
│   ├── arm-cortex-m55.cmake        # ARM toolchain file
│   └── psoc_edge_e82.ld            # Linker script (5 MB SRAM)
│
├── config/
│   ├── FreeRTOSConfig.h            # FreeRTOS configuration
│   ├── cy_bt_config.h              # Bluetooth configuration
│   └── lc3_config.h                # LC3 codec parameters
│
├── docs/
│   ├── README.md                   # Full project plan and gap analysis
│   └── architecture.md             # Detailed design documentation
│
├── source/
│   ├── main.c                      # Application entry, FreeRTOS tasks
│   │
│   ├── audio/                      # Audio subsystem
│   │   ├── audio_task.c/h          # Main audio processing task
│   │   ├── audio_buffers.c/h       # Ring buffer management
│   │   ├── i2s_stream.c/h          # I2S DMA driver (ping-pong)
│   │   └── lc3_wrapper.c/h         # liblc3 encode/decode API
│   │
│   ├── le_audio/                   # LE Audio profiles
│   │   ├── le_audio_manager.c/h    # Top-level LE Audio control
│   │   ├── bap_unicast.c/h         # BAP Unicast Client/Server (CIS)
│   │   ├── bap_broadcast.c/h       # BAP Broadcast Source (Auracast TX)
│   │   ├── bap_broadcast_sink.c/h  # BAP Broadcast Sink (Auracast RX)
│   │   ├── pacs.c/h                # Published Audio Capabilities Service
│   │   └── isoc_handler.c/h        # HCI ISOC data path handling
│   │
│   ├── midi/                       # MIDI subsystem
│   │   ├── midi_ble_service.c/h    # BLE MIDI GATT service
│   │   ├── midi_usb.c/h            # USB MIDI class (emUSB-Device)
│   │   └── midi_router.c/h         # MIDI routing (BLE <-> USB <-> Controller)
│   │
│   ├── wifi/                       # Wi-Fi data bridge
│   │   └── wifi_bridge.c/h         # USB-to-Wi-Fi bridge (WHD + cyhal_sdio)
│   │
│   ├── usb/                        # USB composite device
│   │   └── usb_composite.c/h       # MIDI + CDC composite device
│   │
│   ├── cdc/                        # AT command interface
│   │   ├── cdc_acm.c/h             # USB CDC/ACM virtual serial port
│   │   ├── at_parser.c/h           # AT command parser
│   │   ├── at_commands.h           # CME error codes
│   │   ├── at_system_cmds.c/h      # System commands (AT, ATI, VERSION)
│   │   ├── at_bt_cmds.c/h          # Bluetooth commands
│   │   ├── at_leaudio_cmds.c/h     # LE Audio commands
│   │   └── at_wifi_cmds.c/h        # Wi-Fi commands
│   │
│   └── bluetooth/                  # Bluetooth stack integration
│       ├── bt_init.c/h             # BTSTACK initialization
│       ├── bt_platform_config.c/h  # HCI UART configuration
│       ├── hci_isoc.c/h            # HCI isochronous commands
│       ├── gap_config.c/h          # GAP advertising/scanning/connections
│       └── gatt_db.c/h             # GATT database
│
└── libs/                           # External libraries (submodules)
    ├── TARGET_KIT_PSE84_EVAL_EPC2/ # PSoC Edge E84 BSP
    ├── TARGET_CYW955513EVK-01/     # CYW55513 EVK BSP
    ├── mtb-hal-cat1/               # Hardware Abstraction Layer
    ├── mtb-pdl-cat1/               # Peripheral Driver Library
    ├── abstraction-rtos/           # RTOS abstraction
    ├── core-lib/                   # Infineon core library
    ├── btstack/                    # Infineon BTSTACK
    ├── btstack-integration/        # HCI UART porting layer
    ├── liblc3/                     # Google LC3 codec
    ├── freertos/                   # FreeRTOS kernel
    ├── emusb-device/               # Infineon USB middleware
    └── wifi-host-driver/           # Infineon WHD
```

## Getting Started

### 1. Install Prerequisites

Follow the [Build Prerequisites](#build-prerequisites) section to install all required tools.

### 2. Clone the Repository

```bash
git clone --recursive https://github.com/cotigac/infineon-le-audio.git
cd infineon-le-audio
```

If you already cloned without `--recursive`:
```bash
git submodule update --init --recursive
```

### 3. Build with ModusToolbox (Recommended for EVKs)

The recommended build method for KIT_PSE84_EVAL and CYW955513EVK-01 evaluation kits uses the **ModusToolbox modus-shell terminal**.

#### Project Structure

This repository includes a ModusToolbox project in the `mtb/` folder:

```
infineon-le-audio/
├── mtb/
│   ├── le-audio/              # ModusToolbox application
│   │   ├── proj_cm33_s/       # CM33 Secure core project
│   │   ├── proj_cm33_ns/      # CM33 Non-Secure core project
│   │   ├── proj_cm55/         # CM55 core project
│   │   ├── bsps/              # Board Support Package
│   │   ├── configs/           # Device configuration
│   │   └── Makefile           # Top-level makefile
│   └── mtb_shared/            # Shared library dependencies
├── source/                    # Custom LE Audio source code
├── libs/                      # Git submodules (BSPs, HAL, etc.)
└── ...
```

#### Step 1: Open ModusToolbox modus-shell

```
C:\Users\<username>\ModusToolbox\tools_3.7\modus-shell\Cygwin.bat
```

#### Step 2: Navigate to Project and Build

```bash
# Navigate to the ModusToolbox project
cd /cygdrive/c/Users/<username>/source/repos/infineon-le-audio/mtb/le-audio

# Fetch all library dependencies (first time only)
make getlibs

# Build the project
make build
```

#### Successful Build Output

A successful build produces firmware for all three cores (CM33 Secure, CM33 Non-Secure, CM55):

```
==============================================================================
= Build complete =
==============================================================================
```

**Memory Usage Summary:**

| Core | Region | Used | Available | Utilization |
|------|--------|------|-----------|-------------|
| **CM33 Secure** | SRAM (code) | 1.5 KB | 217 KB | 1% |
| **CM33 Secure** | SRAM (data) | 133 KB | 135 KB | 98% |
| **CM33 Non-Secure** | SRAM (code) | 13.4 KB | 414 KB | 3% |
| **CM33 Non-Secure** | SRAM (data) | 258 KB | 262 KB | 98% |
| **CM55** | ITCM (code) | 12 KB | 256 KB | 5% |
| **CM55** | DTCM (data) | 32 KB | 256 KB | 12% |
| **CM55** | SOCMEM (heap) | 2.87 MB | 2.87 MB | 100% |
| **Shared** | m33_m55_shared | 256 KB | 256 KB | 100% |

**Flash Usage (SMIF0MEM1 External QSPI):**

| Image | Size | Description |
|-------|------|-------------|
| CM33 Secure | 27 KB | Secure services, TrustZone config |
| CM33 Non-Secure | 369 KB | BLE stack, USB, Wi-Fi, MIDI, AT commands |
| CM55 | 143 KB | LC3 codec, I2S streaming, audio DSP |
| **Total** | **539 KB** | Combined firmware (16 MB available) |

**Output Files (in `mtb/le-audio/`):**

| File | Description |
|------|-------------|
| `build/project_hex/proj_cm33_s_signed.hex` | Signed secure image |
| `build/project_hex/proj_cm33_ns_shifted.hex` | Non-secure image (relocated) |
| `build/project_hex/` (merged) | Combined image for flashing |

#### Step 4: Flash to Hardware

```bash
make program
```

### Available PSoC Edge Examples

These examples are available for KIT_PSE84_EVAL_EPC2 and can serve as starting points:

| Example | Description |
|---------|-------------|
| `mtb-example-psoc-edge-btstack-isoc-central` | BLE Isochronous Central (LE Audio) |
| `mtb-example-psoc-edge-btstack-isoc-peripheral` | BLE Isochronous Peripheral (LE Audio) |
| `mtb-example-psoc-edge-i2s` | I2S Audio Streaming |
| `mtb-example-psoc-edge-pdm-to-i2s` | PDM Microphone to I2S |
| `mtb-example-psoc-edge-usb-device-audio-playback` | USB Audio Playback |
| `mtb-example-psoc-edge-usb-device-audio-recorder` | USB Audio Recorder |

List all available examples:
```bash
"C:\Users\<username>\ModusToolbox\tools_3.7\project-creator\project-creator-cli.exe" --list-apps KIT_PSE84_EVAL_EPC2
```

---

### Alternative: Build with CMake (Standalone)

For standalone CMake builds without ModusToolbox's make system:

#### Windows (Git Bash or PowerShell)

```bash
# Set up environment (adjust paths if needed)
export PATH="/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin:$PATH"
export PATH="/c/Program Files/CMake/bin:$PATH"

# Configure and build
mkdir build && cd build
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-cortex-m55.cmake ..
cmake --build .
```

#### Linux/macOS

```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-cortex-m55.cmake ..
cmake --build .
```

> **Note:** The CMake build requires the BSPs and HAL/PDL libraries. The `cycfg_*.h` configuration headers are generated by ModusToolbox's Device Configurator tool from the `design.modus` files in the BSP.

### CMake Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_LE_AUDIO` | ON | Enable LE Audio (BAP, PACS, ASCS) |
| `ENABLE_AURACAST` | ON | Enable Auracast Broadcast (BIS) |
| `ENABLE_MIDI` | ON | Enable MIDI over BLE and USB |
| `ENABLE_USB` | ON | Enable USB Device support |
| `ENABLE_WIFI` | ON | Enable Wi-Fi data bridge |
| `CMAKE_BUILD_TYPE` | Debug | Debug/Release/RelWithDebInfo/MinSizeRel |

### 4. Hardware Setup

1. Connect PSoC Edge E84 eval kit (KIT_PSE84_EVAL) to PC via USB (for programming/debug)
2. Connect CYW55512 module via UART (HCI interface for Bluetooth)
3. Connect CYW55512 module via SDIO (for Wi-Fi data)
4. Connect main application controller via USB High-Speed (MIDI + Wi-Fi bridge)
5. Connect main application controller via I2S (audio streaming)
6. Flash the firmware using ModusToolbox or OpenOCD

## Configuration

### LC3 Codec Settings

Edit `config/lc3_config.h`:

```c
#define LC3_CFG_SAMPLE_RATE         48000   // Sample rate in Hz
#define LC3_CFG_FRAME_DURATION      1       // 0=7.5ms, 1=10ms
#define LC3_CFG_OCTETS_PER_FRAME    100     // Bitrate (100 = 80kbps)
#define LC3_CFG_CHANNELS            1       // 1=mono, 2=stereo
```

### Auracast Broadcast Source Settings

```c
le_audio_broadcast_config_t config = {
    .broadcast_name = "My Instrument",
    .audio_context = LE_AUDIO_CONTEXT_MEDIA,
    .encrypted = false,
    .num_subgroups = 1,
    .num_bis_per_subgroup = 1,
    .presentation_delay_us = 40000
};

le_audio_start_broadcast(&config);
```

### Auracast Broadcast Sink (Receive)

```c
#include "le_audio/le_audio_manager.h"

// Start scanning for Auracast broadcasts
le_audio_broadcast_sink_start_scan();

// Sync to a discovered broadcast (after receiving discovery event)
// broadcast_code is NULL for unencrypted, or 16-byte key for encrypted
le_audio_broadcast_sink_sync(broadcast_id, broadcast_code);

// Or use demo function to auto-sync to first discovered broadcast
le_audio_broadcast_sink_demo_auto_sync(NULL);  // NULL = unencrypted

// Stop receiving
le_audio_broadcast_sink_stop();
```

## LE Audio Profile Stack

```
+─────────────────────────────────────────+
│          CAP (Common Audio Profile)     │
+─────────────────────────────────────────+
│  BAP (Basic Audio Profile)              │
│  ├── Unicast Client/Server (CIS)        │  bap_unicast.c
│  ├── Broadcast Source (BIS TX)          │  bap_broadcast.c
│  └── Broadcast Sink (BIS RX)            │  bap_broadcast_sink.c
+─────────────────────────────────────────+
│  PACS (Published Audio Capabilities)    │  pacs.c
│  ASCS (Audio Stream Control Service)    │  le_audio_manager.c
+─────────────────────────────────────────+
│  HCI ISOC (Isochronous Channels)        │  hci_isoc.c, isoc_handler.c
│  ├── CIS (Connected Isochronous Stream) │  Unicast
│  ├── BIG Create (Broadcast TX)          │  Auracast Source
│  └── BIG Sync (Broadcast RX)            │  Auracast Sink
+─────────────────────────────────────────+
│  liblc3 (Host-Side Codec)               │  lc3_wrapper.c
│  Google LC3 - Apache 2.0 License        │
+─────────────────────────────────────────+
```

## USB Composite Device Architecture

The firmware implements a USB High-Speed (480 Mbps) composite device with four interfaces:

### USB Endpoint Allocation

| Interface | Class | Endpoints | Buffer Size | Purpose |
|-----------|-------|-----------|-------------|---------|
| **0: MIDI** | Audio/MIDI | EP 0x81 IN, EP 0x01 OUT | 512B (HS) | USB MIDI streaming |
| **1: CDC Control** | CDC/ACM | EP 0x83 INT | 8B | CDC notifications |
| **2: CDC Data** | CDC Data | EP 0x82 IN, EP 0x02 OUT | 64B | AT command interface |
| **3: Wi-Fi Bridge** | Vendor (0xFF) | EP 0x84 IN, EP 0x04 OUT | 512B (HS) | USB-to-Wi-Fi data |

### USB Initialization Order

The USB composite device requires specific initialization order:

```c
// 1. Initialize USB stack and create all endpoints (BEFORE enumeration)
usb_composite_init(NULL);    // Creates MIDI, CDC, Wi-Fi endpoints

// 2. Initialize Wi-Fi bridge (WHD/SDIO only)
wifi_bridge_init(NULL);

// 3. Connect Wi-Fi bridge to USB endpoint handle
wifi_bridge_set_handle(usb_composite_get_wifi_bridge_handle());

// 4. Start USB enumeration (AFTER all endpoints configured)
usb_composite_start();       // Calls USBD_Start()
```

## FreeRTOS Task Architecture

### Dual-Core Task Distribution

| Core | Task | Priority | Stack (words) | Purpose |
|------|------|----------|---------------|---------|
| **CM55** | I2S DMA | ISR | - | DMA half/complete callbacks |
| **CM55** | Audio/LC3 | Highest | 4096 | LC3 encode/decode, frame sync |
| **CM55** | IPC | High | 2048 | Inter-processor audio frame exchange |
| **CM33** | ISOC | 6 | 2048 | Isochronous data path (CM33 ↔ CM55 IPC) |
| **CM33** | BLE | 5 | 4096 | BTSTACK, LE Audio control plane |
| **CM33** | USB | 4 | 2048 | USB composite (MIDI + CDC/ACM AT commands) |
| **CM33** | Wi-Fi | 3 | 4096 | WHD packet processing, USB-Wi-Fi bridge |
| **CM33** | MIDI | 2 | 1024 | BLE/USB MIDI routing |

## Memory Requirements

### RAM Usage (Actual Build)

| Region | Size | Notes |
|--------|------|-------|
| **CM33 Non-Secure SRAM** | 258 KB | BSS (168 KB) + heap (88 KB) + code (14 KB) |
| **CM33 Secure SRAM** | 133 KB | Secure services, heap |
| **CM55 DTCM** | 32 KB | Data, BSS, stack |
| **CM55 ITCM** | 12 KB | Fast code (ISRs, critical functions) |
| **CM55 SOCMEM** | 2.87 MB | Audio heap, LC3 buffers |
| **Shared Memory** | 256 KB | IPC between CM33/CM55 |
| **Total SRAM** | 418 KB | Of 1 MB available (40%) |
| **Total SOCMEM** | 3.13 MB | Of 5 MB available (63%) |

### Flash Usage (Actual Build)

| Image | Size | Contents |
|-------|------|----------|
| **CM33 Secure** | 27 KB | TrustZone config, secure services |
| **CM33 Non-Secure** | 369 KB | BTSTACK, USB, Wi-Fi, MIDI, AT commands |
| **CM55** | 143 KB | liblc3, I2S streaming, audio DSP |
| **Total** | **539 KB** | Of 16 MB QSPI flash (3%) |

## Documentation

- [Architecture Design](docs/architecture.md) - Detailed system architecture, data paths, state machines
- [Project Plan & Analysis](docs/README.md) - Complete implementation status and gap analysis

## API Examples

### Start LE Audio Unicast Streaming

```c
#include "le_audio/le_audio_manager.h"

le_audio_unicast_config_t config = {
    .sample_rate = 48000,
    .frame_duration_us = 10000,
    .octets_per_frame = 100,
    .direction = LE_AUDIO_DIR_BIDIRECTIONAL
};

le_audio_start_unicast(conn_handle, &config);
```

### Send MIDI over BLE

```c
#include "midi/midi_ble_service.h"

midi_ble_send_note_on(0, 60, 100);      // Channel 0, Middle C, velocity 100
midi_ble_send_control_change(0, 1, 64); // Channel 0, Mod wheel, value 64
```

### Send MIDI over USB

```c
#include "midi/midi_usb.h"

midi_usb_send_note_on(0, 0, 60, 100);   // Cable 0, Channel 0, Note 60, Vel 100

// Receive MIDI from USB (polling)
midi_usb_event_t event;
if (midi_usb_receive(&event) == 0) {
    // Process received MIDI event
}
```

### AT Command Interface (USB CDC/ACM)

Connect to the USB CDC virtual serial port (115200 baud) and send AT commands:

```
# System commands
AT              → OK
ATI             → Infineon LE Audio Demo v1.0
AT+VERSION?     → +VERSION: 1.0.0

# Bluetooth commands
AT+BTINIT       → OK
AT+BTSTATE?     → +BTSTATE: INITIALIZED
AT+BTNAME=MyDevice → OK
AT+GAPADVSTART  → OK

# LE Audio commands - Broadcast Source (Auracast TX)
AT+LEAINIT      → +LEAINIT: OK,48000,10000
AT+LEABROADCAST=1 → OK                    # Start broadcast source (TX)
AT+LEABROADCAST=0 → OK                    # Stop broadcast source
AT+LEABROADCAST? → +LEABROADCAST: 1,"My Broadcast"

# LE Audio commands - Broadcast Sink (Auracast RX)
AT+LEASCAN=1    → OK                      # Start scanning for broadcasts
AT+LEASCAN=0    → OK                      # Stop scanning
AT+LEASCAN?     → +LEASCAN: 1             # Query scan state
                → +LEABROADCAST_FOUND: 010203,"Studio Audio",-45,OPEN  # URC
AT+LEASYNC=010203 → +LEASYNC: SYNCING,010203  # Sync to broadcast
AT+LEASYNC=010203,<32-char-hex-key> → OK  # Sync to encrypted broadcast
AT+LEASINK?     → +LEASINK: 1,STREAMING   # Query sink state
AT+LEASINK=0    → +LEASINK: STOPPED       # Stop sink
AT+LEADEMO      → +LEADEMO: STARTED       # Auto-sync to first broadcast

# LE Audio status
AT+LEASTATE?    → +LEASTATE: STREAMING
AT+LEAMODE?     → +LEAMODE: BROADCAST_SINK

# Wi-Fi commands
AT+WIFIINIT     → OK
AT+WIFISCAN     → +WIFISCAN: "NetworkName",-45,WPA2
AT+WIFIJOIN=MyNetwork,password123 → OK
AT+WIFIBRIDGE=1 → OK
```

**Programmatic access (from firmware):**

```c
#include "cdc/at_parser.h"
#include "cdc/cdc_acm.h"

// Send response to host
cdc_acm_printf("\r\n+MYEVENT: data\r\n");

// Register custom AT command handler
int my_cmd_handler(int argc, const char *argv[]) {
    cdc_acm_printf("\r\n+MYCMD: %s\r\n", argv[0]);
    return CME_SUCCESS;
}
```

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

### Open Source Components
- **Google** for the open-source [liblc3](https://github.com/google/liblc3) LC3 codec implementation (Apache-2.0)
- **FreeRTOS Project** for the real-time operating system kernel (MIT)
- **Infineon/Cypress** for the open-source HAL, PDL, and RTOS abstraction layers (Apache-2.0)

### Reference Documentation
- **Zephyr Project** for the comprehensive [LE Audio architecture documentation](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html) (Apache-2.0)
- **Bluetooth SIG** for the LE Audio specifications (BAP, PACS, ASCS, LC3)

### Proprietary Components
- **Infineon** for the PSoC Edge and CYW55512 hardware platform
- **Infineon** for the btstack Bluetooth Host Stack and wifi-host-driver (Infineon EULA)
- **SEGGER** for the emUSB-Device middleware (commercial, licensed via Infineon)

## License Compliance

This project is licensed under Apache-2.0. See [LICENSE](LICENSE) for details.

**Important:** This project depends on proprietary Infineon middleware (btstack, wifi-host-driver) and SEGGER middleware (emusb-device) which are subject to their respective license terms. These components are NOT open source.

See [SBOM.md](SBOM.md) for the complete Software Bill of Materials and [NOTICE](NOTICE) for third-party attributions.

## References

### Infineon Documentation
- [CYW55512 Documentation](https://www.infineon.com/cms/en/product/wireless-connectivity/airoc-wi-fi-plus-bluetooth-combos/wi-fi-6-6e-802.11ax/)
- [PSoC Edge Documentation](https://documentation.infineon.com/psocedge/docs/bwb1750411526047)
- [BTSTACK API Reference](https://infineon.github.io/btstack/dual_mode/api_reference_manual/html/modules.html)

### Bluetooth Specifications
- [Bluetooth Core 6.0](https://www.bluetooth.com/specifications/specs/core-specification-6-0/)
- [BAP Specification](https://www.bluetooth.com/specifications/specs/basic-audio-profile-1-0-1/)
- [LC3 Codec Specification](https://www.bluetooth.com/specifications/specs/low-complexity-communication-codec-1-0/)

### Open Source References
- [Zephyr LE Audio Architecture](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html)
- [Auracast Technical Overview](https://www.bluetooth.com/auracast/)
