# RTT-Task Analyser

## Cel projektu
Projekt **RTT-Task Analyser** to narzędzie graficzne i analityczne przeznaczone do wizualizacji, profilowania oraz ewaluacji algorytmów szeregowania zadań w systemach operacyjnych czasu rzeczywistego (RTOS).

Aplikacja przetwarza na żywo binarny strumień zdarzeń przesyłany protokołem SEGGER RTT z mikrokontrolera (np. ESP32-P4 pod kontrolą FreeRTOS). Głównym rezultatem jest rysowanie wykresu Gantta w czasie rzeczywistym dla wielu rdzeni oraz wyliczanie metryk efektywności – w tym czasu zakończenia uszeregowania ($C_{max}$) z wykorzystaniem różnych heurystyk (np. Baseline, Greedy SPDP, Duleung).

## Wymagania
**Sprzętowe:**
* Mikrokontroler (np. ESP32-P4) z wgranym oprogramowaniem generującym komunikaty RTT (scenariusze testowe).
* Programator kompatybilny z SEGGER J-Link.
* **Kluczowe:** Aplikacja działa **wyłącznie** przy podłączonym na żywo mikrokontrolerze z aktywnym strumieniem RTT.

**Programowe:**
* System operacyjny: Linux.
* Zainstalowane środowisko Rust (wersja `stable`).
* Zainstalowany pakiet sterowników SEGGER J-Link Software and Documentation Pack.
* Na systemach Linux wymagane pakiety deweloperskie dla GUI: `libgtk-3-dev`, `libxcb-render0-dev`, `libxcb-shape0-dev`, `libxcb-xfixes0-dev`.

## Instalacja i konfiguracja
1. Pobierz repozytorium projektu:
```bash
   git clone https://github.com/marcel-gruszecki/RTT_analyzer
   cd RTT_analyzer
```
2. Zainstaluj wymagane pakiety:
```bash
    sudo apt-get update
    sudo apt-get install -y libgtk-3-dev libxcb-render0-dev libxcb-shape0-dev libxcb-xfixes0-dev libxkbcommon-dev
```
3. Upewnij się, że masz podłączony mikrokontroler z odpowiednio wgranym oprogramowaniem (firmware ze scenariuszami testowymi) i włączonym serwerem RTT. 
## Uruchomienie demonstracji 
1. Podłącz debuger J-Link i mikrokontroler do komputera.
2. Uruchom układ i zainicjuj transmisję RTT po stronie sprzętowej.
3. W głównym katalogu sklonowanego repozytorium na komputerze uruchom aplikację analityczną:
```bash
cargo run --release
```
## Oczekiwany wynik
Po poprawnym podłączeniu sprzętu i uruchomieniu komendy, otworzy się natywne okno aplikacji graficznej (napisanej w bibliotece egui). Na ekranie powinien pojawić się:
- Centralny wykres Gantta rysowany na żywo, wizualizujący zadania na rdzeniach Core 0 i Core 1.
- Panel analityczny prezentujący bieżące wyliczenia czasu zakończenia ($C_{max}$) dla przetwarzanego w danej chwili scenariusza testowego.
- Wskaźnik poprawnego nawiązania połączenia ze strumieniem RTT.

## Dane
Projekt nie używa zewnętrznych baz danych ani statycznych plików wsadowych (logów). 
Wszystkie dane niezbędne do wizualizacji i analizy są pozyskiwane na żywo ze strumienia RTT generowanego 
przez podłączony mikrokontroler. Dostęp do danych polega na fizycznym wgraniu przygotowanych w kodzie C/FreeRTOS scenariuszy na mikrokontroler.

## Reprodukcja i weryfikacja wyników 
Aby zweryfikować wyniki, należy wgrać na mikrokontroler kod reprezentujący dany scenariusz testowy 
(np. scenariusz z asymetrią obciążeń), a następnie odczytać wartości obliczone w Panelu Analitycznym aplikacji.
Weryfikacja polega na porównaniu wyników uzyskanych w aplikacji w czasie rzeczywistym z referencyjnymi 
danymi zawartymi w pliku arkusza kalkulacyjnego wyniki.xlsx, który został dołączony do repozytorium. 
Plik ten zawiera zestawienie wyników i metryk dla wszystkich obsługiwanych scenariuszy. (Czasy wykonania scenariuszy
mogą się różnić w zależności od sprzętu i konfiguracji!)

