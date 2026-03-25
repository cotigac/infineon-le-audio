/**
 * @file audio_ipc.c
 * @brief Inter-Processor Communication for LC3 Audio Frames
 *
 * Implementation using Infineon's mtb-ipc library for hardware-backed
 * IPC between CM33 and CM55 cores.
 *
 * Build Configuration:
 *   - CM33: Define CORE_CM33 or COMPONENT_CM33
 *   - CM55: Define CORE_CM55 or COMPONENT_CM55
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_ipc.h"
#include "mtb_ipc.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/*******************************************************************************
 * Constants
 ******************************************************************************/

/* IPC channel for audio queues (must be same on both cores) */
#define AUDIO_IPC_CHANNEL           MTB_IPC_CHAN_0

/* Queue numbers */
#define AUDIO_IPC_QUEUE_TX          (0U)  /* CM55 -> CM33 (encoded frames) */
#define AUDIO_IPC_QUEUE_RX          (1U)  /* CM33 -> CM55 (frames to decode) */

/* Semaphore numbers (16+ are user-available per mtb-ipc docs) */
#define AUDIO_IPC_SEMA_INTERNAL     (16U)  /* Internal IPC protection */
#define AUDIO_IPC_SEMA_TX_QUEUE     (17U)  /* TX queue semaphore */
#define AUDIO_IPC_SEMA_RX_QUEUE     (18U)  /* RX queue semaphore */
#define AUDIO_IPC_SEMA_CM55_READY   (19U)  /* CM55 ready signaling */

/* IRQ assignments (must be different per core, starting from MTB_IPC_IRQ_USER) */
#if defined(CORE_CM33) || defined(COMPONENT_CM33)
#define AUDIO_IPC_SEMA_IRQ          (MTB_IPC_IRQ_USER + 0U)
#define AUDIO_IPC_QUEUE_IRQ         (MTB_IPC_IRQ_USER + 1U)
#else /* CM55 */
#define AUDIO_IPC_SEMA_IRQ          (MTB_IPC_IRQ_USER + 2U)
#define AUDIO_IPC_QUEUE_IRQ         (MTB_IPC_IRQ_USER + 3U)
#endif

/* Debug message queue (simple shared memory, separate from mtb-ipc) */
#define DEBUG_QUEUE_DEPTH           (8U)
#define DEBUG_MSG_MAX_LEN           (128U)
#define DEBUG_MAGIC                 (0x44424730U)  /* "DBG0" */

/*******************************************************************************
 * Debug Queue Structure (simple shared memory for early boot debug)
 ******************************************************************************/

typedef struct {
    char     msg[DEBUG_MSG_MAX_LEN];
    uint8_t  length;
    uint8_t  valid;
} debug_msg_t;

typedef struct __attribute__((aligned(32))) {
    uint32_t    magic;
    debug_msg_t msgs[DEBUG_QUEUE_DEPTH];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
} debug_queue_t;

/* Debug queue at fixed address in shared memory */
#define DEBUG_QUEUE_ADDR    (0x262FF000UL)
static debug_queue_t *g_debug = (debug_queue_t *)DEBUG_QUEUE_ADDR;

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/* mtb-ipc objects */
static mtb_ipc_t g_ipc_instance;
static mtb_ipc_queue_t g_tx_queue;  /* CM55 -> CM33 */
static mtb_ipc_queue_t g_rx_queue;  /* CM33 -> CM55 */

/* CM55 ready semaphore (for cross-core synchronization) */
static mtb_ipc_semaphore_t g_cm55_ready_sema;

/* Shared memory for mtb-ipc (allocated by CM33, used by both) */
#if defined(CORE_CM33) || defined(COMPONENT_CM33)
CY_SECTION_SHAREDMEM static mtb_ipc_shared_t g_ipc_shared __attribute__((aligned(32)));
CY_SECTION_SHAREDMEM static mtb_ipc_queue_data_t g_tx_queue_data __attribute__((aligned(32)));
CY_SECTION_SHAREDMEM static mtb_ipc_queue_data_t g_rx_queue_data __attribute__((aligned(32)));
CY_SECTION_SHAREDMEM static uint8_t g_tx_queue_pool[AUDIO_IPC_QUEUE_DEPTH * sizeof(audio_ipc_frame_t)] __attribute__((aligned(32)));
CY_SECTION_SHAREDMEM static uint8_t g_rx_queue_pool[AUDIO_IPC_QUEUE_DEPTH * sizeof(audio_ipc_frame_t)] __attribute__((aligned(32)));
CY_SECTION_SHAREDMEM static mtb_ipc_semaphore_data_t g_cm55_ready_sema_data __attribute__((aligned(32)));
#endif

