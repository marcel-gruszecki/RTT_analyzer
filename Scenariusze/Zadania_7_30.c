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

/* ── RDZEŃ 1: NIEZALEŻNY WĄTEK WYKONAWCZY DLA CORE 1 ─────────────────── */
static void task_runner_core1(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Krótkie zadania na Core 1 (10 zadań po 5 ms = 50 ms łącznie)
        // Generują asymetrię obciążenia i wymuszony przestój na barierze końcowej
        execute_mono_task("t21", 5000); execute_mono_task("t22", 5000);
        execute_mono_task("t23", 5000); execute_mono_task("t24", 5000);
        execute_mono_task("t25", 5000); execute_mono_task("t26", 5000);
        execute_mono_task("t27", 5000); execute_mono_task("t28", 5000);
        execute_mono_task("t29", 5000); execute_mono_task("t30", 5000);

        xTaskNotifyGive(h_core0); // Powiadom Core 0 o zakończeniu sekwencji
    }
}

/* ── RDZEŃ 0: GŁÓWNA PĘTLA ZARZĄDZAJĄCA (Scenariusz 7, n=30) ──────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(50));

    while (1) {
        // Jednoczesny start obu rdzeni na początku cyklu pętli
        xTaskNotifyGive(h_core1);

        // Przeładowany Core 0 - masa krótkich zadań (20 zadań po 5 ms = 100 ms łącznie)
        execute_mono_task("t1", 5000);  execute_mono_task("t2", 5000);  execute_mono_task("t3", 5000);
        execute_mono_task("t4", 5000);  execute_mono_task("t5", 5000);  execute_mono_task("t6", 5000);
        execute_mono_task("t7", 5000);  execute_mono_task("t8", 5000);  execute_mono_task("t9", 5000);
        execute_mono_task("t10", 5000); execute_mono_task("t11", 5000); execute_mono_task("t12", 5000);
        execute_mono_task("t13", 5000); execute_mono_task("t14", 5000); execute_mono_task("t15", 5000);
        execute_mono_task("t16", 5000); execute_mono_task("t17", 5000); execute_mono_task("t18", 5000);
        execute_mono_task("t19", 5000); execute_mono_task("t20", 5000);

        // Oczekiwanie na wyrównanie barierowe
        // Core 1 skończył po 50 ms i czeka bezczynnie przez 50 ms, aż Core 0 wykona swoje 100 ms pracy
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Bilans unikalnych zadań w logu z jednej iteracji:
        // t1...t20 na Core 0 + t21...t30 na Core 1 = Dokładnie 30 unikalnych zadań mono.
        // Czysty model jednordzeniowy, brak zadań z przyrostkiem "_d".

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ── INICJALIZACJA SYSTEMU ────────────────────────────────────────────── */
void app_main(void) {
    tracer_init();

    // Tworzymy wyłącznie wątki dla zadań mono. Pasmo dualne i semafory tutaj nie występują.
    xTaskCreatePinnedToCore(task_runner_core0, "runner_c0", 4096, NULL, 5, &h_core0, 0);
    xTaskCreatePinnedToCore(task_runner_core1, "runner_c1", 4096, NULL, 5, &h_core1, 1);
}