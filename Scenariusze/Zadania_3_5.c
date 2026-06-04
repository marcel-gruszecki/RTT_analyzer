/**
 * @file scenario_3_5.c
 * @brief Scenariusz Testowy 3 - Odwrócona Asymetria (n = 5 zadań)
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Implementuje scenariusz testowy generujący 5 unikalnych zadań w modelu Spdp-Any.
 * Scenariusz odwraca asymetrię obciążeń względem scenariusza 2 – faza jednordzeniowa (Mono)
 * jest w całości wykonywana przez Rdzeń 1 (dwa zadania fx1 i fx2 po 20 ms), podczas gdy
 * główny zarządca na Rdzeniu 0 oczekuje zablokowany na powiadomienie. W dalszej fazie
 * zadania te są powtarzane jako dualne wraz z jednym zadaniem tła (fx3).
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

/* ── RDZEŃ 1: WYKONAWCA FAZY MONO DLA CORE 1 ─────────────────────────── */
static void task_runner_core1(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Wykonanie zadań Mono na rdzeniu 1 - łącznie 40 ms */
        execute_mono_task("fx1", 20000);
        execute_mono_task("fx2", 20000);

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Odwrócona Asymetria, n=5) ───────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        /* Uruchomienie fazy jednordzeniowej na rdzeniu 1 */
        xTaskNotifyGive(h_core1);

        /* Oczekiwanie na ukończenie zadań mono przez rdzeń 1 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Faza zadań dualnych - powtórzenie zadań wariantu elastycznego */
        execute_dual_task("fx1_0_d", "fx1_1_d", 18000);
        execute_dual_task("fx2_0_d", "fx2_1_d", 18000);

        /* Faza zadań dualnych - wykonanie dodatkowego zadania tła */
        execute_dual_task("fx3_0_d", "fx3_1_d", 10000);

        /* Odpoczynek pętli i reset cyklu */
        vTaskDelay(pdMS_TO_TICKS(25));
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