## Testy
W kodzie analizatora nie zaimplementowano standardowych automatycznych testów jednostkowych (np. cargo test). 
Proces testowania i walidacji odbywa się systemowo za pomocą fizycznych scenariuszy testowych.
- Uruchomienie testów: Wgranie wybranego scenariusza brzegowego na sprzęt i uruchomienie analizatora.
- Raporty z testów: Oficjalne wyniki i metryki z przeprowadzonych testów scenariuszowych udokumentowane 
są we wspomnianym pliku wyniki.xlsx.

## Dokumentacja
Całość kodu źródłowego została dokładnie opisana przy pomocy standardowego narzędzia rustdoc. 
Dokumentacja pełni rolę zarówno przewodnika dla użytkownika, jak i specyfikacji architektury oprogramowania 
oraz instrukcji konserwacyjnej.
- Interaktywna dokumentacja techniczna (Architektura i Konserwacja):https://marcel-gruszecki.github.io/Dokumentacja_RTT/rtt_task_analyzer/index.html
Z poziomu powyższego linku można zapoznać się ze strukturą kluczowych modułów:
- app - Architektura interfejsu graficznego.
- communication - Silnik dekodowania strumienia RTT oraz algorytmy (heurystyki) przeliczające czas wykonania.

### Znaczenie i nomenklatura scenariuszy testowych
Wszystkie kody źródłowe implementujące zadania testowe FreeRTOS dla mikrokontrolera ESP32-P4 znajdują się w dedykowanym folderze `Scenariusze/`. Pliki te nazwane są według jednolitego klucza strukturalnego:
`Zadania_[NumerScenariusza]_[LiczbaZadań].c`

*Przykład:* Plik o nazwie `Zadania_1_5.c` oznacza, że zawiera on implementację **Scenariusza 1** konfigurującego pulę **5 unikalnych zadań** ($n=5$).

### Opisy scenariuszy badawczo-testowych
W katalogu ze scenariuszami zaimplementowano osiem unikalnych profilów obciążeń systemowych w celu zweryfikowania odporności i elastyczności zaimplementowanych algorytmów:

* **Scenariusz 1 (Symmetrical Baseline):** Działa jako symetryczny punkt odniesienia, w którym zadania mają jednolite właściwości i są zbalansowane. Czasy trwania w trybie jedno-rdzeniowym generowane są w przewidywalnym zakresie ($p_{\text{single}} \in [10, 30]$ ms), a czasy w trybie dwu-rdzeniowym są skracane o połowę ($p_{\text{dual}} \approx p_{\text{single}}/2$).
* **Scenariusz 2 (Asymmetrical Workload):** Reprezentuje asymetryczny profil obciążenia, w którym jeden rdzeń mierzy się z bardzo dużym narzutem obliczeniowym wynikającym z długich zadań jedno-rdzeniowych ($p_{\text{single}} \in [150, 300]$ ms), podczas gdy drugi rdzeń obsługuje jedynie pojedyncze, odizolowane operacje.
* **Scenariusz 3 (Linear Queue Scaling):** Organizuje zadania sekwencyjnie na obu rdzeniach od najkrótszego czasu wykonania do najdłuższego (skalując je liniowo w przedziale od $5$ do $150$ ms) w celu przeanalizowania zachowania podstawowych kolejek przetwarzania.
* **Scenariusz 4 (High Volatility Execution):** Wprowadza wysoką zmienność (wariancję) długości procesów. Podczas działania systemu bardzo długie ($p \in [200, 500]$ ms) oraz bardzo krótkie zadania ($p \in [1, 5]$ ms) stale i naprzemiennie przeplatają się na osi czasu.
* **Scenariusz 5 (High Parallel Concentration):** Koncentruje się na środowisku o wysokim zagęszczeniu zadań dwu-rdzeniowych (stanowiących ponad $80\%$ całego obciążenia), które wymagają ścisłej synchronizacji barierowej na obu procesorach jednocześnie, przy minimalnej dostępności zadań jedno-rdzeniowych.
* **Scenariusz 6 (Extreme Core Imbalance):** Modeluje skrajnie niezbalansowany stan systemu, w którym długa sekwencja stopniowo rosnących zadań (skalowanych od $10$ do $200$ ms) wykonuje się w całości na jednym rdzeniu, podczas gdy drugi rdzeń pozostaje całkowicie bezczynny (Idle).
* **Scenariusz 7 (Ultra-Short Cluster Overload):** Składa się z gęstego klastra bardzo krótkich operacji ($p_{\text{single}} \in [0.5, 2]$ ms), które silnie przeciążają pojedynczy rdzeń procesora.

