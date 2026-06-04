/**
 * @file scenario_2_100.c
 * @brief Scenariusz Testowy 2 - Elastyczna Asymetria (n = 100 zadań)
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Implementuje scenariusz testowy generujący 100 unikalnych zadań w modelu Spdp-Any.
 * Scenariusz wprowadza kontrolowaną asymetrię obciążeń – Rdzeń 0 wykonuje sekwencję 20
 * unikalnych zadań jednordzeniowych (grupa fx1-fx20 o długości 5 ms każde), podczas gdy
 * Rdzeń 1 pozostaje w stanie oczekiwania. W kolejnych fazach te same zadania są powtarzane
 * w wariantach dualnych, a dopełnienie pełnej puli 100 zadań stanowi 60 dedykowanych
 * zadań barierowych stanowiących tło systemowe.
 */

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

/* Pętla opóźniająca symulująca syntetyczne obciążenie procesora */
static void do_work(volatile long long iters) {
    volatile long long acc = 0;
    for (long long i = 0; i < iters; i++) acc += i;
    (void)acc;
}

/* Generuje oraz rejestruje wykonanie zadania jednordzeniowego (Mono) */
static void execute_mono_task(const char* name, uint32_t duration_us) {
    uint8_t core = esp_cpu_get_core_id();
    uint8_t prio = uxTaskPriorityGet(NULL);
    tracer_record(TRACER_EVT_SWITCHED_IN, name, core, prio);
    do_work(US_TO_ITERS(duration_us));
    tracer_record(TRACER_EVT_SWITCHED_OUT, name, core, prio);
}

/* Synchronizuje oba rdzenie w celu wykonania i rejestracji zadania dwurdzeniowego (Dual) */
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

