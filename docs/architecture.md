# Architecture Design

## System Overview

This document describes the architecture of the Infineon LE Audio demo for musical instruments, implementing full-duplex LE Audio, Auracast broadcast, MIDI over BLE/USB, and Wi-Fi data bridging.

The firmware uses a **dual-core architecture** to maximize performance:
- **Cortex-M33 (CM33)**: Control plane - Bluetooth stack, USB, Wi-Fi, MIDI routing
- **Cortex-M55 (CM55)**: Audio DSP - LC3 codec, I2S streaming, audio buffer management

## Target Hardware

| Component | Part Number | Description |
|-----------|-------------|-------------|
| **MCU** | PSE823GOS4DBZQ3 | PSoC Edge E82, Cortex-M55 @ 400MHz + Cortex-M33 |
| **Wireless** | CYW55512IUBGT | AIROC Wi-Fi 6 + Bluetooth 6.0 combo IC |
| **Eval Kit** | KIT_PSE84_EVAL | PSoC Edge E84 Evaluation Kit (USB HS, SDIO) |

## Dual-Core Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            PSoC Edge E82/E84                                 │
├─────────────────────────────────┬───────────────────────────────────────────┤
│         Cortex-M33              │              Cortex-M55                    │
│        (Control Plane)          │            (Audio DSP)                     │
│                                 │                                            │
│  ┌─────────────────────────┐   │   ┌─────────────────────────────────────┐ │
│  │ BTSTACK + LE Audio Ctrl │   │   │ LC3 Codec (liblc3)                  │ │
│  │ - BAP state machine     │   │   │ - Encode: PCM → LC3 (Helium DSP)    │ │
│  │ - ISOC control          │   │   │ - Decode: LC3 → PCM                 │ │
│  │ - GATT services         │◄──┼──►│ - PLC (Packet Loss Concealment)     │ │
│  └─────────────────────────┘   │   └─────────────────────────────────────┘ │
│                                 │                                            │
│  ┌─────────────────────────┐   │   ┌─────────────────────────────────────┐ │
│  │ USB High-Speed          │   │   │ I2S DMA Streaming                   │ │
│  │ - MIDI class            │   │   │ - Ping-pong buffers                 │ │
│  │ - Wi-Fi data bridge     │   │   │ - 48kHz/16-bit stereo               │ │
│  └─────────────────────────┘   │   │ - DMA half/complete callbacks       │ │
│                                 │   └─────────────────────────────────────┘ │
│  ┌─────────────────────────┐   │                                            │
│  │ Wi-Fi (WHD + SDIO)      │   │   ┌─────────────────────────────────────┐ │
│  │ - SDIO CMD52/CMD53      │   │   │ Audio Buffers                       │ │
│  │ - Packet forwarding     │   │   │ - Ring buffers with metadata        │ │
│  └─────────────────────────┘   │   │ - Frame synchronization             │ │
│                                 │   └─────────────────────────────────────┘ │
│  ┌─────────────────────────┐   │                                            │
│  │ MIDI Router             │   │                                            │
│  │ - BLE ↔ USB ↔ UART      │   │                                            │
│  └─────────────────────────┘   │                                            │
│                                 │                                            │
├─────────────────────────────────┼───────────────────────────────────────────┤
│                        IPC (Inter-Processor Communication)                   │
│  ┌───────────────────────────────────────────────────────────────────────┐ │
│  │ Shared Memory + FreeRTOS Queues                                       │ │
│  │ - LC3 TX Queue: CM55 encoded frames → CM33 ISOC TX                    │ │
│  │ - LC3 RX Queue: CM33 ISOC RX → CM55 decode                            │ │
│  └───────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Why Dual-Core?

| Core | Responsibilities | Rationale |
|------|-----------------|-----------|
| **CM33** | BT stack, USB, Wi-Fi, MIDI | BTSTACK and WHD are optimized for Cortex-M33 |
| **CM55** | LC3 codec, I2S, audio DSP | Helium (MVE) SIMD for efficient DSP operations |

