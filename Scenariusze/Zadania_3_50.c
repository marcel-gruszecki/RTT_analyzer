#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "task_tracer.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include <stdint.h>
#include <stdio.h>

#define US_TO_ITERS(us) ((us) * 40LL)

static TaskHandle_t h_core0, h_core1 = NULL;
static SemaphoreHandle_t xDualStartSem = NULL;
static SemaphoreHandle_t xDualEndSem = NULL;
static volatile uint32_t dual_workload_iters = 0;
static const char* volatile dual_task_name_c1 = NULL;

static void do_work(volatile long long iters) {
    volatile long long acc = 0;
    for (long long i = 0; i < iters; i++) acc += i;
    (void)acc;
}

static void execute_mono_task(const char* name, uint32_t duration_us) {
    uint8_t core = esp_cpu_get_core_id();
    uint8_t prio = uxTaskPriorityGet(NULL);
    tracer_record(TRACER_EVT_SWITCHED_IN, name, core, prio);
    do_work(US_TO_ITERS(duration_us));
    tracer_record(TRACER_EVT_SWITCHED_OUT, name, core, prio);
}

static void execute_dual_task(const char* name_c0, const char* name_c1, uint32_t total_duration_us) {
    uint8_t prio = uxTaskPriorityGet(NULL);
    dual_workload_iters = US_TO_ITERS(total_duration_us) / 2;
    dual_task_name_c1 = name_c1;
    xSemaphoreGive(xDualStartSem);
    tracer_record(TRACER_EVT_SWITCHED_IN, name_c0, 0, prio);
    do_work(dual_workload_iters);
    xSemaphoreTake(xDualEndSem, portMAX_DELAY);
    tracer_record(TRACER_EVT_SWITCHED_OUT, name_c0, 0, prio);
}

/* ── RDZEŃ 1: WĄTEK POMOCNICZY DLA ZADAŃ DUALNYCH ──────────────────────── */
static void vDualWorkerTaskCore1(void *arg) {
    uint8_t prio = uxTaskPriorityGet(NULL);
    while (1) {
        if (xSemaphoreTake(xDualStartSem, portMAX_DELAY) == pdTRUE) {
            tracer_record(TRACER_EVT_SWITCHED_IN, dual_task_name_c1, 1, prio);
            do_work(dual_workload_iters);
            tracer_record(TRACER_EVT_SWITCHED_OUT, dual_task_name_c1, 1, prio);
            xSemaphoreGive(xDualEndSem);
        }
    }
}