/* Local state */
static bool g_ipc_initialized = false;
static bool g_cm55_signaled_ready = false;  /* Cached state for CM33 */
static audio_ipc_stats_t g_ipc_stats = {0};

/* Callback for CM55 */
static audio_ipc_rx_callback_t g_rx_callback = NULL;
static void *g_rx_callback_data = NULL;

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

#if defined(CORE_CM55) || defined(COMPONENT_CM55)
/**
 * @brief Queue event callback for RX frames (CM55 side)
 */
static void rx_queue_callback(void *callback_arg, mtb_ipc_queue_event_t event)
{
    (void)callback_arg;

    if ((event & MTB_IPC_QUEUE_WRITE) && g_rx_callback != NULL) {
        /* New frame available - invoke user callback */
        audio_ipc_frame_t frame;
        while (mtb_ipc_queue_get(&g_rx_queue, &frame, 0) == CY_RSLT_SUCCESS) {
            g_rx_callback(&frame, g_rx_callback_data);
            g_ipc_stats.rx_frames++;
        }
    }
}
#endif

/*******************************************************************************
 * API Functions - CM33 (Primary Core)
 ******************************************************************************/

#if defined(CORE_CM33) || defined(COMPONENT_CM33)

cy_rslt_t audio_ipc_init_primary(void)
{
    cy_rslt_t result;

    if (g_ipc_initialized) {
        return CY_RSLT_SUCCESS;
    }

    printf("[IPC] Initializing mtb-ipc on CM33 (primary)...\n");

    /* Initialize debug queue first (simple shared memory for early boot) */
    memset(g_debug, 0, sizeof(debug_queue_t));
    g_debug->magic = DEBUG_MAGIC;
    __DMB();

    /* Configure mtb-ipc */
    mtb_ipc_config_t ipc_config = {
        .internal_channel_index = AUDIO_IPC_CHANNEL,
        .semaphore_irq = AUDIO_IPC_SEMA_IRQ,
        .queue_irq = AUDIO_IPC_QUEUE_IRQ,
        .semaphore_num = AUDIO_IPC_SEMA_INTERNAL
    };

    /* Initialize mtb-ipc (must be called by first core to boot) */
    result = mtb_ipc_init(&g_ipc_instance, &g_ipc_shared, &ipc_config);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] ERROR: mtb_ipc_init failed: 0x%08lX\n", (unsigned long)result);
        return result;
    }
    printf("[IPC] mtb_ipc_init: OK\n");

    /* Initialize CM55 ready semaphore (CM55 will give this when ready) */
    mtb_ipc_semaphore_config_t sema_config = {
        .preemptable = false,
        .semaphore_num = AUDIO_IPC_SEMA_CM55_READY
    };
    result = mtb_ipc_semaphore_init(&g_ipc_instance, &g_cm55_ready_sema,
                                     &g_cm55_ready_sema_data, &sema_config);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] ERROR: CM55 ready semaphore init failed: 0x%08lX\n", (unsigned long)result);
        return result;
    }
    /* Take the semaphore immediately - CM55 will give it to signal readiness */
    result = mtb_ipc_semaphore_take(&g_cm55_ready_sema, 0);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] WARNING: Could not take CM55 ready semaphore: 0x%08lX\n", (unsigned long)result);
    }
    printf("[IPC] CM55 ready semaphore: OK\n");

    /* Initialize TX queue (CM55 -> CM33) */
    mtb_ipc_queue_config_t tx_config = {
        .channel_num = AUDIO_IPC_CHANNEL,
        .queue_num = AUDIO_IPC_QUEUE_TX,
        .max_num_items = AUDIO_IPC_QUEUE_DEPTH,
        .item_size = sizeof(audio_ipc_frame_t),
        .queue_pool = g_tx_queue_pool,
        .semaphore_num = AUDIO_IPC_SEMA_TX_QUEUE
    };

    result = mtb_ipc_queue_init(&g_ipc_instance, &g_tx_queue, &g_tx_queue_data, &tx_config);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] ERROR: TX queue init failed: 0x%08lX\n", (unsigned long)result);
        return result;
    }
    printf("[IPC] TX queue (CM55->CM33): OK\n");

    /* Initialize RX queue (CM33 -> CM55) */
    mtb_ipc_queue_config_t rx_config = {
        .channel_num = AUDIO_IPC_CHANNEL,
        .queue_num = AUDIO_IPC_QUEUE_RX,
        .max_num_items = AUDIO_IPC_QUEUE_DEPTH,
        .item_size = sizeof(audio_ipc_frame_t),
        .queue_pool = g_rx_queue_pool,
        .semaphore_num = AUDIO_IPC_SEMA_RX_QUEUE
    };

    result = mtb_ipc_queue_init(&g_ipc_instance, &g_rx_queue, &g_rx_queue_data, &rx_config);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] ERROR: RX queue init failed: 0x%08lX\n", (unsigned long)result);
        return result;
    }
    printf("[IPC] RX queue (CM33->CM55): OK\n");

    /* Reset statistics */
    memset(&g_ipc_stats, 0, sizeof(g_ipc_stats));

    g_ipc_initialized = true;
    g_cm55_signaled_ready = false;
    printf("[IPC] CM33 initialization complete\n");

    return CY_RSLT_SUCCESS;
}

