// Add the contents of this file to your FreeRTOSConfig.h (or include it there).
//
// FreeRTOS exposes the current TCB as pxCurrentTCB inside kernel code,
// so these macros can read the task name and priority without an API call.
//
// IMPORTANT: esp_cpu_get_core_id() is ISR-safe and works on both cores.

#include "task_tracer.h"
#include "esp_cpu.h"

// Declared by the FreeRTOS kernel — gives us the current task's TCB fields
// without going through the public API (which would re-acquire the scheduler lock).
extern void *pxCurrentTCBs[];  // indexed by core; each is a TCB_t *

// Convenience macro that casts through void* to reach the public fields
// (pcTaskName is the first field in StaticTask_t / TCB_t).
#define TRACER_CURRENT_NAME() \
    ((const char *)(pxCurrentTCBs[esp_cpu_get_core_id()]))

#define TRACER_CURRENT_PRIO() \
    (*((uint8_t *)((char *)pxCurrentTCBs[esp_cpu_get_core_id()] + sizeof(void *))))

// ----- Simpler alternative (works when configUSE_TRACE_FACILITY == 1) ------
// If the struct fields above don't align, replace the macros with:
//   pcTaskGetName(NULL)  and  uxTaskPriorityGet(NULL)
// Those are safe from task context but NOT from within the scheduler lock.
// For a quick start, use the approach below instead of pxCurrentTCBs:

#undef  TRACER_CURRENT_NAME
#undef  TRACER_CURRENT_PRIO
#define TRACER_CURRENT_NAME() pcTaskGetName(NULL)
#define TRACER_CURRENT_PRIO() ((uint8_t)uxTaskPriorityGet(NULL))

// ── Trace hook definitions (paste into FreeRTOSConfig.h) ────────────────────

#define traceTASK_SWITCHED_IN() \
    tracer_record(TRACER_EVT_SWITCHED_IN,  \
                  TRACER_CURRENT_NAME(),   \
                  (uint8_t)esp_cpu_get_core_id(), \
                  TRACER_CURRENT_PRIO())

#define traceTASK_SWITCHED_OUT() \
    tracer_record(TRACER_EVT_SWITCHED_OUT, \
                  TRACER_CURRENT_NAME(),   \
                  (uint8_t)esp_cpu_get_core_id(), \
                  TRACER_CURRENT_PRIO())
