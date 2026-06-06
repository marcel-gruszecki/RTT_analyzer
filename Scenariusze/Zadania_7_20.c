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

        // ZADANIA 17 - 20: Krótkie zadania tła/szumu na Core 1 (4 zadania po 2000 us = 8 ms)
        // Generują asymetrię względem przeładowanego Core 0 i fabryczną dziurę czasową
        execute_mono_task("t17", 2000); 
        execute_mono_task("t18", 2000); 
        execute_mono_task("t19", 2000); 
        execute_mono_task("t20", 2000); 

        xTaskNotifyGive(h_core0); 
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 6, n=20) ──────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 6 (n = 20): MONOLITYCZNE ZADANIE BLOKUJĄCE ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1); 

        // ZADANIA 1 - 16 (Przeładowany Core 0 - 16 zadań po 5000 us = 80 ms)
        // Zadanie t1 działa jako potężny monolit blokujący, generując ogromną dziurę na Core 1
        execute_mono_task("t1", 5000);   execute_mono_task("t2", 5000);
        execute_mono_task("t3", 5000);   execute_mono_task("t4", 5000);
        execute_mono_task("t5", 5000);   execute_mono_task("t6", 5000);
        execute_mono_task("t7", 5000);   execute_mono_task("t8", 5000);
        execute_mono_task("t9", 5000);   execute_mono_task("t10", 5000);
        execute_mono_task("t11", 5000);  execute_mono_task("t12", 5000);
        execute_mono_task("t13", 5000);  execute_mono_task("t14", 5000);
        execute_mono_task("t15", 5000);  execute_mono_task("t16", 5000);

        // Oczekiwanie na wyrównanie barierowe (Core 1 skończył swoje 8 ms tła bardzo wcześnie i tu czeka)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. PROFILE DUALNE DLA ZADAŃ ELASTYCZNYCH (Brak – brak wolnych zasobów do podziału w tej pętli)

        // 3. PASMO SZTYWNYCH BARIER DUALNYCH (Brak – to czysty scenariusz mono ze strukturą blokującą)
        // Bilans unikalnych zadań: t1...t16 (Core 0) + t17...t20 (Core 1) = Dokładnie 20 unikalnych zadań mono.
        // Brak jakichkolwiek zadań dualnych z przyrostkiem "_d" w wynikowym logu.

        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

void app_main(void) {
    tracer_init();
    xDualStartSem = xSemaphoreCreateBinary();
    xDualEndSem = xSemaphoreCreateBinary();
    
    xTaskCreatePinnedToCore(vDualWorkerTaskCore1, "dual_worker", 2048, NULL, 12, NULL, 1);
    xTaskCreatePinnedToCore(task_runner_core0, "runner_c0", 4096, NULL, 5, &h_core0, 0);
    xTaskCreatePinnedToCore(task_runner_core1, "runner_c1", 4096, NULL, 5, &h_core1, 1);
}