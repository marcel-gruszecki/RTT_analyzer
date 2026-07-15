# Protokół Transmisji i Konfiguracja Środowiska

> **Licencje:** ten katalog zawiera zarówno kod własny (MIT, patrz `/LICENSE.md`), jak i zvendorowaną bibliotekę SEGGER RTT (własna licencja BSD-style) — szczegóły w [`THIRD_PARTY_LICENSES.md`](./THIRD_PARTY_LICENSES.md).

Dane telemetryczne o przełączeniach kontekstu są przesyłane z mikrokontrolera ESP32-P4 do aplikacji analizatora w postaci binarnego strumienia stałorozmiarowych pakietów. Dzięki stałej długości ramki nie jest wymagane dodatkowe pakietowanie długościowe (length framing).

## Układ Pakietu Binarnego 

Każde zarejestrowane zdarzenie zajmuje dokładnie **34 bajty**.

| Offset | Rozmiar (bajt) | Pole | Opis |
|--------|----------------|---------------|--------------------------------------|
| 0      | 1              | magic[0]      | `0xAB` (pierwszy bajt synchronizacji) |
| 1      | 1              | magic[1]      | `0xCD` (drugi bajt synchronizacji) |
| 2      | 1              | type          | Typ zdarzenia: `0x01` = SwitchedIn, `0x02` = SwitchedOut |
| 3      | 1              | core_id       | Identyfikator rdzenia procesora: `0` lub `1` (ESP32-P4 jest dwurdzeniowy) |
| 4      | 1              | priority      | Bieżący priorytet zadania FreeRTOS (`0` = najniższy) |
| 5      | 4              | task_id       | Adres wskaźnika TCB (uint32_t LE) – unikalny dla każdej instancji zadania |
| 9      | 16             | name          | Nazwa zadania FreeRTOS (UTF-8, dopełniona zerami `\0`) |
| 25     | 8              | timestamp_us  | Czas od uruchomienia systemu w mikrosekundach (uint64_t LE) |
| 33     | 1              | checksum      | Suma kontrolna XOR obliczona na bajtach od `0` do `32` |

---

## Transport Danych: JTAG i Protokół RTT

Strumień danych przesyłany jest bezpośrednio przez interfejs debugowania sprzętowego:

`ESP32-P4 (RAM) → JTAG (Bufor RTT) → Programator JTAG → probe-rs (Aplikacja Rust)`

### 1. Konfiguracja w ESP-IDF (`menuconfig`)
W celu poprawnego działania telemetrii, upewnij się, że wsparcie dla trasowania aplikacji jest aktywne w konfiguracji mikrokontrolera:
```text
Component config → Application Level Tracing
  [*] Enable application tracing
  Data Destination: JTAG