#include "task_tracer.h"

#include "esp_timer.h"
#include "esp_cpu.h"
#include "SEGGER_RTT.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

// ── Configuration ────────────────────────────────────────────────────────────

// RTT channel 1 is reserved for trace packets (channel 0 is typically the
// default console). Buffer sized for 64 packets; RTT itself handles flow
// control — no additional ring buffer needed.
#define TRACER_RTT_CHANNEL  1
#define TRACER_RTT_BUF_SIZE (512 * sizeof(tracer_packet_t))

static uint8_t rtt_up_buf[TRACER_RTT_BUF_SIZE];

// ── Internal helpers ─────────────────────────────────────────────────────────

static uint8_t xor_checksum(const uint8_t *data, size_t len)
{
    uint8_t chk = 0;
    for (size_t i = 0; i < len; i++) chk ^= data[i];
    return chk;
}

// ── Public API ───────────────────────────────────────────────────────────────

// ISR-safe: SEGGER_RTT_WriteNoLock is not interrupt-safe, so we use the
// spinlock variant. Calls from within FreeRTOS trace hooks are fine.
void tracer_record(tracer_evt_t type, const char *name,
                   uint8_t core_id, uint8_t priority)
{
    tracer_packet_t pkt;

    pkt.magic[0]     = TRACER_MAGIC_0;
    pkt.magic[1]     = TRACER_MAGIC_1;
    pkt.type         = (uint8_t)type;
    pkt.core_id      = core_id;
    pkt.priority     = priority;
    // TCB pointer is unique per task instance — distinguishes two tasks with
    // the same name running on different cores (or the same core).
    pkt.task_id      = (uint32_t)(uintptr_t)xTaskGetCurrentTaskHandle();
    pkt.timestamp_us = (uint64_t)esp_timer_get_time();
    strncpy(pkt.name, name, TRACER_NAME_LEN - 1);
    pkt.name[TRACER_NAME_LEN - 1] = '\0';
    pkt.checksum = xor_checksum((uint8_t *)&pkt, sizeof(pkt) - 1);

    // SEGGER_RTT_Write is thread/ISR-safe (uses a spinlock internally).
    // Returns 0 if the buffer is full — packet is silently dropped.
    SEGGER_RTT_Write(TRACER_RTT_CHANNEL, &pkt, sizeof(pkt));
}

// ── Initialisation ───────────────────────────────────────────────────────────

void tracer_init(void)
{
    SEGGER_RTT_ConfigUpBuffer(TRACER_RTT_CHANNEL, "tracer",
                              rtt_up_buf, sizeof(rtt_up_buf),
                              SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    // No sender task needed — RTT is polled directly by probe-rs over JTAG.
}
