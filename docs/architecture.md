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
│  │ - CDC/ACM (AT commands) │   │   │ - 48kHz/16-bit stereo               │ │
│  │ - Wi-Fi data bridge     │   │   │ - DMA half/complete callbacks       │ │
│  └─────────────────────────┘   │   └─────────────────────────────────────┘ │
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
Supports both Unicast (CIS) and Broadcast Sink (BIS/Auracast RX).

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
- [x] Broadcast Sink state machine (`bap_broadcast_sink.c`)
- [x] Extended scanning for Auracast broadcasts
- [x] Periodic advertising sync (PA sync)
- [x] BASE parser (Broadcast Audio Source Endpoint)
- [x] BIG sync for receiving BIS streams
- [x] Multi-callback ISOC registry (supports multiple modules)

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

### Path 5: AT Command Interface (USB CDC/ACM)

AT-style command interface for configuring Bluetooth, Wi-Fi, and LE Audio via USB virtual serial port.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Host PC      │───►│ USB CDC/ACM  │───►│ AT Parser    │───►│ Command      │
│ Terminal     │    │ emUSB-Device │    │ Tokenizer    │    │ Handlers     │
│ (AT+...)     │    │ EP 0x82/0x02 │    │ argc/argv    │    │ BT/WiFi/LEA  │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
                           │                   │                   │
                    cdc_acm.c           at_parser.c         at_*_cmds.c
```

**AT Command Categories:**

| Category | Commands | Handler File |
|----------|----------|--------------|
| System | `AT`, `ATI`, `AT+VERSION?`, `AT+RST`, `AT+ECHO` | `at_system_cmds.c` |
| Bluetooth | `AT+BTINIT`, `AT+BTSTATE?`, `AT+BTNAME`, `AT+GAPADVSTART`, `AT+GAPSCAN`, `AT+GAPCONN` | `at_bt_cmds.c` |
| LE Audio | `AT+LEAINIT`, `AT+LEABROADCAST`, `AT+LEAUNICAST`, `AT+LEACODEC`, `AT+LEAINFO?` | `at_leaudio_cmds.c` |
| Wi-Fi | `AT+WIFIINIT`, `AT+WIFISCAN`, `AT+WIFIJOIN`, `AT+WIFIBRIDGE`, `AT+WIFIRSSI?` | `at_wifi_cmds.c` |

**USB Composite Device Architecture:**

```
┌─────────────────────────────────────────────────────────────┐
│                  USB Composite Device                        │
├─────────────────────────┬───────────────────────────────────┤
│   MIDI (existing)       │      CDC/ACM (new)                │
│   EP 0x81/0x01          │      EP 0x82/0x02/0x83            │
│   midi_usb.c            │      cdc_acm.c                    │
└─────────────────────────┴───────────────────────────────────┘
                                      │
                          ┌───────────▼───────────┐
                          │   AT Parser           │
                          │   at_parser.c         │
                          └───────────┬───────────┘
               ┌──────────────────────┼──────────────────────┐
               ▼                      ▼                      ▼
      ┌────────────────┐    ┌────────────────┐    ┌────────────────┐
      │ at_bt_cmds.c   │    │ at_wifi_cmds.c │    │at_leaudio_cmds │
      │ BTSTACK APIs   │    │ WHD APIs       │    │ LE Audio APIs  │
      └────────────────┘    └────────────────┘    └────────────────┘
