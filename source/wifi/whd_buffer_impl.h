/**
 * @file whd_buffer_impl.h
 * @brief WHD Buffer Interface Implementation
 *
 * Provides buffer allocation callbacks for WHD.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WHD_BUFFER_IMPL_H
#define WHD_BUFFER_IMPL_H

#include "whd_network_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the WHD buffer implementation
 *
 * Must be called before using any buffer functions.
 * Initializes the buffer pool and synchronization primitives.
 *
 * @return 0 on success, negative error code on failure
 */
int whd_buffer_impl_init(void);

/**
 * @brief Deinitialize the WHD buffer implementation
 *
 * Releases all resources. Any outstanding buffers become invalid.
 */
void whd_buffer_impl_deinit(void);

/**
 * @brief Get the WHD buffer functions structure
 *
 * Returns a pointer to the whd_buffer_funcs structure that should
 * be passed to whd_init() as the buffer_if parameter.
 *
 * @return Pointer to buffer functions structure
 */
whd_buffer_funcs_t* whd_buffer_impl_get_funcs(void);

/**
 * @brief Get buffer allocation statistics
 *
 * @param alloc     Output: Total allocations
 * @param freed     Output: Total frees
 * @param failures  Output: Allocation failures
 * @param pool_hits Output: Allocations satisfied from pool
 */
void whd_buffer_impl_get_stats(uint32_t *alloc, uint32_t *freed,
                                uint32_t *failures, uint32_t *pool_hits);

#ifdef __cplusplus
}
#endif

#endif /* WHD_BUFFER_IMPL_H */
