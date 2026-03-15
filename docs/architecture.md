# Architecture Design

## System Overview

This document describes the architecture of the Infineon LE Audio demo for musical instruments, implementing full-duplex LE Audio, Auracast broadcast, MIDI over BLE/USB, and Wi-Fi data bridging.

## Target Hardware

| Component | Part Number | Description |
|-----------|-------------|-------------|
| **MCU** | PSE823GOS4DBZQ3 | PSoC Edge E82, Cortex-M55 @ 400MHz + Cortex-M33 |
| **Wireless** | CYW55512IUBGT | AIROC Wi-Fi 6 + Bluetooth 6.0 combo IC |
| **Eval Kit** | KIT_PSE84_EVAL | PSoC Edge E84 Evaluation Kit (USB HS, SDIO) |

## Hardware Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Main Application Processor                            │
│                    (External Instrument / Host Device)                       │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ USB High-Speed (480 Mbps)
                                    │ ├── USB MIDI Class
                                    │ └── Wi-Fi Data (bridged)
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      PSoC Edge E82 (PSE823GOS4DBZQ3)                        │
│                     Cortex-M55 @ 400MHz + Cortex-M33                        │
│                                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐ │
│  │   USB HS     │  │    I2S       │  │   UART       │  │     SDIO 3.0     │ │
│  │  (480Mbps)   │  │   Master     │  │   (HCI)      │  │   (SDR50/DDR50)  │ │
│  │  MIDI + Data │  │   Audio      │  │   BT Host    │  │   WLAN Host      │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘ │
│         │                 │                 │                    │           │
│  ┌──────▼───────┐  ┌──────▼───────┐  ┌──────▼───────┐  ┌────────▼─────────┐ │
│  │ USB Device   │  │  Audio DMA   │  │  BTSTACK +   │  │  wifi-host-drv   │ │
│  │ Middleware   │  │   Buffer     │  │  LE Audio    │  │  Wi-Fi Bridge    │ │
│  │ (HS capable) │  │  (Ping-pong) │  │  Profiles    │  │                  │ │
│  └──────────────┘  └──────┬───────┘  └──────▲───────┘  └──────────────────┘ │
│                           │                 │                                │
│                    ┌──────▼─────────────────┴──────┐                        │
│                    │         liblc3 (Host-Side)    │                        │
│                    │    LC3 Encode/Decode on PSoC  │                        │
│                    │  Unified for Unicast+Broadcast│                        │
│                    └───────────────────────────────┘                        │
│                                                                              │
│  FreeRTOS (Tasks: Audio/LC3, BLE, USB, Wi-Fi, MIDI)                         │
└──────────────────────────────┬──────────────────────┬───────────────────────┘
                               │                      │
                               │ UART (HCI+ISOC)      │ SDIO (Wi-Fi Data)
                               │                      │
                               ▼                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          CYW55512IUBGT                                       │
│  ┌──────────────────────────────────┐  ┌──────────────────────────────────┐ │
│  │    Bluetooth 6.0 LE Controller   │  │       Wi-Fi 6 (802.11ax)         │ │
│  │   ISOC Transport (BIS/CIS)       │  │    2.4 GHz + 5 GHz Dual-Band     │ │
│  │   LC3 codec NOT used (HCI mode)  │  │    Up to 1.2 Gbps PHY Rate       │ │
│  └──────────────────────────────────┘  └──────────────────────────────────┘ │
│                                                                              │
│  Handles: BLE advertising, scanning, connections, ISOC, Wi-Fi association   │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Data Paths

### Path 1: Wi-Fi Data (USB HS → SDIO)

```
┌────────────────┐    ┌────────────────┐    ┌────────────────┐    ┌────────────────┐
│ App Processor  │───►│ PSoC Edge E82  │───►│ SDIO Driver    │───►│ CYW55512       │
│ USB HS Host    │    │ USB HS Device  │    │ wifi-host-drv  │    │ WLAN Subsystem │
│ (Network Data) │    │ Data Bridge    │    │ SDR50/DDR50    │    │ 802.11ax       │
└────────────────┘    └────────────────┘    └────────────────┘    └────────────────┘
```

