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

        // Zadania Mono na Core 1 (Ekstremalna zmienność długości i przeplatanie)
        execute_mono_task("fx1", 1000);   // Bardzo krótkie
        execute_mono_task("fx2", 20000);  // Bardzo długie
        execute_mono_task("fx3", 2000);   // Krótkie
        execute_mono_task("fx4", 15000);  // Długie
        execute_mono_task("fx5", 1000);   // Bardzo krótkie

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 4, n=20) ───────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 4 (n = 20): WYSOKA ZMIENNOŚĆ DŁUGOŚCI ZADAŃ I WYKONYWANIE ICH NA ZMIANĘ ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1);

        // Zadania Mono na Core 0 (Przeplatanie czasów - unikalne zadania)
        execute_mono_task("fx6", 18000);  // Długie (blokujące)
        execute_mono_task("fx7", 1000);   // Bardzo krótkie
        execute_mono_task("fx8", 10000);  // Średnie
        execute_mono_task("fx9", 2000);   // Krótkie
        execute_mono_task("fx10", 12000); // Średnio-długie
        execute_mono_task("fx11", 1000);  // Bardzo krótkie
        execute_mono_task("fx12", 8000);  // Średnie

        // Oczekiwanie na wyrównanie barierowe (Core 1 kończy fx1-fx5)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. FAZA DUAL PARZYSTE (5 par dla zadań mono, wysoka zmienność czasów)
        execute_dual_task("fx1_0_d", "fx1_1_d", 16000);  // Długie (barierowo 8 ms)
        execute_dual_task("fx2_0_d", "fx2_1_d", 2000);   // Bardzo krótkie (barierowo 1 ms)
        execute_dual_task("fx3_0_d", "fx3_1_d", 12000);  // Średnie (barierowo 6 ms)
        execute_dual_task("fx4_0_d", "fx4_1_d", 2000);   // Bardzo krótkie (barierowo 1 ms)
        execute_dual_task("fx5_0_d", "fx5_1_d", 14000);  // Średnio-długie (barierowo 7 ms)

        // 3. FAZA DUAL TŁO (3 czyste zadania bez odpowiednika mono, wysoka zmienność)
        execute_dual_task("t1_0_d", "t1_1_d", 2000);     // Bardzo krótkie (barierowo 1 ms)
        execute_dual_task("t2_0_d", "t2_1_d", 18000);    // Długie (barierowo 9 ms)
        execute_dual_task("t3_0_d", "t3_1_d", 4000);     // Krótkie (barierowo 2 ms)

        vTaskDelay(pdMS_TO_TICKS(20));
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