```

**Implementation Status:**
- [x] USB CDC/ACM class with emUSB-Device (`USBD_CDC_Add()`)
- [x] AT command parser with line buffering and tokenization
- [x] CME error codes (3GPP TS 27.007 based)
- [x] System commands (AT, ATI, VERSION, RST, ECHO)
- [x] Bluetooth commands (BTINIT, BTSTATE, BTNAME, GAP operations)
- [x] LE Audio commands (LEAINIT, LEABROADCAST, LEAUNICAST, LEACODEC)
- [x] Wi-Fi commands (WIFIINIT, WIFISCAN, WIFIJOIN, WIFIBRIDGE)
- [x] USB composite device (MIDI + CDC)
- [x] Async URC (Unsolicited Result Code) support for scan results

---

## Software Architecture

### Initialization Architecture

The firmware uses a coordinated initialization sequence between CM33 and CM55 cores,
synchronized via FreeRTOS event groups. This architecture ensures:
- No deadlocks if BT or CM55 initialization fails
- Graceful degradation (system continues with reduced functionality)
- Deterministic startup sequence

#### Event Group Synchronization

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Event Group: g_system_events                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  EVT_BT_READY     (bit 0)  │  BT stack initialized (BTM_ENABLED_EVT)        │
│  EVT_CM55_READY   (bit 1)  │  CM55 IPC ready (audio_ipc_is_ready())         │
│  EVT_SYSTEM_READY (bit 2)  │  All modules initialized, tasks may proceed    │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### Initialization Sequence

```
CM33 main()                                    CM55 main()
───────────                                    ───────────
│ cybsp_init()                                 │ cybsp_init()
│ setup_clib_support()                         │ setup_clib_support()
│ setup_tickless_idle_timer()                  │ setup_tickless_idle_timer()
│ app_kv_store_init()                          │ __enable_irq()
│ button_lib_init()                            │
│                                              │
│ audio_ipc_init_primary() ◄─────────────────► │ (CM55 boots here)
│   Sets magic, cm33_ready=true                │
│                                              │ init_audio_system()
│ Cy_SysEnableCM55() ──────────────────────────┤   audio_ipc_init_secondary()
│                                              │     Waits for cm33_ready (5s)
│ xEventGroupCreate()                          │     Sets cm55_ready=true
│                                              │   audio_task_init()
│ Create all FreeRTOS tasks:                   │   audio_task_start()
│   main_task (pri 6)                          │
│   ipc_debug_task (pri 1)                     │ xTaskCreate(ipc_task)
│   ble_task (pri 5)                           │
│   usb_task (pri 4)                           │
│   midi_task (pri 2)                          │
│   wifi_task (pri 3)                          │
│                                              │
│ application_start()                          │
│   bt_init() - async BT stack init            │
│                                              │
│ vTaskStartScheduler() ◄───────────────────── │ vTaskStartScheduler()
│         │                                    │         │
│         ▼                                    │         ▼
│   ┌─────────────┐                            │   ┌─────────────┐
│   │ main_task   │ (highest priority)         │   │ audio_task  │ (running)
│   │ Wait 15s    │                            │   │ ipc_task    │ (monitoring)
│   │ EVT_BT_READY│                            │   └─────────────┘
│   └─────────────┘                            │
│         │                                    │
│   BTM_ENABLED_EVT fires                      │
│   app_le_audio_on_bt_ready()                 │
│   xEventGroupSetBits(EVT_BT_READY)           │
│         │                                    │
│   main_task wakes:                           │
│     Poll CM55 IPC ready (5s timeout)         │
│     xEventGroupSetBits(EVT_CM55_READY)       │
│     init_control_modules()                   │
│     xEventGroupSetBits(EVT_SYSTEM_READY)     │
│         │                                    │
│   All tasks wake and proceed                 │
│         ▼                                    │
│   ble_task, usb_task, wifi_task, midi_task   │
│   All were waiting on EVT_SYSTEM_READY       │
└──────────────────────────────────────────────┘
```

#### Graceful Degradation

| Failure Scenario | Timeout | System Behavior |
|-----------------|---------|-----------------|
| BT init fails | 15s | System continues without Bluetooth |
| CM55 IPC fails | 5s | System continues without audio |
| Both fail | 20s | USB/MIDI only mode |

### FreeRTOS Dual-Core Task Structure

The firmware runs FreeRTOS on both cores with separate schedulers:

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                  PSoC Edge E82/E84                                       │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│           CM33 Core Scheduler (configMAX_PRIORITIES = 7)                                 │
├───────────────┬───────────┬───────────┬───────────┬───────────────┬─────────────────────┤
│ main_task     │ BLE Task  │ USB Task  │ Wi-Fi Task│ MIDI Task     │ ipc_debug_task      │
│ Priority: 6   │ Priority: │ Priority: │ Priority: │ Priority: 2   │ Priority: 1         │
│ (highest)     │ 5         │ 4         │ 3         │               │ (lowest)            │
├───────────────┼───────────┼───────────┼───────────┼───────────────┼─────────────────────┤
│ System init   │ BTSTACK   │ USB HS    │ SDIO TX/RX│ BLE/USB/UART  │ CM55 debug printf   │
│ Wait BT ready │ le_audio_ │ MIDI class│ WHD pkts  │ routing       │ (debug only)        │
│ Wait CM55 IPC │ process() │ CDC/ACM   │ wifi_     │               │                     │
│ init_control_ │ ISOC ctrl │ AT cmds   │ bridge_   │               │                     │
│ modules()     │           │           │ process() │               │                     │
│ Set SYS_READY │           │           │           │               │                     │
└───────────────┴───────────┴───────────┴───────────┴───────────────┴─────────────────────┘
                          │                                   │
                          └─────────── IPC (Shared Memory) ───┘
                          │                                   │
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│           CM55 Core Scheduler (configMAX_PRIORITIES = 7)                                 │
├───────────────────────────────────────────┬─────────────────────────────────────────────┤
│ Audio/LC3 Task                            │ IPC Task                                    │
│ Priority: 6 (highest)                     │ Priority: 5                                 │
├───────────────────────────────────────────┼─────────────────────────────────────────────┤
│ LC3 encode/decode                         │ Health monitoring (debug only)              │
│ I2S DMA coordination                      │ Stats logging every 10s                    │
│ Frame sync, PLC                           │ Queue overflow/underflow warnings           │
└───────────────────────────────────────────┴─────────────────────────────────────────────┘
```

