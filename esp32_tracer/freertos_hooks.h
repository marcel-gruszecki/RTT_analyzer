/**
 * @file freertos_hooks.h
 * @brief Definicje Makr i Haków (Hooks) dla Jądra FreeRTOS
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Definiuje oficjalne haki systemowe jądra FreeRTOS (traceTASK_SWITCHED_IN/OUT).
 * Odpowiada za automatyczne przechwytywanie momentów przełączeń kontekstu zadań
 * na obu rdzeniach procesora ESP32-P4 i przekazywanie ich do bufora telemetrii.
 */

#pragma once

#include "task_tracer.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Makra pomocnicze wyciągające bezpiecznie parametry bieżącego zadania bez naruszania blokad jądra */
#define TRACER_CURRENT_NAME() pcTaskGetName(NULL)
#define TRACER_CURRENT_PRIO() ((uint8_t)uxTaskPriorityGet(NULL))

/* ── Definicje haków telemetrycznych (należy dołączyć lub wkleić do FreeRTOSConfig.h) ── */

/**
 * @brief Hak wywoływany przez jądro automatycznie w momencie aktywacji (wejścia) zadania na rdzeń.
 */
#define traceTASK_SWITCHED_IN() \
    tracer_record(TRACER_EVT_SWITCHED_IN,  \
                  TRACER_CURRENT_NAME(),   \
                  (uint8_t)esp_cpu_get_core_id(), \
                  TRACER_CURRENT_PRIO())

/**
 * @brief Hak wywoływany przez jądro automatycznie w momencie wywłaszczenia lub zablokowania (wyjścia) zadania.
 */
#define traceTASK_SWITCHED_OUT() \
    tracer_record(TRACER_EVT_SWITCHED_OUT, \
                  TRACER_CURRENT_NAME(),   \
                  (uint8_t)esp_cpu_get_core_id(), \
                  TRACER_CURRENT_PRIO())