---

### Struktura katalogów i plików źródłowych
```text
.
├── Cargo.lock                    # Blokada wersji zależności bibliotecznych Cargo
├── Cargo.toml                    # Konfiguracja projektu Rust i definicje zależności
├── README.md                     # Główny dokument informacyjny projektu
├── esp32_tracer/                 # Komponent firmware w C: biblioteka hooks rejestrująca przełączenia RTOS
│   ├── CMakeLists.txt            # Definicje budowania dla systemu ESP-IDF
│   ├── freertos_hooks.h          # Hooki systemowe FreeRTOS przechwytujące zdarzenia
│   ├── README.md                 # Dokumentacja modułu diagnostycznego firmware
│   ├── SEGGER_RTT.c              # Implementacja niskopoziomowej biblioteki SEGGER RTT
│   ├── SEGGER_RTT_Conf.h         # Konfiguracja rozmiarów buforów i kanałów RTT
│   ├── SEGGER_RTT.h              # Plik nagłówkowy API SEGGER RTT
│   ├── task_tracer.c             # Logika rejestracji i formatowania pakietów zdarzeń
│   └── task_tracer.h             # Eksport makra tracer_record i definicji zdarzeń EVT_IN/EVT_OUT
├── Scenariusze/                  # Kody źródłowe firmware (C/FreeRTOS) generujące obciążenia testowe
│   ├── Zadania_1_5.c             # Scenariusz 1: pula n = 5 unikalnych zadań
│   ├── Zadania_1_10.c            # Scenariusz 1: pula n = 10 unikalnych zadań
│   ├── ...                       # Wykazy instancji dla kolejnych kroków n ∈ {5, 10, 20, 30, 50, 100}
│   ├── Zadania_4_10.c            # Scenariusz 4: pula n = 10 unikalnych zadań
│   └── ...                       # Pełne pokrycie profili od Scenariusza 1 do Scenariusza 8
└── src/                          # Komponent PC: oprogramowanie klienckie i analityczne (Rust)
    ├── main.rs                   # Punkt wejścia, inicjalizacja i uruchomienie okna egui
    ├── app.rs                    # Stan główny aplikacji, pętla zdarzeń UI i logika paneli
    ├── app/
    │   ├── tracer.rs             # Wątek w tle odbierający binarne pakiety za pośrednictwem probe-rs
    │   └── ui/                   # Implementacja i renderowanie paneli interfejsu (top, task, analysis)
    └── communication/
        ├── protocol.rs           # Definicja i parsowanie formatu pakietów RTT (nagłówek MAGIC, 34 bajty)
        ├── session.rs            # Nawiązanie sesji ze sprzętem przez probe-rs, odczyt bufora cyklicznego
        ├── scheduler.rs          # Parser strukturalny konwertujący surowe zdarzenia -> TaskExecution
        └── heuristics.rs         # Silnik analityczny: implementacja algorytmów (GreedySpdp, SplitOff, DuLeung)
```

## Dokumentacja biblioteki diagnostycznej esp32_tracer

Komponent `esp32_tracer` stanowi sprzętowo-programową podstawę systemu telemetrii. Jest to niskopoziomowa biblioteka napisana w języku C, zintegrowana z systemem operacyjnym FreeRTOS na mikrokontrolerze ESP32-P4. Odpowiada za natychmiastowe przechwytywanie zmian stanów procesów i bezblokowe przesyłanie surowych danych telemetrycznych przez interfejs JTAG/SWD do oprogramowania analizatora PC.

### Protokół binarny i struktura pakietu telemetrii
Transmisja danych diagnostycznych opiera się na stałych pakietach o rozmiarze dokładnie **34 bajtów**. Każda ramka jest ściśle upakowana przy użyciu atrybutu `__attribute__((packed))`, co eliminuje dopełnienia kompilatora (padding) i zapewnia idealną spójność ze strukturami danych dekodera w języku Rust.

Morfologia surowej ramki binarnej (`tracer_packet_t`):

