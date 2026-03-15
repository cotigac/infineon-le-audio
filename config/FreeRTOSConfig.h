/**
 * @file FreeRTOSConfig.h
 * @brief FreeRTOS Configuration for PSoC Edge E81 LE Audio Demo
 *
 * This configuration is optimized for:
 * - PSoC Edge E81 (Cortex-M55 @ 400MHz)
 * - Real-time LE Audio processing (LC3 encode/decode)
 * - Multiple tasks: Audio, BLE, MIDI, I2S
 * - Low-latency audio with 7.5ms/10ms frame timing
 *
 * Task Priority Allocation:
 * ┌─────────────────────────────────────────────────────────┐
 * │ Priority │ Task                  │ Description          │
 * ├──────────┼───────────────────────┼──────────────────────┤
 * │    7     │ I2S DMA Handler       │ Audio DMA callbacks  │
 * │    6     │ Audio/LC3 Task        │ Encode/decode        │
 * │    5     │ ISOC Handler          │ BLE isochronous data │
 * │    4     │ BLE Stack Task        │ BTSTACK processing   │
 * │    3     │ MIDI Router           │ MIDI message routing │
 * │    2     │ USB Task              │ USB MIDI handling    │
 * │    1     │ Application Task      │ User interface       │
 * │    0     │ Idle Task             │ Idle/sleep           │
 * └──────────┴───────────────────────┴──────────────────────┘
 *
 * Copyright 2024 Cristian Cotiga
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Hardware Configuration
 ******************************************************************************/

/**
 * @brief CPU clock frequency in Hz
 *
 * PSoC Edge E81 Cortex-M55 runs at up to 400 MHz
 */
#define configCPU_CLOCK_HZ                          ((uint32_t)400000000)

/**
 * @brief Peripheral clock for timers (if different from CPU)
 */
#define configPERIPHERAL_CLOCK_HZ                   configCPU_CLOCK_HZ

/*******************************************************************************
 * Scheduler Configuration
 ******************************************************************************/

/**
 * @brief Use preemptive scheduler
 *
 * Required for real-time audio - higher priority tasks preempt lower ones
 */
#define configUSE_PREEMPTION                        1

/**
 * @brief RTOS tick rate in Hz
 *
 * 1000 Hz = 1ms tick resolution
 * Good balance for audio timing and CPU overhead
 */
#define configTICK_RATE_HZ                          ((TickType_t)1000)

/**
 * @brief Maximum task priorities
 *
 * 8 priority levels (0-7):
 * - 0: Idle (lowest)
 * - 7: Highest priority (I2S/Audio ISR-level tasks)
 */
#define configMAX_PRIORITIES                        8

/**
 * @brief Idle task should yield if there are ready tasks at idle priority
 */
#define configIDLE_SHOULD_YIELD                     1

/**
 * @brief Use time slicing for tasks at the same priority
 */
#define configUSE_TIME_SLICING                      1

/**
 * @brief Use port-optimized task selection (ARM Cortex-M specific)
 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION     1

/*******************************************************************************
 * Memory Configuration
 ******************************************************************************/

/**
 * @brief Minimum stack size for any task (in words, not bytes)
 *
 * 256 words = 1024 bytes, sufficient for minimal tasks
 */
#define configMINIMAL_STACK_SIZE                    ((uint16_t)256)

/**
 * @brief Total heap size available to FreeRTOS
 *
 * 256 KB for audio buffers, task stacks, and queues
 * PSoC Edge E82 has 5 MB SRAM, so this is conservative
 */
#define configTOTAL_HEAP_SIZE                       ((size_t)(256 * 1024))

/**
 * @brief Maximum length of task names
 */
#define configMAX_TASK_NAME_LEN                     16

/**
 * @brief Use 32-bit tick counter (for longer uptime before overflow)
 */
#define configUSE_16_BIT_TICKS                      0

/**
 * @brief Memory allocation scheme
 *
 * heap_4: Coalescing block allocator - good for varying allocation sizes
 * Suitable for audio buffer allocation
 */
