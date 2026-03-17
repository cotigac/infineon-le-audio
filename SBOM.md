# Software Bill of Materials (SBOM)

## Project Information

| Field | Value |
|-------|-------|
| **Name** | infineon-le-audio |
| **Version** | 1.0.0 |
| **License** | Apache-2.0 |
| **Repository** | https://github.com/cotigac/infineon-le-audio |

## First-Party Components

All code in the `/source` directory is original implementation licensed under Apache-2.0.

| Component | Path | Description |
|-----------|------|-------------|
| LE Audio Stack | `source/le_audio/` | BAP Unicast, BAP Broadcast (Auracast), PACS, ISOC handler |
| Audio Processing | `source/audio/` | LC3 wrapper, I2S streaming, audio buffers, audio task |
| MIDI | `source/midi/` | BLE MIDI service, USB MIDI class, MIDI router |
| Wi-Fi Bridge | `source/wifi/` | USB-to-Wi-Fi data bridge using WHD |
| Bluetooth Init | `source/bluetooth/` | BTSTACK initialization, GAP, GATT, HCI ISOC |
| IPC | `source/ipc/` | Inter-processor communication for dual-core |

**Implementation basis:**
- Bluetooth SIG specifications (BAP 1.0.1, PACS, ASCS, LC3)
- Infineon BTSTACK API documentation
- Zephyr LE Audio architecture documentation (reference only, no code derived)

---

## Third-Party Open Source Components

| Component | Version | License | Copyright | Repository |
|-----------|---------|---------|-----------|------------|
| liblc3 | latest | Apache-2.0 | 2022 Google LLC | https://github.com/google/liblc3 |
| freertos | 11.1.0 | MIT | FreeRTOS Project | https://github.com/FreeRTOS/FreeRTOS-Kernel |
| mtb-hal-cat1 | latest | Apache-2.0 | 2018-2024 Cypress/Infineon | https://github.com/Infineon/mtb-hal-cat1 |
| mtb-pdl-cat1 | latest | Apache-2.0 | Cypress/Infineon | https://github.com/Infineon/mtb-pdl-cat1 |
| abstraction-rtos | latest | Apache-2.0 | 2018-2022 Cypress/Infineon | https://github.com/Infineon/abstraction-rtos |
| core-lib | latest | Apache-2.0 | 2018-2025 Infineon Technologies AG | https://github.com/Infineon/core-lib |

### Nested Dependencies

| Parent | Dependency | License | Notes |
|--------|------------|---------|-------|
| mtb-pdl-cat1 | Cadence Ethernet Driver | Apache-2.0 | `drivers/third_party/ethernet/` |
| freertos | Partner/Community Ports | MIT | Architecture-specific ports |

---

## Third-Party Proprietary Components

> **IMPORTANT**: These components are NOT open source. They are licensed under the Infineon End User License Agreement (EULA) and are provided for use with Infineon hardware only.

| Component | License | Copyright | Repository | Notes |
|-----------|---------|-----------|------------|-------|
| btstack | Infineon EULA | Infineon Technologies Americas Corp. | https://github.com/Infineon/btstack | Bluetooth Host Stack |
| btstack-integration | Infineon EULA | Infineon Technologies Americas Corp. | https://github.com/Infineon/btstack-integration | Platform porting layer |
| wifi-host-driver | Infineon EULA | Infineon Technologies Americas Corp. | https://github.com/Infineon/wifi-host-driver | Wi-Fi Host Driver (WHD) |
| emusb-device | SEGGER Commercial | Cypress/Infineon (via SEGGER license) | https://github.com/Infineon/emusb-device | Binary-only USB stack |
| TARGET_KIT_PSE84_EVAL_EPC2 | Infineon EULA | Infineon Technologies | https://github.com/Infineon/TARGET_KIT_PSE84_EVAL_EPC2 | PSoC Edge E84 BSP |
| TARGET_CYW955513EVK-01 | Infineon EULA | Infineon Technologies | https://github.com/Infineon/TARGET_CYW955513EVK-01 | CYW55513 EVK BSP |

### Proprietary Component Details

**btstack**: Infineon's proprietary Bluetooth Host Protocol Stack supporting BR/EDR and BLE. Provides ATT/GATT, SMP, L2CAP, and profile implementations. Dual-licensed for commercial use only with Infineon hardware.

**wifi-host-driver (WHD)**: Proprietary driver for Infineon AIROC Wi-Fi chips. Provides SDIO/SPI interface abstraction and Wi-Fi protocol stack. Binary portions included.

**emusb-device**: Commercial USB device middleware from SEGGER Microcontroller Systems GmbH. Licensed to Infineon for redistribution with Infineon hardware. Provided as pre-built binary libraries only.

---

## Specifications and Standards

| Specification | Version | Source |
|---------------|---------|--------|
| Basic Audio Profile (BAP) | 1.0.1 | Bluetooth SIG |
| Published Audio Capabilities Service (PACS) | 1.0 | Bluetooth SIG |
| Audio Stream Control Service (ASCS) | 1.0 | Bluetooth SIG |
| Low Complexity Communication Codec (LC3) | 1.0 | Bluetooth SIG |
| Bluetooth Core | 6.0 | Bluetooth SIG |
| USB MIDI Class | 1.0 | USB-IF |

---

## License Compatibility Matrix

| License | Commercial Use | Modification | Distribution | Patent Grant |
|---------|----------------|--------------|--------------|--------------|
| Apache-2.0 | Yes | Yes | Yes | Yes |
| MIT | Yes | Yes | Yes | No |
| Infineon EULA | Limited* | No | Limited* | No |
| SEGGER Commercial | Limited* | No | Limited* | No |

*Limited to use with Infineon/Cypress hardware as per license terms.

---

## SBOM Format

This document follows a simplified human-readable format. For machine-readable SBOM, consider generating:
- **SPDX**: `spdx-sbom-generator`
- **CycloneDX**: `cyclonedx-cli`

---

## References

- [Infineon ModusToolbox](https://www.infineon.com/modustoolbox)
- [Bluetooth SIG Specifications](https://www.bluetooth.com/specifications/specs/)
- [Zephyr LE Audio Documentation](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/audio/bluetooth-le-audio-arch.html)
- [SPDX License List](https://spdx.org/licenses/)
