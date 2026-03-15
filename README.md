# Infineon LE Audio Demo

PSoC Edge + CYW55511 demo: Full-duplex LE Audio (LC3), Auracast broadcast, BLE/USB MIDI, and I2S streaming for musical instruments.

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-PSoC%20Edge%20E81-green.svg)](https://www.infineon.com/cms/en/product/microcontroller/32-bit-psoc-arm-cortex-microcontroller/32-bit-psoc-arm-cortex-for-industrial/psoc-edge/)
[![Bluetooth](https://img.shields.io/badge/Bluetooth-6.0%20LE%20Audio-blue.svg)](https://www.bluetooth.com/learn-about-bluetooth/recent-enhancements/le-audio/)

## Overview

This project implements a comprehensive Bluetooth LE Audio solution for musical instruments on Infineon hardware. It demonstrates how to build a full-featured audio streaming device with support for the latest Bluetooth LE Audio standards including Auracast broadcast.

### Target Application

A musical instrument (synthesizer, digital piano, guitar processor, etc.) that needs to:
- Stream high-quality audio wirelessly to LE Audio headphones/speakers
- Broadcast audio to multiple receivers simultaneously (Auracast)
- Send/receive MIDI over Bluetooth LE and USB
- Interface with a main application controller via I2S

### Key Features

| Feature | Description | Status |
|---------|-------------|--------|
| **LE Audio Unicast** | Full-duplex audio streaming via CIS (Connected Isochronous Stream) | In Development |
| **LE Audio Broadcast (Auracast)** | One-to-many audio broadcast via BIS (Broadcast Isochronous Stream) | Planned |
| **LC3 Codec** | Low Complexity Communication Codec - host-side implementation using Google liblc3 | In Development |
| **BLE MIDI** | MIDI over Bluetooth Low Energy GATT service | Planned |
| **USB MIDI** | USB Full-Speed MIDI class device | Planned |
| **I2S Streaming** | DMA-based bidirectional audio with main application controller | Planned |

## Hardware

### Bill of Materials

| Component | Part Number | Description |
|-----------|-------------|-------------|
| **MCU** | PSoC Edge E81 (PSE81x) | ARM Cortex-M55 @ 400MHz with Helium DSP |
| **Bluetooth** | CYW55511 | Bluetooth 6.0 LE combo IC with Wi-Fi 6 |
| **Eval Kit** | KIT_PSE84_EVAL | PSoC Edge E84 Evaluation Kit (or E81 equivalent) |
| **BT Module** | CYW955513EVK-01 | CYW55511/12/13 evaluation kit with audio codec |

### Hardware Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Main Application Controller                   │
│                     (External Instrument MCU)                    │
└────────────────────────────┬────────────────────────────────────┘
                             │ I2S (PCM Audio)
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                      PSoC Edge E81 (PSE81x)                     │
│                     Cortex-M55 @ 400MHz                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │   USB FS    │  │    I2S      │  │      UART (HCI)         │  │
│  │   (MIDI)    │  │   Master    │  │   (BT Host Interface)   │  │
│  └──────┬──────┘  └──────┬──────┘  └───────────┬─────────────┘  │
│         │                │                     │                 │
│  ┌──────▼──────┐  ┌──────▼──────┐  ┌──────────▼──────────────┐  │
│  │ USB Device  │  │ Audio DMA   │  │   BTSTACK + LE Audio    │  │
│  │ Middleware  │  │   Buffer    │  │   Profile Libraries     │  │
│  └─────────────┘  └──────┬──────┘  └──────────▲──────────────┘  │
│                          │                     │                 │
│                   ┌──────▼─────────────────────┴──────┐         │
│                   │         liblc3 (Host-Side)        │         │
│                   │    LC3 Encode/Decode on PSoC      │         │
│                   │  Unified for Unicast + Broadcast  │         │
│                   └───────────────────────────────────┘         │
│                                                                  │
│  FreeRTOS (Tasks: Audio/LC3, BLE, MIDI, I2S)                    │
└─────────────────────────────────────────────────────────────────┘
                             │ UART (HCI with ISOC)
                             │ LC3 frames over HCI
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                        CYW55511                                  │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │              Bluetooth 6.0 LE Controller                    ││
│  │         ISOC Transport (BIS/CIS) - Radio Only               ││
│  │            (LC3 codec NOT used - HCI mode)                  ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                  │
│  Handles: BLE advertising, scanning, connections, ISOC streams  │
└─────────────────────────────────────────────────────────────────┘
```

### Audio Data Flow

#### Unified Host-Side LC3 Architecture

All LC3 encoding and decoding runs on the PSoC Edge host using Google's open-source liblc3 library. This provides a consistent architecture for both unicast and broadcast modes.

```
TRANSMIT PATH:
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Main Ctrl    │───►│ PSoC Edge    │───►│ liblc3       │───►│ CYW55511     │
│ I2S PCM      │    │ I2S RX DMA   │    │ LC3 Encode   │    │ HCI ISOC TX  │
│ 48kHz/16bit  │    │ Audio Buffer │    │ (host CPU)   │    │ BLE Radio    │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘

RECEIVE PATH:
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ CYW55511     │───►│ liblc3       │───►│ PSoC Edge    │───►│ Main Ctrl    │
│ HCI ISOC RX  │    │ LC3 Decode   │    │ I2S TX DMA   │    │ I2S PCM      │
│ BLE Radio    │    │ (host CPU)   │    │ Audio Buffer │    │ 48kHz/16bit  │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

## Software

### Requirements

| Software | Version | Purpose |
|----------|---------|---------|
| ModusToolbox | 3.x | Infineon development environment |
| PSoC Edge Device Pack | Latest | PSoC Edge E81/E84 support |
| CYW55511 Firmware | Latest | Bluetooth controller firmware |
| FreeRTOS | 10.x | Real-time operating system |

### Dependencies

| Library | License | Purpose |
|---------|---------|---------|
| [liblc3](https://github.com/google/liblc3) | Apache 2.0 | LC3 codec encode/decode |
| [Zephyr LE Audio](https://github.com/zephyrproject-rtos/zephyr) | Apache 2.0 | BAP broadcast source reference |
| Infineon BTSTACK | Infineon | Bluetooth host stack |
| Infineon PDL | Infineon | PSoC peripheral drivers |

### Project Structure

```
infineon-le-audio/
├── README.md                           # This file
├── LICENSE                             # Apache 2.0 license
├── .gitignore                          # Git ignore rules
│
├── docs/                               # Documentation
│   ├── README.md                       # Full project plan and analysis
│   └── architecture.md                 # Detailed design documentation
│
├── config/                             # Configuration files
│   └── lc3_config.h                    # LC3 codec configuration
│
├── source/                             # Source code
│   ├── main.c                          # Application entry point
│   │
│   ├── audio/                          # Audio subsystem
│   │   ├── lc3_wrapper.h               # LC3 codec API
│   │   ├── lc3_wrapper.c               # LC3 codec implementation
│   │   ├── i2s_stream.h                # I2S streaming API
│   │   └── i2s_stream.c                # I2S streaming (planned)
│   │
│   ├── le_audio/                       # LE Audio profiles
│   │   ├── le_audio_manager.h          # Top-level LE Audio API
│   │   └── ...                         # (implementation planned)
│   │
│   ├── midi/                           # MIDI subsystem (planned)
│   │
│   └── bluetooth/                      # Bluetooth stack integration (planned)
│
└── libs/                               # External libraries
    ├── liblc3/                         # Google LC3 codec
    ├── btstack/                        # Infineon BTSTACK
    └── freertos/                       # FreeRTOS kernel
```

## Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/cotigac/infineon-le-audio.git
cd infineon-le-audio
```

### 2. Fetch Dependencies

```bash
# Clone liblc3 into libs/
git clone https://github.com/google/liblc3.git libs/liblc3

# BTSTACK and FreeRTOS will be provided by ModusToolbox
```

### 3. Import into ModusToolbox

1. Open ModusToolbox IDE
2. File → Import → Existing Projects into Workspace
3. Select the `infineon-le-audio` directory
4. Build and flash to your evaluation kit

### 4. Hardware Setup

1. Connect PSoC Edge E81 eval kit to PC via USB
2. Connect CYW55511 module via UART (HCI interface)
3. Connect main application controller via I2S (optional)
4. Power on and program the firmware

## Configuration

### LC3 Codec Settings

Edit `config/lc3_config.h` to customize audio settings:

```c
#define LC3_CFG_SAMPLE_RATE         48000   // Sample rate in Hz
#define LC3_CFG_FRAME_DURATION      1       // 0=7.5ms, 1=10ms
#define LC3_CFG_OCTETS_PER_FRAME    100     // Bitrate (100 = 80kbps)
#define LC3_CFG_CHANNELS            1       // 1=mono, 2=stereo
```

### Auracast Broadcast Settings

Configure broadcast parameters in your application:

```c
le_audio_broadcast_config_t config = {
    .broadcast_name = "My Instrument",
    .audio_context = LE_AUDIO_CONTEXT_MEDIA,
    .encrypted = false,
    .num_subgroups = 1,
    .num_bis_per_subgroup = 1,
    .presentation_delay_us = 40000
};
```

## LE Audio Profile Stack

```
┌─────────────────────────────────────────┐
│          CAP (Common Audio Profile)     │
├─────────────────────────────────────────┤
│  BAP (Basic Audio Profile)              │
│  ├── Unicast Client/Server (CIS)        │
│  └── Broadcast Source/Sink (BIS)        │
├─────────────────────────────────────────┤
│  PACS (Published Audio Capabilities)    │
│  ASCS (Audio Stream Control Service)    │
│  BASS (Broadcast Audio Scan Service)    │
├─────────────────────────────────────────┤
│  HCI ISOC (Isochronous Channels)        │
│  ├── CIS (Connected Isochronous Stream) │
│  └── BIS (Broadcast Isochronous Stream) │
├─────────────────────────────────────────┤
│  liblc3 (Host-Side Codec)               │
│  Google LC3 - Apache 2.0 License        │
└─────────────────────────────────────────┘
```

## Memory Requirements

| Component | RAM | Flash |
|-----------|-----|-------|
| FreeRTOS kernel | ~10 KB | ~20 KB |
| BTSTACK + LE Audio profiles | ~80 KB | ~200 KB |
| liblc3 (encoder + decoder) | ~40 KB | ~60 KB |
| Audio buffers | ~20 KB | - |
| USB middleware | ~8 KB | ~30 KB |
| Application code | ~30 KB | ~120 KB |
| **Total** | **~188 KB** | **~430 KB** |

PSoC Edge E81 has 4 MB SRAM and 512 KB Flash - plenty of headroom.

## Documentation

- [Architecture Design](docs/architecture.md) - Detailed system architecture
- [Full Project Plan](docs/README.md) - Complete implementation plan and analysis

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
- **Infineon** for the PSoC Edge and CYW55511 hardware platform
- **Bluetooth SIG** for the LE Audio specifications

## References

### Infineon Documentation
- [CYW55513 Documentation](https://documentation.infineon.com/cyw55513/docs/nhu1755169548502)
- [PSoC Edge Documentation](https://documentation.infineon.com/psocedge/docs/bwb1750411526047)
- [BTSTACK API Reference](https://infineon.github.io/btstack/dual_mode/api_reference_manual/html/modules.html)

### Bluetooth Specifications
- [Bluetooth Core 6.0](https://www.bluetooth.com/specifications/specs/core-specification-6-0/)
- [BAP Specification](https://www.bluetooth.com/specifications/specs/basic-audio-profile-1-0-1/)
- [LC3 Codec Specification](https://www.bluetooth.com/specifications/specs/low-complexity-communication-codec-1-0/)

### Open Source References
- [Zephyr LE Audio Architecture](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html)
- [Auracast Technical Overview](https://www.bluetooth.com/wp-content/uploads/2024/05/2403_How_To_Auracast_Transmitter.pdf)
