/**
 * @file scenario_3_100.c
 * @brief Scenariusz Testowy 3 - Odwrócona Asymetria / Sortowanie Rosnące (n = 100 zadań)
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Implementuje scenariusz testowy generujący 100 unikalnych zadań w modelu Spdp-Any.
 * Faza jednordzeniowa (Mono) rozdzielona jest na oba rdzenie i w pełni posortowana rosnąco
 * według czasu trwania (fx1-fx20 na Rdzeniu 0 od 1 do 20 ms oraz fx21-fx40 na Rdzeniu 1
 * od 21 do 40 ms). Faza równoległa (Dual) realizuje wykonanie 40 par wariantu elastycznego
 * oraz 20 zadań barierowych tła, zachowując ścisłe sortowanie rosnące (SJF - Shortest Job
 * First) względem ich całkowitego czasu barierowego.
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

        /* Zadania mono na rdzeniu 1 sformatowane w porządku rosnącym (od 21 ms do 40 ms) */
        execute_mono_task("fx21", 21000); execute_mono_task("fx22", 22000);
        execute_mono_task("fx23", 23000); execute_mono_task("fx24", 24000);
        execute_mono_task("fx25", 25000); execute_mono_task("fx26", 26000);
        execute_mono_task("fx27", 27000); execute_mono_task("fx28", 28000);
        execute_mono_task("fx29", 29000); execute_mono_task("fx30", 30000);
        execute_mono_task("fx31", 31000); execute_mono_task("fx32", 32000);
        execute_mono_task("fx33", 33000); execute_mono_task("fx34", 34000);
        execute_mono_task("fx35", 35000); execute_mono_task("fx36", 36000);
        execute_mono_task("fx37", 37000); execute_mono_task("fx38", 38000);
        execute_mono_task("fx39", 39000); execute_mono_task("fx40", 40000);

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 3, n=100) ──────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        /* Inicjalizacja fazy jednordzeniowej na rdzeniu 1 */
        xTaskNotifyGive(h_core1);

        /* Zadania mono na rdzeniu 0 sformatowane w porządku rosnącym (od 1 ms do 20 ms) */
        execute_mono_task("fx1", 1000);   execute_mono_task("fx2", 2000);
        execute_mono_task("fx3", 3000);   execute_mono_task("fx4", 4000);
        execute_mono_task("fx5", 5000);   execute_mono_task("fx6", 6000);
        execute_mono_task("fx7", 7000);   execute_mono_task("fx8", 8000);
        execute_mono_task("fx9", 9000);   execute_mono_task("fx10", 10000);
        execute_mono_task("fx11", 11000); execute_mono_task("fx12", 12000);
        execute_mono_task("fx13", 13000); execute_mono_task("fx14", 14000);
        execute_mono_task("fx15", 15000); execute_mono_task("fx16", 16000);
        execute_mono_task("fx17", 17000); execute_mono_task("fx18", 18000);
        execute_mono_task("fx19", 19000); execute_mono_task("fx20", 20000);

        /* Oczekiwanie na wyrównanie barierowe z rdzeniem 1 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Faza zadań dualnych posortowana rosnąco według czasu barierowego (SJF) */
        execute_dual_task("fx1_0_d", "fx1_1_d", 2000);     execute_dual_task("fx2_0_d", "fx2_1_d", 4000);
        execute_dual_task("fx3_0_d", "fx3_1_d", 6000);     execute_dual_task("fx4_0_d", "fx4_1_d", 8000);
        execute_dual_task("fx5_0_d", "fx5_1_d", 10000);    execute_dual_task("fx6_0_d", "fx6_1_d", 12000);
        execute_dual_task("fx7_0_d", "fx7_1_d", 14000);    execute_dual_task("fx8_0_d", "fx8_1_d", 16000);
        execute_dual_task("fx9_0_d", "fx9_1_d", 18000);    execute_dual_task("fx10_0_d", "fx10_1_d", 20000);
        execute_dual_task("fx11_0_d", "fx11_1_d", 22000);  execute_dual_task("fx12_0_d", "fx12_1_d", 24000);
        execute_dual_task("fx13_0_d", "fx13_1_d", 26000);  execute_dual_task("fx14_0_d", "fx14_1_d", 28000);
        execute_dual_task("fx15_0_d", "fx15_1_d", 30000);  execute_dual_task("fx16_0_d", "fx16_1_d", 32000);
        execute_dual_task("fx17_0_d", "fx17_1_d", 34000);  execute_dual_task("fx18_0_d", "fx18_1_d", 36000);
        execute_dual_task("fx19_0_d", "fx19_1_d", 38000);  execute_dual_task("fx20_0_d", "fx20_1_d", 40000);
        execute_dual_task("fx21_0_d", "fx21_1_d", 42000);  execute_dual_task("fx22_0_d", "fx22_1_d", 44000);
        execute_dual_task("fx23_0_d", "fx23_1_d", 46000);  execute_dual_task("fx24_0_d", "fx24_1_d", 48000);
        execute_dual_task("fx25_0_d", "fx25_1_d", 50000);  execute_dual_task("fx26_0_d", "fx26_1_d", 52000);
        execute_dual_task("fx27_0_d", "fx27_1_d", 54000);  execute_dual_task("fx28_0_d", "fx28_1_d", 56000);
        execute_dual_task("fx29_0_d", "fx29_1_d", 58000);  execute_dual_task("fx30_0_d", "fx30_1_d", 60000);
        execute_dual_task("fx31_0_d", "fx31_1_d", 62000);  execute_dual_task("fx32_0_d", "fx32_1_d", 64000);
        execute_dual_task("fx33_0_d", "fx33_1_d", 66000);  execute_dual_task("fx34_0_d", "fx34_1_d", 68000);
        execute_dual_task("fx35_0_d", "fx35_1_d", 70000);  execute_dual_task("fx36_0_d", "fx36_1_d", 72000);
        execute_dual_task("fx37_0_d", "fx37_1_d", 74000);  execute_dual_task("fx38_0_d", "fx38_1_d", 76000);
        execute_dual_task("fx39_0_d", "fx39_1_d", 78000);  execute_dual_task("fx40_0_d", "fx40_1_d", 80000);

        /* Faza zadań dualnych tła, umieszczona na końcu według kryterium SJF */
        execute_dual_task("t1_0_d", "t1_1_d", 84000);     execute_dual_task("t2_0_d", "t2_1_d", 88000);
        execute_dual_task("t3_0_d", "t3_1_d", 92000);     execute_dual_task("t4_0_d", "t4_1_d", 96000);
        execute_dual_task("t5_0_d", "t5_1_d", 100000);    execute_dual_task("t6_0_d", "t6_1_d", 104000);
        execute_dual_task("t7_0_d", "t7_1_d", 108000);    execute_dual_task("t8_0_d", "t8_1_d", 112000);
        execute_dual_task("t9_0_d", "t9_1_d", 116000);    execute_dual_task("t10_0_d", "t10_1_d", 120000);
        execute_dual_task("t11_0_d", "t11_1_d", 124000);  execute_dual_task("t12_0_d", "t12_1_d", 128000);
        execute_dual_task("t13_0_d", "t13_1_d", 132000);  execute_dual_task("t14_0_d", "t14_1_d", 134000);
        execute_dual_task("t15_0_d", "t15_1_d", 136000);  execute_dual_task("t16_0_d", "t16_1_d", 138000);
        execute_dual_task("t17_0_d", "t17_1_d", 140000);  execute_dual_task("t18_0_d", "t18_1_d", 142000);
        execute_dual_task("t19_0_d", "t19_1_d", 144000);  execute_dual_task("t20_0_d", "t20_1_d", 146000);

        /* Reset pętli i wyzwolenie kolejnego cyklu */
        vTaskDelay(pdMS_TO_TICKS(10));
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