### Task Distribution by Core

| Core | Task | Name | Priority | Stack | Purpose |
|------|------|------|----------|-------|---------|
| **CM33** | main_task | "MAIN" | 6 | 2048 | System init coordinator, then idle |
| **CM33** | ble_task | "BLE" | 5 | 4096 | BTSTACK, LE Audio control plane |
| **CM33** | usb_task | "USB" | 4 | 2048 | USB enumeration, MIDI + CDC/ACM |
| **CM33** | wifi_task | "WiFi" | 3 | 4096 | WHD packet processing |
| **CM33** | midi_task | "MIDI" | 2 | 1024 | BLE/USB/UART routing |
| **CM33** | ipc_debug_task | "IPC_DBG" | 1 | 512 | CM55 debug printf relay (debug only) |
| **CM55** | audio_task_main | "AudioDSP" | 6 | 4096 | LC3 encode/decode, I2S DMA |
| **CM55** | ipc_task | "IPC" | 5 | 2048 | Health monitoring (debug only) |

**Notes:**
- CDC/ACM AT command processing is integrated into the USB task
- `ipc_debug_task` (CM33) and `ipc_task` (CM55) are **debug/monitoring only**
- Audio frame IPC transfer happens inside `audio_task_main` via `audio_ipc_send/receive`
- Production builds can remove debug tasks to save ~10KB RAM

### Task Stack Sizes

| Task | Core | Stack Size | Purpose |
|------|------|------------|---------|
| main_task | CM33 | 2048 words | System init, event group waits |
| Audio | CM55 | 4096 words | LC3 codec requires significant stack (Helium DSP) |
| IPC | CM55 | 2048 words | Queue monitoring (debug only) |
| BLE | CM33 | 4096 words | BTSTACK callback processing |
| USB | CM33 | 2048 words | USB enumeration and data |
| Wi-Fi | CM33 | 4096 words | WHD packet processing |
| MIDI | CM33 | 1024 words | Lightweight routing |
| ipc_debug | CM33 | 512 words | Debug printf relay (debug only) |

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
│  BAP (Basic Audio Profile)              │  bap_unicast.c, bap_broadcast.c,
│  ├── Unicast Client/Server (CIS)        │  bap_broadcast_sink.c
│  ├── Broadcast Source (BIS TX)          │  Auracast TX
│  └── Broadcast Sink (BIS RX)            │  Auracast RX
├─────────────────────────────────────────┤
│  PACS (Published Audio Capabilities)    │  pacs.c
│  ASCS (Audio Stream Control Service)    │  (in le_audio_manager.c)
│  BASS (Broadcast Audio Scan Service)    │  (Future)
├─────────────────────────────────────────┤
│  HCI ISOC (Isochronous Channels)        │  hci_isoc.c, isoc_handler.c
│  ├── CIS (Connected Isochronous Stream) │  Unicast
│  ├── BIG Create (Broadcast TX)          │  Auracast Source
│  └── BIG Sync (Broadcast RX)            │  Auracast Sink
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