### Path 2: BLE MIDI (USB HS → UART)

```
┌────────────────┐    ┌────────────────┐    ┌────────────────┐    ┌────────────────┐
│ App Processor  │───►│ PSoC Edge E82  │───►│ BTSTACK        │───►│ CYW55512       │
│ USB HS MIDI    │    │ USB MIDI Class │    │ GATT Server    │    │ BLE Subsystem  │
│ (MIDI Events)  │    │ MIDI Router    │    │ UART @ 3Mbps   │    │ BLE MIDI TX    │
└────────────────┘    └────────────────┘    └────────────────┘    └────────────────┘
```

### Path 3: LE Audio (I2S → UART/ISOC)

```
┌────────────────┐    ┌────────────────┐    ┌────────────────┐    ┌────────────────┐
│ Main Controller│───►│ PSoC Edge E82  │───►│ liblc3         │───►│ CYW55512       │
│ I2S PCM Audio  │    │ I2S RX DMA     │    │ LC3 Encode     │    │ HCI ISOC TX    │
│ 48kHz/16bit    │    │ Audio Buffer   │    │ (host CPU)     │    │ BLE/Auracast   │
└────────────────┘    └────────────────┘    └────────────────┘    └────────────────┘
```

## Audio Data Flow

### Unified Host-Side LC3 Architecture

All LC3 encoding/decoding runs on the PSoC Edge host, providing a consistent architecture for both unicast and broadcast modes.

#### Transmit Path (Encode)

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Main Ctrl    │───►│ PSoC Edge    │───►│ liblc3       │───►│ CYW55512     │
│ I2S PCM      │    │ I2S RX DMA   │    │ LC3 Encode   │    │ HCI ISOC TX  │
│ 48kHz/16bit  │    │ Audio Buffer │    │ (host CPU)   │    │ BLE Radio    │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

#### Receive Path (Decode)

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ CYW55512     │───►│ liblc3       │───►│ PSoC Edge    │───►│ Main Ctrl    │
│ HCI ISOC RX  │    │ LC3 Decode   │    │ I2S TX DMA   │    │ I2S PCM      │
│ BLE Radio    │    │ (host CPU)   │    │ Audio Buffer │    │ 48kHz/16bit  │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

## Software Architecture

### FreeRTOS Task Structure

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           FreeRTOS Scheduler                                  │
├─────────────┬─────────────┬─────────────┬─────────────┬──────────┬──────────┤
│ I2S Task    │ Audio/LC3   │ BLE Task    │ USB Task    │ Wi-Fi    │ MIDI     │
│ Priority: 7 │ Priority: 6 │ Priority: 5 │ Priority: 4 │ Task     │ Task     │
│ (Highest)   │             │             │             │ Prio: 3  │ Prio: 2  │
├─────────────┼─────────────┼─────────────┼─────────────┼──────────┼──────────┤
│ DMA ISR     │ LC3 encode  │ BTSTACK     │ USB HS enum │ SDIO TX  │ BLE/USB  │
│ Buffer swap │ LC3 decode  │ GAP/GATT    │ MIDI class  │ SDIO RX  │ routing  │
│ Ping-pong   │ Frame sync  │ BAP/ISOC    │ Data bridge │ Packets  │          │
└─────────────┴─────────────┴─────────────┴─────────────┴──────────┴──────────┘
```

### Inter-Task Communication

```
┌─────────────────────────────────────────────────────────────────┐
│                       Message Queues                             │
├─────────────────────────────────────────────────────────────────┤
│ pcm_rx_queue    : PCM samples from I2S RX → Audio task          │
│ pcm_tx_queue    : PCM samples from Audio task → I2S TX          │
│ lc3_tx_queue    : LC3 frames from Audio task → BLE task         │
│ lc3_rx_queue    : LC3 frames from BLE task → Audio task         │
│ midi_queue      : MIDI events (BLE ↔ USB ↔ Main controller)     │
│ bt_event_queue  : Bluetooth stack events                         │
│ wifi_tx_queue   : Wi-Fi packets from USB → SDIO TX              │
│ wifi_rx_queue   : Wi-Fi packets from SDIO RX → USB              │
│ usb_data_queue  : USB HS bulk data for Wi-Fi bridge             │
└─────────────────────────────────────────────────────────────────┘
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

