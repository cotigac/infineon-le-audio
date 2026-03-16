/**
 * @file bt_platform_config.h
 * @brief Bluetooth Platform Configuration for PSoC Edge + CYW55512
 *
 * Provides platform configuration interface for HCI UART transport between
 * PSoC Edge E84 and CYW55512 Bluetooth 6.0 combo IC.
 *
 * NOTE: For PSoC Edge with MTB HAL, the btstack-integration library
 * automatically handles platform configuration using BSP defines.
 * This header provides optional baud rate overrides.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_PLATFORM_CONFIG_H
#define BT_PLATFORM_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * HCI UART Configuration (baud rate overrides)
 *
 * These can be used to override BSP defaults if needed.
 * The btstack-integration library reads pin configurations from BSP defines:
 *   CYBSP_BT_UART_TX, CYBSP_BT_UART_RX, CYBSP_BT_UART_RTS, CYBSP_BT_UART_CTS
 *   CYBSP_BT_POWER, CYBSP_BT_DEVICE_WAKE, CYBSP_BT_HOST_WAKE
 ******************************************************************************/

/** Baud rate for firmware download (lower speed for reliability) */
#ifndef BT_HCI_FW_DOWNLOAD_BAUD
#define BT_HCI_FW_DOWNLOAD_BAUD     (115200)
#endif

/** Baud rate for normal operation (high speed for LE Audio) */
#ifndef BT_HCI_FEATURE_BAUD
#define BT_HCI_FEATURE_BAUD         (3000000)  /* 3 Mbps */
#endif

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize Bluetooth platform
 *
 * For PSoC Edge with MTB HAL, this function is a no-op since the
 * btstack-integration library handles platform configuration automatically.
 * It's kept for API compatibility and debug logging.
 *
 * @return 0 on success
 */
int bt_platform_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_PLATFORM_CONFIG_H */
