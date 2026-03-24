# CM33/CM55 Initialization Refactoring

## Problem Summary

The current initialization architecture has several critical issues:

| Issue | Severity | Location |
|-------|----------|----------|
| `init_control_modules()` inside `ble_task` | HIGH | main.c:348 |
| Binary semaphore for multi-task sync | HIGH | main.c:520 |
| CM55 IPC busy-wait with `__NOP()` | HIGH | audio_ipc.c:356-359 |
| BLE task self-deletes on timeout | MEDIUM | main.c:342 |
| No CM55 ready synchronization | MEDIUM | main.c:203-207 |

### Deadlock Scenario

```
T0:  BLE task takes g_bt_ready_sem (blocks, 10s timeout)
T1:  USB task takes g_bt_ready_sem (blocks, portMAX_DELAY)
T2:  MIDI task takes g_bt_ready_sem (blocks, portMAX_DELAY)
T3:  WiFi task takes g_bt_ready_sem (blocks, portMAX_DELAY)
T4:  BT stack times out after 10 seconds
T5:  BLE task calls vTaskDelete(NULL)
     --> Semaphore never given!
     --> USB, MIDI, WiFi blocked FOREVER
```

## Solution Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         main()                               │
│  1. Platform init (BSP, UART, IPC, boot CM55)               │
│  2. Create event group (replaces binary semaphore)          │
│  3. Create ALL tasks including new main_task                │
│  4. Start BT stack (async)                                  │
│  5. Start scheduler                                          │
└─────────────────────────────────────────────────────────────┘
                              │
    ┌─────────────────────────┼─────────────────────────┐
    ▼                         ▼                         ▼
┌──────────┐           ┌──────────┐              ┌──────────┐
│main_task │           │ ble_task │              │Other tasks│
│ (NEW)    │           │          │              │           │
│Priority 6│           │Priority 5│              │           │
│          │           │          │              │           │
│1. Wait   │           │1. Wait   │              │1. Wait    │
│  BT_READY│           │  BT_READY│              │  SYS_READY│
│2. Wait   │           │2. BLE    │              │2. Task    │
│  CM55_RDY│           │  process │              │  process  │
│3. Init   │           │  loop    │              │           │
│  modules │           │          │              │           │
│4. Set    │           │          │              │           │
│  SYS_RDY │           │          │              │           │
└──────────┘           └──────────┘              └──────────┘
```

## Event Group Design

Replace binary semaphore with FreeRTOS event group:

```c
#include "event_groups.h"

#define EVT_BT_READY      (1 << 0)  // BT stack initialized
#define EVT_CM55_READY    (1 << 1)  // CM55 IPC ready
#define EVT_SYSTEM_READY  (1 << 2)  // All modules initialized

EventGroupHandle_t g_system_events = NULL;
```

### Event Flow

```
BT Stack (BTM_ENABLED_EVT)
    │
    ▼
app_le_audio_on_bt_ready()
    │
    ├──► xEventGroupSetBits(EVT_BT_READY)
    │
    ▼
main_task wakes up
    │
    ├──► Wait for CM55 IPC (polling with timeout)
    │
    ├──► xEventGroupSetBits(EVT_CM55_READY)
    │
    ├──► init_control_modules()
    │
    └──► xEventGroupSetBits(EVT_SYSTEM_READY)
              │
              ▼
         All tasks wake up and proceed
```

## Files to Modify

### 1. `mtb/le-audio/proj_cm33_ns/source/main.c`

#### Add includes and macros

```c
#include "event_groups.h"

#define MAIN_TASK_STACK_SIZE    (2048)
#define MAIN_TASK_PRIORITY      (6)  // Highest priority for init

#define EVT_BT_READY      (1 << 0)
#define EVT_CM55_READY    (1 << 1)
#define EVT_SYSTEM_READY  (1 << 2)
```

#### Replace semaphore with event group

```c
// OLD:
SemaphoreHandle_t g_bt_ready_sem = NULL;

// NEW:
EventGroupHandle_t g_system_events = NULL;
```

#### Add main_task

```c
static TaskHandle_t g_main_task_handle = NULL;