## LC3 Codec Configuration

### Supported Configurations

| Parameter | Value | Notes |
|-----------|-------|-------|
| Sample Rate | 48000 Hz | Standard LE Audio rate |
| Frame Duration | 10 ms | 480 samples per frame |
| Bit Depth | 16-bit | Signed PCM |
| Channels | Mono or Stereo | Per stream |
| Bitrate | 80-96 kbps | Per channel |
| Octets per Frame | 100 bytes | At 80 kbps |

### CPU Budget (Cortex-M55 @ 400MHz)

| Operation | Time | % of Frame |
|-----------|------|------------|
| LC3 Encode (mono) | ~5 ms | 50% |
| LC3 Decode (mono) | ~3.75 ms | 37.5% |
| Full Duplex Stereo | ~17.5 ms | 175%* |

*Note: Stereo full-duplex exceeds single-core budget. Options:
1. Use mono for full-duplex
2. Use 24kHz sample rate for stereo
3. Optimize with SIMD/Helium DSP

## Memory Map

### RAM Usage (Estimated)

| Component | Size | Notes |
|-----------|------|-------|
| FreeRTOS kernel | 10 KB | Tasks, queues, semaphores |
| BTSTACK + profiles | 80 KB | LE Audio profiles |
| liblc3 state | 40 KB | Encoder + decoder (stereo) |
| Audio buffers | 20 KB | I2S + LC3 ring buffers |
| USB middleware | 8 KB | MIDI class |
| Application | 30 KB | Task stacks, variables |
| **Total** | **~188 KB** | PSoC Edge E81 has 4 MB |

### Flash Usage (Estimated)

| Component | Size |
|-----------|------|
| FreeRTOS | 20 KB |
| BTSTACK + profiles | 200 KB |
| liblc3 | 60 KB |
| USB middleware | 30 KB |
| Application code | 120 KB |
| **Total** | **~430 KB** |

## Key Design Decisions

### 1. Host-Side LC3 (vs CYW55511 Offload)

**Decision**: Run LC3 codec on PSoC Edge host

**Rationale**:
- Unified architecture for unicast and broadcast
- Full control over codec parameters
- Easier debugging and audio path visibility
- Auracast porting requires host-side LC3 anyway

**Trade-off**: Higher CPU load on PSoC, but Cortex-M55 @ 400MHz can handle it.

### 2. FreeRTOS (vs Zephyr)

**Decision**: Use FreeRTOS with ModusToolbox

**Rationale**:
- CYW55511 driver not available in Zephyr
- ModusToolbox has mature PSoC Edge support
- Infineon BTSTACK is FreeRTOS-native
- Port only BAP profiles from Zephyr (not entire OS)

### 3. HCI Mode (vs Embedded Mode)

**Decision**: Use CYW55511 in HCI mode

**Rationale**:
- Host controls all profile logic
- LC3 encoding/decoding on host
- More flexibility for custom profiles
- Easier to port Zephyr LE Audio code

## File Organization

