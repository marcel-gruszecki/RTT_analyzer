/**
 * @file scenario_2_20.c
 * @brief Scenariusz Testowy 2 - Elastyczna Asymetria (n = 20 zadań)
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Implementuje scenariusz testowy generujący 20 unikalnych zadań w modelu Spdp-Any.
 * Wprowadza kontrolowaną asymetrię obciążeń – Rdzeń 0 wykonuje cztery długie zadania mono
 * (po 20 ms), podczas gdy Rdzeń 1 oczekuje bezczynnie na synchronizację. W dalszej fazie
 * te same cztery zadania są powtarzane w wariantach dualnych wraz z dwunastoma zadaniami tła,
 * co pozwala analizatorowi na bezpośrednie porównanie efektywności struktur mono vs dual.
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
static SemaphoreHandle_t xDualEndSem   = NULL;
static volatile uint32_t    dual_workload_iters = 0;
static const char* volatile dual_task_name_c1   = NULL;

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
    tracer_record(TRACER_EVT_SWITCHED_IN,  name, core, prio);
    do_work(US_TO_ITERS(duration_us));
    tracer_record(TRACER_EVT_SWITCHED_OUT, name, core, prio);
}

/* Synchronizuje oba rdzenie w celu wykonania i rejestracji zadania dwurdzeniowego (Dual) */
static void execute_dual_task(const char* name_c0, const char* name_c1,
                               uint32_t total_duration_us) {
    uint8_t prio = uxTaskPriorityGet(NULL);
    dual_workload_iters = US_TO_ITERS(total_duration_us) / 2;
    dual_task_name_c1   = name_c1;
    xSemaphoreGive(xDualStartSem);
    tracer_record(TRACER_EVT_SWITCHED_IN,  name_c0, 0, prio);
    do_work(dual_workload_iters);
    xSemaphoreTake(xDualEndSem, portMAX_DELAY);
    tracer_record(TRACER_EVT_SWITCHED_OUT, name_c0, 0, prio);
}

/* ── RDZEŃ 1: WĄTEK POMOCNICZY DLA ZADAŃ DUALNYCH ──────────────────────── */
static void vDualWorkerTaskCore1(void *arg) {
    uint8_t prio = uxTaskPriorityGet(NULL);
    while (1) {
        if (xSemaphoreTake(xDualStartSem, portMAX_DELAY) == pdTRUE) {
            tracer_record(TRACER_EVT_SWITCHED_IN,  dual_task_name_c1, 1, prio);
            do_work(dual_workload_iters);
            tracer_record(TRACER_EVT_SWITCHED_OUT, dual_task_name_c1, 1, prio);
            xSemaphoreGive(xDualEndSem);
        }
    }
}

/* ── RDZEŃ 0: NIESKOŃCZONA PĘTLA GŁÓWNA (Asymetria, n=20) ──────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        /* Faza zadań mono na rdzeniu 0 (baza asymetrii obciążenia) - łącznie 80 ms */
        xTaskNotifyGive(h_core1);

        execute_mono_task("fx1", 20000);
        execute_mono_task("fx2", 20000);
        execute_mono_task("fx3", 20000);
        execute_mono_task("fx4", 20000);

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Faza zadań dualnych - powtórzenie zadań wariantu elastycznego */
        execute_dual_task("fx1_0_d", "fx1_1_d", 4000);
        execute_dual_task("fx2_0_d", "fx2_1_d", 4000);
        execute_dual_task("fx3_0_d", "fx3_1_d", 4000);
        execute_dual_task("fx4_0_d", "fx4_1_d", 4000);

        /* Faza zadań dualnych - wykonanie dodatkowych zadań tła */
        execute_dual_task("t1_0_d",  "t1_1_d",  4000);
        execute_dual_task("t2_0_d",  "t2_1_d",  4000);
        execute_dual_task("t3_0_d",  "t3_1_d",  4000);
        execute_dual_task("t4_0_d",  "t4_1_d",  4000);
        execute_dual_task("t5_0_d",  "t5_1_d",  4000);
        execute_dual_task("t6_0_d",  "t6_1_d",  4000);
        execute_dual_task("t7_0_d",  "t7_1_d",  4000);
        execute_dual_task("t8_0_d",  "t8_1_d",  4000);
        execute_dual_task("t9_0_d",  "t9_1_d",  4000);
        execute_dual_task("t10_0_d", "t10_1_d", 4000);
        execute_dual_task("t11_0_d", "t11_1_d", 4000);
        execute_dual_task("t12_0_d", "t12_1_d", 4000);

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
    xDualEndSem   = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(vDualWorkerTaskCore1, "dual_worker", 2048, NULL, 12, NULL,     1);
    xTaskCreatePinnedToCore(task_runner_core0,    "runner_c0",   4096, NULL,  5, &h_core0, 0);
    xTaskCreatePinnedToCore(task_runner_core1,    "runner_c1",   4096, NULL,  5, &h_core1, 1);
}