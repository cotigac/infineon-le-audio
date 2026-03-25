/**
 * @file audio_ipc.c
 * @brief Inter-Processor Communication for LC3 Audio Frames
 *
 * Implementation of IPC between CM33 and CM55 cores using Infineon's
 * mtb-ipc library for LC3 audio frame transfer.
 *
 * Build Configuration:
 *   - CM33: Define CORE_CM33 or COMPONENT_CM33
 *   - CM55: Define CORE_CM55 or COMPONENT_CM55
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_ipc.h"
#include "cy_ipc_drv.h"
#include "cy_sysint.h"
#include "cy_syslib.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* Include mtb-ipc if available, otherwise use PDL IPC directly */
#if defined(COMPONENT_MTB_IPC)
#include "mtb_ipc.h"
#define USE_MTB_IPC 1
#else
#define USE_MTB_IPC 0
#endif

/*******************************************************************************
 * Constants
 ******************************************************************************/

/* IPC channel assignments for PSoC Edge */
#define IPC_CHANNEL_LC3_TX          (8U)   /* CM55 -> CM33 encoded frames */
#define IPC_CHANNEL_LC3_RX          (9U)   /* CM33 -> CM55 frames to decode */

/* IPC interrupt configuration */
#define IPC_INTR_STRUCT_TX          (8U)
#define IPC_INTR_STRUCT_RX          (9U)

/* Shared memory section (in SOCMEM, accessible by both cores) */
#define AUDIO_IPC_SHARED_MEM_ATTR   __attribute__((section(".cy_sharedmem"), aligned(32)))

/*******************************************************************************
 * Shared Memory Structures
 ******************************************************************************/

/**
 * @brief Circular buffer for LC3 frames in shared memory
 */
typedef struct __attribute__((aligned(32))) {
    audio_ipc_frame_t frames[AUDIO_IPC_QUEUE_DEPTH];
    volatile uint32_t head;        /* Write index (producer) */
    volatile uint32_t tail;        /* Read index (consumer) */
    volatile uint32_t count;       /* Number of frames in queue */
    volatile bool     initialized; /* Queue is ready */
} audio_ipc_queue_t;

/**
 * @brief Debug message entry in shared memory
 */
typedef struct __attribute__((aligned(4))) {
    char     msg[AUDIO_IPC_DEBUG_MSG_MAX_LEN];
    uint8_t  length;
    uint8_t  valid;
    uint8_t  reserved[2];
} audio_ipc_debug_msg_t;

/**
 * @brief Debug message queue in shared memory
 */
typedef struct __attribute__((aligned(32))) {
    audio_ipc_debug_msg_t msgs[AUDIO_IPC_DEBUG_QUEUE_DEPTH];
    volatile uint32_t head;        /* Write index (CM55) */
    volatile uint32_t tail;        /* Read index (CM33) */
    volatile uint32_t count;       /* Number of messages */
} audio_ipc_debug_queue_t;

/**
 * @brief Shared memory region for IPC
 */
typedef struct __attribute__((aligned(32))) {
    audio_ipc_queue_t tx_queue;    /* CM55 -> CM33 (encoded frames) */
    audio_ipc_queue_t rx_queue;    /* CM33 -> CM55 (frames to decode) */
    volatile bool     cm33_ready;  /* CM33 has initialized */
    volatile bool     cm55_ready;  /* CM55 has initialized */
    uint32_t          magic;       /* Magic number for validation */
    audio_ipc_debug_queue_t debug_queue;  /* CM55 -> CM33 debug messages */
} audio_ipc_shared_t;

#define AUDIO_IPC_MAGIC     (0x4C433341U)  /* "LC3A" */

/*******************************************************************************
 * Private Variables
 ******************************************************************************/

/* Shared memory at FIXED ADDRESS - must match m33_m55_shared region in linker scripts
 * CM33 linker: m33_m55_shared at 0x262FC000 (or 0x062FC000 cached)
 * CM55 linker: m33_m55_shared at 0x262FC000
 * Using non-cached address for reliable cross-core visibility */
#define AUDIO_IPC_SHARED_ADDR   (0x262FC000UL)

/* Pointer to shared memory (same fixed address on both cores) */
static audio_ipc_shared_t *g_ipc = (audio_ipc_shared_t *)AUDIO_IPC_SHARED_ADDR;

/* Local state */
static bool g_ipc_initialized = false;
static audio_ipc_stats_t g_ipc_stats = {0};

/* Callback for CM55 */
static audio_ipc_rx_callback_t g_rx_callback = NULL;
static void *g_rx_callback_data = NULL;

/*******************************************************************************
 * Private Functions - Queue Operations
 ******************************************************************************/

/**
 * @brief Initialize a queue
 */
