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

        // Zadania Mono na Core 1 (Ciężkie zadania generujące odwróconą asymetrię)
        execute_mono_task("fx1", 10000); execute_mono_task("fx2", 10000);
        execute_mono_task("fx3", 10000); execute_mono_task("fx4", 10000);
        execute_mono_task("fx5", 10000); execute_mono_task("fx6", 10000);

        xTaskNotifyGive(h_core0);
    }
}

/* ── RDZEŃ 0: GŁÓWNY ZARZĄDCA PĘTLI (Scenariusz 5, n=100) ───────────────── */
static void task_runner_core0(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // === SCENARIUSZ 5 (n = 100): WIĘKSZOŚĆ ZADAŃ DWURDZENIOWYCH I NIELICZNE POJEDYNCZE ===

        // 1. START FAZY MONO
        xTaskNotifyGive(h_core1);

        // Zadania mono pod przyszłe pary dualne na Core 0 (fx7 do fx28 -> 22 zadania)
        execute_mono_task("fx7", 300);  execute_mono_task("fx8", 300);  execute_mono_task("fx9", 300);
        execute_mono_task("fx10", 300); execute_mono_task("fx11", 300); execute_mono_task("fx12", 300);
        execute_mono_task("fx13", 300); execute_mono_task("fx14", 300); execute_mono_task("fx15", 300);
        execute_mono_task("fx16", 300); execute_mono_task("fx17", 300); execute_mono_task("fx18", 300);
        execute_mono_task("fx19", 300); execute_mono_task("fx20", 300); execute_mono_task("fx21", 300);
        execute_mono_task("fx22", 300); execute_mono_task("fx23", 300); execute_mono_task("fx24", 300);
        execute_mono_task("fx25", 300); execute_mono_task("fx26", 300); execute_mono_task("fx27", 300);
        execute_mono_task("fx28", 300);

        // Zadania czyste Mono na Core 0 (Lekkie zadania uzupełniające pulę unikalnych nazw fx29 do fx53 -> 25 zadań)
        execute_mono_task("fx29", 120); execute_mono_task("fx30", 120); execute_mono_task("fx31", 120);
        execute_mono_task("fx32", 120); execute_mono_task("fx33", 120); execute_mono_task("fx34", 120);
        execute_mono_task("fx35", 120); execute_mono_task("fx36", 120); execute_mono_task("fx37", 120);
        execute_mono_task("fx38", 120); execute_mono_task("fx39", 120); execute_mono_task("fx40", 120);
        execute_mono_task("fx41", 120); execute_mono_task("fx42", 120); execute_mono_task("fx43", 120);
        execute_mono_task("fx44", 120); execute_mono_task("fx45", 120); execute_mono_task("fx46", 120);
        execute_mono_task("fx47", 120); execute_mono_task("fx48", 120); execute_mono_task("fx49", 120);
        execute_mono_task("fx50", 120); execute_mono_task("fx51", 120); execute_mono_task("fx52", 120);
        execute_mono_task("fx53", 120);

        // Oczekiwanie na wyrównanie barierowe (Core 0 szybko kończy i czeka na Core 1)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. FAZA DUAL PARZYSTE (28 par dla istniejących zadań mono fx1-fx28)
        execute_dual_task("fx1_0_d", "fx1_1_d", 2000);   execute_dual_task("fx2_0_d", "fx2_1_d", 2000);
        execute_dual_task("fx3_0_d", "fx3_1_d", 2000);   execute_dual_task("fx4_0_d", "fx4_1_d", 2000);
        execute_dual_task("fx5_0_d", "fx5_1_d", 2000);   execute_dual_task("fx6_0_d", "fx6_1_d", 2000);
        execute_dual_task("fx7_0_d", "fx7_1_d", 1000);   execute_dual_task("fx8_0_d", "fx8_1_d", 1000);
        execute_dual_task("fx9_0_d", "fx9_1_d", 1000);   execute_dual_task("fx10_0_d", "fx10_1_d", 1000);
        execute_dual_task("fx11_0_d", "fx11_1_d", 1000); execute_dual_task("fx12_0_d", "fx12_1_d", 1000);
        execute_dual_task("fx13_0_d", "fx13_1_d", 1000); execute_dual_task("fx14_0_d", "fx14_1_d", 1000);
        execute_dual_task("fx15_0_d", "fx15_1_d", 1000); execute_dual_task("fx16_0_d", "fx16_1_d", 1000);
        execute_dual_task("fx17_0_d", "fx17_1_d", 1000); execute_dual_task("fx18_0_d", "fx18_1_d", 1000);
        execute_dual_task("fx19_0_d", "fx19_1_d", 1000); execute_dual_task("fx20_0_d", "fx20_1_d", 1000);
        execute_dual_task("fx21_0_d", "fx21_1_d", 1000); execute_dual_task("fx22_0_d", "fx22_1_d", 1000);
        execute_dual_task("fx23_0_d", "fx23_1_d", 1000); execute_dual_task("fx24_0_d", "fx24_1_d", 1000);
        execute_dual_task("fx25_0_d", "fx25_1_d", 1000); execute_dual_task("fx26_0_d", "fx26_1_d", 1000);
        execute_dual_task("fx27_0_d", "fx27_1_d", 1000); execute_dual_task("fx28_0_d", "fx28_1_d", 1000);

        // 3. FAZA DUAL TŁO (19 czystych zadań dualnych jako uzupełnienie tła barierowego -> t1 do t19)
        execute_dual_task("t1_0_d", "t1_1_d", 1000);   execute_dual_task("t2_0_d", "t2_1_d", 1000);
        execute_dual_task("t3_0_d", "t3_1_d", 1000);   execute_dual_task("t4_0_d", "t4_1_d", 1000);
        execute_dual_task("t5_0_d", "t5_1_d", 1000);   execute_dual_task("t6_0_d", "t6_1_d", 1000);
        execute_dual_task("t7_0_d", "t7_1_d", 1000);   execute_dual_task("t8_0_d", "t8_1_d", 1000);
        execute_dual_task("t9_0_d", "t9_1_d", 1000);   execute_dual_task("t10_0_d", "t10_1_d", 1000);
        execute_dual_task("t11_0_d", "t11_1_d", 1000); execute_dual_task("t12_0_d", "t12_1_d", 1000);
        execute_dual_task("t13_0_d", "t13_1_d", 1000); execute_dual_task("t14_0_d", "t14_1_d", 1000);
        execute_dual_task("t15_0_d", "t15_1_d", 1000); execute_dual_task("t16_0_d", "t16_1_d", 1000);
        execute_dual_task("t17_0_d", "t17_1_d", 1000); execute_dual_task("t18_0_d", "t18_1_d", 1000);
        execute_dual_task("t19_0_d", "t19_1_d", 1000);

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