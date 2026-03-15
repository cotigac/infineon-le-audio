# Architecture Design

## System Overview

This document describes the architecture of the Infineon LE Audio demo for musical instruments, implementing full-duplex LE Audio, Auracast broadcast, and MIDI over BLE/USB.

## Hardware Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Main Application Controller                   │
│                     (External Instrument MCU)                    │
└────────────────────────────┬────────────────────────────────────┘
                             │ I2S (PCM Audio Stream)
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

## Audio Data Flow

### Unified Host-Side LC3 Architecture

All LC3 encoding/decoding runs on the PSoC Edge host, providing a consistent architecture for both unicast and broadcast modes.

#### Transmit Path (Encode)

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Main Ctrl    │───►│ PSoC Edge    │───►│ liblc3       │───►│ CYW55511     │
│ I2S PCM      │    │ I2S RX DMA   │    │ LC3 Encode   │    │ HCI ISOC TX  │
│ 48kHz/16bit  │    │ Audio Buffer │    │ (host CPU)   │    │ BLE Radio    │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

#### Receive Path (Decode)

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ CYW55511     │───►│ liblc3       │───►│ PSoC Edge    │───►│ Main Ctrl    │
│ HCI ISOC RX  │    │ LC3 Decode   │    │ I2S TX DMA   │    │ I2S PCM      │
│ BLE Radio    │    │ (host CPU)   │    │ Audio Buffer │    │ 48kHz/16bit  │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

## Software Architecture

### FreeRTOS Task Structure

```
┌─────────────────────────────────────────────────────────────────┐
│                      FreeRTOS Scheduler                          │
├─────────────┬─────────────┬─────────────┬─────────────┬─────────┤
│ I2S Task    │ Audio/LC3   │ BLE Task    │ USB Task    │ MIDI    │
│ Priority: 6 │ Priority: 5 │ Priority: 4 │ Priority: 3 │ Task    │
│ (Highest)   │             │             │             │ Prio: 2 │
├─────────────┼─────────────┼─────────────┼─────────────┼─────────┤
│ DMA ISR     │ LC3 encode  │ BTSTACK     │ USB enum    │ BLE/USB │
│ Buffer swap │ LC3 decode  │ GAP/GATT    │ MIDI class  │ routing │
│ Ping-pong   │ Frame sync  │ BAP/ISOC    │ Endpoints   │         │
└─────────────┴─────────────┴─────────────┴─────────────┴─────────┘
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
├── audio/
│   ├── audio_task.c        # Main audio processing task
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
│   ├── midi_usb.c          # USB MIDI class
│   ├── midi_usb.h
│   ├── midi_router.c       # MIDI routing logic
│   └── midi_router.h
│
└── bluetooth/
    ├── bt_init.c           # BTSTACK initialization
    ├── bt_init.h
    ├── hci_isoc.c          # HCI isochronous commands
    ├── hci_isoc.h
    ├── gap_config.c        # GAP advertising/scanning
    └── gatt_db.c           # GATT database
```

## References

- [Bluetooth Core 6.0 Specification](https://www.bluetooth.com/specifications/specs/)
- [BAP Specification](https://www.bluetooth.com/specifications/specs/basic-audio-profile-1-0-1/)
- [Google liblc3](https://github.com/google/liblc3)
- [Zephyr LE Audio](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html)