static void queue_init(audio_ipc_queue_t *queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->initialized = true;

    /* Clear all frames */
    memset(queue->frames, 0, sizeof(queue->frames));
}

/**
 * @brief Check if queue is full
 */
static inline bool queue_is_full(const audio_ipc_queue_t *queue)
{
    return queue->count >= AUDIO_IPC_QUEUE_DEPTH;
}

/**
 * @brief Check if queue is empty
 */
static inline bool queue_is_empty(const audio_ipc_queue_t *queue)
{
    return queue->count == 0;
}

/**
 * @brief Put frame into queue (non-blocking)
 */
static cy_rslt_t queue_put(audio_ipc_queue_t *queue, const audio_ipc_frame_t *frame)
{
    if (queue_is_full(queue)) {
        return CY_RSLT_TYPE_ERROR;
    }

    /* Copy frame to queue */
    uint32_t head = queue->head;
    memcpy(&queue->frames[head], frame, sizeof(audio_ipc_frame_t));

    /* Memory barrier to ensure data is written before index update */
    __DMB();

    /* Update head index */
    queue->head = (head + 1) % AUDIO_IPC_QUEUE_DEPTH;
    queue->count++;

    return CY_RSLT_SUCCESS;
}

/**
 * @brief Get frame from queue (non-blocking)
 */
