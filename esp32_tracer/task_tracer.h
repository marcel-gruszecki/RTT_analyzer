/**
 * @file task_tracer.h
 * @brief Biblioteka Telemetrii FreeRTOS dla ESP32-P4 - Plik Nagłówkowy
 *
 * Projekt: RTT-Task Analyser
 * Autor: Marcel Gruszecki (UAM)
 * Opis: Definiuje binarny format ramki sieciowej (34 bajty) przesyłanej przez JTAG/RTT
 * oraz sygnatury funkcji rejestrujących przełączenia kontekstu we FreeRTOS.
 */

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bajty synchronizacji ramki binarnej */
#define TRACER_MAGIC_0      0xAB
#define TRACER_MAGIC_1      0xCD

/* Maksymalna długość nazwy zadania (zgodna z configMAX_TASK_NAME_LEN we FreeRTOS) */
#define TRACER_NAME_LEN     16

/**
 * @brief Typy zdarzeń przełączenia kontekstu zadań.
 */
typedef enum __attribute__((packed)) {
    TRACER_EVT_SWITCHED_IN  = 0x01,  /* Rozpoczęcie wykonywania zadania */
    TRACER_EVT_SWITCHED_OUT = 0x02,  /* Wstrzymanie lub wywłaszczenie zadania */
} tracer_evt_t;

/**
 * @brief Struktura surowego pakietu telemetrycznego (format zgodny z dekoderem w Rust).
 */
typedef struct __attribute__((packed)) {
    uint8_t      magic[2];              /* Sekwencja startowa {0xAB, 0xCD} */
    uint8_t      type;                  /* Typ zdarzenia (tracer_evt_t) */
    uint8_t      core_id;               /* Identyfikator rdzenia (0 lub 1) */
    uint8_t      priority;              /* Bieżący priorytet zadania FreeRTOS */
    uint32_t     task_id;               /* Unikalny identyfikator (wskaźnik na TCB) */
    char         name[TRACER_NAME_LEN]; /* Nazwa zadania (zakończona zerem) */
    uint64_t     timestamp_us;          /* Czas od uruchomienia systemu w mikrosekundach */
    uint8_t      checksum;              /* Suma kontrolna XOR wszystkich poprzednich bajtów */
} tracer_packet_t;

/**
 * @brief Inicjalizuje bufor kołowy SEGGER RTT na kanale 1.
 * * Funkcja musi być wywołana jednorazowo przed uruchomieniem planisty (schedulera) FreeRTOS.
 */
void tracer_init(void);

/**
 * @brief Rejestruje zdarzenie przełączenia kontekstu i zapisuje je do bufora RTT.
 * * Funkcja jest bezpieczna do wywołania wewnątrz przerwań (ISR-safe), nieblokująca
 * oraz chroniona za pomocą blokady spinlock.
 */
void tracer_record(tracer_evt_t type, const char *name,
                   uint8_t core_id, uint8_t priority);

#ifdef __cplusplus
}
#endif