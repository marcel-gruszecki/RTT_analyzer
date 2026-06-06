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

        // Krótkie zadania na Core 1 (30 zadań po 3 ms = 90 ms łącznie)
        // Generują asymetrię obciążenia i wymuszony przestój na barierze końcowej
        execute_mono_task("t71", 3000);  execute_mono_task("t72", 3000);  execute_mono_task("t73", 3000);
        execute_mono_task("t74", 3000);  execute_mono_task("t75", 3000);  execute_mono_task("t76", 3000);
        execute_mono_task("t77", 3000);  execute_mono_task("t78", 3000);  execute_mono_task("t79", 3000);
        execute_mono_task("t80", 3000);  execute_mono_task("t81", 3000);  execute_mono_task("t82", 3000);
        execute_mono_task("t83", 3000);  execute_mono_task("t84", 3000);  execute_mono_task("t85", 3000);
        execute_mono_task("t86", 3000);  execute_mono_task("t87", 3000);  execute_mono_task("t88", 3000);
        execute_mono_task("t89", 3000);  execute_mono_task("t90", 3000);  execute_mono_task("t91", 3000);
        execute_mono_task("t92", 3000);  execute_mono_task("t93", 3000);  execute_mono_task("t94", 3000);
        execute_mono_task("t95", 3000);  execute_mono_task("t96", 3000);  execute_mono_task("t97", 3000);
        execute_mono_task("t98", 3000);  execute_mono_task("t99", 3000);  execute_mono_task("t100", 3000);

        xTaskNotifyGive(h_core0); // Powiadom Core 0 o zakończeniu sekwencji
    }
}

/* ── RDZEŃ 0: GŁÓWNA PĘTLA ZARZĄDZAJĄCA (Scenariusz 7, n=100) ──────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(50));

    while (1) {
        // Jednoczesny start obu rdzeni na początku cyklu pętli
        xTaskNotifyGive(h_core1);

        // Przeładowany Core 0 - masa krótkich zadań (70 zadań po 3 ms = 210 ms łącznie)
        execute_mono_task("t1", 3000);   execute_mono_task("t2", 3000);   execute_mono_task("t3", 3000);
        execute_mono_task("t4", 3000);   execute_mono_task("t5", 3000);   execute_mono_task("t6", 3000);
        execute_mono_task("t7", 3000);   execute_mono_task("t8", 3000);   execute_mono_task("t9", 3000);
        execute_mono_task("t10", 3000);  execute_mono_task("t11", 3000);  execute_mono_task("t12", 3000);
        execute_mono_task("t13", 3000);  execute_mono_task("t14", 3000);  execute_mono_task("t15", 3000);
        execute_mono_task("t16", 3000);  execute_mono_task("t17", 3000);  execute_mono_task("t18", 3000);
        execute_mono_task("t19", 3000);  execute_mono_task("t20", 3000);  execute_mono_task("t21", 3000);
        execute_mono_task("t22", 3000);  execute_mono_task("t23", 3000);  execute_mono_task("t24", 3000);
        execute_mono_task("t25", 3000);  execute_mono_task("t26", 3000);  execute_mono_task("t27", 3000);
        execute_mono_task("t28", 3000);  execute_mono_task("t29", 3000);  execute_mono_task("t30", 3000);
        execute_mono_task("t31", 3000);  execute_mono_task("t32", 3000);  execute_mono_task("t33", 3000);
        execute_mono_task("t34", 3000);  execute_mono_task("t35", 3000);  execute_mono_task("t36", 3000);
        execute_mono_task("t37", 3000);  execute_mono_task("t38", 3000);  execute_mono_task("t39", 3000);
        execute_mono_task("t40", 3000);  execute_mono_task("t41", 3000);  execute_mono_task("t42", 3000);
        execute_mono_task("t43", 3000);  execute_mono_task("t44", 3000);  execute_mono_task("t45", 3000);
        execute_mono_task("t46", 3000);  execute_mono_task("t47", 3000);  execute_mono_task("t48", 3000);
        execute_mono_task("t49", 3000);  execute_mono_task("t50", 3000);  execute_mono_task("t51", 3000);
        execute_mono_task("t52", 3000);  execute_mono_task("t53", 3000);  execute_mono_task("t54", 3000);
        execute_mono_task("t55", 3000);  execute_mono_task("t56", 3000);  execute_mono_task("t57", 3000);
        execute_mono_task("t58", 3000);  execute_mono_task("t59", 3000);  execute_mono_task("t60", 3000);
        execute_mono_task("t61", 3000);  execute_mono_task("t62", 3000);  execute_mono_task("t63", 3000);
        execute_mono_task("t64", 3000);  execute_mono_task("t65", 3000);  execute_mono_task("t66", 3000);
        execute_mono_task("t67", 3000);  execute_mono_task("t68", 3000);  execute_mono_task("t69", 3000);
        execute_mono_task("t70", 3000);

        // Oczekiwanie na wyrównanie barierowe
        // Core 1 kończy pracę po 90 ms i czeka bezczynnie przez 120 ms, aż Core 0 dobije do 210 ms
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Bilans unikalnych zadań w logu z jednej iteracji:
        // t1...t70 na Core 0 + t71...t100 na Core 1 = Dokładnie 100 unikalnych zadań mono.
        // Czysty model jednordzeniowy, brak zadań z przyrostkiem "_d".

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