/* ── RDZEŃ 1: WYKONAWCA FAZY MONO DLA CORE 1 ─────────────────────────── */
static void task_runner_core1(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Zadania Mono na Core 1 (posortowane rosnąco: 11ms do 20ms)
        execute_mono_task("fx11", 11000);
        execute_mono_task("fx12", 12000);
        execute_mono_task("fx13", 13000);
        execute_mono_task("fx14", 14000);
        execute_mono_task("fx15", 15000);
        execute_mono_task("fx16", 16000);
        execute_mono_task("fx17", 17000);
        execute_mono_task("fx18", 18000);
        execute_mono_task("fx19", 19000);
        execute_mono_task("fx20", 20000);

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 3, n=50) ───────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 3 (n = 50): USZEREGOWANIE OD NAJKRÓTSZEGO DO NAJDŁUŻSZEGO ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1);

        // Zadania Mono na Core 0 (posortowane rosnąco: 1ms do 10ms)
        execute_mono_task("fx1", 1000);
        execute_mono_task("fx2", 2000);
        execute_mono_task("fx3", 3000);
        execute_mono_task("fx4", 4000);
        execute_mono_task("fx5", 5000);
        execute_mono_task("fx6", 6000);
        execute_mono_task("fx7", 7000);
        execute_mono_task("fx8", 8000);
        execute_mono_task("fx9", 9000);
        execute_mono_task("fx10", 10000);

        // Oczekiwanie na wyrównanie barierowe (Core 1 kończy fx11-fx20)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. FAZA DUAL PARZYSTE (20 par dla zadań mono, posortowane rosnąco według łącznego czasu)
        execute_dual_task("fx1_0_d", "fx1_1_d", 2000);    // barierowo 1 ms
        execute_dual_task("fx2_0_d", "fx2_1_d", 4000);    // barierowo 2 ms
        execute_dual_task("fx3_0_d", "fx3_1_d", 6000);    // barierowo 3 ms
        execute_dual_task("fx4_0_d", "fx4_1_d", 8000);    // barierowo 4 ms
        execute_dual_task("fx5_0_d", "fx5_1_d", 10000);   // barierowo 5 ms
        execute_dual_task("fx6_0_d", "fx6_1_d", 12000);   // barierowo 6 ms
        execute_dual_task("fx7_0_d", "fx7_1_d", 14000);   // barierowo 7 ms
        execute_dual_task("fx8_0_d", "fx8_1_d", 16000);   // barierowo 8 ms
        execute_dual_task("fx9_0_d", "fx9_1_d", 18000);   // barierowo 9 ms
        execute_dual_task("fx10_0_d", "fx10_1_d", 20000); // barierowo 10 ms
        execute_dual_task("fx11_0_d", "fx11_1_d", 22000); // barierowo 11 ms
        execute_dual_task("fx12_0_d", "fx12_1_d", 24000); // barierowo 12 ms
        execute_dual_task("fx13_0_d", "fx13_1_d", 26000); // barierowo 13 ms
        execute_dual_task("fx14_0_d", "fx14_1_d", 28000); // barierowo 14 ms
        execute_dual_task("fx15_0_d", "fx15_1_d", 30000); // barierowo 15 ms
        execute_dual_task("fx16_0_d", "fx16_1_d", 32000); // barierowo 16 ms
        execute_dual_task("fx17_0_d", "fx17_1_d", 34000); // barierowo 17 ms
        execute_dual_task("fx18_0_d", "fx18_1_d", 36000); // barierowo 18 ms
        execute_dual_task("fx19_0_d", "fx19_1_d", 38000); // barierowo 19 ms
        execute_dual_task("fx20_0_d", "fx20_1_d", 40000); // barierowo 20 ms

        // 3. FAZA DUAL TŁO (10 czystych zadań bez par mono, najdłuższe czasy na końcu pętli)
        execute_dual_task("t1_0_d", "t1_1_d", 50000);     // barierowo 25 ms
        execute_dual_task("t2_0_d", "t2_1_d", 60000);     // barierowo 30 ms
        execute_dual_task("t3_0_d", "t3_1_d", 70000);     // barierowo 35 ms
        execute_dual_task("t4_0_d", "t4_1_d", 80000);     // barierowo 40 ms
        execute_dual_task("t5_0_d", "t5_1_d", 84000);     // barierowo 42 ms
        execute_dual_task("t6_0_d", "t6_1_d", 88000);     // barierowo 44 ms
        execute_dual_task("t7_0_d", "t7_1_d", 92000);     // barierowo 46 ms
        execute_dual_task("t8_0_d", "t8_1_d", 96000);     // barierowo 48 ms
        execute_dual_task("t9_0_d", "t9_1_d", 98000);     // barierowo 49 ms
        execute_dual_task("t10_0_d", "t10_1_d", 100000);  // barierowo 50 ms

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {
    tracer_init();
    xDualStartSem = xSemaphoreCreateBinary();
    xDualEndSem = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(vDualWorkerTaskCore1, "dual_worker", 2048, NULL, 12, NULL, 1);
    xTaskCreatePinnedToCore(task_runner_core0, "runner_c0", 4096, NULL, 5, &h_core0, 0);
    xTaskCreatePinnedToCore(task_runner_core1, "runner_c1", 4096, NULL, 5, &h_core1, 1);
}