| Przesunięcie (bity/bajty) | Typ danych | Nazwa pola | Opis |
| :--- | :--- | :--- | :--- |
| `0x00 - 0x01` | `uint8_t[2]` | `magic` | Stała sekwencja startowa synchronizacji ramki: `{0xAB, 0xCD}`. |
| `0x02` | `uint8_t` | `type` | Typ zdarzenia RTOS (`0x01`: `SWITCHED_IN`, `0x02`: `SWITCHED_OUT`). |
| `0x03` | `uint8_t` | `core_id` | Fizyczny rdzeń realizujący zadanie: `0` (Core 0) lub `1` (Core 1). |
| `0x04` | `uint8_t` | `priority` | Bieżący priorytet priorytetu zadania FreeRTOS. |
| `0x05 - 0x08` | `uint32_t` | `task_id` | Unikalny identyfikator zadania (rzutowany adres wskaźnika TCB). |
| `0x09 - 0x18` | `char[16]` | `name` | Tekstowa nazwa zadania, uzupełniona zerem (`\0`). |
| `0x19 - 0x20` | `uint64_t` | `timestamp_us` | Sprzętowy znacznik czasu w mikrosekundach od startu systemu. |
| `0x21` | `uint8_t` | `checksum` | Bitowa suma kontrolna XOR wszystkich poprzedzających 33 bajtów. |

### Specyfikacja API i funkcji bibliotecznych

#### 1. Inicjalizacja kanału telemetrii
```c
void tracer_init(void);
```
- Opis: Rezerwuje i konfiguruje dedykowany kanał `Kanał 1` protokołu SEGGER RTT pod nazwą `tracer`. Alokuje statyczny bufor kołowy pamięci RAM o rozmiarze dostosowanym do wielokrotności rozmiaru ramki ($512 \times 34\text{ B}$).
- Mechanizm bezpieczeństwa: Kanał zostaje zainicjalizowany w trybie sprzętowym `SEGGER_RTT_MODE_NO_BLOCK_SKIP`. Oznacza to, że w przypadku przepełnienia bufora kołowego (np. z powodu opóźnień transmisji JTAG), najstarsze pakiety są pomijane, co całkowicie zapobiega blokowaniu planisty FreeRTOS i gwarantuje brak deterministycznego narzutu na czas rzeczywisty aplikacji osadzonej.
- Wymagania: Funkcja musi zostać wywołana jednorazowo w sekcji startowej funkcji app_main, przed uruchomieniem planisty zadań FreeRTOS.

#### 2. Rejestracja zdarzenia RTOS
```c
void tracer_record(tracer_evt_t type, const char *name, uint8_t core_id, uint8_t priority);
```
- Opis: Tworzy binarny pakiet strukturalny w pamięci, automatycznie ekstrahuje uchwyt bieżącego zadania poprzez `xTaskGetCurrentTaskHandle()`, pobiera mikrosekundowy znacznik czasu wysokiej rozdzielczości z kontrolera `esp_timer_get_time()` oraz oblicza sumę kontrolną XOR za pomocą wewnętrznej funkcji pomocniczej `xor_checksum`.
- Zarządzanie współbieżnością: Funkcja jest w pełni bezpieczna pod kątem wielordzeniowości (multicore-safe) oraz bezpieczna do wywołania w kontekście obsługi przerwań sprzętowych (`ISR-safe`). Zapis do bufora za pomocą `SEGGER_RTT_Write` chroniony jest wewnętrznymi blokadami sprzętowymi typu spinlock na poziomie warstwy abstrakcji sprzętu (HAL).


## Dokumentacja użytkownika

Aplikacja została zaprojektowana tak, aby zapewnić intuicyjne, graficzne środowisko analityczne do badania szeregowania zadań systemowych. Ponieważ narzędzie nie posiada wbudowanego trybu symulacji offline z plików tekstowych, jego działanie opiera się wyłącznie na przetwarzaniu danych pobieranych na żywo ze sprzętu.

### Pierwsze kroki (Podłączenie i synchronizacja)
1. Do kodu źródłowego firmware wgranego na mikrokontroler (wybrany scenariusz z katalogu Scenariusze/) musi być włączona i skompilowana biblioteka `esp32_tracer`. Funkcja `tracer_init()`musi zostać wywołana na początku funkcji inicjalizującej system.
2. Uruchom aplikację `rtt_task_analyzer`.
3. Na górnym panelu kontrolnym kliknij przycisk `Połącz`.
4. Prawidłową synchronizację ze sprzętem sygnalizuje zmiana statusu połączenia oraz pojawienie się zielonego napisu `Połączono`.