/* ── RDZEŃ 0: NIESKOŃCZONA PĘTLA GŁÓWNA (Asymetria, n=100) ─────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        xTaskNotifyGive(h_core1);

        /* Faza zadań mono na rdzeniu 0 (baza asymetrii obciążenia) - 20 zadań, łącznie 100 ms */
        execute_mono_task("fx1", 5000);  execute_mono_task("fx2", 5000);  execute_mono_task("fx3", 5000);  execute_mono_task("fx4", 5000);
        execute_mono_task("fx5", 5000);  execute_mono_task("fx6", 5000);  execute_mono_task("fx7", 5000);  execute_mono_task("fx8", 5000);
        execute_mono_task("fx9", 5000);  execute_mono_task("fx10", 5000); execute_mono_task("fx11", 5000); execute_mono_task("fx12", 5000);
        execute_mono_task("fx13", 5000); execute_mono_task("fx14", 5000); execute_mono_task("fx15", 5000); execute_mono_task("fx16", 5000);
        execute_mono_task("fx17", 5000); execute_mono_task("fx18", 5000); execute_mono_task("fx19", 5000); execute_mono_task("fx20", 5000);

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Faza zadań dualnych - powtórzenie zadań wariantu elastycznego (fx1 - fx20) */
        execute_dual_task("fx1_0_d", "fx1_1_d", 1000);   execute_dual_task("fx2_0_d", "fx2_1_d", 1000);
        execute_dual_task("fx3_0_d", "fx3_1_d", 1000);   execute_dual_task("fx4_0_d", "fx4_1_d", 1000);
        execute_dual_task("fx5_0_d", "fx5_1_d", 1000);   execute_dual_task("fx6_0_d", "fx6_1_d", 1000);
        execute_dual_task("fx7_0_d", "fx7_1_d", 1000);   execute_dual_task("fx8_0_d", "fx8_1_d", 1000);
        execute_dual_task("fx9_0_d", "fx9_1_d", 1000);   execute_dual_task("fx10_0_d", "fx10_1_d", 1000);
        execute_dual_task("fx11_0_d", "fx11_1_d", 1000); execute_dual_task("fx12_0_d", "fx12_1_d", 1000);
        execute_dual_task("fx13_0_d", "fx13_1_d", 1000); execute_dual_task("fx14_0_d", "fx14_1_d", 1000);
        execute_dual_task("fx15_0_d", "fx15_1_d", 1000); execute_dual_task("fx16_0_d", "fx16_1_d", 1000);
        execute_dual_task("fx17_0_d", "fx17_1_d", 1000); execute_dual_task("fx18_0_d", "fx18_1_d", 1000);
        execute_dual_task("fx19_0_d", "fx19_1_d", 1000); execute_dual_task("fx20_0_d", "fx20_1_d", 1000);

        /* Faza zadań dualnych - wykonanie dedykowanego tła bez odpowiedników mono (t1 - t60) */
        execute_dual_task("t1_0_d", "t1_1_d", 1000);   execute_dual_task("t2_0_d", "t2_1_d", 1000);
        execute_dual_task("t3_0_d", "t3_1_d", 1000);   execute_dual_task("t4_0_d", "t4_1_d", 1000);
        execute_dual_task("t5_0_d", "t5_1_d", 1000);   execute_dual_task("t6_0_d", "t6_1_d", 1000);
        execute_dual_task("t7_0_d", "t7_1_d", 1000);   execute_dual_task("t8_0_d", "t8_1_d", 1000);
        execute_dual_task("t9_0_d", "t9_1_d", 1000);   execute_dual_task("t10_0_d", "t10_1_d", 1000);
        execute_dual_task("t11_0_d", "t11_1_d", 1000); execute_dual_task("t12_0_d", "t12_1_d", 1000);
        execute_dual_task("t13_0_d", "t13_1_d", 1000); execute_dual_task("t14_0_d", "t14_1_d", 1000);
        execute_dual_task("t15_0_d", "t15_1_d", 1000); execute_dual_task("t16_0_d", "t16_1_d", 1000);
        execute_dual_task("t17_0_d", "t17_1_d", 1000); execute_dual_task("t18_0_d", "t18_1_d", 1000);
        execute_dual_task("t19_0_d", "t19_1_d", 1000); execute_dual_task("t20_0_d", "t20_1_d", 1000);
        execute_dual_task("t21_0_d", "t21_1_d", 1000); execute_dual_task("t22_0_d", "t22_1_d", 1000);
        execute_dual_task("t23_0_d", "t23_1_d", 1000); execute_dual_task("t24_0_d", "t24_1_d", 1000);
        execute_dual_task("t25_0_d", "t25_1_d", 1000); execute_dual_task("t26_0_d", "t26_1_d", 1000);
        execute_dual_task("t27_0_d", "t27_1_d", 1000); execute_dual_task("t28_0_d", "t28_1_d", 1000);
        execute_dual_task("t29_0_d", "t29_1_d", 1000); execute_dual_task("t30_0_d", "t30_1_d", 1000);
        execute_dual_task("t31_0_d", "t31_1_d", 1000); execute_dual_task("t32_0_d", "t32_1_d", 1000);
        execute_dual_task("t33_0_d", "t33_1_d", 1000); execute_dual_task("t34_0_d", "t34_1_d", 1000);
        execute_dual_task("t35_0_d", "t35_1_d", 1000); execute_dual_task("t36_0_d", "t36_1_d", 1000);
        execute_dual_task("t37_0_d", "t37_1_d", 1000); execute_dual_task("t38_0_d", "t38_1_d", 1000);
        execute_dual_task("t39_0_d", "t39_1_d", 1000); execute_dual_task("t40_0_d", "t40_1_d", 1000);
        execute_dual_task("t41_0_d", "t41_1_d", 1000); execute_dual_task("t42_0_d", "t42_1_d", 1000);
        execute_dual_task("t43_0_d", "t43_1_d", 1000); execute_dual_task("t44_0_d", "t44_1_d", 1000);
        execute_dual_task("t45_0_d", "t45_1_d", 1000); execute_dual_task("t46_0_d", "t46_1_d", 1000);
        execute_dual_task("t47_0_d", "t47_1_d", 1000); execute_dual_task("t48_0_d", "t48_1_d", 1000);
        execute_dual_task("t49_0_d", "t49_1_d", 1000); execute_dual_task("t50_0_d", "t50_1_d", 1000);
        execute_dual_task("t51_0_d", "t51_1_d", 1000); execute_dual_task("t52_0_d", "t52_1_d", 1000);
        execute_dual_task("t53_0_d", "t53_1_d", 1000); execute_dual_task("t54_0_d", "t54_1_d", 1000);
        execute_dual_task("t55_0_d", "t55_1_d", 1000); execute_dual_task("t56_0_d", "t56_1_d", 1000);
        execute_dual_task("t57_0_d", "t57_1_d", 1000); execute_dual_task("t58_0_d", "t58_1_d", 1000);
        execute_dual_task("t59_0_d", "t59_1_d", 1000); execute_dual_task("t60_0_d", "t60_1_d", 1000);

        /* Reset pętli i wyzwolenie kolejnego cyklu */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ── RDZEŃ 1: WĄTEK SYNCHRONIZACYJNY FAZY MONO ───────────────────────── */
static void task_runner_core1(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xTaskNotifyGive(h_core0);
    }
}

/* ── INICJALIZACJA SYSTEMU ────────────────────────────────────────────── */
void app_main(void) {
    tracer_init();
    xDualStartSem = xSemaphoreCreateBinary();
    xDualEndSem = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(vDualWorkerTaskCore1, "dual_worker", 2048, NULL, 12, NULL, 1);
    xTaskCreatePinnedToCore(task_runner_core0, "runner_c0", 4096, NULL, 5, &h_core0, 0);
    xTaskCreatePinnedToCore(task_runner_core1, "runner_c1", 4096, NULL, 5, &h_core1, 1);
}