**Performance Benefits:**
- LC3 codec benefits from CM55's Helium DSP (up to 8x faster than CM33)
- Control plane runs concurrently with audio processing
- No contention between BT stack callbacks and codec execution
- I2S DMA callbacks have deterministic timing on dedicated core

## Hardware Block Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Main Application Processor                            │
│                    (External Instrument / Host Device)                       │
└────────────────────────────┬────────────────────┬───────────────────────────┘
                             │ USB High-Speed     │ I2S Audio
                             │ (480 Mbps)         │ (48kHz/16-bit)
                             │ MIDI + Data        │
                             ▼                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      PSoC Edge E82 (PSE823GOS4DBZQ3)                        │
│                                                                              │
│  ┌────────────────────────────────┐   ┌────────────────────────────────────┐│
│  │         CM33 Core              │   │           CM55 Core                ││
│  │        (Control Plane)         │   │         (Audio DSP)                ││
│  │                                │   │                                    ││
│  │  ┌──────────┐ ┌──────────────┐│   │  ┌──────────────┐ ┌──────────────┐ ││
│  │  │ USB HS   │ │   BTSTACK    ││   │  │  I2S DMA     │ │   liblc3     │ ││
│  │  │ emUSB    │ │  LE Audio    ││   │  │ Ping-pong    │ │ LC3 Codec    │ ││
│  │  │ MIDI+Data│ │  Profiles    ││   │  │ Buffers      │ │ Helium DSP   │ ││
│  │  └────┬─────┘ └──────┬───────┘│   │  └──────┬───────┘ └──────┬───────┘ ││
│  │       │              │        │   │         │                │         ││
│  │  ┌────▼────┐  ┌──────▼──────┐ │   │  ┌──────▼────────────────▼───────┐ ││
│  │  │ Wi-Fi   │  │  HCI ISOC   │ │◄──┼──│    Audio Task + IPC Task      │ ││
│  │  │ WHD     │  │  Handler    │ │   │  │  LC3 Encode/Decode + Buffers  │ ││
│  │  │ SDIO    │  │             │ │   │  │                               │ ││
│  │  └────┬────┘  └─────────────┘ │   │  └───────────────────────────────┘ ││
│  │       │                       │   │                                    ││
│  │  ┌────▼────────────────────┐  │   │                                    ││
│  │  │  MIDI Router            │  │   │                                    ││
│  │  │  BLE ↔ USB ↔ UART       │  │   │                                    ││
│  │  └─────────────────────────┘  │   │                                    ││
│  └────────────────────────────────┘   └────────────────────────────────────┘│
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────────┐│
│  │                    IPC (Shared Memory + Queues)                          ││
│  │   TX: CM55 (I2S RX → LC3 Encode) → CM33 (ISOC TX)                       ││
│  │   RX: CM33 (ISOC RX) → CM55 (LC3 Decode → I2S TX)                       ││
│  └──────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  FreeRTOS: CM33 (BLE, USB, WiFi, MIDI) | CM55 (Audio, IPC)                  │
└──────────────────────────────┬──────────────────────┬───────────────────────┘
                               │ UART (HCI+ISOC)      │ SDIO (Wi-Fi Data)
                               │ 3 Mbps               │ Up to 208 MHz
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

---

## Data Paths

### Path 1: LE Audio TX (I2S → LC3 → ISOC) - Dual-Core

PCM audio from main controller is encoded to LC3 on CM55 and transmitted via CM33.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Main Ctrl    │───►│ CM55         │───►│ CM55         │───►│ IPC Queue    │───►│ CM33         │───►│ CYW55512     │
│ I2S PCM      │    │ I2S RX DMA   │    │ Audio Task   │    │ TX (LC3)     │    │ ISOC Handler │    │ BLE Radio    │
│ 48kHz/16bit  │    │ Ping-pong    │    │ LC3 Encode   │    │ Shared Mem   │    │ HCI ISOC TX  │    │ CIS/BIS      │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
       │                   │                   │                   │                   │                   │
       │              ═══════════ CM55 Core ══════════             ╠═══════ CM33 Core ═══════╣           │
       │            i2s_stream.c  lc3_wrapper.c                   │      isoc_handler.c                  │