cy_rslt_t audio_ipc_send_to_decoder(const audio_ipc_frame_t *frame)
{
    if (!g_ipc_initialized || frame == NULL) {
        return CY_RSLT_TYPE_ERROR;
    }

    /* Put frame in RX queue (CM33 -> CM55), non-blocking */
    cy_rslt_t result = mtb_ipc_queue_put(&g_rx_queue, (void *)frame, 0);

    if (result == CY_RSLT_SUCCESS) {
        g_ipc_stats.tx_frames++;
    } else {
        g_ipc_stats.tx_queue_full++;
    }

    return result;
}

cy_rslt_t audio_ipc_receive_from_encoder(audio_ipc_frame_t *frame)
{
    if (!g_ipc_initialized || frame == NULL) {
        return CY_RSLT_TYPE_ERROR;
    }

    /* Get frame from TX queue (CM55 -> CM33), non-blocking */
    cy_rslt_t result = mtb_ipc_queue_get(&g_tx_queue, frame, 0);

    if (result == CY_RSLT_SUCCESS) {
        g_ipc_stats.rx_frames++;
    } else {
        g_ipc_stats.rx_queue_empty++;
    }

    return result;
}

uint32_t audio_ipc_encoder_frames_available(void)
{
    if (!g_ipc_initialized) {
        return 0;
    }
    return mtb_ipc_queue_count(&g_tx_queue);
}

#endif /* CORE_CM33 */

/*******************************************************************************
 * API Functions - CM55 (Secondary Core)
 ******************************************************************************/

#if defined(CORE_CM55) || defined(COMPONENT_CM55)