static cy_rslt_t queue_get(audio_ipc_queue_t *queue, audio_ipc_frame_t *frame)
{
    if (queue_is_empty(queue)) {
        return CY_RSLT_TYPE_ERROR;
    }

    /* Copy frame from queue */
    uint32_t tail = queue->tail;
    memcpy(frame, &queue->frames[tail], sizeof(audio_ipc_frame_t));

    /* Memory barrier to ensure data is read before index update */
    __DMB();

    /* Update tail index */
    queue->tail = (tail + 1) % AUDIO_IPC_QUEUE_DEPTH;
    queue->count--;

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Private Functions - IPC Notifications
 ******************************************************************************/

#if defined(CORE_CM33) || defined(COMPONENT_CM33)

/**
 * @brief Notify CM55 that new frame is available for decode
 */
static void notify_cm55_rx_available(void)
{
    /* Use IPC channel to signal CM55 */
    IPC_STRUCT_Type *ipc_struct = Cy_IPC_Drv_GetIpcBaseAddress(IPC_CHANNEL_LC3_RX);

    /* Acquire lock, set notify, release */
    if (Cy_IPC_Drv_LockAcquire(ipc_struct) == CY_IPC_DRV_SUCCESS) {
        Cy_IPC_Drv_AcquireNotify(ipc_struct, (1UL << IPC_INTR_STRUCT_RX));
        Cy_IPC_Drv_LockRelease(ipc_struct, CY_IPC_NO_NOTIFICATION);
    }
}

#endif /* CORE_CM33 */

#if defined(CORE_CM55) || defined(COMPONENT_CM55)

/**
 * @brief Notify CM33 that encoded frame is available
 */
static void notify_cm33_tx_available(void)
{
    /* Use IPC channel to signal CM33 */
    IPC_STRUCT_Type *ipc_struct = Cy_IPC_Drv_GetIpcBaseAddress(IPC_CHANNEL_LC3_TX);

    /* Acquire lock, set notify, release */
    if (Cy_IPC_Drv_LockAcquire(ipc_struct) == CY_IPC_DRV_SUCCESS) {
        Cy_IPC_Drv_AcquireNotify(ipc_struct, (1UL << IPC_INTR_STRUCT_TX));
        Cy_IPC_Drv_LockRelease(ipc_struct, CY_IPC_NO_NOTIFICATION);
    }
}

/**
 * @brief IPC interrupt handler for CM55 (frames from CM33)
 */
static void ipc_rx_interrupt_handler(void)
{
    IPC_INTR_STRUCT_Type *ipc_intr = Cy_IPC_Drv_GetIntrBaseAddr(IPC_INTR_STRUCT_RX);

    /* Clear interrupt */
    Cy_IPC_Drv_ClearInterrupt(ipc_intr, CY_IPC_NO_NOTIFICATION,
                               (1UL << IPC_CHANNEL_LC3_RX));

    /* If callback registered, invoke it for each available frame */
    if (g_rx_callback != NULL) {
        audio_ipc_frame_t frame;
        while (queue_get(&g_ipc->rx_queue, &frame) == CY_RSLT_SUCCESS) {
            g_rx_callback(&frame, g_rx_callback_data);
            g_ipc_stats.rx_frames++;
        }
    }
}

#endif /* CORE_CM55 */

/*******************************************************************************
 * API Functions - CM33 (Primary Core)
 ******************************************************************************/

#if defined(CORE_CM33) || defined(COMPONENT_CM33)

cy_rslt_t audio_ipc_init_primary(void)
{
    if (g_ipc_initialized) {
        return CY_RSLT_SUCCESS;
    }

    /* Initialize shared memory region */
    memset(g_ipc, 0, sizeof(audio_ipc_shared_t));

    /* Initialize queues */
    queue_init(&g_ipc->tx_queue);
    queue_init(&g_ipc->rx_queue);

    /* Set magic and ready flag */
    g_ipc->magic = AUDIO_IPC_MAGIC;
    g_ipc->cm33_ready = true;

    /* Memory barrier to ensure all writes are visible */
    __DMB();
    __DSB();

    /* Reset statistics */
    memset(&g_ipc_stats, 0, sizeof(g_ipc_stats));

    g_ipc_initialized = true;

    return CY_RSLT_SUCCESS;
}

cy_rslt_t audio_ipc_send_to_decoder(const audio_ipc_frame_t *frame)
{
    if (!g_ipc_initialized || frame == NULL) {
        return CY_RSLT_TYPE_ERROR;
    }

    if (!g_ipc->cm55_ready) {
        /* CM55 not ready yet */
        return CY_RSLT_TYPE_ERROR;
    }

    cy_rslt_t result = queue_put(&g_ipc->rx_queue, frame);

    if (result == CY_RSLT_SUCCESS) {
        g_ipc_stats.tx_frames++;
        /* Notify CM55 */
        notify_cm55_rx_available();
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

    cy_rslt_t result = queue_get(&g_ipc->tx_queue, frame);

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
    return g_ipc->tx_queue.count;
}

#endif /* CORE_CM33 */

/*******************************************************************************
 * API Functions - CM55 (Secondary Core)
 ******************************************************************************/

#if defined(CORE_CM55) || defined(COMPONENT_CM55)

cy_rslt_t audio_ipc_init_secondary(void)
{
    uint32_t timeout_ms = 5000U;  /* 5 second timeout */
    volatile bool cm33_ready_val;

    if (g_ipc_initialized) {
        return CY_RSLT_SUCCESS;
    }

    /* Data synchronization barrier to ensure we see CM33's writes */
    __DSB();
    __DMB();

    printf("[CM55 IPC] Shared memory at 0x%08lX\n", (unsigned long)(uintptr_t)g_ipc);
    printf("[CM55 IPC] magic=0x%08lX, cm33_ready=%d\n",
           (unsigned long)g_ipc->magic, (int)g_ipc->cm33_ready);

    /* Wait for CM33 to initialize shared memory */
    printf("[CM55 IPC] Waiting for CM33...\n");
    while (timeout_ms > 0) {
        /* Force fresh read from memory */
        __DSB();
        __DMB();
        cm33_ready_val = g_ipc->cm33_ready;

        if (cm33_ready_val && g_ipc->magic == AUDIO_IPC_MAGIC) {
            break;  /* CM33 is ready */
        }

        /* Use Cy_SysLib_Delay (ms) instead of DelayUs - more reliable pre-scheduler */
        Cy_SysLib_Delay(1);
        timeout_ms--;
    }

    if (timeout_ms == 0) {
        printf("[CM55 IPC] ERROR: CM33 not ready (timeout)\n");
        printf("[CM55 IPC]   magic=0x%08lX (expected 0x%08lX)\n",
               (unsigned long)g_ipc->magic, (unsigned long)AUDIO_IPC_MAGIC);
        printf("[CM55 IPC]   cm33_ready=%d\n", (int)g_ipc->cm33_ready);
        return CY_RSLT_TYPE_ERROR;
    }

    printf("[CM55 IPC] CM33 ready after %lu ms\n", (unsigned long)(5000U - timeout_ms));

    /* Memory barrier to ensure we see CM33's writes */
    __DMB();

    /* Setup IPC interrupt for receiving frames from CM33 */
    cy_stc_sysint_t ipc_intr_config = {
        .intrSrc = (IRQn_Type)(cpuss_interrupts_ipc_0_IRQn + IPC_INTR_STRUCT_RX),
        .intrPriority = 3U
    };

    Cy_SysInt_Init(&ipc_intr_config, ipc_rx_interrupt_handler);
    NVIC_EnableIRQ(ipc_intr_config.intrSrc);

    /* Enable interrupt for the RX channel */
    IPC_INTR_STRUCT_Type *ipc_intr = Cy_IPC_Drv_GetIntrBaseAddr(IPC_INTR_STRUCT_RX);
    Cy_IPC_Drv_SetInterruptMask(ipc_intr, CY_IPC_NO_NOTIFICATION,
                                 (1UL << IPC_CHANNEL_LC3_RX));

    /* Reset statistics */
    memset(&g_ipc_stats, 0, sizeof(g_ipc_stats));

    /* Signal that CM55 is ready */
    g_ipc->cm55_ready = true;
    __DMB();

    g_ipc_initialized = true;

    return CY_RSLT_SUCCESS;
}

cy_rslt_t audio_ipc_send_encoded_frame(const audio_ipc_frame_t *frame)
{
    if (!g_ipc_initialized || frame == NULL) {
        return CY_RSLT_TYPE_ERROR;
    }

    cy_rslt_t result = queue_put(&g_ipc->tx_queue, frame);

    if (result == CY_RSLT_SUCCESS) {
        g_ipc_stats.tx_frames++;
        /* Notify CM33 */
        notify_cm33_tx_available();
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

    cy_rslt_t result = queue_get(&g_ipc->rx_queue, frame);

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
    return g_ipc->rx_queue.count;
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
    return g_ipc_initialized && g_ipc->cm33_ready && g_ipc->cm55_ready;
}

void audio_ipc_deinit(void)
{
    g_ipc_initialized = false;

#if defined(CORE_CM33) || defined(COMPONENT_CM33)
    g_ipc->cm33_ready = false;
#endif

#if defined(CORE_CM55) || defined(COMPONENT_CM55)
    g_ipc->cm55_ready = false;
    g_rx_callback = NULL;
    g_rx_callback_data = NULL;
#endif
}

/*******************************************************************************
 * API Functions - Debug Output
 ******************************************************************************/

/**
 * @brief Write debug message to shared memory queue (CM55 side)
 */
void audio_ipc_debug_print(const char *msg)
{
    if (msg == NULL || g_ipc == NULL) {
        return;
    }

    /* Check if CM33 has initialized (magic is set) */
    if (g_ipc->magic != AUDIO_IPC_MAGIC) {
        return;  /* CM33 hasn't initialized shared memory yet */
    }

    audio_ipc_debug_queue_t *q = &g_ipc->debug_queue;

    /* Check if queue is full */
    if (q->count >= AUDIO_IPC_DEBUG_QUEUE_DEPTH) {
        return;  /* Drop message if queue full */
    }

    /* Copy message to queue */
    uint32_t head = q->head;
    audio_ipc_debug_msg_t *entry = &q->msgs[head];

    /* Copy message, truncating if necessary */
    size_t len = strlen(msg);
    if (len >= AUDIO_IPC_DEBUG_MSG_MAX_LEN) {
        len = AUDIO_IPC_DEBUG_MSG_MAX_LEN - 1;
    }
    memcpy(entry->msg, msg, len);
    entry->msg[len] = '\0';
    entry->length = (uint8_t)len;

    /* Memory barrier before marking valid */
    __DMB();
    entry->valid = 1;

    /* Update head and count */
    q->head = (head + 1) % AUDIO_IPC_DEBUG_QUEUE_DEPTH;
    q->count++;

    __DMB();
}

/**
 * @brief Formatted debug printf (CM55 side)
 */
void audio_ipc_debug_printf(const char *fmt, ...)
{
    char buf[AUDIO_IPC_DEBUG_MSG_MAX_LEN];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    audio_ipc_debug_print(buf);
}

/**
 * @brief Check number of pending debug messages (CM33 side)
 */
uint32_t audio_ipc_debug_available(void)
{
    if (g_ipc == NULL || g_ipc->magic != AUDIO_IPC_MAGIC) {
        return 0;
    }
    return g_ipc->debug_queue.count;
}

/**
 * @brief Read debug message from queue (CM33 side)
 */
bool audio_ipc_debug_read(char *buf, uint32_t buf_size)
{
    if (buf == NULL || buf_size == 0 || g_ipc == NULL) {
        return false;
    }

    if (g_ipc->magic != AUDIO_IPC_MAGIC) {
        return false;
    }

    audio_ipc_debug_queue_t *q = &g_ipc->debug_queue;

    if (q->count == 0) {
        return false;
    }

    uint32_t tail = q->tail;
    audio_ipc_debug_msg_t *entry = &q->msgs[tail];

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

    /* Memory barrier before updating indices */
    __DMB();

    /* Update tail and count */
    q->tail = (tail + 1) % AUDIO_IPC_DEBUG_QUEUE_DEPTH;
    q->count--;

    return true;
}

/**
 * @brief Process and print all pending debug messages (CM33 side)
 */
void audio_ipc_debug_process(void)
{
#if defined(CORE_CM33) || defined(COMPONENT_CM33)
    char buf[AUDIO_IPC_DEBUG_MSG_MAX_LEN];

    while (audio_ipc_debug_read(buf, sizeof(buf))) {
        /* Print with [CM55] prefix - message may already have newline */
        if (buf[0] != '\0') {
            printf("[CM55] %s", buf);
            /* Add newline if not present */
            size_t len = strlen(buf);
            if (len > 0 && buf[len-1] != '\n') {
                printf("\n");
            }
        }
    }
#endif
}

/* end of file */
