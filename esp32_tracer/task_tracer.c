/**
 * @file task_tracer.c
 * @brief Biblioteka Telemetrii FreeRTOS dla ESP32-P4 - Implementacja
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Implementuje mechanizm niskopoziomowego zapisu zdarzeń RTOS.
 * Odpowiada za konfigurację bufora SEGGER RTT, wyliczanie sumy kontrolnej XOR,
 * ekstrakcję uchwytów zadań (TCB) oraz bezpieczny asynchroniczny zapis danych.
 */

#include "task_tracer.h"

#include "esp_timer.h"
#include "esp_cpu.h"
#include "SEGGER_RTT.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

/* Kanał 1 RTT zarezerwowany dla pakietów telemetrii (Kanał 0 służy dla konsoli logów) */
#define TRACER_RTT_CHANNEL  1
#define TRACER_RTT_BUF_SIZE (512 * sizeof(tracer_packet_t))

/* Alokacja statycznego bufora w pamięci mikrokontrolera dla kanału RTT */
static volatile uint8_t rtt_up_buf[TRACER_RTT_BUF_SIZE];

/* Wylicza sumę kontrolną XOR dla zadanej tablicy bajtów */
static uint8_t xor_checksum(const uint8_t *data, size_t len)
{
    uint8_t chk = 0;
    for (size_t i = 0; i < len; i++) chk ^= data[i];
    return chk;
}

/* Zapisuje zdarzenie telemetryczne FreeRTOS bezpośrednio do pamięci RAM (bufor RTT) */
void tracer_record(tracer_evt_t type, const char *name,
                   uint8_t core_id, uint8_t priority)
{
    tracer_packet_t pkt;

    /* Budowanie binarnego pakietu telemetrycznego */
    pkt.magic[0]     = TRACER_MAGIC_0;
    pkt.magic[1]     = TRACER_MAGIC_1;
    pkt.type         = (uint8_t)type;
    pkt.core_id      = core_id;
    pkt.priority     = priority;

    /* Rzutowanie wskaźnika bieżącego zadania na uint32_t jako unikalny identyfikator instancji */
    pkt.task_id      = (uint32_t)(uintptr_t)xTaskGetCurrentTaskHandle();
    pkt.timestamp_us = (uint64_t)esp_timer_get_time();

    strncpy(pkt.name, name, TRACER_NAME_LEN - 1);
    pkt.name[TRACER_NAME_LEN - 1] = '\0';

    /* Obliczanie sumy kontrolnej z pominięciem ostatniego bajtu (którym jest samo pole checksum) */
    pkt.checksum = xor_checksum((uint8_t *)&pkt, sizeof(pkt) - 1);

    /* Bezpieczny, nieblokujący zapis do bufora RTT (funkcja wewnętrznie używa blokady spinlock) */
    SEGGER_RTT_Write(TRACER_RTT_CHANNEL, &pkt, sizeof(pkt));
}

/* Konfiguruje kanał wyjściowy RTT w trybie bez blokowania planisty RTOS */
void tracer_init(void)
{
    SEGGER_RTT_ConfigUpBuffer(TRACER_RTT_CHANNEL, "tracer",
                              (uint8_t *)rtt_up_buf, sizeof(rtt_up_buf),
                              SEGGER_RTT_MODE_NO_BLOCK_SKIP);
}