```

**Implementation Status:**
- [x] I2S DMA buffer structure (ping-pong) - CM55
- [x] Audio ring buffers with metadata - CM55
- [x] LC3 wrapper calling liblc3 (Helium DSP) - CM55
- [x] IPC queue for LC3 frames (CM55 → CM33)
- [x] ISOC handler state machine - CM33
- [x] `cyhal_i2s_init()` - HAL integration on CM55
- [x] `isoc_handler_tx_frame()` - Wired to IPC queue on CM33
- [x] `hci_isoc_send_data()` - Wired to BTSTACK via `wiced_bt_isoc_write()`

### Path 2: LE Audio RX (ISOC → LC3 → I2S) - Dual-Core

LC3 audio from Bluetooth is received on CM33 and decoded on CM55.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ CYW55512     │───►│ CM33         │───►│ IPC Queue    │───►│ CM55         │───►│ CM55         │───►│ Main Ctrl    │
│ BLE Radio    │    │ ISOC Handler │    │ RX (LC3)     │    │ Audio Task   │    │ I2S TX DMA   │    │ I2S PCM      │
│ CIS/BIS      │    │ HCI ISOC RX  │    │ Shared Mem   │    │ LC3 Decode   │    │ Ping-pong    │    │ 48kHz/16bit  │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
       │                   │                   │                   │                   │                   │
       │              ═══ CM33 Core ═══        ╠═══════════════ CM55 Core ════════════════╣               │
       │           isoc_handler.c              │      lc3_wrapper.c    i2s_stream.c                       │
```

**Implementation Status:**
- [x] ISOC RX buffer structure - CM33
- [x] IPC queue for LC3 frames (CM33 → CM55)
- [x] LC3 decode with PLC (packet loss concealment) - CM55
- [x] I2S TX buffer management - CM55
- [x] BTSTACK ISOC data callback registration - CM33
- [x] `isoc_handler_rx_frame()` - Posts to IPC queue on CM33
- [x] `cyhal_i2s_write_async()` - HAL integration via `i2s_stream_write()` on CM55

### Path 3: MIDI (USB ↔ BLE ↔ Controller)

MIDI events routed between USB host, BLE devices, and main controller.

```
                    ┌──────────────┐
                    │ USB Host     │
                    │ MIDI App     │
                    └──────┬───────┘
                           │ USB MIDI Class
                           ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Main Ctrl    │◄──►│ MIDI Router  │◄──►│ BLE MIDI     │
│ UART MIDI    │    │ FreeRTOS     │    │ GATT Service │
│ (31.25kbps)  │    │ Queues       │    │ Notifications│
└──────────────┘    └──────────────┘    └──────┬───────┘
       │                   │                   │
  midi_router.c      midi_router.c      midi_ble_service.c
                                               │
                                               ▼
                                        ┌──────────────┐
                                        │ CYW55512     │
                                        │ BLE Radio    │
                                        └──────────────┘
```

**MIDI Router Architecture:**
```c
// Routing rules (configured in midi_router.c)
BLE      → USB, Controller
USB      → BLE, Controller
Controller → BLE, USB
Internal → All (for generated events)
```

**Implementation Status:**
- [x] MIDI message parsing and generation
- [x] BLE MIDI packet format (timestamps)
- [x] USB MIDI packet format (cable numbers)
- [x] FreeRTOS routing queues with mutex
- [x] `emUSB-Device` initialization with MIDI class
- [x] BTSTACK GATT service registration
- [x] USB MIDI TX/RX with `USBD_MIDI_Write()` / `USBD_MIDI_Receive()`
- [x] FreeRTOS tick-based timestamp generation
- [x] Public `midi_usb_receive()` API for polling RX queue

### Path 4: Wi-Fi Bridge (USB HS → SDIO)

