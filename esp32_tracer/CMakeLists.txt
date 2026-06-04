# # CMakeLists.txt — Konfiguracja Komponentu ESP-IDF
#
# Projekt: RTT-Task Analyser
# Autor: Marcel Gruszecki (UAM)
# Moduł: `esp32_tracer::CMakeLists`
# Opis: Skrypt budujący systemu CMake dla środowiska ESP-IDF.
#       Rejestruje pliki telemetrii oraz dołączoną lokalnie bibliotekę SEGGER RTT,
#       definiując zależności od podsystemów FreeRTOS i timerów sprzętowych.

idf_component_register(
    SRCS
        "task_tracer.c"
        "SEGGER_RTT.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        freertos
        esp_timer
        esp_hw_support
)