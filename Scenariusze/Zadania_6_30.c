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

        // Profil schodkowy na Core 1 (parzyste stopnie schodków)
        execute_mono_task("fx2", 2000);    // 2 ms
        execute_mono_task("fx4", 4000);    // 4 ms
        execute_mono_task("fx6", 6000);    // 6 ms
        execute_mono_task("fx8", 8000);    // 8 ms
        execute_mono_task("fx10", 10000);  // 10 ms
        execute_mono_task("fx12", 12000);  // 12 ms
        execute_mono_task("fx14", 14000);  // 14 ms
        execute_mono_task("fx16", 16000);  // 16 ms
        execute_mono_task("fx18", 18000);  // 18 ms
        execute_mono_task("fx20", 20000);  // 20 ms
        execute_mono_task("fx22", 22000);  // 22 ms
        execute_mono_task("fx24", 24000);  // 24 ms
        execute_mono_task("fx26", 26000);  // 26 ms

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 6, n=30) ──────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 6 (n = 30): MONOLITYCZNE ZADANIE BLOKUJĄCE ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1);

        // Profil schodkowy na Core 0 (nieparzyste stopnie schodków)
        // Zadanie fx1 działa jako potężny monolit blokujący (55 ms), generując dziurę na Core 1
        execute_mono_task("fx1", 55000);
        execute_mono_task("fx3", 3000);
        execute_mono_task("fx5", 5000);
        execute_mono_task("fx7", 7000);
        execute_mono_task("fx9", 9000);
        execute_mono_task("fx11", 11000);
        execute_mono_task("fx13", 13000);
        execute_mono_task("fx15", 15000);
        execute_mono_task("fx17", 17000);
        execute_mono_task("fx19", 19000);
        execute_mono_task("fx21", 21000);
        execute_mono_task("fx23", 23000);
        execute_mono_task("fx25", 25000);

        // Oczekiwanie na wyrównanie barierowe (Core 1 kończy 13 zadań mono po 182 ms i poczeka na Core 0)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. PROFILE DUALNE DLA ZADAŃ ELASTYCZNYCH (Uzupełnienie struktury do n=30 unikalnych zadań)
        // Dokładnie 30 unikalnych zadań w całej pętli: fx1-fx26 w mono oraz fx27-fx30 w strukturze dualnej
        execute_dual_task("fx27_0_d", "fx27_1_d", 27000); // 27 ms rozbite na oba rdzenie
        execute_dual_task("fx28_0_d", "fx28_1_d", 28000); // 28 ms rozbite na oba rdzenie
        execute_dual_task("fx29_0_d", "fx29_1_d", 29000); // 29 ms rozbite na oba rdzenie
        execute_dual_task("fx30_0_d", "fx30_1_d", 30000); // 30 ms rozbite na oba rdzenie

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