#define configSUPPORT_STATIC_ALLOCATION             1
#define configSUPPORT_DYNAMIC_ALLOCATION            1

/**
 * @brief Stack overflow detection
 *
 * Method 2: Check stack watermark - catches more overflows
 */
#define configCHECK_FOR_STACK_OVERFLOW              2

/**
 * @brief Application must provide memory for idle and timer tasks
 * when using static allocation
 */
#define configKERNEL_PROVIDED_STATIC_MEMORY         0

/*******************************************************************************
 * Task Feature Configuration
 ******************************************************************************/

/**
 * @brief Enable task notifications (used extensively in audio_task)
 *
 * Task notifications are faster than semaphores/queues for ISR-to-task signaling
 */
#define configUSE_TASK_NOTIFICATIONS                1

/**
 * @brief Number of notification indices per task
 *
 * 4 indices allow separate notification channels:
 * - Index 0: I2S events
 * - Index 1: ISOC events
 * - Index 2: Control commands
 * - Index 3: Reserved
 */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES       4

/**
 * @brief Enable mutexes for shared resource protection
 */
#define configUSE_MUTEXES                           1

/**
 * @brief Enable recursive mutexes
 */
#define configUSE_RECURSIVE_MUTEXES                 1

/**
 * @brief Enable counting semaphores
 */
#define configUSE_COUNTING_SEMAPHORES               1

/**
 * @brief Enable queue sets (for waiting on multiple queues)
 */
#define configUSE_QUEUE_SETS                        1

/*******************************************************************************
 * Timer Configuration
 ******************************************************************************/

/**
 * @brief Enable software timers
 */
#define configUSE_TIMERS                            1

/**
 * @brief Timer task priority (higher than most application tasks)
 */
#define configTIMER_TASK_PRIORITY                   (configMAX_PRIORITIES - 2)

/**
 * @brief Timer command queue length
 */
#define configTIMER_QUEUE_LENGTH                    16

/**
 * @brief Timer task stack depth (in words)
 */
#define configTIMER_TASK_STACK_DEPTH                512

/*******************************************************************************
 * Co-routine Configuration (disabled)
 ******************************************************************************/

#define configUSE_CO_ROUTINES                       0
#define configMAX_CO_ROUTINE_PRIORITIES             2

/*******************************************************************************
 * Runtime Statistics and Debugging
 ******************************************************************************/

/**
 * @brief Enable runtime statistics collection
 *
 * Useful for CPU usage monitoring during development
 */
#define configGENERATE_RUN_TIME_STATS               1

/**
 * @brief Configure runtime stats timer
 *
 * Use a hardware timer with higher resolution than the tick
 * The timer should count at least 10x faster than the tick rate
 */
#if (configGENERATE_RUN_TIME_STATS == 1)
    /* These must be defined by the application */
    extern void vConfigureTimerForRunTimeStats(void);
    extern uint32_t ulGetRunTimeCounterValue(void);
    #define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()    vConfigureTimerForRunTimeStats()
    #define portGET_RUN_TIME_COUNTER_VALUE()            ulGetRunTimeCounterValue()
#endif

/**
 * @brief Enable trace facility for visualization tools
 */
#define configUSE_TRACE_FACILITY                    1

/**
 * @brief Include stats formatting functions
 */
#define configUSE_STATS_FORMATTING_FUNCTIONS        1

/**
 * @brief Record the stack high water mark
 */
#define configRECORD_STACK_HIGH_ADDRESS             1

/*******************************************************************************
 * Hook Functions
 ******************************************************************************/

/**
 * @brief Enable idle hook (for sleep/power management)
 */
#define configUSE_IDLE_HOOK                         1

/**
 * @brief Enable tick hook (for periodic maintenance)
 */
#define configUSE_TICK_HOOK                         0

/**
 * @brief Enable malloc failed hook (for debugging)
 */
#define configUSE_MALLOC_FAILED_HOOK                1

/**
 * @brief Enable daemon task startup hook
 */
