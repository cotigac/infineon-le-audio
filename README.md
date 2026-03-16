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

| Feature | Description | Status |
|---------|-------------|--------|
| **LE Audio Unicast** | Full-duplex audio streaming via CIS (Connected Isochronous Stream) | Implemented |
| **LE Audio Broadcast (Auracast)** | One-to-many audio broadcast via BIS (Broadcast Isochronous Stream) | Implemented |
| **LC3 Codec** | Host-side LC3 encode/decode using Google liblc3 | Implemented |
| **BLE MIDI** | MIDI over Bluetooth Low Energy GATT service | Implemented |
| **USB MIDI** | USB High-Speed MIDI class device (480 Mbps) | Implemented |
| **I2S Streaming** | DMA-based bidirectional audio with ping-pong buffers | Implemented |
| **Wi-Fi Bridge** | USB HS to SDIO to CYW55512 WLAN data path | Implemented |

## Hardware

### Target Hardware

| Component | Part Number | Description |
|-----------|-------------|-------------|
| **MCU** | PSE823GOS4DBZQ3 | PSoC Edge E82, Cortex-M55 @ 400MHz + Cortex-M33 |
| **Wireless** | CYW55512IUBGT | AIROC Wi-Fi 6 + Bluetooth 6.0 combo IC |
| **Eval Kit** | KIT_PSE84_EVAL | PSoC Edge E84 Evaluation Kit (USB HS, SDIO) |

### Hardware Architecture

```
+-----------------------------------------------------------------------------+
|                        Main Application Processor                            |
|                    (External Instrument / Host Device)                       |
+-------------------------------------+---------------------------------------+
                                      | USB High-Speed (480 Mbps)
                                      |   - USB MIDI Class
                                      |   - Wi-Fi Data (bridged)
                                      v
+-----------------------------------------------------------------------------+
|                      PSoC Edge E82 (PSE823GOS4DBZQ3)                        |
|                     Cortex-M55 @ 400MHz + Cortex-M33                        |
|                                                                              |
|  +--------------+  +--------------+  +--------------+  +------------------+ |
|  |   USB HS     |  |    I2S       |  |   UART       |  |     SDIO 3.0     | |
|  |  (480Mbps)   |  |   Master     |  |   (HCI)      |  |   (SDR50/DDR50)  | |
|  |  MIDI + Data |  |   Audio      |  |   BT Host    |  |   WLAN Host      | |
|  +------+-------+  +------+-------+  +------+-------+  +--------+---------+ |
|         |                 |                 |                    |           |
|  +------v-------+  +------v-------+  +------v-------+  +--------v---------+ |
|  | emUSB-Device |  |  Audio DMA   |  |  BTSTACK +   |  |  wifi-host-drv   | |
|  |  Middleware  |  |   Buffer     |  |  LE Audio    |  |  Wi-Fi Bridge    | |
|  | (HS capable) |  |  (Ping-pong) |  |  Profiles    |  |                  | |
|  +--------------+  +------+-------+  +------^-------+  +------------------+ |
|                           |                 |                                |
|                    +------v-----------------+------+                        |
|                    |         liblc3 (Host-Side)    |                        |
|                    |    LC3 Encode/Decode on PSoC  |                        |
|                    |  Unified for Unicast+Broadcast|                        |
|                    +-------------------------------+                        |
|                                                                              |
|  FreeRTOS (Tasks: Audio/LC3, BLE, USB, Wi-Fi, MIDI)                         |
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

### Requirements

| Software | Version | Purpose |
|----------|---------|---------|
| ModusToolbox | 3.x | Infineon development environment |
| PSoC Edge Device Pack | Latest | PSoC Edge E82/E84 support |
| CYW55512 Firmware | Latest | Bluetooth controller firmware |
| ARM GNU Toolchain | 12.x+ | arm-none-eabi-gcc compiler |
| CMake | 3.20+ | Build system |

### Dependencies (Git Submodules)

| Library | License | Purpose |
|---------|---------|---------|
| [liblc3](https://github.com/google/liblc3) | Apache 2.0 | LC3 codec encode/decode |
| [btstack](https://github.com/Infineon/btstack) | Infineon | Bluetooth host stack |
| [btstack-integration](https://github.com/Infineon/btstack-integration) | Infineon | HCI UART porting layer |
| [freertos](https://github.com/Infineon/freertos) | MIT | Real-time operating system |
| [emusb-device](https://github.com/Infineon/emusb-device) | Segger | USB High-Speed middleware |
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
│   │   ├── bap_broadcast.c/h       # BAP Broadcast Source (Auracast/BIS)
│   │   ├── pacs.c/h                # Published Audio Capabilities Service
│   │   └── isoc_handler.c/h        # HCI ISOC data path handling
│   │
│   ├── midi/                       # MIDI subsystem
│   │   ├── midi_ble_service.c/h    # BLE MIDI GATT service
│   │   ├── midi_usb.c/h            # USB MIDI class (emUSB-Device)
│   │   └── midi_router.c/h         # MIDI routing (BLE <-> USB <-> Controller)
│   │
│   ├── wifi/                       # Wi-Fi data bridge
│   │   ├── wifi_sdio.c/h           # SDIO driver (cyhal_sdio HAL)
│   │   └── wifi_bridge.c/h         # USB-to-Wi-Fi bridge (WHD)
│   │
│   └── bluetooth/                  # Bluetooth stack integration
│       ├── bt_init.c/h             # BTSTACK initialization
│       ├── bt_platform_config.c/h  # HCI UART configuration
│       ├── hci_isoc.c/h            # HCI isochronous commands
│       ├── gap_config.c/h          # GAP advertising/scanning/connections
│       └── gatt_db.c/h             # GATT database
│
└── libs/                           # External libraries (submodules)
    ├── btstack/                    # Infineon BTSTACK
    ├── btstack-integration/        # HCI UART porting layer
    ├── liblc3/                     # Google LC3 codec
    ├── freertos/                   # FreeRTOS kernel
    ├── emusb-device/               # Segger USB middleware
    └── wifi-host-driver/           # Infineon WHD
```