Network data bridged from USB to Wi-Fi for the main controller.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ App Processor│───►│ PSoC Edge    │───►│ Wi-Fi Bridge │───►│ SDIO Driver  │───►│ CYW55512     │
│ USB HS Host  │    │ USB Bulk EP  │    │ TX Queue     │    │ CMD52/CMD53  │    │ WLAN Radio   │
│ (Network)    │    │ emUSB-Device │    │ FreeRTOS     │    │ cyhal_sdio   │    │ 802.11ax     │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
                           │                   │                   │
                    wifi_bridge.c       wifi_bridge.c       WHD (cyhal_sdio)
```

**Implementation Status:**
- [x] Packet buffer management (16 buffers)
- [x] TX/RX queues with FreeRTOS
- [x] SDIO command scaffolding
- [x] `emUSB-Device` bulk endpoint init (`USBD_BULK_Add()`)
- [x] `whd_init()` - Wi-Fi Host Driver initialization
- [x] `whd_wifi_on()` - Wi-Fi power on
- [x] `whd_network_register_link_callback()` - Link state notifications
- [x] `cyhal_sdio_init()` - HAL integration
- [x] `cyhal_sdio_send_cmd()` - CMD52 (register read/write)
- [x] `cyhal_sdio_bulk_transfer()` - CMD53 (block data)
- [x] `cyhal_sdio_register_callback()` - Async DMA with IRQ

---

## Software Architecture

### FreeRTOS Dual-Core Task Structure

The firmware runs FreeRTOS on both cores with separate schedulers:

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                  PSoC Edge E82/E84                                       │
├─────────────────────────────────────────┬───────────────────────────────────────────────┤
│           CM33 Core Scheduler           │             CM55 Core Scheduler               │
│           (Control Plane)               │              (Audio DSP)                      │
├─────────────┬───────────┬───────────────┼───────────────────┬───────────────────────────┤
│ BLE Task    │ USB Task  │ Wi-Fi Task    │ Audio/LC3 Task    │ IPC Task                  │
│ Priority: 5 │ Priority: │ Priority: 3   │ Priority: Highest │ Priority: High            │
│             │ 4         │               │                   │                           │
├─────────────┼───────────┼───────────────┼───────────────────┼───────────────────────────┤
│ BTSTACK     │ USB HS    │ SDIO TX/RX    │ LC3 encode        │ TX Queue poll             │
│ le_audio_   │ MIDI class│ WHD packets   │ LC3 decode        │ (CM55 → CM33)             │
│ process()   │ Data      │ wifi_bridge_  │ I2S DMA ISR       │ RX Queue receive          │
│ ISOC ctrl   │ bridge    │ process()     │ Frame sync        │ (CM33 → CM55)             │
├─────────────┼───────────┼───────────────┼───────────────────┼───────────────────────────┤
│ MIDI Task   │           │               │ I2S DMA (ISR)     │                           │
│ Priority: 2 │           │               │ Highest priority  │                           │
│ BLE/USB     │           │               │ DMA callbacks     │                           │
│ routing     │           │               │                   │                           │
└─────────────┴───────────┴───────────────┴───────────────────┴───────────────────────────┘
                          │                                   │
                          └─────────── IPC (Shared Memory) ───┘
```

### Task Distribution by Core

| Core | Task | Priority | Stack | Purpose |
|------|------|----------|-------|---------|
| **CM55** | I2S DMA | ISR | - | DMA half/complete callbacks |
| **CM55** | Audio/LC3 | Highest | 4096 | LC3 encode/decode, frame sync |
| **CM55** | IPC | High | 2048 | Inter-processor queue management |
| **CM33** | BLE | 5 | 4096 | BTSTACK, LE Audio control plane |
| **CM33** | USB | 4 | 2048 | USB enumeration, MIDI class |
| **CM33** | Wi-Fi | 3 | 4096 | WHD packet processing |
| **CM33** | MIDI | 2 | 1024 | BLE/USB routing |

### Task Stack Sizes

