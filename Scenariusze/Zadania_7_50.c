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

        // Krótkie zadania na Core 1 (15 zadań po 4 ms = 60 ms łącznie)
        // Generują asymetrię obciążenia i wymuszony przestój na barierze końcowej
        execute_mono_task("t36", 4000); execute_mono_task("t37", 4000);
        execute_mono_task("t38", 4000); execute_mono_task("t39", 4000);
        execute_mono_task("t40", 4000); execute_mono_task("t41", 4000);
        execute_mono_task("t42", 4000); execute_mono_task("t43", 4000);
        execute_mono_task("t44", 4000); execute_mono_task("t45", 4000);
        execute_mono_task("t46", 4000); execute_mono_task("t47", 4000);
        execute_mono_task("t48", 4000); execute_mono_task("t49", 4000);
        execute_mono_task("t50", 4000);

        xTaskNotifyGive(h_core0); // Powiadom Core 0 o zakończeniu sekwencji
    }
}

/* ── RDZEŃ 0: GŁÓWNA PĘTLA ZARZĄDZAJĄCA (Scenariusz 7, n=50) ──────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(50));

    while (1) {
        // Jednoczesny start obu rdzeni na początku cyklu pętli
        xTaskNotifyGive(h_core1);

        // Przeładowany Core 0 - masa krótkich zadań (35 zadań po 4 ms = 140 ms łącznie)
        execute_mono_task("t1", 4000);  execute_mono_task("t2", 4000);  execute_mono_task("t3", 4000);
        execute_mono_task("t4", 4000);  execute_mono_task("t5", 4000);  execute_mono_task("t6", 4000);
        execute_mono_task("t7", 4000);  execute_mono_task("t8", 4000);  execute_mono_task("t9", 4000);
        execute_mono_task("t10", 4000); execute_mono_task("t11", 4000); execute_mono_task("t12", 4000);
        execute_mono_task("t13", 4000); execute_mono_task("t14", 4000); execute_mono_task("t15", 4000);
        execute_mono_task("t16", 4000); execute_mono_task("t17", 4000); execute_mono_task("t18", 4000);
        execute_mono_task("t19", 4000); execute_mono_task("t20", 4000); execute_mono_task("t21", 4000);
        execute_mono_task("t22", 4000); execute_mono_task("t23", 4000); execute_mono_task("t24", 4000);
        execute_mono_task("t25", 4000); execute_mono_task("t26", 4000); execute_mono_task("t27", 4000);
        execute_mono_task("t28", 4000); execute_mono_task("t29", 4000); execute_mono_task("t30", 4000);
        execute_mono_task("t31", 4000); execute_mono_task("t32", 4000); execute_mono_task("t33", 4000);
        execute_mono_task("t34", 4000); execute_mono_task("t35", 4000);

        // Oczekiwanie na wyrównanie barierowe
        // Core 1 kończy pracę po 60 ms i czeka bezczynnie przez 80 ms, aż Core 0 dobije do 140 ms
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Bilans unikalnych zadań w logu z jednej iteracji:
        // t1...t35 na Core 0 + t36...t50 na Core 1 = Dokładnie 50 unikalnych zadań mono.
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