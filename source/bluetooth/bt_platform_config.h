/**
 * @file bt_platform_config.h
 * @brief Bluetooth Platform Configuration for PSoC Edge + CYW55512
 *
 * Defines the hardware configuration for HCI UART transport between
 * PSoC Edge E82 and CYW55512 Bluetooth 6.0 combo IC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_PLATFORM_CONFIG_H
#define BT_PLATFORM_CONFIG_H

#include "cybt_platform_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Pin Definitions for PSoC Edge E82 + CYW55512
 *
 * These pins connect the PSoC Edge MCU to the CYW55512 combo IC via HCI UART.
 * Adjust these based on your specific board design.
 ******************************************************************************/

/* HCI UART pins - adjust to match your BSP */
#ifndef CYBSP_BT_UART_TX
#define CYBSP_BT_UART_TX        (P5_0)  /* PSoC Edge TX -> CYW55512 RX */
#endif

#ifndef CYBSP_BT_UART_RX
#define CYBSP_BT_UART_RX        (P5_1)  /* PSoC Edge RX <- CYW55512 TX */
#endif

#ifndef CYBSP_BT_UART_RTS
#define CYBSP_BT_UART_RTS       (P5_2)  /* PSoC Edge RTS -> CYW55512 CTS */
#endif

#ifndef CYBSP_BT_UART_CTS
#define CYBSP_BT_UART_CTS       (P5_3)  /* PSoC Edge CTS <- CYW55512 RTS */
#endif

/* Bluetooth power/reset control */
#ifndef CYBSP_BT_POWER
#define CYBSP_BT_POWER          (P6_0)  /* BT chip power/reset control */
#endif

/* Bluetooth device wake pin (for sleep mode) */
#ifndef CYBSP_BT_DEVICE_WAKE
#define CYBSP_BT_DEVICE_WAKE    (P6_1)  /* Host -> Device wake signal */
#endif

/* Bluetooth host wake pin (for sleep mode) */
#ifndef CYBSP_BT_HOST_WAKE
#define CYBSP_BT_HOST_WAKE      (P6_2)  /* Device -> Host wake signal */
#endif

/*******************************************************************************
 * HCI UART Configuration
 ******************************************************************************/

/** Baud rate for firmware download (lower speed for reliability) */
#define BT_HCI_FW_DOWNLOAD_BAUD     (115200)

/** Baud rate for normal operation (high speed for LE Audio) */
#define BT_HCI_FEATURE_BAUD         (3000000)  /* 3 Mbps */

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Get the platform configuration for Bluetooth stack
 *
 * Returns a pointer to the static platform configuration structure
 * that should be passed to cybt_platform_config_init().
 *
 * @return Pointer to platform configuration
 */
const cybt_platform_config_t* bt_get_platform_config(void);

/**
 * @brief Initialize Bluetooth platform
 *
 * Configures the HCI UART and initializes the btstack-integration
 * porting layer. Must be called before wiced_bt_stack_init().
 *
 * @return 0 on success, negative error code on failure
 */
int bt_platform_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_PLATFORM_CONFIG_H */
