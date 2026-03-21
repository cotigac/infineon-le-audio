# PSoC Edge MCU: LE Audio Demo

This firmware implements a comprehensive Bluetooth LE Audio solution for musical instruments on the PSoC Edge E84 MCU with AIROC CYW55513 Wi-Fi and Bluetooth combo chip. It provides full-duplex audio streaming, Auracast broadcast, MIDI over BLE/USB, and Wi-Fi data bridging.

This code example has a three project structure: CM33 secure, CM33 non-secure, and CM55 projects. All three projects are programmed to the external QSPI flash and executed in Execute in Place (XIP) mode. Extended boot launches the CM33 secure project from a fixed location in the external flash, which then configures the protection settings and launches the CM33 non-secure application. Additionally, CM33 non-secure application enables CM55 CPU and launches the CM55 application.

[View this README on GitHub.](https://github.com/cotigac/infineon-le-audio)

See the [Architecture Documentation](../../docs/architecture.md) for detailed system design.


## Key Features

- **LE Audio Unicast** (CM33+CM55): Full-duplex audio streaming via CIS (Connected Isochronous Stream)
- **LE Audio Broadcast / Auracast** (CM33+CM55): One-to-many audio broadcast via BIS (Broadcast Isochronous Stream)
- **LC3 Codec** (CM55): Host-side LC3 encode/decode using Google liblc3 (Helium DSP)
- **BLE MIDI** (CM33): MIDI over Bluetooth Low Energy GATT service
- **USB MIDI** (CM33): USB High-Speed MIDI class device (480 Mbps)
- **I2S Streaming** (CM55): DMA-based bidirectional audio with ping-pong buffers
- **Wi-Fi Bridge** (CM33): USB HS to SDIO to CYW55512 WLAN data path
- **USB CDC/ACM** (CM33): AT command interface for BT/Wi-Fi/LE Audio configuration


## Dual-Core Architecture

- **proj_cm33_s** - Cortex-M33 (Secure): TrustZone bootstrap, security configuration
- **proj_cm33_ns** - Cortex-M33 (Non-Secure): BLE stack, USB, Wi-Fi, MIDI control plane
- **proj_cm55** - Cortex-M55: LC3 codec, I2S audio DSP (Helium acceleration)


## Requirements

- [ModusToolbox](https://www.infineon.com/modustoolbox) v3.6 or later (tested with v3.6)
- Board support package (BSP) minimum required version: 1.0.0
- Programming language: C
- Associated parts: [PSoC Edge E84 MCU](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm)


## Supported toolchains (make variable 'TOOLCHAIN')

- GNU Arm Embedded Compiler v14.2.1 (`GCC_ARM`) - Default value of `TOOLCHAIN`
- Arm Compiler v6.22 (`ARM`)
- IAR C/C++ Compiler v9.50.2 (`IAR`)
- LLVM Embedded Toolchain for Arm v19.1.5 (`LLVM_ARM`)


## Supported kits (make variable 'TARGET')

- [PSoC Edge E84 Evaluation Kit](https://www.infineon.com/KIT_PSE84_EVAL) (`KIT_PSE84_EVAL_EPC2`) - Default value of `TARGET`
- [PSoC Edge E84 Evaluation Kit](https://www.infineon.com/KIT_PSE84_EVAL) (`KIT_PSE84_EVAL_EPC4`)


## Hardware setup

This example uses the board's default configuration. See the kit user guide to ensure that the board is configured correctly.

Ensure the following jumper and pin configuration on board.
- BOOT SW must be in the HIGH/ON position
- J20 and J21 must be in the tristate/not connected (NC) position

**Audio Connections:**
- **I2S**: External codec/controller - 48kHz/16-bit bidirectional audio
- **USB**: Host PC or controller - MIDI + CDC/ACM + Wi-Fi bridge
- **UART**: Debug terminal - 115200 baud, 8N1


## Software setup

See the [ModusToolbox tools package installation guide](https://www.infineon.com/ModusToolboxInstallguide) for information about installing and configuring the tools package.

Install a terminal emulator if you do not have one. Instructions in this document use [Tera Term](https://teratermproject.github.io/index-en.html).

This example requires no additional software or tools.


## Quick start (command line)

For users who prefer building and programming from the command line without using an IDE:

1. Open a terminal - Windows: Open modus-shell from the Start menu. Linux/macOS: Use any terminal application.

2. Navigate to the project directory and run the following commands

3. Fetch dependencies (first time only): `make getlibs`

4. Build all three cores: `make build` - This creates build/app_combined.hex containing CM33 Secure, CM33 Non-Secure, and CM55 firmware.

5. Program the board - connect the board via the KitProg3 USB connector, then run: `make program`

6. Open a terminal at 115200 baud, 8N1 to view debug output from KitProg3 COM port.


## IDE project generation

To open this project in your preferred IDE, generate the IDE-specific project files:

- **VS Code**: Run `make vscode`, then open le-audio.code-workspace
- **Eclipse**: Run `make eclipse`, then import existing project
- **Keil uVision**: Run `make uvision5`, then open the .cprj file
- **IAR Embedded Workbench**: Run `make ewarm8`, then open the .ipcf file

After generating, follow the IDE-specific instructions in [Using the code example](docs/using_the_code_example.md).


## Operation

See [Using the code example](docs/using_the_code_example.md) for detailed instructions on creating a project, opening it in various supported IDEs, and performing tasks such as building, programming, and debugging.

1. Connect the board to your PC using the provided USB cable through the KitProg3 USB connector

2. Open a terminal program and select the KitProg3 COM port. Set the serial port parameters to 8N1 and 115200 baud

3. After programming, the application starts automatically. Observe the Bluetooth stack and application trace messages on the UART terminal

4. The device will initialize all subsystems: Bluetooth stack (BTSTACK), LE Audio manager (BAP, PACS, ASCS), USB composite device (MIDI + CDC/ACM), Wi-Fi host driver (WHD), and I2S audio streaming (on CM55)

5. Use the AT command interface via USB CDC/ACM (virtual serial port) to control the device


## Example AT Commands

**System commands:** AT, ATI, AT+VERSION?

**Bluetooth commands:** AT+BTINIT, AT+BTSTATE?, AT+BTNAME=MyDevice, AT+GAPADVSTART

**LE Audio commands:** AT+LEAINIT, AT+LEABROADCAST=1, AT+LEASTATE?

**Wi-Fi commands:** AT+WIFIINIT, AT+WIFISCAN, AT+WIFIJOIN=MyNetwork,password123


## Audio data flow

**Transmit Path (I2S RX to LC3 Encode to ISOC TX):**
Main Controller to I2S RX (CM55) to LC3 Encode (CM55) to IPC Queue to ISOC Handler (CM33) to HCI UART to CYW55512

**Receive Path (ISOC RX to LC3 Decode to I2S TX):**
CYW55512 to HCI UART to ISOC Handler (CM33) to IPC Queue to LC3 Decode (CM55) to I2S TX (CM55) to Main Controller


## Memory usage

**Build Output (Debug Configuration):**
- CM33 Secure: SRAM data 133 KB / 135 KB (98%)
- CM33 Non-Secure: SRAM data 258 KB / 262 KB (98%)
- CM55: DTCM data 32 KB / 256 KB (12%)
- CM55: SOCMEM heap 2.87 MB / 2.87 MB (100%)
- Shared: IPC Memory 256 KB / 256 KB (100%)

**Flash Usage (SMIF0MEM1 External QSPI):**
- CM33 Secure: 27 KB - TrustZone config, secure services
- CM33 Non-Secure: 369 KB - BLE stack, USB, Wi-Fi, MIDI, AT commands
- CM55: 143 KB - liblc3, I2S streaming, audio DSP
- Total: 539 KB (16 MB available)


## Resources and settings

This section explains the ModusToolbox software resources and their configurations as used in this code example. Note that all the configuration explained in this section has already been implemented in the code example.

- **Bluetooth Configurator:** Used to generate the Bluetooth LE GATT database and various Bluetooth settings. Settings are stored in the file named design.cybt.

- **Device Configurator:** Used to configure device peripherals including I2S, UART (HCI), SDIO, and USB. Settings are stored in design.modus.

See the [Bluetooth Configurator guide](https://www.infineon.com/ModusToolboxBLEConfig) for more details.


## Related resources

Resources  | Links
-----------|----------------------------------
Application notes  | [AN235935](https://www.infineon.com/AN235935) - Getting started with PSoC Edge E8 MCU on ModusToolbox software
Code examples  | [Using ModusToolbox](https://github.com/Infineon/Code-Examples-for-ModusToolbox-Software) on GitHub
Device documentation | [PSoC Edge MCU datasheets](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm#documents)
Development kits | Select your kits from the [Evaluation board finder](https://www.infineon.com/cms/en/design-support/finder-selection-tools/product-finder/evaluation-board)
Libraries  | [btstack](https://github.com/Infineon/btstack) - Bluetooth Host Stack
Tools  | [ModusToolbox](https://www.infineon.com/modustoolbox) - Infineon development tools


## Other resources

- [Full Project Documentation](https://github.com/cotigac/infineon-le-audio)
- [Architecture Design](../../docs/architecture.md)
- [Implementation Status](../../docs/README.md)
- [Zephyr LE Audio Architecture](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html) (reference)
- [Bluetooth LE Audio Overview](https://www.bluetooth.com/learn-about-bluetooth/recent-enhancements/le-audio/)


## Document history

Document title: Infineon LE Audio Demo - PSoC Edge MCU: LE Audio for Musical Instruments

 Version | Description of change
 ------- | ---------------------
 1.0.0   | Initial release with full LE Audio, MIDI, and Wi-Fi support


All referenced product or service names and trademarks are the property of their respective owners.

The Bluetooth word mark and logos are registered trademarks owned by Bluetooth SIG, Inc., and any use of such marks by Infineon is under license.