## Getting Started

### 1. Clone the Repository

```bash
git clone --recursive https://github.com/cotigac/infineon-le-audio.git
cd infineon-le-audio
```

If you already cloned without `--recursive`:
```bash
git submodule update --init --recursive
```

### 2. Build with CMake

```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-cortex-m55.cmake ..
cmake --build .
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_LE_AUDIO` | ON | Enable LE Audio (BAP, PACS, ASCS) |
| `ENABLE_AURACAST` | ON | Enable Auracast Broadcast (BIS) |
| `ENABLE_MIDI` | ON | Enable MIDI over BLE and USB |
| `ENABLE_USB` | ON | Enable USB Device support |
| `ENABLE_WIFI` | ON | Enable Wi-Fi data bridge |
| `CMAKE_BUILD_TYPE` | Debug | Debug/Release/RelWithDebInfo/MinSizeRel |

### 3. Hardware Setup

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

### Auracast Broadcast Settings

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

## LE Audio Profile Stack

```
+─────────────────────────────────────────+
│          CAP (Common Audio Profile)     │
+─────────────────────────────────────────+
│  BAP (Basic Audio Profile)              │  bap_unicast.c, bap_broadcast.c
│  ├── Unicast Client/Server (CIS)        │
│  └── Broadcast Source/Sink (BIS)        │  Auracast
+─────────────────────────────────────────+
│  PACS (Published Audio Capabilities)    │  pacs.c
│  ASCS (Audio Stream Control Service)    │  le_audio_manager.c
+─────────────────────────────────────────+
│  HCI ISOC (Isochronous Channels)        │  hci_isoc.c, isoc_handler.c
│  ├── CIS (Connected Isochronous Stream) │  Unicast
│  └── BIS (Broadcast Isochronous Stream) │  Auracast
+─────────────────────────────────────────+
│  liblc3 (Host-Side Codec)               │  lc3_wrapper.c
│  Google LC3 - Apache 2.0 License        │
+─────────────────────────────────────────+
```

## FreeRTOS Task Architecture

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| I2S DMA | ISR | - | DMA half/complete callbacks |
| Audio/LC3 | 6 (Highest) | 4096 | LC3 encode/decode, frame sync |
| BLE | 5 | 4096 | BTSTACK, LE Audio profiles |
| USB | 4 | 2048 | USB enumeration, MIDI class |
| Wi-Fi | 3 | 4096 | WHD packet processing |
| MIDI | 2 | 1024 | BLE/USB routing |

## Memory Requirements

### RAM Usage (Estimated)

| Component | Size | Notes |
|-----------|------|-------|
| FreeRTOS kernel | 10 KB | Tasks, queues, semaphores |
| BTSTACK + profiles | 80 KB | LE Audio profiles |
| liblc3 state | 40 KB | Encoder + decoder (stereo) |
| Audio buffers | 20 KB | I2S + LC3 ring buffers |
| USB middleware | 8 KB | MIDI + bulk classes |
| Wi-Fi buffers | 16 KB | 16 x 1500 byte packets |
| Application | 30 KB | Task stacks, variables |
| **Total** | **~204 KB** | PSoC Edge E82 has 5 MB |

### Flash Usage (Estimated)

| Component | Size |
|-----------|------|
| FreeRTOS | 20 KB |
| BTSTACK + profiles | 200 KB |
| liblc3 | 60 KB |
| USB middleware | 30 KB |
| WHD | 100 KB |
| Application code | 120 KB |
| **Total** | **~530 KB** |

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

- **Google** for the open-source [liblc3](https://github.com/google/liblc3) implementation
- **Zephyr Project** for the comprehensive LE Audio stack reference
- **Infineon** for the PSoC Edge and CYW55512 hardware platform
- **Segger** for the emUSB-Device middleware
- **Bluetooth SIG** for the LE Audio specifications

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
