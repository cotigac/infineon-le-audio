/**
 * @file wifi_sdio.h
 * @brief SDIO Driver Interface for CYW55512 Wi-Fi
 *
 * This module provides the SDIO bus interface for communicating with
 * the CYW55512 Wi-Fi chip. It implements the hardware abstraction layer
 * required by the Wi-Fi Host Driver (WHD).
 *
 * Hardware Interface:
 * - SDIO 3.0 (SDR50/DDR50)
 * - 4-bit data bus
 * - Up to 208 MHz clock
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WIFI_SDIO_H
#define WIFI_SDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** SDIO bus speed modes */
typedef enum {
    WIFI_SDIO_SPEED_DEFAULT = 0,    /**< Default speed (25 MHz) */
    WIFI_SDIO_SPEED_HIGH,           /**< High speed (50 MHz) */
    WIFI_SDIO_SPEED_SDR50,          /**< SDR50 (100 MHz) */
    WIFI_SDIO_SPEED_DDR50,          /**< DDR50 (50 MHz DDR) */
    WIFI_SDIO_SPEED_SDR104          /**< SDR104 (208 MHz) */
} wifi_sdio_speed_t;

/** SDIO bus width */
typedef enum {
    WIFI_SDIO_BUS_WIDTH_1BIT = 1,   /**< 1-bit data bus */
    WIFI_SDIO_BUS_WIDTH_4BIT = 4    /**< 4-bit data bus */
} wifi_sdio_bus_width_t;

/** SDIO transfer direction */
typedef enum {
    WIFI_SDIO_READ = 0,             /**< Read from device */
    WIFI_SDIO_WRITE = 1             /**< Write to device */
} wifi_sdio_direction_t;

/** SDIO configuration */
typedef struct {
    wifi_sdio_speed_t speed;        /**< Bus speed mode */
    wifi_sdio_bus_width_t bus_width;/**< Data bus width */
    uint32_t block_size;            /**< Block size for transfers */
    bool use_dma;                   /**< Enable DMA transfers */
} wifi_sdio_config_t;

/** SDIO statistics */
typedef struct {
    uint32_t bytes_sent;            /**< Total bytes sent */
    uint32_t bytes_received;        /**< Total bytes received */
    uint32_t commands_sent;         /**< Total commands sent */
    uint32_t errors;                /**< Total errors */
    uint32_t crc_errors;            /**< CRC errors */
    uint32_t timeout_errors;        /**< Timeout errors */
} wifi_sdio_stats_t;

/** SDIO callback for async operations */
typedef void (*wifi_sdio_callback_t)(bool success, void *user_data);

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

/** Default SDIO configuration for CYW55512 */
#define WIFI_SDIO_CONFIG_DEFAULT {      \
    .speed = WIFI_SDIO_SPEED_SDR50,     \
    .bus_width = WIFI_SDIO_BUS_WIDTH_4BIT, \
    .block_size = 512,                  \
    .use_dma = true                     \
}

/*******************************************************************************
 * API Functions
 ******************************************************************************/

/**
 * @brief Initialize the SDIO driver
 *
 * @param config Pointer to configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int wifi_sdio_init(const wifi_sdio_config_t *config);

/**
 * @brief Deinitialize the SDIO driver
 */
void wifi_sdio_deinit(void);

/**
 * @brief Send an SDIO command (CMD52 - single byte)
 *
 * @param write true for write, false for read
 * @param function SDIO function number (0-7)
 * @param address Register address
 * @param data Data byte to write (ignored for read)
 * @param response Pointer to store response byte
 * @return 0 on success, negative error code on failure
 */
int wifi_sdio_cmd52(bool write, uint8_t function, uint32_t address,
                    uint8_t data, uint8_t *response);

/**
 * @brief Send an SDIO command (CMD53 - multi-byte)
 *
 * @param write true for write, false for read
 * @param function SDIO function number (0-7)
 * @param address Starting address
 * @param increment true to increment address after each transfer
 * @param buffer Data buffer
 * @param length Number of bytes to transfer
 * @return 0 on success, negative error code on failure
 */
int wifi_sdio_cmd53(bool write, uint8_t function, uint32_t address,
                    bool increment, uint8_t *buffer, uint32_t length);

/**
 * @brief Send an SDIO command (CMD53) using block mode
 *
 * @param write true for write, false for read
 * @param function SDIO function number (0-7)
 * @param address Starting address
 * @param increment true to increment address after each transfer
 * @param buffer Data buffer
 * @param block_count Number of blocks to transfer
 * @return 0 on success, negative error code on failure
 */
int wifi_sdio_cmd53_block(bool write, uint8_t function, uint32_t address,
                          bool increment, uint8_t *buffer, uint32_t block_count);

/**
 * @brief Async CMD53 transfer with callback
 *
 * @param write true for write, false for read
 * @param function SDIO function number
 * @param address Starting address
 * @param buffer Data buffer
 * @param length Number of bytes
 * @param callback Completion callback
 * @param user_data User data for callback
 * @return 0 on success, negative error code on failure
 */
int wifi_sdio_cmd53_async(bool write, uint8_t function, uint32_t address,
                          uint8_t *buffer, uint32_t length,
                          wifi_sdio_callback_t callback, void *user_data);

/**
 * @brief Enable SDIO interrupt
 *
 * @param function SDIO function number
 * @param enable true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int wifi_sdio_enable_interrupt(uint8_t function, bool enable);

/**
 * @brief Check if SDIO interrupt is pending
 *
 * @param function SDIO function number
 * @return true if interrupt pending
 */
bool wifi_sdio_interrupt_pending(uint8_t function);

/**
 * @brief Set bus speed
 *
 * @param speed Speed mode
 * @return 0 on success, negative error code on failure
 */
int wifi_sdio_set_speed(wifi_sdio_speed_t speed);

/**
 * @brief Set bus width
 *
 * @param width Bus width
 * @return 0 on success, negative error code on failure
 */
int wifi_sdio_set_bus_width(wifi_sdio_bus_width_t width);

/**
 * @brief Get SDIO statistics
 *
 * @param stats Pointer to statistics structure
 */
void wifi_sdio_get_stats(wifi_sdio_stats_t *stats);

/**
 * @brief Reset SDIO statistics
 */
void wifi_sdio_reset_stats(void);

/**
 * @brief Check if SDIO is initialized and ready
 *
 * @return true if ready
 */
bool wifi_sdio_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SDIO_H */
