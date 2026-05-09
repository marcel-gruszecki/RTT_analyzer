# Multi Task Analyser

Aplikacja desktopowa do analizy harmonogramowania zadań na mikrokontrolerze
ESP32-P4 (FreeRTOS SMP, 2 rdzenie). Czyta dane śladu z urządzenia przez JTAG,
paruje zdarzenia w wykonania zadań i uruchamia algorytmy harmonogramowania
porównując je z rzeczywistym przypisaniem przez OS.

## Co robi

1. Łączy się z ESP32-P4 przez JTAG (`probe-rs`).
2. Czyta pakiety `SWITCHED_IN`/`SWITCHED_OUT` z kanału SEGGER RTT (kanał 1).
3. Paruje je w pełne wykonania (`TaskExecution`) z czasem CPU per zadanie.
4. Po kliknięciu przycisku oblicza Cmax dla wybranych algorytmów i mierzy
   czas ich wykonania.
5. Wyświetla tabelę wyników (Cmax, baseline, poprawa, czas algorytmu)
   i diagram Gantta wybranego algorytmu.

## Uruchomienie

```bash
cargo run
```

Wymaga:
- podłączonego ESP32-P4 z firmware'em używającym `task_tracer.h`,
- sterowników JTAG (`probe-rs` automatycznie wykrywa programator).

## Algorytmy

Wszystkie rozwiązują problem **P | pmtn, spdp-any | Cmax** (preempcja, M=2,
zadanie może iść w trybie 1- lub 2-rdzeniowym; w trybie 2-rdz. czas ścienny
to `LIN_FACTOR · p_j` na obu rdzeniach jednocześnie, `LIN_FACTOR = 0.6`).

| Algorytm        | Złożoność   | Optymalność |
|-----------------|-------------|-------------|
| **GreedySpdp**  | `O(n²)`     | heurystyk — może utknąć w lokalnym minimum |
| **SplitOff**    | `O(n²)`     | heurystyk — McNaughton-guided offload |
| **DuLeung1989** | `O(n · 2ⁿ)` | **OPTYMALNY** — Du, Leung 1989, Theorem 6 |

Du-Leung enumeruje wszystkie `2ⁿ` przypisań trybów (S2 ⊆ zadań) i dla każdego
oblicza optymalny preempcyjny Cmax (formuła Blazewicza). Dla `n ≤ 22`
praktyczny; dla większych `n` aplikacja robi fallback do `SplitOff`.

## UI

W panelu analizy są dwa przyciski:

- **▶ Uruchom wszystkie** — odpala 3 algorytmy i pokazuje porównawczą tabelę.
- **▶ Tylko wybrany** — odpala wyłącznie algorytm wybrany z listy obok.
  Przydatne np. do mierzenia czasu samego DuLeunga bez czekania na heurystyki.

Tabela wyników ma kolumny:
- Algorytm, Cmax [µs], Bazowy [µs], Poprawa, **Czas algorytmu** (ns/µs/ms/s).

Pod tabelą diagram Gantta dla aktualnie wybranego algorytmu.

## Struktura

```
src/
├── main.rs                       wejście, uruchomienie egui
├── app.rs                        stan aplikacji, główna pętla UI
├── app/
│   ├── tracer.rs                 wątek tła odbierający dane z ESP32
│   └── ui/                       panele UI (top, task, analysis)
└── communication/
    ├── protocol.rs               format pakietów RTT (34 bajty)
    ├── session.rs                attach przez probe-rs, czytanie RTT
    ├── scheduler.rs              parser zdarzeń → TaskExecution
    └── heuristics.rs             3 algorytmy harmonogramowania
```