| Task | Core | Stack Size | Purpose |
|------|------|------------|---------|
| Audio | CM55 | 4096 bytes | LC3 codec requires significant stack (Helium DSP) |
| IPC | CM55 | 2048 bytes | Queue management, shared memory access |
| BLE | CM33 | 4096 bytes | BTSTACK callback processing |
| USB | CM33 | 2048 bytes | USB enumeration and data |
| Wi-Fi | CM33 | 4096 bytes | WHD packet processing |
| MIDI | CM33 | 1024 bytes | Lightweight routing |

### Inter-Task Communication

#### Inter-Processor Communication (IPC) - CM33 ↔ CM55

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    IPC Architecture (Shared Memory)                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  LC3 TX Path (Audio Encode - CM55 → CM33):                                  │
│    CM55 Audio Task → [g_lc3_tx_queue] → CM33 ISOC Handler                   │
│    - Queue holds encoded LC3 frames (max 155 bytes each)                    │
│    - CM55 posts after encode, CM33 polls for ISOC TX                        │
│                                                                              │
│  LC3 RX Path (Audio Decode - CM33 → CM55):                                  │
│    CM33 ISOC Handler → [g_lc3_rx_queue] → CM55 Audio Task                   │
│    - Queue holds received LC3 frames from BTSTACK                           │
│    - CM33 posts on ISOC RX callback, CM55 polls for decode                  │
│                                                                              │
│  Shared Memory Region:                                                       │
│    - Located in SOCMEM (accessible by both cores)                           │
│    - Queue structures use atomic operations                                  │
│    - No mutex needed (single producer, single consumer)                     │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### Intra-Core Synchronization

```
┌─────────────────────────────────────────────────────────────────┐
│              CM33 FreeRTOS Synchronization                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  LE Audio Manager (le_audio_manager.c):                         │
│    └── state_mutex        : State machine protection            │
│                                                                  │
│  MIDI Router (midi_router.c):                                   │
│    ├── msg_queue          : MIDI message routing                │
│    └── queue_mutex        : Thread-safe queue access            │
│                                                                  │
│  MIDI BLE (midi_ble_service.c):                                 │
│    └── tx_mutex           : Thread-safe GATT notifications      │
│                                                                  │
│  MIDI USB (midi_usb.c):                                         │
│    ├── tx_mutex           : Thread-safe EP IN                   │
│    └── rx_mutex           : Thread-safe EP OUT                  │
│                                                                  │
│  Wi-Fi Bridge (wifi_bridge.c):                                  │
│    ├── buffer_mutex       : Thread-safe buffer pool             │
│    ├── tx_queue           : USB → SDIO packets                  │
│    └── rx_queue           : SDIO → USB packets                  │
│                                                                  │
│  Wi-Fi SDIO (WHD via cyhal_sdio):                               │
│    └── Managed by WHD library internally                        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│              CM55 FreeRTOS Synchronization                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Audio Task (audio_task.c):                                     │
│    ├── pcm_rx_queue       : I2S RX DMA → LC3 encoder            │
│    ├── pcm_tx_queue       : LC3 decoder → I2S TX DMA            │
│    └── i2s_mutex          : I2S hardware access                 │
│                                                                  │
│  Audio Buffers (audio_buffers.c):                               │
│    └── ring_mutex         : Thread-safe ring buffer access      │
│                                                                  │
│  LC3 Wrapper (lc3_wrapper.c):                                   │
│    └── codec_mutex        : Thread-safe encoder/decoder state   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## LE Audio Profile Stack

```
┌─────────────────────────────────────────┐
│          CAP (Common Audio Profile)     │  (Future)
├─────────────────────────────────────────┤
│  BAP (Basic Audio Profile)              │  bap_unicast.c, bap_broadcast.c
│  ├── Unicast Client/Server (CIS)        │
│  └── Broadcast Source/Sink (BIS)        │  Auracast
├─────────────────────────────────────────┤
│  PACS (Published Audio Capabilities)    │  pacs.c
│  ASCS (Audio Stream Control Service)    │  (in le_audio_manager.c)
│  BASS (Broadcast Audio Scan Service)    │  (Future)
├─────────────────────────────────────────┤
│  HCI ISOC (Isochronous Channels)        │  hci_isoc.c, isoc_handler.c
│  ├── CIS (Connected Isochronous Stream) │  Unicast
│  └── BIS (Broadcast Isochronous Stream) │  Auracast
├─────────────────────────────────────────┤
│  liblc3 (Host-Side Codec)               │  lc3_wrapper.c
│  Google LC3 - Apache 2.0 License        │
└─────────────────────────────────────────┘
```

### BAP State Machine (Unicast)

```
        ┌─────────────┐
        │    IDLE     │
        └──────┬──────┘
               │ Discover PACS
               ▼
        ┌─────────────┐
        │  DISCOVERY  │
        └──────┬──────┘
               │ Read PAC records
               ▼
        ┌─────────────┐
        │ CODEC_CONFIG│ ◄── Configure LC3 parameters
        └──────┬──────┘
               │ Write ASE Codec Config
               ▼
        ┌─────────────┐
        │  QOS_CONFIG │ ◄── Configure CIG/CIS parameters
        └──────┬──────┘
               │ Write ASE QoS Config
               ▼
        ┌─────────────┐
        │   ENABLING  │ ◄── Create CIS
        └──────┬──────┘
               │ Write ASE Enable
               ▼
        ┌─────────────┐
        │  STREAMING  │ ◄── LC3 data flowing
        └─────────────┘
