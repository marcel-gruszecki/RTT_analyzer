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

        // Zadania 1 do 11: czyste Mono na Core 1
        execute_mono_task("fx1", 1000);  execute_mono_task("fx2", 1000);
        execute_mono_task("fx3", 1000);  execute_mono_task("fx4", 1000);
        execute_mono_task("fx5", 1000);  execute_mono_task("fx6", 1000);
        execute_mono_task("fx7", 1000);  execute_mono_task("fx8", 1000);
        execute_mono_task("fx9", 1000);  execute_mono_task("fx10", 1000);
        execute_mono_task("fx11", 1000);

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 4, n=30) ───────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 4 (n = 30): MONOLITYCZNE ZADANIE BLOKUJĄCE ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1); 

        // Zadanie 12: czyste Mono na Core 0 (Potężny monolit blokujący Core 1)
        execute_mono_task("fx12", 25000); 

        // Oczekiwanie na wyrównanie barierowe
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Zadania 13 do 30: czyste Dual (Unikalne nazwy bazowe fx13-fx30 -> Łącznie 30 unikalnych zadań)
        execute_dual_task("fx13_0_d", "fx13_1_d", 20000); 
        execute_dual_task("fx14_0_d", "fx14_1_d", 1600);  
        execute_dual_task("fx15_0_d", "fx15_1_d", 1600);  
        execute_dual_task("fx16_0_d", "fx16_1_d", 1600);  
        execute_dual_task("fx17_0_d", "fx17_1_d", 1600);  
        execute_dual_task("fx18_0_d", "fx18_1_d", 1600);  
        execute_dual_task("fx19_0_d", "fx19_1_d", 1600);  
        execute_dual_task("fx20_0_d", "fx20_1_d", 1600);  
        execute_dual_task("fx21_0_d", "fx21_1_d", 1600);  
        execute_dual_task("fx22_0_d", "fx22_1_d", 1600);  
        execute_dual_task("fx23_0_d", "fx23_1_d", 1600);  
        execute_dual_task("fx24_0_d", "fx24_1_d", 1600);  
        execute_dual_task("fx25_0_d", "fx25_1_d", 1500);  
        execute_dual_task("fx26_0_d", "fx26_1_d", 1500);  
        execute_dual_task("fx27_0_d", "fx27_1_d", 1500);  
        execute_dual_task("fx28_0_d", "fx28_1_d", 1500);  
        execute_dual_task("fx29_0_d", "fx29_1_d", 1500);  
        execute_dual_task("fx30_0_d", "fx30_1_d", 1500);  

        vTaskDelay(pdMS_TO_TICKS(15)); 
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