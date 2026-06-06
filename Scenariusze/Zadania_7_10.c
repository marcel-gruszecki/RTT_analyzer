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

        // ZADANIA 7 - 10: Krótkie zadania na Core 1 (4 zadania po 15 ms = 60 ms łącznie)
        // Generują asymetrię względem przeładowanego Core 0 i przestój systemowy
        execute_mono_task("t7", 15000);
        execute_mono_task("t8", 15000);
        execute_mono_task("t9", 15000);
        execute_mono_task("t10", 15000);

        xTaskNotifyGive(h_core0); // Powiadom Core 0 o zakończeniu sekwencji
    }
}

/* ── RDZEŃ 0: GŁÓWNA PĘTLA ZARZĄDZAJĄCA (Scenariusz 7, n=10) ──────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(50));

    while (1) {
        // Jednoczesny start obu rdzeni na początku cyklu pętli
        xTaskNotifyGive(h_core1);

        // ZADANIA 1 - 6 (Przeładowany Core 0 - 6 zadań po 15 ms = 90 ms łącznie)
        execute_mono_task("t1", 15000);
        execute_mono_task("t2", 15000);
        execute_mono_task("t3", 15000);
        execute_mono_task("t4", 15000);
        execute_mono_task("t5", 15000);
        execute_mono_task("t6", 15000);

        // Blokada pętli dopóki Core 1 nie potwierdzi zakończenia (choć skończył o 30 ms wcześniej)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Bilans unikalnych zadań w logu z jednej iteracji:
        // t1...t6 na Core 0 + t7...t10 na Core 1 = Dokładnie 10 unikalnych zadań mono.
        // Brak jakichkolwiek zadań dualnych i semaforów barierowych.

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ── INICJALIZACJA SYSTEMU ────────────────────────────────────────────── */
void app_main(void) {
    tracer_init();

    // Tworzymy wyłącznie wątki dla zadań mono. Pasmo dualne i semafory barierowe tutaj nie występują.
    xTaskCreatePinnedToCore(task_runner_core0, "runner_c0", 4096, NULL, 5, &h_core0, 0);
    xTaskCreatePinnedToCore(task_runner_core1, "runner_c1", 4096, NULL, 5, &h_core1, 1);
}