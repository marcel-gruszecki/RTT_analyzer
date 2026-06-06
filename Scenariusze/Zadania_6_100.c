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

        // Rdzeń 1: Krótkie zadania (wczesne ukończenie fazy mono i potężna bezczynność/luka systemowa)
        execute_mono_task("fx2", 1000);   execute_mono_task("fx4", 1000);   execute_mono_task("fx6", 1000);
        execute_mono_task("fx8", 1000);   execute_mono_task("fx10", 1000);  execute_mono_task("fx12", 1000);
        execute_mono_task("fx14", 1000);  execute_mono_task("fx16", 1000);  execute_mono_task("fx18", 1000);
        execute_mono_task("fx20", 1000);  execute_mono_task("fx22", 1000);  execute_mono_task("fx24", 1000);
        execute_mono_task("fx26", 1000);  execute_mono_task("fx28", 1000);  execute_mono_task("fx30", 1000);

        // Drobne czyste mono dla unikalności nazw (fx45 do fx68 -> 24 zadania po 500 us = 12 ms)
        execute_mono_task("fx45", 500);   execute_mono_task("fx46", 500);   execute_mono_task("fx47", 500);
        execute_mono_task("fx48", 500);   execute_mono_task("fx49", 500);   execute_mono_task("fx50", 500);
        execute_mono_task("fx51", 500);   execute_mono_task("fx52", 500);   execute_mono_task("fx53", 500);
        execute_mono_task("fx54", 500);   execute_mono_task("fx55", 500);   execute_mono_task("fx56", 500);
        execute_mono_task("fx57", 500);   execute_mono_task("fx58", 500);   execute_mono_task("fx59", 500);
        execute_mono_task("fx60", 500);   execute_mono_task("fx61", 500);   execute_mono_task("fx62", 500);
        execute_mono_task("fx63", 500);   execute_mono_task("fx64", 500);   execute_mono_task("fx65", 500);
        execute_mono_task("fx66", 500);   execute_mono_task("fx67", 500);   execute_mono_task("fx68", 500);
        // Rdzeń 1 kończy pracę po ok. 27 ms i bezczynnie czeka na zakończenie wielkiego profilu monolitycznego Core 0

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 6, n=100) ───────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 6 (n = 100): JEDEN RDZEŃ SEKWENCJA CORAZ DŁUŻSZYCH ZADAŃ ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1);

        // Rdzeń 0: Drastycznie narastająca sekwencja monolitycznych zadań (15 zadań)
        execute_mono_task("fx1", 1000);   execute_mono_task("fx3", 2000);   execute_mono_task("fx5", 3000);
        execute_mono_task("fx7", 4000);   execute_mono_task("fx9", 5000);   execute_mono_task("fx11", 6000);
        execute_mono_task("fx13", 7000);  execute_mono_task("fx15", 8000);  execute_mono_task("fx17", 9000);
        execute_mono_task("fx19", 10000); execute_mono_task("fx21", 11000); execute_mono_task("fx23", 12000);
        execute_mono_task("fx25", 13000); execute_mono_task("fx27", 14000); execute_mono_task("fx29", 15000);

        // Dopełniające czyste mono dla unikalności nazw (fx31 do fx44 -> 14 zadań z ciągłym wzrostem skali)
        execute_mono_task("fx31", 16000); execute_mono_task("fx32", 17000); execute_mono_task("fx33", 18000);
        execute_mono_task("fx34", 19000); execute_mono_task("fx35", 20000); execute_mono_task("fx36", 21000);
        execute_mono_task("fx37", 22000); execute_mono_task("fx38", 23000); execute_mono_task("fx39", 24000);
        execute_mono_task("fx40", 25000); execute_mono_task("fx41", 26000); execute_mono_task("fx42", 27000);
        execute_mono_task("fx43", 28000); execute_mono_task("fx44", 30000); // Gigantyczny monolit końcowy

        // Oczekiwanie na wyrównanie barierowe (Rdzeń 1 dawno ukończył swoje zadania i bezużytecznie czekał)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. FAZA DUAL PARZYSTE (30 par dla istniejących zadań mono fx1 do fx30)
        execute_dual_task("fx1_0_d", "fx1_1_d", 2000);   execute_dual_task("fx2_0_d", "fx2_1_d", 2000);
        execute_dual_task("fx3_0_d", "fx3_1_d", 2000);   execute_dual_task("fx4_0_d", "fx4_1_d", 2000);
        execute_dual_task("fx5_0_d", "fx5_1_d", 2000);   execute_dual_task("fx6_0_d", "fx6_1_d", 2000);
        execute_dual_task("fx7_0_d", "fx7_1_d", 2000);   execute_dual_task("fx8_0_d", "fx8_1_d", 2000);
        execute_dual_task("fx9_0_d", "fx9_1_d", 2000);   execute_dual_task("fx10_0_d", "fx10_1_d", 2000);
        execute_dual_task("fx11_0_d", "fx11_1_d", 2000); execute_dual_task("fx12_0_d", "fx12_1_d", 2000);
        execute_dual_task("fx13_0_d", "fx13_1_d", 2000); execute_dual_task("fx14_0_d", "fx14_1_d", 2000);
        execute_dual_task("fx15_0_d", "fx15_1_d", 2000); execute_dual_task("fx16_0_d", "fx16_1_d", 2000);
        execute_dual_task("fx17_0_d", "fx17_1_d", 2000); execute_dual_task("fx18_0_d", "fx18_1_d", 2000);
        execute_dual_task("fx19_0_d", "fx19_1_d", 2000); execute_dual_task("fx20_0_d", "fx20_1_d", 2000);
        execute_dual_task("fx21_0_d", "fx21_1_d", 2000); execute_dual_task("fx22_0_d", "fx22_1_d", 2000);
        execute_dual_task("fx23_0_d", "fx23_1_d", 2000); execute_dual_task("fx24_0_d", "fx24_1_d", 2000);
        execute_dual_task("fx25_0_d", "fx25_1_d", 2000); execute_dual_task("fx26_0_d", "fx26_1_d", 2000);
        execute_dual_task("fx27_0_d", "fx27_1_d", 2000); execute_dual_task("fx28_0_d", "fx28_1_d", 2000);
        execute_dual_task("fx29_0_d", "fx29_1_d", 2000); execute_dual_task("fx30_0_d", "fx30_1_d", 2000);

        // 3. FAZA DUAL TŁO (2 czyste zadania dualne bez odpowiednika mono)
        execute_dual_task("t1_0_d", "t1_1_d", 4000);
        execute_dual_task("t2_0_d", "t2_1_d", 4000);

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