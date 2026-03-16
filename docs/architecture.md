# Architecture Design

## System Overview

This document describes the architecture of the Infineon LE Audio demo for musical instruments, implementing full-duplex LE Audio, Auracast broadcast, MIDI over BLE/USB, and Wi-Fi data bridging.

## Target Hardware

| Component | Part Number | Description |
|-----------|-------------|-------------|
| **MCU** | PSE823GOS4DBZQ3 | PSoC Edge E82, Cortex-M55 @ 400MHz + Cortex-M33 |
| **Wireless** | CYW55512IUBGT | AIROC Wi-Fi 6 + Bluetooth 6.0 combo IC |
| **Eval Kit** | KIT_PSE84_EVAL | PSoC Edge E84 Evaluation Kit (USB HS, SDIO) |

## Hardware Block Diagram

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
│  │ emUSB-Device │  │  Audio DMA   │  │  BTSTACK +   │  │  wifi-host-drv   │ │
│  │  Middleware  │  │   Buffer     │  │  LE Audio    │  │  Wi-Fi Bridge    │ │
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

### Path 1: LE Audio TX (I2S → LC3 → ISOC)

PCM audio from main controller is encoded to LC3 and transmitted over Bluetooth.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Main Ctrl    │───►│ PSoC Edge    │───►│ Audio Task   │───►│ ISOC Handler │───►│ CYW55512     │
│ I2S PCM      │    │ I2S RX DMA   │    │ liblc3       │    │ HCI ISOC     │    │ BLE Radio    │
│ 48kHz/16bit  │    │ Ping-pong    │    │ LC3 Encode   │    │ CIS/BIS TX   │    │              │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
       │                   │                   │                   │                   │
       │    cyhal_i2s      │  audio_buffers    │   lc3_wrapper     │    hci_isoc       │
       │    i2s_stream.c   │  audio_task.c     │   audio_task.c    │    isoc_handler.c │
```

**Implementation Status:**
- [x] I2S DMA buffer structure (ping-pong)
- [x] Audio ring buffers with metadata
- [x] LC3 wrapper calling liblc3
- [x] ISOC handler state machine
- [ ] `cyhal_i2s_init()` - HAL integration
- [ ] `isoc_handler_send_sdu()` - Wire to audio task
- [ ] `hci_isoc_send_data()` - Wire to BTSTACK

### Path 2: LE Audio RX (ISOC → LC3 → I2S)

LC3 audio from Bluetooth is decoded and sent to main controller.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ CYW55512     │───►│ ISOC Handler │───►│ Audio Task   │───►│ PSoC Edge    │───►│ Main Ctrl    │
│ BLE Radio    │    │ HCI ISOC     │    │ liblc3       │    │ I2S TX DMA   │    │ I2S PCM      │
│              │    │ CIS/BIS RX   │    │ LC3 Decode   │    │ Ping-pong    │    │ 48kHz/16bit  │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

**Implementation Status:**
- [x] ISOC RX buffer structure
- [x] LC3 decode with PLC (packet loss concealment)
- [x] I2S TX buffer management
- [ ] BTSTACK ISOC data callback registration
- [ ] `cyhal_i2s_write_async()` - HAL integration

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
- [ ] `emUSB-Device` initialization
- [ ] BTSTACK GATT service registration
- [ ] Controller UART initialization

### Path 4: Wi-Fi Bridge (USB HS → SDIO)

Network data bridged from USB to Wi-Fi for the main controller.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ App Processor│───►│ PSoC Edge    │───►│ Wi-Fi Bridge │───►│ SDIO Driver  │───►│ CYW55512     │
│ USB HS Host  │    │ USB Bulk EP  │    │ TX Queue     │    │ CMD52/CMD53  │    │ WLAN Radio   │
│ (Network)    │    │ emUSB-Device │    │ FreeRTOS     │    │ cyhal_sdio   │    │ 802.11ax     │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
                           │                   │                   │
                    wifi_bridge.c       wifi_bridge.c       wifi_sdio.c
```

**Implementation Status:**
- [x] Packet buffer management (16 buffers)
- [x] TX/RX queues with FreeRTOS
- [x] SDIO command scaffolding
- [ ] `emUSB-Device` bulk endpoint init
- [ ] `whd_init()` - Wi-Fi Host Driver
- [ ] `cyhal_sdio_init()` - HAL integration

---

## Software Architecture

