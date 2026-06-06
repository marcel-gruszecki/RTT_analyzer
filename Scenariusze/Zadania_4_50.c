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

        // Zadania Mono na Core 1 (Ekstremalna zmienność czasów i przeplatanie wartości)
        execute_mono_task("fx1", 1000);   // Bardzo krótkie
        execute_mono_task("fx2", 30000);  // Potężny monolit
        execute_mono_task("fx3", 2000);   // Krótkie
        execute_mono_task("fx4", 25000);  // Długie
        execute_mono_task("fx5", 1000);   // Bardzo krótkie
        execute_mono_task("fx6", 20000);  // Średnio-długie
        execute_mono_task("fx7", 3000);   // Krótkie
        execute_mono_task("fx8", 18000);  // Średnie
        execute_mono_task("fx9", 1000);   // Bardzo krótkie
        execute_mono_task("fx10", 15000); // Średnie

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 4, n=50) ───────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 4 (n = 50): WYSOKA ZMIENNOŚĆ DŁUGOŚCI ZADAŃ I WYKONYWANIE ICH NA ZMIANĘ ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1);

        // Zadania Mono na Core 0 (Przeplatanie - unikalne zadania bez bezpośrednich par dualnych)
        execute_mono_task("fx11", 2000);  // Krótkie (sparowane w dualu)
        execute_mono_task("fx12", 28000); // Bardzo długie (sparowane w dualu)
        execute_mono_task("fx13", 1000);  // Bardzo krótkie (sparowane w dualu)
        execute_mono_task("fx14", 24000); // Długie (sparowane w dualu)
        execute_mono_task("fx15", 1500);  // Bardzo krótkie (sparowane w dualu)

        execute_mono_task("fx16", 1000);  // Poniższe czyste mono uzupełniają unikalność do n=50
        execute_mono_task("fx17", 22000);
        execute_mono_task("fx18", 1000);
        execute_mono_task("fx19", 19000);
        execute_mono_task("fx20", 3000);
        execute_mono_task("fx21", 17000);
        execute_mono_task("fx22", 1000);
        execute_mono_task("fx23", 14000);
        execute_mono_task("fx24", 2000);
        execute_mono_task("fx25", 12000);
        execute_mono_task("fx26", 1000);
        execute_mono_task("fx27", 10000);
        execute_mono_task("fx28", 2500);
        execute_mono_task("fx29", 8000);
        execute_mono_task("fx30", 1000);

        // Oczekiwanie na wyrównanie barierowe (Core 1 kończy fx1-fx10)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. FAZA DUAL PARZYSTE (15 par dla zadań mono fx1-fx15, wysoka zmienność czasów)
        execute_dual_task("fx1_0_d", "fx1_1_d", 24000); // Bardzo długie (barierowo 12 ms)
        execute_dual_task("fx2_0_d", "fx2_1_d", 2000);  // Bardzo krótkie (barierowo 1 ms)
        execute_dual_task("fx3_0_d", "fx3_1_d", 20000); // Długie (barierowo 10 ms)
        execute_dual_task("fx4_0_d", "fx4_1_d", 1600);  // Bardzo krótkie (barierowo 0.8 ms)
        execute_dual_task("fx5_0_d", "fx5_1_d", 16000); // Średnie (barierowo 8 ms)
        execute_dual_task("fx6_0_d", "fx6_1_d", 1600);  // Bardzo krótkie (barierowo 0.8 ms)
        execute_dual_task("fx7_0_d", "fx7_1_d", 14000); // Średnio-długie (barierowo 7 ms)
        execute_dual_task("fx8_0_d", "fx8_1_d", 4000);  // Krótkie (barierowo 2 ms)
        execute_dual_task("fx9_0_d", "fx9_1_d", 12000); // Średnie (barierowo 6 ms)
        execute_dual_task("fx10_0_d", "fx10_1_d", 2400); // Bardzo krótkie
        execute_dual_task("fx11_0_d", "fx11_1_d", 10000); // Średnie
        execute_dual_task("fx12_0_d", "fx12_1_d", 2000);  // Bardzo krótkie
        execute_dual_task("fx13_0_d", "fx13_1_d", 8000);  // Średnie
        execute_dual_task("fx14_0_d", "fx14_1_d", 1600);  // Bardzo krótkie
        execute_dual_task("fx15_0_d", "fx15_1_d", 6000);  // Krótkie

        // 3. FAZA DUAL TŁO (5 czystych zadań bez odpowiednika mono, wysoka zmienność trwania)
        execute_dual_task("t1_0_d", "t1_1_d", 2000);    // Bardzo krótkie (barierowo 1 ms)
        execute_dual_task("t2_0_d", "t2_1_d", 26000);   // Ekstremalnie długie (barierowo 13 ms)
        execute_dual_task("t3_0_d", "t3_1_d", 4000);    // Krótkie (barierowo 2 ms)
        execute_dual_task("t4_0_d", "t4_1_d", 18000);   // Długie (barierowo 9 ms)
        execute_dual_task("t5_0_d", "t5_1_d", 2000);    // Bardzo krótkie (barierowo 1 ms)

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