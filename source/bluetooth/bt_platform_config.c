/**
 * @file bt_platform_config.c
 * @brief Bluetooth Platform Configuration Implementation
 *
 * Platform configuration for PSoC Edge E84 with CYW55512.
 *
 * NOTE: For PSoC Edge with MTB HAL, the btstack-integration library
 * handles platform configuration automatically. It reads pin assignments
 * from BSP defines (CYBSP_BT_UART_TX, etc.) and configures the HCI
 * transport internally via platform_hal_next_wrapper.c.
 *
 * This file provides a minimal bt_platform_init() for API compatibility
 * and debug logging.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bt_platform_config.h"
#include <stdio.h>

/*******************************************************************************
 * API Implementation
 ******************************************************************************/

int bt_platform_init(void)
{
    /*
     * For PSoC Edge with MTB HAL (not CY_USING_HAL), the btstack-integration
     * library automatically configures the HCI UART transport using:
     *   - platform_hal_next_wrapper.c for GPIO/UART initialization
     *   - BSP defines for pin assignments (CYBSP_BT_UART_*, CYBSP_BT_POWER, etc.)
     *   - cybsp_bt_config.h for baud rate settings
     *
     * No explicit cybt_platform_config_init() call is needed.
     * The library initializes when wiced_bt_stack_init() is called.
     */

    printf("BT Platform: PSoC Edge E84 + CYW55512\n");
    printf("  HCI Transport: UART (btstack-integration handles config)\n");
    printf("  FW Download Baud: %lu\n", (unsigned long)BT_HCI_FW_DOWNLOAD_BAUD);
    printf("  Feature Baud: %lu\n", (unsigned long)BT_HCI_FEATURE_BAUD);
    printf("BT Platform: Ready\n");

    return 0;
}
