#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Packet framing bytes (scan for these to find packet boundaries)
#define TRACER_MAGIC_0      0xAB
#define TRACER_MAGIC_1      0xCD

// Maximum task name length — must match configMAX_TASK_NAME_LEN in FreeRTOSConfig.h
#define TRACER_NAME_LEN     16

typedef enum __attribute__((packed)) {
    TRACER_EVT_SWITCHED_IN  = 0x01,  // task started executing
    TRACER_EVT_SWITCHED_OUT = 0x02,  // task stopped executing (preempted or blocked)
} tracer_evt_t;

// Wire format — 34 bytes total, all fields little-endian
typedef struct __attribute__((packed)) {
    uint8_t      magic[2];           // {0xAB, 0xCD}
    uint8_t      type;               // tracer_evt_t
    uint8_t      core_id;            // 0 or 1 (ESP32-P4 is dual-core)
    uint8_t      priority;           // FreeRTOS task priority
    uint32_t     task_id;            // TCB pointer cast to uint32_t — unique per task instance
    char         name[TRACER_NAME_LEN]; // null-terminated task name
    uint64_t     timestamp_us;       // microseconds since boot (esp_timer_get_time)
    uint8_t      checksum;           // XOR of all preceding bytes
} tracer_packet_t;

// Call once before starting the FreeRTOS scheduler.
// Configures the SEGGER RTT up-buffer on channel 1.
// probe-rs polls this buffer directly over JTAG — no sender task needed.
void tracer_init(void);

// Called automatically by FreeRTOS trace hooks — also usable manually.
// ISR-safe (uses a spinlock + ring buffer; never blocks).
void tracer_record(tracer_evt_t type, const char *name,
                   uint8_t core_id, uint8_t priority);

#ifdef __cplusplus
}
#endif