```
source/
├── main.c                  # Application entry point
│
├── audio/
│   ├── audio_task.c        # Main audio processing task
│   ├── audio_task.h
│   ├── lc3_wrapper.c       # liblc3 encode/decode API
│   ├── lc3_wrapper.h
│   ├── i2s_stream.c        # I2S DMA driver
│   ├── i2s_stream.h
│   ├── audio_buffers.c     # Ring buffer management
│   └── audio_buffers.h
│
├── le_audio/
│   ├── le_audio_manager.c  # Top-level LE Audio control
│   ├── le_audio_manager.h
│   ├── bap_unicast.c       # BAP unicast client/server
│   ├── bap_unicast.h
│   ├── bap_broadcast.c     # BAP broadcast source (Auracast)
│   ├── bap_broadcast.h
│   ├── pacs.c              # Published Audio Capabilities
│   ├── pacs.h
│   ├── isoc_handler.c      # HCI ISOC data path
│   └── isoc_handler.h
│
├── midi/
│   ├── midi_ble_service.c  # BLE MIDI GATT service
│   ├── midi_ble_service.h
│   ├── midi_usb.c          # USB MIDI class (High-Speed)
│   ├── midi_usb.h
│   ├── midi_router.c       # MIDI routing logic
│   └── midi_router.h
│
├── wifi/                   # NEW - Wi-Fi/SDIO data path
│   ├── wifi_init.c         # Wi-Fi subsystem initialization
│   ├── wifi_init.h
│   ├── sdio_driver.c       # SDIO driver for CYW55512 WLAN
│   ├── sdio_driver.h
│   ├── usb_wifi_bridge.c   # USB HS ↔ SDIO data bridge
│   └── usb_wifi_bridge.h
│
└── bluetooth/
    ├── bt_init.c           # BTSTACK initialization
    ├── bt_init.h
    ├── hci_isoc.c          # HCI isochronous commands
    ├── hci_isoc.h
    ├── gap_config.c        # GAP advertising/scanning
    ├── gap_config.h
    ├── gatt_db.c           # GATT database
    └── gatt_db.h
```

## USB High-Speed Requirements

### Why USB HS (Not USB FS)

| Aspect | USB Full-Speed | USB High-Speed |
|--------|----------------|----------------|
| Speed | 12 Mbps | 480 Mbps |
| MIDI Latency | Higher | Lower |
| Wi-Fi Bridge | NOT viable | Required |
| Bulk Transfer | 64 bytes/pkt | 512 bytes/pkt |

**CRITICAL**: The Wi-Fi data bridge requires USB High-Speed. USB Full-Speed (12 Mbps) cannot sustain Wi-Fi 6 throughput.

### USB HS Middleware

The current `usbdev` library only supports USB Full-Speed. PSoC Edge E82/E84 has USB 2.0 High-Speed hardware that requires a different driver:

- **Required**: USB HS capable middleware (not USBFS)
- **IP Block**: Different from MXUSBFS
- **Endpoints**: 512 bytes (vs 64 for FS)

## SDIO Interface (Wi-Fi)

### CYW55512 WLAN Interface

| Parameter | Value |
|-----------|-------|
| Interface | SDIO 3.0 |
| Mode | SDR50 / DDR50 |
| Clock | Up to 208 MHz |
| Bus Width | 4-bit |

### Required Library

- [wifi-host-driver](https://github.com/Infineon/wifi-host-driver)

## References

- [Bluetooth Core 6.0 Specification](https://www.bluetooth.com/specifications/specs/)
- [BAP Specification](https://www.bluetooth.com/specifications/specs/basic-audio-profile-1-0-1/)
- [Google liblc3](https://github.com/google/liblc3)
- [Zephyr LE Audio](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html)
- [CYW55512 Documentation](https://www.infineon.com/cms/en/product/wireless-connectivity/airoc-wi-fi-plus-bluetooth-combos/wi-fi-6-6e-802.11ax/)
- [PSoC Edge E82 Datasheet](https://www.infineon.com/cms/en/product/microcontroller/32-bit-psoc-arm-cortex-microcontroller/psoc-edge-microcontrollers/)