```

### BAP State Machine (Broadcast/Auracast)

```
        ┌─────────────┐
        │    IDLE     │
        └──────┬──────┘
               │ Configure broadcast
               ▼
        ┌─────────────┐
        │ CONFIGURING │ ◄── Set LC3 config, build BASE
        └──────┬──────┘
               │ Start periodic advertising
               ▼
        ┌─────────────┐
        │   ENABLED   │ ◄── Extended + Periodic ADV
        └──────┬──────┘
               │ Create BIG
               ▼
        ┌─────────────┐
        │  STREAMING  │ ◄── LC3 data on BIS
        └─────────────┘
```

---

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

| Operation | Time | % of 10ms Frame |
|-----------|------|-----------------|
| LC3 Encode (mono) | ~5 ms | 50% |
| LC3 Decode (mono) | ~3.75 ms | 37.5% |
| Full Duplex Mono | ~8.75 ms | 87.5% |
| Full Duplex Stereo | ~17.5 ms | 175%* |

*Note: Stereo full-duplex exceeds single-core budget. Options:
1. Use mono for full-duplex (recommended)
2. Use 24kHz sample rate for stereo
3. Optimize with Helium DSP SIMD instructions

---

## Memory Map

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

---

## Key Design Decisions

### 1. Host-Side LC3 (vs CYW55512 Offload)

**Decision**: Run LC3 codec on PSoC Edge host using liblc3

**Rationale**:
- Unified architecture for unicast and broadcast
- Full control over codec parameters
- Easier debugging and audio path visibility
- Auracast requires host-side LC3 anyway (CYW55512 offload is unicast-only)

**Trade-off**: Higher CPU load on PSoC, but Cortex-M55 @ 400MHz can handle it.

### 2. FreeRTOS (vs Zephyr)

**Decision**: Use FreeRTOS with ModusToolbox

**Rationale**:
- CYW55512 driver not available in Zephyr
- ModusToolbox has mature PSoC Edge support
- Infineon BTSTACK is FreeRTOS-native
- Port only BAP profiles from Zephyr (not entire OS)

### 3. HCI Mode (vs Embedded Mode)

**Decision**: Use CYW55512 in HCI mode

**Rationale**:
- Host controls all profile logic
- LC3 encoding/decoding on host
- More flexibility for custom profiles
- Standard HCI allows potential portability

### 4. USB High-Speed (vs Full-Speed)

**Decision**: Use USB 2.0 High-Speed (480 Mbps)

**Rationale**:
- Wi-Fi bridge requires high throughput
- USB FS (12 Mbps) cannot sustain Wi-Fi 6 data rates
- PSoC Edge E82/E84 has USB HS hardware

---

## File Organization

### Dual-Core Project Structure

```
infineon-le-audio/
├── CMakeLists.txt                  # Standalone CMake build (alternative)
├── cmake/
│   ├── arm-cortex-m55.cmake        # Toolchain file
│   └── psoc_edge_e82.ld            # Linker script
│
├── config/
│   ├── FreeRTOSConfig.h            # FreeRTOS settings
│   ├── cy_bt_config.h              # Bluetooth configuration
│   └── lc3_config.h                # LC3 codec parameters
│
├── source/                          # Shared source code (both cores)
│   ├── main.c                       # Original standalone main (reference)
│   │
│   ├── audio/                       # ══════ Built on CM55 ══════
│   │   ├── audio_task.c/h           # Main audio processing task
│   │   ├── audio_buffers.c/h        # Ring buffer management
│   │   ├── i2s_stream.c/h           # I2S DMA driver (Helium DSP)
│   │   └── lc3_wrapper.c/h          # liblc3 encode/decode API
│   │
│   ├── le_audio/                    # ══════ Built on CM33 ══════
│   │   ├── le_audio_manager.c/h     # Top-level LE Audio control
│   │   ├── bap_unicast.c/h          # BAP unicast client/server
│   │   ├── bap_broadcast.c/h        # BAP broadcast source (Auracast)
│   │   ├── pacs.c/h                 # Published Audio Capabilities
│   │   └── isoc_handler.c/h         # HCI ISOC data path
│   │
│   ├── midi/                        # ══════ Built on CM33 ══════
│   │   ├── midi_ble_service.c/h     # BLE MIDI GATT service
│   │   ├── midi_usb.c/h             # USB MIDI class
│   │   └── midi_router.c/h          # MIDI routing logic
│   │
│   ├── wifi/                        # ══════ Built on CM33 ══════
│   │   └── wifi_bridge.c/h          # USB-Wi-Fi data bridge (uses WHD/cyhal_sdio)
│   │
│   └── bluetooth/                   # ══════ Built on CM33 ══════
│       ├── bt_init.c/h              # BTSTACK initialization
│       ├── bt_platform_config.c/h   # HCI UART configuration
│       ├── hci_isoc.c/h             # HCI isochronous commands
│       ├── gap_config.c/h           # GAP advertising/scanning
│       └── gatt_db.c/h              # GATT database
│
├── mtb/                             # ModusToolbox Project (Recommended)
│   ├── le-audio/
│   │   ├── proj_cm33_s/             # CM33 Secure core (TrustZone bootstrap)
│   │   │   └── main.c               # Minimal secure bootstrap
│   │   │
│   │   ├── proj_cm33_ns/            # CM33 Non-Secure (Control Plane)
│   │   │   ├── source/
│   │   │   │   ├── main.c           # CM33 entry: BT, USB, WiFi, MIDI
│   │   │   │   ├── app_le_audio.c   # Bridge to LE Audio manager
│   │   │   │   └── main_example.c   # Original Infineon example (reference)
│   │   │   └── Makefile             # CM33 build: BTSTACK, emUSB, WHD
│   │   │
│   │   ├── proj_cm55/               # CM55 Core (Audio DSP)
│   │   │   ├── main.c               # CM55 entry: LC3, I2S, IPC
│   │   │   ├── main_example.c       # Original Infineon example (reference)
│   │   │   └── Makefile             # CM55 build: liblc3, audio
│   │   │
│   │   ├── bsps/                    # Board Support Package
│   │   ├── configs/                 # Device configuration
│   │   ├── common.mk                # Shared build settings
│   │   └── Makefile                 # Top-level: builds all three cores
│   │
│   └── mtb_shared/                  # Shared library dependencies
│
├── libs/                            # External libraries (submodules)
│   ├── btstack/                     # Infineon BTSTACK
│   ├── btstack-integration/         # HCI-UART porting layer
│   ├── liblc3/                      # Google LC3 codec
│   ├── emusb-device/                # Segger USB middleware
│   ├── wifi-host-driver/            # Infineon WHD
│   └── freertos/                    # FreeRTOS kernel
│
└── docs/
    ├── README.md                    # Project plan & analysis
    └── architecture.md              # This file
