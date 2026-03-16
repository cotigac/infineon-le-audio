/**
 * @file whd_buffer_impl.c
 * @brief WHD Buffer Interface Implementation using FreeRTOS
 *
 * Implements whd_buffer_funcs callbacks for packet buffer management.
 * Uses FreeRTOS dynamic memory allocation with optional pool-based
 * allocation for better real-time performance.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "whd_buffer_impl.h"
#include "whd_network_types.h"
#include "whd_types.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <string.h>
#include <stdlib.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** Maximum packet buffer size (MTU + headers) */
#define WHD_BUFFER_MAX_SIZE         (WHD_LINK_MTU + 64)

/** Header space reserved at the front of each buffer */
#define WHD_BUFFER_HEADER_SPACE     64

/** Buffer pool size for pre-allocated buffers (0 = use dynamic only) */
#define WHD_BUFFER_POOL_SIZE        16

/*******************************************************************************
 * Types
 ******************************************************************************/

/**
 * @brief Packet buffer structure
 *
 * This wraps raw data with metadata for buffer management.
 */
typedef struct whd_buffer_impl {
    uint8_t *data;              /**< Pointer to actual data start */
    uint8_t *current;           /**< Current position in buffer */
    uint16_t total_size;        /**< Total allocated size */
    uint16_t current_size;      /**< Current data size */
    bool in_use;                /**< True if buffer is allocated */
    bool from_pool;             /**< True if from static pool */
} whd_buffer_impl_t;

/*******************************************************************************
 * Static Data
 ******************************************************************************/

#if WHD_BUFFER_POOL_SIZE > 0
/** Static buffer pool for TX/RX packets */
static whd_buffer_impl_t buffer_pool[WHD_BUFFER_POOL_SIZE];
static uint8_t buffer_data_pool[WHD_BUFFER_POOL_SIZE][WHD_BUFFER_MAX_SIZE];
static SemaphoreHandle_t pool_mutex = NULL;
#endif

/** Statistics */
static struct {
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t alloc_failures;
    uint32_t pool_hits;
    uint32_t pool_misses;
} buffer_stats = {0};

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

#if WHD_BUFFER_POOL_SIZE > 0
/**
 * @brief Try to allocate from static pool
 */