---

### Panel Wykresy (Przycisk "Wykresy" na górnym panelu)
Po udanym nawiązaniu połączenia z mikrokontrolerem, aplikacja aktywuje panel wykresów prezentujący dane z bufora SEGGER RTT w czasie rzeczywistym. Widok interfejsu podzielony jest na funkcjonalne obszary:

* **Lewy panel:** Zawiera listę oraz typy wszystkich unikalnych zadań, które system operacyjny FreeRTOS aktualnie rejestruje na mikrokontrolerze.
* **Panel centralny (Wykres Gantta):** Prezentuje oś czasu, na której dynamicznie rysowane są bloki wykonania zadań z precyzyjnym podziałem na `Core 0` oraz `Core 1`. Każde zadanie posiada własny, generowany automatycznie kolor. Interfejs wspiera przybliżanie (Zoom) za pomocą rolki myszy oraz przesuwanie wykresu.
* **Dolna tabela zdarzeń:** Rejestruje chronologiczny i aktualizowany na bieżąco wykaz wykonanych procesów. Z tabeli można wyczytać szczegółowe parametry:
    * Nazwę zadania,
    * Czas rozpoczęcia zadania (`Start [us]`),
    * Czas zakończenia zadania (`Koniec [us]`),
    * Identyfikator rdzenia fizycznego, na którym proces się wykonał (`Rdzeń`),
    * Całkowity czas procesora przydzielony na to wykonanie (`Czas [us]`).
* **Prawy panel metryk:** Wyświetla podstawowe, zagregowane parametry wydajnościowe mikrokontrolera. Można tu monitorować bieżące procentowe obciążenie każdego z rdzeni, sumaryczną liczbę zidentyfikowanych unikalnych zadań oraz łączną ilość zarejestrowanych przełączeń kontekstu.

---

### Panel Analizy (Przycisk "Analiza" na górnym panelu)
Panel analizy pozwala na uruchomienie silnika optymalizacyjnego, który ewaluuje zebrane pakiety i oblicza alternatywne plany uszeregowania procesów.

#### Obsługa panelu analitycznego (UI)
W panelu sterowania analizą użytkownik ma do dyspozycji dwa tryby wykonywania obliczeń:
* ▶ **Uruchom wszystkie** — Uruchamia równolegle trzy zaimplementowane algorytmy matematyczne i generuje zbiorczą tabelę porównawczą.
* ▶ **Tylko wybrany** — Wykonuje wyłącznie jeden algorytm wskazany z listy rozwijanej umieszczonej obok. Opcja ta jest przydatna przy dużych zbiorach zadań do mierzenia czystego czasu wykonania algorytmu dokładnego bez czekania na zakończenie pracy heurystyk.

Po wywołaniu obliczeń aplikacja generuje tabelę wyników o następującej strukturze kolumnowej:
* `Algorytm` — Nazwa zastosowanej metody szeregowania.
* `Cmax [µs]` — Obliczony całkowity czas zakończenia uszeregowania (makespan).
* `Bazowy [µs]` — Rzeczywisty czas wykonania ($C_{max}$) przechwycony bezpośrednio ze śladu FreeRTOS na mikrokontrolerze (punkt odniesienia).
* `Poprawa` — Procentowy zysk wydajnościowy względem harmonogramu bazowego.
* `Czas algorytmu` — Dokładny czas, jaki procesor PC poświęcił na wykonanie obliczeń optymalizacyjnych (wyrażany dynamicznie w $ns / \mu s / ms / s$).

Bezpośrednio pod tabelą analityczną aplikacja generuje statyczny wykres Gantta odzwierciedlający strukturę i rozkład preempcji dla *aktualnie klikniętego/wybranego* z tabeli algorytmu.