static void main_task(void *pvParameters)
{
    (void)pvParameters;
    EventBits_t bits;

    printf("[MAIN] System initialization task started\n");

    /* Step 1: Wait for BT stack ready (15 second timeout) */
    printf("[MAIN] Waiting for BT stack...\n");
    bits = xEventGroupWaitBits(g_system_events, EVT_BT_READY,
                                pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    if (!(bits & EVT_BT_READY)) {
        printf("[MAIN] WARNING: BT stack timeout - continuing without BT\n");
    } else {
        printf("[MAIN] BT stack ready\n");
    }

    /* Step 2: Wait for CM55 IPC ready (5 second timeout) */
    printf("[MAIN] Waiting for CM55 IPC...\n");
    uint32_t cm55_timeout = 500;  // 5 seconds (500 * 10ms)
    while (!audio_ipc_is_ready() && cm55_timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        cm55_timeout--;
    }
    if (audio_ipc_is_ready()) {
        printf("[MAIN] CM55 IPC ready\n");
        xEventGroupSetBits(g_system_events, EVT_CM55_READY);
    } else {
        printf("[MAIN] WARNING: CM55 IPC timeout - continuing without audio\n");
    }

    /* Step 3: Initialize control modules */
    printf("[MAIN] Initializing control modules...\n");
    init_control_modules();

    /* Step 4: Signal system ready */
    g_app_running = true;
    xEventGroupSetBits(g_system_events, EVT_SYSTEM_READY);
    printf("[MAIN] System initialization complete!\n\n");

    /* main_task idles - could monitor system health */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

#### Modify ble_task

```c
static void ble_task(void *pvParameters)
{
    (void)pvParameters;
    EventBits_t bits;

    printf("[BLE] Task started, waiting for BT stack...\n");

    /* Wait for BT ready - graceful degradation on timeout */
    bits = xEventGroupWaitBits(g_system_events, EVT_BT_READY,
                                pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    if (!(bits & EVT_BT_READY)) {
        printf("[BLE] WARNING: BT stack timeout - task will idle\n");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    printf("[BLE] BT stack ready, waiting for system init...\n");

    /* Wait for system ready before processing */
    xEventGroupWaitBits(g_system_events, EVT_SYSTEM_READY,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    printf("[BLE] Starting main loop\n");

    while (g_app_running) {
        le_audio_process();
        app_le_audio_process();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

#### Modify usb_task, midi_task, wifi_task

```c
static void usb_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[USB] Task started, waiting for system ready...\n");

    /* Wait for SYSTEM ready - all modules initialized */
    xEventGroupWaitBits(g_system_events, EVT_SYSTEM_READY,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    printf("[USB] System ready, starting USB processing\n");

    while (g_app_running) {
        usb_composite_process();
        midi_usb_process();
        at_bt_cmds_process();
        at_leaudio_cmds_process();
        at_wifi_cmds_process();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Similar changes for midi_task and wifi_task
```

#### Modify main()

```c
/* Create event group instead of semaphore */
g_system_events = xEventGroupCreate();
if (g_system_events == NULL) {
    printf("ERROR: Failed to create system event group\n");
    handle_app_error();
}

/* Create main_task FIRST */
task_result = xTaskCreate(main_task, "MAIN", MAIN_TASK_STACK_SIZE, NULL,
                          MAIN_TASK_PRIORITY, &g_main_task_handle);
if (task_result != pdPASS) {
    printf("ERROR: Failed to create main task\n");
    handle_app_error();
}

/* Then create other tasks... */
```

### 2. `mtb/le-audio/proj_cm33_ns/source/app_le_audio.c`

```c
// Change extern declaration
extern EventGroupHandle_t g_system_events;

void app_le_audio_on_bt_ready(void)
{
    printf("LE Audio: BT stack ready, signaling tasks\r\n");

    /* Signal BT ready event */
    if (g_system_events != NULL) {
        xEventGroupSetBits(g_system_events, (1 << 0));  // EVT_BT_READY
    }
}
```

### 3. `source/ipc/audio_ipc.c`

Fix the busy-wait loop in CM55:

```c
cy_rslt_t audio_ipc_init_secondary(void)
{
    uint32_t timeout_ms = 5000;  /* 5 second timeout - real time based */

    if (g_ipc_initialized) {
        return CY_RSLT_SUCCESS;
    }

    /* Memory barrier before checking shared memory */
    __DMB();

    /* Wait for CM33 to initialize shared memory - time-based delay */
    while (!g_ipc->cm33_ready && timeout_ms > 0) {
        Cy_SysLib_DelayUs(1000);  /* 1ms delay */
        timeout_ms--;
        __DMB();  /* Refresh view of shared memory each iteration */
    }

    if (timeout_ms == 0 || g_ipc->magic != AUDIO_IPC_MAGIC) {
        return CY_RSLT_TYPE_ERROR;
    }

    /* Rest of initialization unchanged... */
}
```

## Task Priority Summary

| Task | Priority | Role |
|------|----------|------|
| main_task | 6 | System initialization, then idle |
| ble_task | 5 | BLE/BTSTACK processing |
| usb_task | 4 | USB composite device |
| wifi_task | 3 | Wi-Fi bridge |
| midi_task | 2 | MIDI routing |
| ipc_debug_task | 1 | CM55 debug message processing |

## Testing Checklist

- [ ] Build succeeds for CM33 and CM55
- [ ] BT initialization works, EVT_BT_READY is set
- [ ] CM55 IPC initialization works, EVT_CM55_READY is set
- [ ] Control modules initialize successfully
- [ ] EVT_SYSTEM_READY is set, all tasks proceed
- [ ] BLE timeout doesn't cause deadlock
- [ ] CM55 timeout doesn't cause crash
- [ ] CM55 debug messages appear in console
- [ ] LE Audio streaming works end-to-end

## Rollback Plan

If issues arise, revert to the commit before this refactoring. The changes are isolated to:
- `main.c` - Task architecture
- `app_le_audio.c` - Signal mechanism
- `audio_ipc.c` - CM55 timeout