#define configUSE_DAEMON_TASK_STARTUP_HOOK          0

/*******************************************************************************
 * API Function Inclusion
 ******************************************************************************/

/**
 * @brief Include API functions as needed
 *
 * Set to 1 to include, 0 to exclude (saves code space)
 */
#define INCLUDE_vTaskPrioritySet                    1
#define INCLUDE_uxTaskPriorityGet                   1
#define INCLUDE_vTaskDelete                         1
#define INCLUDE_vTaskSuspend                        1
#define INCLUDE_vTaskDelayUntil                     1
#define INCLUDE_vTaskDelay                          1
#define INCLUDE_xTaskGetSchedulerState              1
#define INCLUDE_xTaskGetCurrentTaskHandle           1
#define INCLUDE_uxTaskGetStackHighWaterMark         1
#define INCLUDE_uxTaskGetStackHighWaterMark2        1
#define INCLUDE_xTaskGetIdleTaskHandle              1
#define INCLUDE_eTaskGetState                       1
#define INCLUDE_xEventGroupSetBitFromISR            1
#define INCLUDE_xTimerPendFunctionCall              1
#define INCLUDE_xTaskAbortDelay                     1
#define INCLUDE_xTaskGetHandle                      1
#define INCLUDE_xTaskResumeFromISR                  1
#define INCLUDE_xSemaphoreGetMutexHolder            1

/*******************************************************************************
 * Interrupt Configuration (ARM Cortex-M55)
 ******************************************************************************/

/**
 * @brief Cortex-M interrupt priority configuration
 *
 * The Cortex-M55 uses 3 priority bits (8 priority levels)
 * Lower numeric values = higher priority
 */

/**
 * @brief Priority bits implemented by the hardware
 */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS                         __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS                         3  /* Cortex-M55 typically uses 3 bits */
#endif

/**
 * @brief Lowest interrupt priority (highest numeric value)
 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY     ((1 << configPRIO_BITS) - 1)

/**
 * @brief Maximum priority for ISRs that can call FreeRTOS API
 *
 * Interrupts at priorities 0-4 cannot call FreeRTOS functions
 * Interrupts at priorities 5-7 can call FreeRTOS API (FromISR versions)
 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

/**
 * @brief Kernel interrupt priority (lowest)
 */