### FreeRTOS Task Structure

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           FreeRTOS Scheduler                                  │
├─────────────┬─────────────┬─────────────┬─────────────┬──────────┬──────────┤
│ I2S DMA     │ Audio/LC3   │ BLE Task    │ USB Task    │ Wi-Fi    │ MIDI     │
│ (ISR)       │ Priority: 6 │ Priority: 5 │ Priority: 4 │ Task     │ Task     │
│ Highest     │             │             │             │ Prio: 3  │ Prio: 2  │
├─────────────┼─────────────┼─────────────┼─────────────┼──────────┼──────────┤
│ DMA half/   │ LC3 encode  │ BTSTACK     │ USB HS enum │ SDIO TX  │ BLE/USB  │
│ complete    │ LC3 decode  │ bt_process()│ MIDI class  │ SDIO RX  │ routing  │
│ callbacks   │ Frame sync  │ le_audio_   │ Data bridge │ WHD pkts │ midi_    │
│             │ ISOC send   │ process()   │ midi_usb_   │ wifi_    │ router_  │
│             │             │             │ process()   │ bridge_  │ process()│
│             │             │             │             │ process()│          │
└─────────────┴─────────────┴─────────────┴─────────────┴──────────┴──────────┘
```

### Task Stack Sizes

| Task | Stack Size | Purpose |
|------|------------|---------|
| Audio | 4096 bytes | LC3 codec requires significant stack |
| BLE | 4096 bytes | BTSTACK callback processing |
| USB | 2048 bytes | USB enumeration and data |
| Wi-Fi | 4096 bytes | WHD packet processing |
| MIDI | 1024 bytes | Lightweight routing |

### Inter-Task Communication

```
┌─────────────────────────────────────────────────────────────────┐
│                   FreeRTOS Synchronization                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Audio Module (le_audio_manager.c):                             │
│    ├── tx_queue_handle    : PCM → LC3 encoder                   │
│    ├── rx_queue_handle    : LC3 decoder → PCM                   │
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
│  Wi-Fi SDIO (wifi_sdio.c):                                      │
│    └── bus_mutex          : Thread-safe SDIO bus access         │
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

```
infineon-le-audio/
├── CMakeLists.txt              # Build system
├── cmake/
│   ├── arm-cortex-m55.cmake    # Toolchain file
│   └── psoc_edge_e82.ld        # Linker script
│
├── config/
│   ├── FreeRTOSConfig.h        # FreeRTOS settings
│   ├── cy_bt_config.h          # Bluetooth configuration
│   └── lc3_config.h            # LC3 codec parameters
│
├── source/
│   ├── main.c                  # Application entry, task creation
│   │
│   ├── audio/
│   │   ├── audio_task.c/h      # Main audio processing task
│   │   ├── audio_buffers.c/h   # Ring buffer management
│   │   ├── i2s_stream.c/h      # I2S DMA driver
│   │   └── lc3_wrapper.c/h     # liblc3 encode/decode API
│   │
│   ├── le_audio/
│   │   ├── le_audio_manager.c/h# Top-level LE Audio control
│   │   ├── bap_unicast.c/h     # BAP unicast client/server
│   │   ├── bap_broadcast.c/h   # BAP broadcast source (Auracast)
│   │   ├── pacs.c/h            # Published Audio Capabilities
│   │   └── isoc_handler.c/h    # HCI ISOC data path
│   │
│   ├── midi/
│   │   ├── midi_ble_service.c/h# BLE MIDI GATT service
│   │   ├── midi_usb.c/h        # USB MIDI class
│   │   └── midi_router.c/h     # MIDI routing logic
│   │
│   ├── wifi/
│   │   ├── wifi_sdio.c/h       # SDIO driver for CYW55512
│   │   └── wifi_bridge.c/h     # USB-Wi-Fi data bridge
│   │
│   └── bluetooth/
│       ├── bt_init.c/h         # BTSTACK initialization
│       ├── bt_platform_config.c/h # HCI UART configuration
│       ├── hci_isoc.c/h        # HCI isochronous commands
│       ├── gap_config.c/h      # GAP advertising/scanning
│       └── gatt_db.c/h         # GATT database
│
├── libs/                        # External libraries (submodules)
│   ├── btstack/                 # Infineon BTSTACK
│   ├── btstack-integration/     # HCI-UART porting layer
│   ├── liblc3/                  # Google LC3 codec
│   ├── emusb-device/            # Segger USB middleware
│   ├── wifi-host-driver/        # Infineon WHD
│   └── freertos/                # FreeRTOS kernel
│
└── docs/
    ├── README.md               # Project plan & analysis
    └── architecture.md         # This file
```

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