#### Charakterystyka matematyczna i opis zaimplementowanych algorytmów
Wszystkie zaimplementowane metody rozwiązują problem szeregowania klasy **$P2 \mid pmtn, spdp\text{-}any \mid C_{max}$** w środowisku dwurdzeniowym ($M=2$). Model dopuszcza pełną preempcję (przerywanie i wznawianie zadań) oraz migrację między rdzeniami. Zadania mogą działać w jednym z dwóch trybów:
* **Tryb 1-rdzeniowy (Monolithic - $S_1$):** Zadanie w danym momencie wykonuje się na jednym, dowolnym rdzeniu.
* **Tryb 2-rdzeniowy (Parallelized/Dual - $S_2$):** Zadanie ze wsparciem dla równoległości wymaga jednoczesnego zajęcia obu rdzeni procesora, redukując swój fizyczny czas trwania na osi czasu.

W silniku analitycznym zaimplementowano trzy podejścia algorytmiczne:
##### 1. GreedySpdp
* **Złożoność obliczeniowa:** $O(n^2)$
* **Klasa:** Heurystyka lokalnego poszukiwania.
* **Zasada działania:** Algorytm rozpoczyna proces optymalizacji z założeniem, że wszystkie zarejestrowane zadania znajdują się w zbiorze jednotaktowym $S_1$ (tryb jedno-rdzeniowy). W każdej kolejnej iteracji algorytm symuluje przeniesienie pojedynczego zadania do zbioru $S_2$ (tryb dwu-rdzeniowy) i oblicza wpływ tej zmiany na globalny wskaźnik $C_{max}$. Wybierane jest to zadanie, którego relokacja daje największą natychmiastową redukcję czasu. Pętla poprawy wykonuje się tak długo, aż żadna pojedyncza zmiana trybu nie będzie w stanie skrócić harmonogramu.
* **Ograniczenie:** Ze względu na zachłanną naturę, algorytm może utknąć w lokalnym minimum matematycznym.

##### 2. SplitOff
* **Złożoność obliczeniowa:** $O(n^2)$
* **Klasa:** Heurystyka odciążania sterowana barierą (McNaughton-guided offload).
* **Zasada działania:** Algorytm wstępnie sortuje wszystkie zadania według ich czasów trwania w sposób malejący i planuje je jako zadania jedno-rdzeniowe. Zamiast testować losowe kombinacje zmian, algorytm oblicza teoretyczną dolną granicę dla zadań jedno-rdzeniowych na podstawie reguły wraparound McNaughtona: $\max(p_{\max}, \sum p_i / 2)$. Następnie identyfikuje precyzyjnie to zadanie (wąskie gardło), którego realizacja przekracza wyznaczoną granicę pojemności rdzenia i wymusza przerwanie (preempcję) oraz przerzucenie pracy. Wytypowane zadanie barierowe jest przenoszone do trybu dwu-rdzeniowego ($S_2$), co skraca całkowity czas trwania szczytu. Proces powtarza się iteracyjnie do momentu stabilizacji globalnego $C_{max}$.

##### 3. DuLeung1989
* **Złożoność obliczeniowa:** $O(n \cdot M^H)$, gdzie $M$ to liczba rdzeni (procesorów, $M=2$), a $H$ to liczba zadań kwalifikujących się do trybu dwu-rdzeniowego. W najgorszym przypadku ($H = n$) sprowadza się to do $O(n \cdot 2^n)$.
* **Klasa:** Algorytm dokładny (Exact) oparty na strategii Du i Leunga.
* **Zasada działania:** Algorytm gwarantuje znalezienie matematycznie optymalnego harmonogramu o absolutnie minimalnym $C_{max}$. Dokonuje on pełnej enumeracji (przeglądu wyczerpującego) całej przestrzeni stanów, analizując wszystkie $M^H$ możliwych kombinacji podziału zbioru zadań na tryby jedno- ($S_1$) i dwu-rdzeniowe ($S_2$). Każdy unikalny podział jest oceniany analitycznie przy użyciu preempcyjnej formuły matematycznej Błażewicza, a algorytm zwraca konfigurację o najniższym koszcie.
* **Ograniczenie:** Z uwagi na wykładniczy wzrost złożoności, algorytm jest w pełni praktyczny i stabilny czasowo dla systemów, gdzie liczba zadań wynosi $n \le 22$. W przypadku wykrycia większej liczby procesów ($n > 22$), aplikacja automatycznie uruchamia bezpieczny mechanizm fallback, rezygnując z algorytmu dokładnego na rzecz stabilnej heurystyki `SplitOff`, co zapobiega zawieszeniu interfejsu użytkownika (Timeout).