#define configKERNEL_INTERRUPT_PRIORITY             (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/**
 * @brief Maximum syscall interrupt priority
 *
 * Interrupts above this priority (lower numeric value) cannot call FreeRTOS API
 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY        (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/**
 * @brief Maximum API call interrupt priority
 */
#define configMAX_API_CALL_INTERRUPT_PRIORITY       configMAX_SYSCALL_INTERRUPT_PRIORITY

/*******************************************************************************
 * Assertion Configuration
 ******************************************************************************/

/**
 * @brief Define assertion handler
 *
 * Called when configASSERT() fails
 */
extern void vAssertCalled(const char *file, int line);
#define configASSERT(x)     if ((x) == 0) vAssertCalled(__FILE__, __LINE__)

/*******************************************************************************
 * Application-Specific Task Stack Sizes
 ******************************************************************************/

/**
 * @brief Audio task stack size (in words)
 *
 * Large stack for LC3 codec processing and PCM buffers
 * 4096 words = 16 KB
 */
#define configAUDIO_TASK_STACK_SIZE                 4096

/**
 * @brief BLE stack task size (in words)
 *
 * BTSTACK requires significant stack space
 * 2048 words = 8 KB
 */
#define configBLE_TASK_STACK_SIZE                   2048

/**
 * @brief MIDI task stack size (in words)
 *
 * Moderate stack for MIDI routing
 * 1024 words = 4 KB
 */
#define configMIDI_TASK_STACK_SIZE                  1024

/**
 * @brief USB task stack size (in words)
 *
 * USB middleware stack requirements
 * 1024 words = 4 KB
 */
#define configUSB_TASK_STACK_SIZE                   1024

/**
 * @brief ISOC handler task stack size (in words)
 *
 * BLE isochronous data handling
 * 1536 words = 6 KB
 */
#define configISOC_TASK_STACK_SIZE                  1536

/*******************************************************************************
 * Application-Specific Task Priorities
 ******************************************************************************/

/**
 * @brief Task priorities for the LE Audio demo
 *
 * Higher number = higher priority (opposite of interrupt priorities!)
 */
#define configPRIORITY_IDLE                         0
#define configPRIORITY_APP                          1
#define configPRIORITY_USB                          2
#define configPRIORITY_MIDI                         3
#define configPRIORITY_BLE_STACK                    4
#define configPRIORITY_ISOC_HANDLER                 5
#define configPRIORITY_AUDIO                        6
#define configPRIORITY_I2S_DMA                      7

/*******************************************************************************
 * Queue Configuration
 ******************************************************************************/

/**
 * @brief Enable queue registry (for kernel-aware debugging)
 */
#define configQUEUE_REGISTRY_SIZE                   16

/*******************************************************************************
 * Newlib Thread Safety
 ******************************************************************************/

/**
 * @brief Enable newlib reentrant support
 *
 * Required if using newlib with multiple tasks
 */
#define configUSE_NEWLIB_REENTRANT                  1

/*******************************************************************************
 * TrustZone Configuration (if applicable)
 ******************************************************************************/

/**
 * @brief Enable TrustZone secure-side support
 *
 * Set to 1 if running FreeRTOS on the secure side
 */
#define configENABLE_TRUSTZONE                      0

/**
 * @brief Run FreeRTOS on the non-secure side
 */
#define configRUN_FREERTOS_SECURE_ONLY              0

/*******************************************************************************
 * MPU Configuration (if applicable)
 ******************************************************************************/

/**
 * @brief Enable Memory Protection Unit support
 */
#define configENABLE_MPU                            0

/**
 * @brief Enable FPU support (Cortex-M55 has FPU)
 */
#define configENABLE_FPU                            1

/**
 * @brief Enable MVE (Helium) support for Cortex-M55
 *
 * MVE (M-profile Vector Extension) provides DSP/SIMD acceleration
 * Useful for LC3 codec performance
 */
#define configENABLE_MVE                            1

/*******************************************************************************
 * Idle Task Configuration
 ******************************************************************************/

/**
 * @brief Enable tickless idle for power saving
 *
 * Can be enabled for battery-powered devices
 * May need tuning for audio latency requirements
 */
#define configUSE_TICKLESS_IDLE                     0

/**
 * @brief Expected idle time before entering tickless mode
 */
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP       2

/*******************************************************************************
 * Event Groups Configuration
 ******************************************************************************/

/**
 * @brief Number of bits in an event group
 *
 * 24 bits available for user events (8 bits reserved)
 */
#define configUSE_EVENT_GROUPS                      1

/*******************************************************************************
 * Stream Buffer Configuration
 ******************************************************************************/

/**
 * @brief Enable stream buffers
 *
 * Useful for audio data streaming between tasks
 */
#define configUSE_STREAM_BUFFERS                    1

/**
 * @brief Trigger level for stream buffer wakeup
 */
#define configSTREAM_BUFFER_TRIGGER_LEVEL_TEST_MARGIN   2

/*******************************************************************************
 * Message Buffer Configuration
 ******************************************************************************/

/**
 * @brief Enable message buffers
 *
 * Useful for MIDI message passing
 */
#define configUSE_MESSAGE_BUFFERS                   1

/*******************************************************************************
 * SysTick Configuration
 ******************************************************************************/

/**
 * @brief Use Cortex-M SysTick timer for the tick interrupt
 */
#define configUSE_SYSTICK                           1

/*******************************************************************************
 * Definitions for Cortex-M Port
 ******************************************************************************/

/**
 * @brief Map FreeRTOS port handlers to standard names
 */
#define xPortPendSVHandler                          PendSV_Handler
#define vPortSVCHandler                             SVC_Handler

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_CONFIG_H */
