/**
 * @file bt_platform_config.c
 * @brief Bluetooth Platform Configuration Implementation
 *
 * Implements the hardware configuration for HCI UART transport between
 * PSoC Edge E82 and CYW55512 Bluetooth 6.0 combo IC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bt_platform_config.h"
#include <stdio.h>

/*******************************************************************************
 * Platform Configuration Structure
 ******************************************************************************/

/**
 * Static platform configuration for PSoC Edge + CYW55512
 *
 * This configures:
 * - HCI UART pins and baud rates
 * - Bluetooth chip power control
 * - Sleep mode settings (disabled by default for development)
 */
static const cybt_platform_config_t bt_platform_cfg = {
    .hci_config = {
        .hci_transport = CYBT_HCI_UART,
        .hci = {
            .hci_uart = {
                /* UART pin assignment */
                .uart_tx_pin = CYBSP_BT_UART_TX,
                .uart_rx_pin = CYBSP_BT_UART_RX,
                .uart_rts_pin = CYBSP_BT_UART_RTS,
                .uart_cts_pin = CYBSP_BT_UART_CTS,

                /* Baud rates */
                .baud_rate_for_fw_download = BT_HCI_FW_DOWNLOAD_BAUD,
                .baud_rate_for_feature = BT_HCI_FEATURE_BAUD,

                /* UART format */
                .data_bits = 8,
                .stop_bits = 1,
                .parity = CYHAL_UART_PARITY_NONE,
                .flow_control = true  /* Required for high-speed HCI */
            }
        }
    },

    .controller_config = {
        /* Bluetooth chip power/reset pin */
        .bt_power_pin = CYBSP_BT_POWER,

        /* Sleep mode configuration */
        .sleep_mode = {
            /* Disable sleep mode during development for easier debugging */
            .sleep_mode_enabled = CYBT_SLEEP_MODE_DISABLED,

            /* Wake pins (configured but not used when sleep disabled) */
            .device_wakeup_pin = CYBSP_BT_DEVICE_WAKE,
            .host_wakeup_pin = CYBSP_BT_HOST_WAKE,

            /* Wake signal polarity */
            .device_wake_polarity = CYBT_WAKE_ACTIVE_LOW,
            .host_wake_polarity = CYBT_WAKE_ACTIVE_LOW
        }
    },

    /* Memory pool for Bluetooth task communication */
    .task_mem_pool_size = 4096  /* 4KB, increase if needed */
};

/*******************************************************************************
 * API Implementation
 ******************************************************************************/

const cybt_platform_config_t* bt_get_platform_config(void)
{
    return &bt_platform_cfg;
}

int bt_platform_init(void)
{
    printf("BT Platform: Initializing HCI UART transport\n");
    printf("  TX Pin:  0x%04X\n", (unsigned)CYBSP_BT_UART_TX);
    printf("  RX Pin:  0x%04X\n", (unsigned)CYBSP_BT_UART_RX);
    printf("  RTS Pin: 0x%04X\n", (unsigned)CYBSP_BT_UART_RTS);
    printf("  CTS Pin: 0x%04X\n", (unsigned)CYBSP_BT_UART_CTS);
    printf("  FW Download Baud: %lu\n", (unsigned long)BT_HCI_FW_DOWNLOAD_BAUD);
    printf("  Feature Baud: %lu\n", (unsigned long)BT_HCI_FEATURE_BAUD);

    /* Initialize the btstack-integration porting layer */
    cybt_platform_config_init(&bt_platform_cfg);

    printf("BT Platform: Initialized\n");

    return 0;
}