cy_rslt_t audio_ipc_init_secondary(void)
{
    cy_rslt_t result;

    if (g_ipc_initialized) {
        return CY_RSLT_SUCCESS;
    }

    printf("[IPC] Initializing mtb-ipc on CM55 (secondary)...\n");

    /* Configure mtb-ipc (must match CM33 config for channel and internal semaphore) */
    mtb_ipc_config_t ipc_config = {
        .internal_channel_index = AUDIO_IPC_CHANNEL,
        .semaphore_irq = AUDIO_IPC_SEMA_IRQ,
        .queue_irq = AUDIO_IPC_QUEUE_IRQ,
        .semaphore_num = AUDIO_IPC_SEMA_INTERNAL
    };

    /* Get handle from CM33's initialized IPC (blocks until CM33 is ready) */
    printf("[IPC] Waiting for CM33 (timeout: %lu ms)...\n",
           (unsigned long)AUDIO_IPC_INIT_TIMEOUT_MS);

    result = mtb_ipc_get_handle(&g_ipc_instance, &ipc_config, AUDIO_IPC_INIT_TIMEOUT_MS);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] ERROR: mtb_ipc_get_handle failed: 0x%08lX\n", (unsigned long)result);
        return result;
    }
    printf("[IPC] Connected to CM33\n");

    /* Get handle to TX queue (CM55 -> CM33) */
    result = mtb_ipc_queue_get_handle(&g_ipc_instance, &g_tx_queue,
                                       AUDIO_IPC_CHANNEL, AUDIO_IPC_QUEUE_TX);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] ERROR: TX queue get_handle failed: 0x%08lX\n", (unsigned long)result);
        return result;
    }
    printf("[IPC] TX queue handle: OK\n");

    /* Get handle to RX queue (CM33 -> CM55) */
    result = mtb_ipc_queue_get_handle(&g_ipc_instance, &g_rx_queue,
                                       AUDIO_IPC_CHANNEL, AUDIO_IPC_QUEUE_RX);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] ERROR: RX queue get_handle failed: 0x%08lX\n", (unsigned long)result);
        return result;
    }
    printf("[IPC] RX queue handle: OK\n");

    /* Register callback for incoming frames */
    mtb_ipc_queue_register_callback(&g_rx_queue, rx_queue_callback, NULL);
    mtb_ipc_queue_enable_event(&g_rx_queue, MTB_IPC_QUEUE_WRITE, true);

    /* Get handle to CM55 ready semaphore and give it to signal CM33 */
    result = mtb_ipc_semaphore_get_handle(&g_ipc_instance, &g_cm55_ready_sema,
                                           AUDIO_IPC_SEMA_CM55_READY, 1000000); /* 1s timeout */
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] ERROR: CM55 ready semaphore get_handle failed: 0x%08lX\n", (unsigned long)result);
        return result;
    }

    /* Give the semaphore to signal CM33 that CM55 is ready */
    result = mtb_ipc_semaphore_give(&g_cm55_ready_sema);
    if (result != CY_RSLT_SUCCESS) {
        printf("[IPC] WARNING: Could not give CM55 ready semaphore: 0x%08lX\n", (unsigned long)result);
    } else {
        printf("[IPC] CM55 ready semaphore given\n");
    }

    /* Reset statistics */
    memset(&g_ipc_stats, 0, sizeof(g_ipc_stats));

    g_ipc_initialized = true;
    printf("[IPC] CM55 initialization complete\n");

    return CY_RSLT_SUCCESS;
}

cy_rslt_t audio_ipc_send_encoded_frame(const audio_ipc_frame_t *frame)
{
    if (!g_ipc_initialized || frame == NULL) {
        return CY_RSLT_TYPE_ERROR;
    }

    /* Put frame in TX queue (CM55 -> CM33), non-blocking */
    cy_rslt_t result = mtb_ipc_queue_put(&g_tx_queue, (void *)frame, 0);

    if (result == CY_RSLT_SUCCESS) {
        g_ipc_stats.tx_frames++;
    } else {
        g_ipc_stats.tx_queue_full++;
    }

    return result;
}

cy_rslt_t audio_ipc_receive_for_decode(audio_ipc_frame_t *frame)
{
    if (!g_ipc_initialized || frame == NULL) {
        return CY_RSLT_TYPE_ERROR;
    }

    /* Get frame from RX queue (CM33 -> CM55), non-blocking */
    cy_rslt_t result = mtb_ipc_queue_get(&g_rx_queue, frame, 0);

    if (result == CY_RSLT_SUCCESS) {
        g_ipc_stats.rx_frames++;
    } else {
        g_ipc_stats.rx_queue_empty++;
    }

    return result;
}

uint32_t audio_ipc_decoder_frames_available(void)
{
    if (!g_ipc_initialized) {
        return 0;
    }
    return mtb_ipc_queue_count(&g_rx_queue);
}

cy_rslt_t audio_ipc_register_rx_callback(audio_ipc_rx_callback_t callback, void *user_data)
{
    g_rx_callback = callback;
    g_rx_callback_data = user_data;
    return CY_RSLT_SUCCESS;
}

#endif /* CORE_CM55 */

/*******************************************************************************
 * API Functions - Common
 ******************************************************************************/

cy_rslt_t audio_ipc_get_stats(audio_ipc_stats_t *stats)
{
    if (stats == NULL) {
        return CY_RSLT_TYPE_ERROR;
    }

    memcpy(stats, &g_ipc_stats, sizeof(audio_ipc_stats_t));
    return CY_RSLT_SUCCESS;
}

void audio_ipc_reset_stats(void)
{
    memset(&g_ipc_stats, 0, sizeof(g_ipc_stats));
}