### BAP State Machine (Broadcast Source/Auracast TX)

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

### BAP State Machine (Broadcast Sink/Auracast RX)

```
        ┌─────────────┐
        │    IDLE     │
        └──────┬──────┘
               │ Start extended scanning
               ▼
        ┌─────────────┐
        │  SCANNING   │ ◄── Look for broadcast SID
        └──────┬──────┘
               │ Found broadcast, sync to PA
               ▼
        ┌─────────────┐
        │  PA_SYNCING │ ◄── Periodic ADV sync in progress
        └──────┬──────┘
               │ PA sync established, parse BASE
               ▼
        ┌─────────────┐
        │  PA_SYNCED  │ ◄── BASE parsed, BIGInfo received
        └──────┬──────┘
               │ BIG sync (select BIS indices)
               ▼
        ┌─────────────┐
        │ BIG_SYNCING │ ◄── Synchronizing to BIG
        └──────┬──────┘
               │ BIG sync established
               ▼
        ┌─────────────┐
        │  STREAMING  │ ◄── Receiving LC3 data on BIS
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
│   │   ├── bap_broadcast.c/h        # BAP broadcast source (Auracast TX)
│   │   ├── bap_broadcast_sink.c/h   # BAP broadcast sink (Auracast RX)
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
│   ├── usb/                         # ══════ Built on CM33 ══════
│   │   └── usb_composite.c/h        # USB composite device (MIDI + CDC)
│   │
│   ├── cdc/                         # ══════ Built on CM33 ══════
│   │   ├── cdc_acm.c/h              # USB CDC/ACM virtual serial port
│   │   ├── at_parser.c/h            # AT command parser (tokenizer, dispatcher)
│   │   ├── at_commands.h            # CME error codes and common definitions
│   │   ├── at_system_cmds.c/h       # System commands (AT, ATI, VERSION, RST)
│   │   ├── at_bt_cmds.c/h           # Bluetooth commands (BTINIT, GAP operations)
│   │   ├── at_leaudio_cmds.c/h      # LE Audio commands (broadcast, unicast)
│   │   └── at_wifi_cmds.c/h         # Wi-Fi commands (scan, join, bridge)
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
| **CM33** | `le_audio/bap_broadcast.c` | BAP Broadcast Source (Auracast TX) |
| **CM33** | `le_audio/bap_broadcast_sink.c` | BAP Broadcast Sink (Auracast RX) |
| **CM33** | `le_audio/pacs.c` | Published Audio Capabilities |
| **CM33** | `le_audio/isoc_handler.c` | HCI ISOC data path |
| **CM33** | `bluetooth/bt_platform_config.c` | HCI UART configuration |
| **CM33** | `bluetooth/hci_isoc.c` | HCI isochronous commands |
| **CM33** | `midi/midi_ble_service.c` | BLE MIDI GATT service |
| **CM33** | `midi/midi_usb.c` | USB MIDI class |
| **CM33** | `midi/midi_router.c` | MIDI routing logic |
| **CM33** | `wifi/wifi_bridge.c` | USB-Wi-Fi data bridge (uses WHD/cyhal_sdio) |
| **CM33** | `usb/usb_composite.c` | USB composite device (MIDI + CDC) |
| **CM33** | `cdc/cdc_acm.c` | USB CDC/ACM virtual serial port |
| **CM33** | `cdc/at_parser.c` | AT command parser and dispatcher |
| **CM33** | `cdc/at_system_cmds.c` | System AT commands |
| **CM33** | `cdc/at_bt_cmds.c` | Bluetooth AT commands |
| **CM33** | `cdc/at_leaudio_cmds.c` | LE Audio AT commands |
| **CM33** | `cdc/at_wifi_cmds.c` | Wi-Fi AT commands |

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