```

### Source File Distribution by Core

| Core | Source Files | Purpose |
|------|--------------|---------|
| **CM55** | `audio/audio_task.c` | Main audio processing loop |
| **CM55** | `audio/audio_buffers.c` | Ring buffer management |
| **CM55** | `audio/i2s_stream.c` | I2S DMA with Helium DSP |
| **CM55** | `audio/lc3_wrapper.c` | liblc3 encode/decode |
| **CM33** | `le_audio/le_audio_manager.c` | LE Audio state machine |
| **CM33** | `le_audio/bap_unicast.c` | BAP Unicast Client/Server |
| **CM33** | `le_audio/bap_broadcast.c` | BAP Broadcast (Auracast) |
| **CM33** | `le_audio/pacs.c` | Published Audio Capabilities |
| **CM33** | `le_audio/isoc_handler.c` | HCI ISOC data path |
| **CM33** | `bluetooth/bt_platform_config.c` | HCI UART configuration |
| **CM33** | `bluetooth/hci_isoc.c` | HCI isochronous commands |
| **CM33** | `midi/midi_ble_service.c` | BLE MIDI GATT service |
| **CM33** | `midi/midi_usb.c` | USB MIDI class |
| **CM33** | `midi/midi_router.c` | MIDI routing logic |
| **CM33** | `wifi/wifi_bridge.c` | USB-Wi-Fi data bridge (uses WHD/cyhal_sdio) |

---

## Hardware Interfaces

### HCI UART (PSoC Edge ↔ CYW55512 Bluetooth)

| Parameter | Value |
|-----------|-------|
| Baud Rate (FW Download) | 115200 bps |
| Baud Rate (Feature) | 3,000,000 bps |
| Flow Control | RTS/CTS (required) |
| Data Format | 8N1 |

Pin assignments (default, configurable in `bt_platform_config.h`):
```
CYBSP_BT_UART_TX  = P5_0  (PSoC TX → CYW55512 RX)
CYBSP_BT_UART_RX  = P5_1  (PSoC RX ← CYW55512 TX)
CYBSP_BT_UART_RTS = P5_2  (PSoC RTS → CYW55512 CTS)
CYBSP_BT_UART_CTS = P5_3  (PSoC CTS ← CYW55512 RTS)
CYBSP_BT_POWER    = P6_0  (BT chip power control)
```

### SDIO (PSoC Edge ↔ CYW55512 Wi-Fi)

| Parameter | Value |
|-----------|-------|
| Interface | SDIO 3.0 |
| Speed Mode | SDR50 / DDR50 |
| Clock | Up to 208 MHz |
| Bus Width | 4-bit |
| Block Size | 512 bytes |

### I2S (PSoC Edge ↔ Main Controller)

| Parameter | Value |
|-----------|-------|
| Sample Rate | 48000 Hz |
| Bit Depth | 16-bit |
| Channels | Stereo |
| Mode | Master (PSoC generates clocks) |
| Format | I2S standard |

### USB High-Speed

| Parameter | Value |
|-----------|-------|
| Speed | 480 Mbps (High-Speed) |
| MIDI Endpoint Size | 512 bytes |
| Bulk Endpoint Size | 512 bytes |

---

## References

- [Bluetooth Core 6.0 Specification](https://www.bluetooth.com/specifications/specs/)
- [BAP Specification](https://www.bluetooth.com/specifications/specs/basic-audio-profile-1-0-1/)
- [Google liblc3](https://github.com/google/liblc3)
- [Zephyr LE Audio](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html)
- [CYW55512 Documentation](https://www.infineon.com/cms/en/product/wireless-connectivity/airoc-wi-fi-plus-bluetooth-combos/wi-fi-6-6e-802.11ax/)
- [PSoC Edge E82 Datasheet](https://www.infineon.com/cms/en/product/microcontroller/32-bit-psoc-arm-cortex-microcontroller/psoc-edge-microcontrollers/)
- [BTSTACK API Reference](https://infineon.github.io/btstack/dual_mode/api_reference_manual/html/modules.html)