static whd_buffer_impl_t* pool_alloc(uint16_t size)
{
    if (pool_mutex == NULL || size > WHD_BUFFER_MAX_SIZE) {
        return NULL;
    }

    if (xSemaphoreTake(pool_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return NULL;
    }

    for (int i = 0; i < WHD_BUFFER_POOL_SIZE; i++) {
        if (!buffer_pool[i].in_use) {
            buffer_pool[i].in_use = true;
            buffer_pool[i].from_pool = true;
            buffer_pool[i].data = buffer_data_pool[i];
            buffer_pool[i].current = buffer_pool[i].data + WHD_BUFFER_HEADER_SPACE;
            buffer_pool[i].total_size = WHD_BUFFER_MAX_SIZE;
            buffer_pool[i].current_size = size;
            buffer_stats.pool_hits++;
            xSemaphoreGive(pool_mutex);
            return &buffer_pool[i];
        }
    }

    xSemaphoreGive(pool_mutex);
    buffer_stats.pool_misses++;
    return NULL;
}

/**
 * @brief Free buffer back to pool
 */
static void pool_free(whd_buffer_impl_t *buf)
{
    if (pool_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(pool_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        buf->in_use = false;
        buf->current_size = 0;
        xSemaphoreGive(pool_mutex);
    }
}
#endif

/*******************************************************************************
 * WHD Buffer Interface Callbacks
 ******************************************************************************/

/**
 * @brief Allocate a packet buffer
 */
static whd_result_t impl_host_buffer_get(whd_buffer_t *buffer,
                                          whd_buffer_dir_t direction,
                                          uint16_t size,
                                          uint32_t timeout_ms)
{
    whd_buffer_impl_t *impl = NULL;
    (void)direction;  /* We use unified pool */
    (void)timeout_ms; /* TODO: Implement timeout for pool wait */

    if (buffer == NULL || size == 0) {
        return WHD_BADARG;
    }

    uint16_t alloc_size = size + WHD_BUFFER_HEADER_SPACE;

#if WHD_BUFFER_POOL_SIZE > 0
    /* Try pool first for small buffers */
    if (alloc_size <= WHD_BUFFER_MAX_SIZE) {
        impl = pool_alloc(size);
    }
#endif

    /* Fall back to dynamic allocation */
    if (impl == NULL) {
        impl = (whd_buffer_impl_t *)pvPortMalloc(sizeof(whd_buffer_impl_t));
        if (impl == NULL) {
            buffer_stats.alloc_failures++;
            return WHD_BUFFER_UNAVAILABLE_TEMPORARY;
        }

        impl->data = (uint8_t *)pvPortMalloc(alloc_size);
        if (impl->data == NULL) {
            vPortFree(impl);
            buffer_stats.alloc_failures++;
            return WHD_BUFFER_UNAVAILABLE_TEMPORARY;
        }

        impl->from_pool = false;
        impl->total_size = alloc_size;
        impl->current = impl->data + WHD_BUFFER_HEADER_SPACE;
        impl->current_size = size;
        impl->in_use = true;
    }

    *buffer = (whd_buffer_t)impl;
    buffer_stats.alloc_count++;
    return WHD_SUCCESS;
}

/**
 * @brief Release a packet buffer
 */
static void impl_buffer_release(whd_buffer_t buffer, whd_buffer_dir_t direction)
{
    whd_buffer_impl_t *impl = (whd_buffer_impl_t *)buffer;
    (void)direction;

    if (impl == NULL) {
        return;
    }

#if WHD_BUFFER_POOL_SIZE > 0
    if (impl->from_pool) {
        pool_free(impl);
        buffer_stats.free_count++;
        return;
    }
#endif

    /* Free dynamic allocation */
    if (impl->data != NULL) {
        vPortFree(impl->data);
    }
    vPortFree(impl);
    buffer_stats.free_count++;
}

/**
 * @brief Get current data pointer
 */
static uint8_t* impl_buffer_get_current_piece_data_pointer(whd_buffer_t buffer)
{
    whd_buffer_impl_t *impl = (whd_buffer_impl_t *)buffer;
    if (impl == NULL) {
        return NULL;
    }
    return impl->current;
}

/**
 * @brief Get current data size
 */
static uint16_t impl_buffer_get_current_piece_size(whd_buffer_t buffer)
{
    whd_buffer_impl_t *impl = (whd_buffer_impl_t *)buffer;
    if (impl == NULL) {
        return 0;
    }
    return impl->current_size;
}

/**
 * @brief Set buffer data size
 */
static whd_result_t impl_buffer_set_size(whd_buffer_t buffer, uint16_t size)
{
    whd_buffer_impl_t *impl = (whd_buffer_impl_t *)buffer;
    if (impl == NULL) {
        return WHD_BADARG;
    }

    /* Check bounds */
    uint16_t max_size = impl->total_size - (impl->current - impl->data);
    if (size > max_size) {
        return WHD_BUFFER_ALLOC_FAIL;
    }

    impl->current_size = size;
    return WHD_SUCCESS;
}

/**
 * @brief Adjust header space at front of buffer
 */
static whd_result_t impl_buffer_add_remove_at_front(whd_buffer_t *buffer,
                                                     int32_t add_remove_amount)
{
    if (buffer == NULL || *buffer == NULL) {
        return WHD_BADARG;
    }

    whd_buffer_impl_t *impl = (whd_buffer_impl_t *)*buffer;

    /* Calculate new position */
    uint8_t *new_current = impl->current + add_remove_amount;

    /* Check bounds */
    if (new_current < impl->data ||
        new_current > impl->data + impl->total_size) {
        return WHD_BUFFER_ALLOC_FAIL;
    }

    /* Adjust size accordingly */
    if (add_remove_amount > 0) {
        /* Moving forward - decreasing header space, data shrinks */
        if ((uint32_t)add_remove_amount > impl->current_size) {
            impl->current_size = 0;
        } else {
            impl->current_size -= add_remove_amount;
        }
    } else {
        /* Moving backward - increasing header space, data grows */
        impl->current_size += (-add_remove_amount);
    }

    impl->current = new_current;
    return WHD_SUCCESS;
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

/** Global buffer functions structure for WHD */
static whd_buffer_funcs_t whd_buffer_funcs_impl = {
    .whd_host_buffer_get = impl_host_buffer_get,
    .whd_buffer_release = impl_buffer_release,
    .whd_buffer_get_current_piece_data_pointer = impl_buffer_get_current_piece_data_pointer,
    .whd_buffer_get_current_piece_size = impl_buffer_get_current_piece_size,
    .whd_buffer_set_size = impl_buffer_set_size,
    .whd_buffer_add_remove_at_front = impl_buffer_add_remove_at_front,
};

int whd_buffer_impl_init(void)
{
#if WHD_BUFFER_POOL_SIZE > 0
    /* Initialize buffer pool */
    memset(buffer_pool, 0, sizeof(buffer_pool));
    memset(&buffer_stats, 0, sizeof(buffer_stats));

    pool_mutex = xSemaphoreCreateMutex();
    if (pool_mutex == NULL) {
        return -1;
    }
#endif

    return 0;
}

void whd_buffer_impl_deinit(void)
{
#if WHD_BUFFER_POOL_SIZE > 0
    if (pool_mutex != NULL) {
        vSemaphoreDelete(pool_mutex);
        pool_mutex = NULL;
    }
#endif
}

whd_buffer_funcs_t* whd_buffer_impl_get_funcs(void)
{
    return &whd_buffer_funcs_impl;
}

void whd_buffer_impl_get_stats(uint32_t *alloc, uint32_t *freed,
                                uint32_t *failures, uint32_t *pool_hits)
{
    if (alloc) *alloc = buffer_stats.alloc_count;
    if (freed) *freed = buffer_stats.free_count;
    if (failures) *failures = buffer_stats.alloc_failures;
    if (pool_hits) *pool_hits = buffer_stats.pool_hits;
}
