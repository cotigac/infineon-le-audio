# PSOC&trade; Edge MCU: LE Audio Demo

This firmware implements a comprehensive Bluetooth LE Audio solution for musical instruments on the PSOC&trade; Edge E84 MCU with AIROC&trade; CYW55513 Wi-Fi & Bluetooth&reg; combo chip. It provides full-duplex audio streaming, Auracast broadcast, MIDI over BLE/USB, and Wi-Fi data bridging.

This code example has a three project structure: CM33 secure, CM33 non-secure, and CM55 projects. All three projects are programmed to the external QSPI flash and executed in Execute in Place (XIP) mode. Extended boot launches the CM33 secure project from a fixed location in the external flash, which then configures the protection settings and launches the CM33 non-secure application. Additionally, CM33 non-secure application enables CM55 CPU and launches the CM55 application.

[View this README on GitHub.](https://github.com/cotigac/infineon-le-audio)

See the [Architecture Documentation](../../docs/architecture.md) for detailed system design.


## Key Features

Feature | Core | Description
--------|------|------------
LE Audio Unicast | CM33+CM55 | Full-duplex audio streaming via CIS (Connected Isochronous Stream)
LE Audio Broadcast (Auracast) | CM33+CM55 | One-to-many audio broadcast via BIS (Broadcast Isochronous Stream)
LC3 Codec | CM55 | Host-side LC3 encode/decode using Google liblc3 (Helium DSP)
BLE MIDI | CM33 | MIDI over Bluetooth Low Energy GATT service
USB MIDI | CM33 | USB High-Speed MIDI class device (480 Mbps)
I2S Streaming | CM55 | DMA-based bidirectional audio with ping-pong buffers
Wi-Fi Bridge | CM33 | USB HS to SDIO to CYW55512 WLAN data path
USB CDC/ACM | CM33 | AT command interface for BT/Wi-Fi/LE Audio configuration


## Dual-Core Architecture

Project | Core | Purpose
--------|------|--------
proj_cm33_s | Cortex-M33 (Secure) | TrustZone bootstrap, security configuration
proj_cm33_ns | Cortex-M33 (Non-Secure) | BLE stack, USB, Wi-Fi, MIDI control plane
proj_cm55 | Cortex-M55 | LC3 codec, I2S audio DSP (Helium acceleration)


## Requirements

- [ModusToolbox&trade;](https://www.infineon.com/modustoolbox) v3.6 or later (tested with v3.6)
- Board support package (BSP) minimum required version: 1.0.0
- Programming language: C
- Associated parts: [PSOC&trade; Edge E84 MCU](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm)


## Supported toolchains (make variable 'TOOLCHAIN')

- GNU Arm&reg; Embedded Compiler v14.2.1 (`GCC_ARM`) – Default value of `TOOLCHAIN`
- Arm&reg; Compiler v6.22 (`ARM`)
- IAR C/C++ Compiler v9.50.2 (`IAR`)
- LLVM Embedded Toolchain for Arm&reg; v19.1.5 (`LLVM_ARM`)

## Supported kits (make variable 'TARGET')

- [PSOC&trade; Edge E84 Evaluation Kit](https://www.infineon.com/KIT_PSE84_EVAL) (`KIT_PSE84_EVAL_EPC2`) – Default value of `TARGET`
- [PSOC&trade; Edge E84 Evaluation Kit](https://www.infineon.com/KIT_PSE84_EVAL) (`KIT_PSE84_EVAL_EPC4`)


## Hardware setup

This example uses the board's default configuration. See the kit user guide to ensure that the board is configured correctly.

Ensure the following jumper and pin configuration on board.
- BOOT SW must be in the HIGH/ON position
- J20 and J21 must be in the tristate/not connected (NC) position

### Audio Connections

Interface | Connection | Description
----------|------------|------------
I2S | External codec/controller | 48kHz/16-bit bidirectional audio
USB | Host PC or controller | MIDI + CDC/ACM + Wi-Fi bridge
UART | Debug terminal | 115200 baud, 8N1


## Software setup

See the [ModusToolbox&trade; tools package installation guide](https://www.infineon.com/ModusToolboxInstallguide) for information about installing and configuring the tools package.

Install a terminal emulator if you do not have one. Instructions in this document use [Tera Term](https://teratermproject.github.io/index-en.html).

This example requires no additional software or tools.


## Quick start (command line)

For users who prefer building and programming from the command line without using an IDE:

1. **Open a terminal** <br>
   Windows: Open *modus-shell* from the Start menu (provides access to all ModusToolbox&trade; tools) <br>
   Linux/macOS: Use any terminal application

2. **Navigate to the project directory** and run the following commands

3. **Fetch dependencies** (first time only): `make getlibs`

4. **Build all three cores**: `make build` <br>
   This creates *build/app_combined.hex* containing CM33 Secure, CM33 Non-Secure, and CM55 firmware.

5. **Program the board** – connect the board via the KitProg3 USB connector, then run: `make program`

6. **Open a terminal** at 115200 baud, 8N1 to view debug output from KitProg3 COM port.


## IDE project generation

To open this project in your preferred IDE, generate the IDE-specific project files:

IDE | Command | Then open
----|---------|----------
VS Code | `make vscode` | *le-audio.code-workspace*
Eclipse | `make eclipse` | Import existing project
Keil uVision | `make uvision5` | *.cprj* file
IAR Embedded Workbench | `make ewarm8` | *.ipcf* file

After generating, follow the IDE-specific instructions in [Using the code example](docs/using_the_code_example.md).


## Operation

See [Using the code example](docs/using_the_code_example.md) for detailed instructions on creating a project, opening it in various supported IDEs, and performing tasks such as building, programming, and debugging.

1. Connect the board to your PC using the provided USB cable through the KitProg3 USB connector

2. Open a terminal program and select the KitProg3 COM port. Set the serial port parameters to 8N1 and 115200 baud

3. After programming, the application starts automatically. Observe the Bluetooth&reg; stack and application trace messages on the UART terminal

4. The device will initialize all subsystems: <br>
   Bluetooth stack (BTSTACK), LE Audio manager (BAP, PACS, ASCS), USB composite device (MIDI + CDC/ACM), Wi-Fi host driver (WHD), and I2S audio streaming (on CM55)

5. Use the AT command interface via USB CDC/ACM (virtual serial port) to control the device

### Example AT Commands

**System commands:** `AT`, `ATI`, `AT+VERSION?`

**Bluetooth commands:** `AT+BTINIT`, `AT+BTSTATE?`, `AT+BTNAME=MyDevice`, `AT+GAPADVSTART`

**LE Audio commands:** `AT+LEAINIT`, `AT+LEABROADCAST=1`, `AT+LEASTATE?`

**Wi-Fi commands:** `AT+WIFIINIT`, `AT+WIFISCAN`, `AT+WIFIJOIN=MyNetwork,password123`


## Audio data flow

**Transmit Path (I2S RX to LC3 Encode to ISOC TX):**

Main Controller -> I2S RX [CM55] -> LC3 Encode [CM55] -> IPC Queue -> ISOC Handler [CM33] -> HCI UART -> CYW55512

**Receive Path (ISOC RX to LC3 Decode to I2S TX):**

CYW55512 -> HCI UART -> ISOC Handler [CM33] -> IPC Queue -> LC3 Decode [CM55] -> I2S TX [CM55] -> Main Controller


## Memory usage

### Build Output (Debug Configuration)

Core | Region | Used | Available | Utilization
-----|--------|------|-----------|------------
CM33 Secure | SRAM (data) | 133 KB | 135 KB | 98%
CM33 Non-Secure | SRAM (data) | 258 KB | 262 KB | 98%
CM55 | DTCM (data) | 32 KB | 256 KB | 12%
CM55 | SOCMEM (heap) | 2.87 MB | 2.87 MB | 100%
Shared | IPC Memory | 256 KB | 256 KB | 100%

### Flash Usage (SMIF0MEM1 External QSPI)

Image | Size | Description
------|------|------------
CM33 Secure | 27 KB | TrustZone config, secure services
CM33 Non-Secure | 369 KB | BLE stack, USB, Wi-Fi, MIDI, AT commands
CM55 | 143 KB | liblc3, I2S streaming, audio DSP
**Total** | **539 KB** | Combined firmware (16 MB available)


## Resources and settings

This section explains the ModusToolbox&trade; software resources and their configurations as used in this code example. Note that all the configuration explained in this section has already been implemented in the code example.

- **Bluetooth&reg; Configurator:** Used to generate the Bluetooth&reg; LE GATT database and various Bluetooth&reg; settings. Settings are stored in the file named *design.cybt*.

- **Device Configurator:** Used to configure device peripherals including I2S, UART (HCI), SDIO, and USB. Settings are stored in *design.modus*.

See the [Bluetooth&reg; Configurator guide](https://www.infineon.com/ModusToolboxBLEConfig) for more details.


## Related resources

Resources  | Links
-----------|----------------------------------
Application notes  | [AN235935](https://www.infineon.com/AN235935) – Getting started with PSOC&trade; Edge E8 MCU on ModusToolbox&trade; software <br> [AN236697](https://www.infineon.com/AN236697) – Getting started with PSOC&trade; MCU and AIROC&trade; Connectivity devices
Code examples  | [Using ModusToolbox&trade;](https://github.com/Infineon/Code-Examples-for-ModusToolbox-Software) on GitHub
Device documentation | [PSOC&trade; Edge MCU datasheets](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm#documents) <br> [PSOC&trade; Edge MCU reference manuals](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm#documents)
Development kits | Select your kits from the [Evaluation board finder](https://www.infineon.com/cms/en/design-support/finder-selection-tools/product-finder/evaluation-board)
Libraries  | [btstack](https://github.com/Infineon/btstack) – Bluetooth Host Stack <br> [btstack-integration](https://github.com/Infineon/btstack-integration) – BTSTACK platform adaptation layer <br> [wifi-host-driver](https://github.com/Infineon/wifi-host-driver) – Wi-Fi Host Driver (WHD) <br> [emusb-device](https://github.com/Infineon/emusb-device) – USB Device Middleware <br> [liblc3](https://github.com/google/liblc3) – Google LC3 codec (Apache 2.0)
Tools  | [ModusToolbox&trade;](https://www.infineon.com/modustoolbox) – ModusToolbox&trade; software is a collection of easy-to-use libraries and tools enabling rapid development with Infineon MCUs

<br>


## Other resources

- [Full Project Documentation](https://github.com/cotigac/infineon-le-audio)
- [Architecture Design](../../docs/architecture.md)
- [Implementation Status](../../docs/README.md)
- [Zephyr LE Audio Architecture](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html) (reference)
- [Bluetooth LE Audio Overview](https://www.bluetooth.com/learn-about-bluetooth/recent-enhancements/le-audio/)


## Document history

Document title: *Infineon LE Audio Demo* – *PSOC&trade; Edge MCU: LE Audio for Musical Instruments*

 Version | Description of change
 ------- | ---------------------
 1.0.0   | Initial release with full LE Audio, MIDI, and Wi-Fi support
<br>


All referenced product or service names and trademarks are the property of their respective owners.

The Bluetooth&reg; word mark and logos are registered trademarks owned by Bluetooth SIG, Inc., and any use of such marks by Infineon is under license.

PSOC&trade;, formerly known as PSoC&trade;, is a trademark of Infineon Technologies. Any references to PSoC&trade; in this document or others shall be deemed to refer to PSOC&trade;.

---------------------------------------------------------

Copyright 2024-2025, Cristian Cotiga. Licensed under Apache License 2.0.

This project uses Infineon middleware (btstack, wifi-host-driver) and SEGGER middleware (emusb-device) which are subject to their respective license terms.
