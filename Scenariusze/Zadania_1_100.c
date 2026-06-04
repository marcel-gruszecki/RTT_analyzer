/**
 * @file scenario_1_100.c
 * @brief Scenariusz Testowy 1 - Idealna Symetria (n = 100 zadań)
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Implementuje scenariusz testowy generujący 100 unikalnych zadań w modelu Spdp-Any.
 * Faza sekwencyjna składa się z 60 zadań mono (dynamicznie generowanych w pętli), trwających
 * po 3 ms (30 zadań na Rdzeń 0 oraz 30 zadań na Rdzeń 1, co daje łącznie 90 ms fazy mono).
 * Faza równoległa generuje dynamicznie 40 zadań dualnych o łącznym czasie barierowym 400 ms
 * (każde zadanie to połówki po 5 ms na rdzeń). Nazewnictwo dostosowane do parsera w Rust.
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

/* ── RDZEŃ 0: NIESKOŃCZONA PĘTLA GŁÓWNA (Symetria automatyczna, n=100) ──── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(50));
    xTaskNotifyGive(h_core1);

    char name_buf_c0[32];
    char name_buf_c1[32];

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Faza zadań mono na rdzeniu 0 - dynamicznie generowane 30 zadań, łącznie 90 ms */
        for (int i = 1; i <= 30; i++) {
            snprintf(name_buf_c0, sizeof(name_buf_c0), "m0_task_%02d", i);
            execute_mono_task(name_buf_c0, 3000);
        }

        /* Faza zadań dualnych - dynamicznie generowane 40 zadań barierowych, łącznie 400 ms */
        for (int i = 1; i <= 40; i++) {
            snprintf(name_buf_c0, sizeof(name_buf_c0), "d_task_%02d_0_d", i);
            snprintf(name_buf_c1, sizeof(name_buf_c1), "d_task_%02d_1_d", i);
            execute_dual_task(name_buf_c0, name_buf_c1, 10000);
        }

        /* Reset pętli i wyzwolenie kolejnego cyklu */
        vTaskDelay(pdMS_TO_TICKS(20));
        xTaskNotifyGive(h_core1);
        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 1: NIEZALEŻNE SYMETRYCZNE ZADANIA MONO ──────────────────────── */
static void task_runner_core1(void *arg) {
    char name_buf_c1[32];
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Faza zadań mono na rdzeniu 1 - dynamicznie generowane 30 zadań, łącznie 90 ms */
        for (int i = 1; i <= 30; i++) {
            snprintf(name_buf_c1, sizeof(name_buf_c1), "m1_task_%02d", i);
            execute_mono_task(name_buf_c1, 3000);
        }
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

    xTaskNotifyGive(h_core0);
}