bool audio_ipc_is_ready(void)
{
#if defined(CORE_CM33) || defined(COMPONENT_CM33)
    /* On CM33: check if both CM33 is initialized AND CM55 has signaled ready */
    if (!g_ipc_initialized) {
        return false;
    }

    /* If already confirmed ready, return cached value */
    if (g_cm55_signaled_ready) {
        return true;
    }

    /* Try to take the semaphore with 0 timeout (non-blocking check) */
    /* If we can take it, CM55 has given it (signaling ready) */
    cy_rslt_t result = mtb_ipc_semaphore_take(&g_cm55_ready_sema, 0);
    if (result == CY_RSLT_SUCCESS) {
        g_cm55_signaled_ready = true;
        return true;
    }

    return false;
#else
    /* On CM55: just check local initialization */
    return g_ipc_initialized;
#endif
}

void audio_ipc_deinit(void)
{
    if (!g_ipc_initialized) {
        return;
    }

    /* Free queues */
    mtb_ipc_queue_free(&g_tx_queue);
    mtb_ipc_queue_free(&g_rx_queue);

    /* Free semaphore */
    mtb_ipc_semaphore_free(&g_cm55_ready_sema);

    g_ipc_initialized = false;

#if defined(CORE_CM33) || defined(COMPONENT_CM33)
    g_cm55_signaled_ready = false;
#endif

#if defined(CORE_CM55) || defined(COMPONENT_CM55)
    g_rx_callback = NULL;
    g_rx_callback_data = NULL;
#endif
}

/*******************************************************************************
 * API Functions - Debug Output (simple shared memory, works before mtb-ipc init)
 ******************************************************************************/

void audio_ipc_debug_print(const char *msg)
{
    if (msg == NULL || g_debug == NULL) {
        return;
    }

    /* Check if CM33 has initialized the debug queue */
    if (g_debug->magic != DEBUG_MAGIC) {
        return;
    }

    /* Check if queue is full */
    if (g_debug->count >= DEBUG_QUEUE_DEPTH) {
        return;
    }

    /* Copy message to queue */
    uint32_t head = g_debug->head;
    debug_msg_t *entry = &g_debug->msgs[head];

    size_t len = strlen(msg);
    if (len >= DEBUG_MSG_MAX_LEN) {
        len = DEBUG_MSG_MAX_LEN - 1;
    }
    memcpy(entry->msg, msg, len);
    entry->msg[len] = '\0';
    entry->length = (uint8_t)len;

    __DMB();
    entry->valid = 1;

    /* Update indices */
    g_debug->head = (head + 1) % DEBUG_QUEUE_DEPTH;
    g_debug->count++;
    __DMB();
}

void audio_ipc_debug_printf(const char *fmt, ...)
{
    char buf[DEBUG_MSG_MAX_LEN];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    audio_ipc_debug_print(buf);
}

uint32_t audio_ipc_debug_available(void)
{
    if (g_debug == NULL || g_debug->magic != DEBUG_MAGIC) {
        return 0;
    }
    return g_debug->count;
}

bool audio_ipc_debug_read(char *buf, uint32_t buf_size)
{
    if (buf == NULL || buf_size == 0 || g_debug == NULL) {
        return false;
    }

    if (g_debug->magic != DEBUG_MAGIC || g_debug->count == 0) {
        return false;
    }

    uint32_t tail = g_debug->tail;
    debug_msg_t *entry = &g_debug->msgs[tail];

    if (!entry->valid) {
        return false;
    }

    /* Copy message */
    size_t copy_len = entry->length;
    if (copy_len >= buf_size) {
        copy_len = buf_size - 1;
    }
    memcpy(buf, entry->msg, copy_len);
    buf[copy_len] = '\0';

    /* Clear entry */
    entry->valid = 0;
    __DMB();

    /* Update indices */
    g_debug->tail = (tail + 1) % DEBUG_QUEUE_DEPTH;
    g_debug->count--;

    return true;
}

void audio_ipc_debug_process(void)
{
#if defined(CORE_CM33) || defined(COMPONENT_CM33)
    char buf[DEBUG_MSG_MAX_LEN];

    while (audio_ipc_debug_read(buf, sizeof(buf))) {
        if (buf[0] != '\0') {
            printf("[CM55] %s", buf);
            size_t len = strlen(buf);
            if (len > 0 && buf[len-1] != '\n') {
                printf("\n");
            }
        }
    }
#endif
}

/* end of file */
