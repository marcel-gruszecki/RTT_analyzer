/**
 * @file scenario_1_20.c
 * @brief Scenariusz Testowy 1 - Idealna Symetria (n = 20 zadań)
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Implementuje scenariusz testowy generujący 20 unikalnych zadań w modelu Spdp-Any.
 * Całość obciążenia stanowią wyłącznie zadania jednordzeniowe (Mono).
 * Faza sekwencyjna składa się z 20 zadań mono trwających po 10 ms, rozłożonych
 * idealnie symetrycznie pomiędzy rdzeniami (po 10 zadań na Rdzeń 0 oraz Rdzeń 1).
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

/* ── RDZEŃ 0: PĘTLA GŁÓWNA (Symetria, n=20 czystych zadań Mono) ── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(50));
    xTaskNotifyGive(h_core1);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Faza zadań mono na rdzeniu 0 - łącznie 100 ms */
        execute_mono_task("mono_c0_t1",  10000);
        execute_mono_task("mono_c0_t2",  10000);
        execute_mono_task("mono_c0_t3",  10000);
        execute_mono_task("mono_c0_t4",  10000);
        execute_mono_task("mono_c0_t5",  10000);
        execute_mono_task("mono_c0_t6",  10000);
        execute_mono_task("mono_c0_t7",  10000);
        execute_mono_task("mono_c0_t8",  10000);
        execute_mono_task("mono_c0_t9",  10000);
        execute_mono_task("mono_c0_t10", 10000);

        /* Reset pętli i wyzwolenie kolejnego cyklu */
        vTaskDelay(pdMS_TO_TICKS(20));
        xTaskNotifyGive(h_core1);
        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 1: NIEZALEŻNE SYMETRYCZNE ZADANIA MONO ──────────────────────── */
static void task_runner_core1(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Faza zadań mono na rdzeniu 1 - łącznie 100 ms */
        execute_mono_task("mono_c1_t1",  10000);
        execute_mono_task("mono_c1_t2",  10000);
        execute_mono_task("mono_c1_t3",  10000);
        execute_mono_task("mono_c1_t4",  10000);
        execute_mono_task("mono_c1_t5",  10000);
        execute_mono_task("mono_c1_t6",  10000);
        execute_mono_task("mono_c1_t7",  10000);
        execute_mono_task("mono_c1_t8",  10000);
        execute_mono_task("mono_c1_t9",  10000);
        execute_mono_task("mono_c1_t10", 10000);
    }
}

/* ── INICJALIZACJA SYSTEMU ────────────────────────────────────────────── */
void app_main(void) {
    tracer_init();

    xTaskCreatePinnedToCore(task_runner_core0, "runner_c0", 4096, NULL, 5, &h_core0, 0);
    xTaskCreatePinnedToCore(task_runner_core1, "runner_c1", 4096, NULL, 5, &h_core1, 1);

    xTaskNotifyGive(h_core0);
}