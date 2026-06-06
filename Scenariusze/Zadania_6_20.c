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

        // Rdzeń 1: Wykonuje krótkie zadania Mono i wcześnie kończy pracę
        execute_mono_task("fx2", 2000);   // 2 ms
        execute_mono_task("fx4", 4000);   // 4 ms
        execute_mono_task("fx6", 4000);   // 4 ms
        execute_mono_task("fx10", 2000);  // czyste mono dla unikalności nazw
        execute_mono_task("fx11", 2000);  // czyste mono dla unikalności nazw
        execute_mono_task("fx12", 2000);  // czyste mono dla unikalności nazw
        // Łącznie ok. 16 ms pracy, po czym następuje długa bezczynność i oczekiwanie na Core 0

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 6, n=20) ───────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 6 (n = 20): JEDEN RDZEŃ SEKWENCJA CORAZ DŁUŻSZYCH ZADAŃ ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1);

        // Rdzeń 0: Sekwencja rosnących monolitycznych zadań (łącznie aż 54 ms)
        execute_mono_task("fx1", 2000);   // Krótkie zadanie (2 ms)
        execute_mono_task("fx3", 4000);   // Średnie zadanie (4 ms)
        execute_mono_task("fx5", 12000);  // Długie zadanie (12 ms)
        execute_mono_task("fx7", 16000);  // Bardzo długie zadanie (16 ms)
        execute_mono_task("fx8", 20000);  // Ekstremalnie długie zadanie (20 ms)
        execute_mono_task("fx9", 24000);  // Maksymalny monolit (24 ms)

        // Oczekiwanie na wyrównanie barierowe (Rdzeń 1 dawno temu skończył swoje fazy i czeka)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. FAZA DUAL PARZYSTE (6 par dla istniejących zadań mono fx1 do fx6)
        execute_dual_task("fx1_0_d", "fx1_1_d", 4000);   // barierowo 2 ms
        execute_dual_task("fx2_0_d", "fx2_1_d", 4000);   // barierowo 2 ms
        execute_dual_task("fx3_0_d", "fx3_1_d", 6000);   // barierowo 3 ms
        execute_dual_task("fx4_0_d", "fx4_1_d", 8000);   // barierowo 4 ms
        execute_dual_task("fx5_0_d", "fx5_1_d", 12000);  // barierowo 6 ms
        execute_dual_task("fx6_0_d", "fx6_1_d", 16000);  // barierowo 8 ms

        // 3. FAZA DUAL TŁO (2 czyste zadania dualne bez odpowiednika mono)
        execute_dual_task("t1_0_d", "t1_1_d", 10000);    // barierowo 5 ms
        execute_dual_task("t2_0_d", "t2_1_d", 12000);    // barierowo 6 ms

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