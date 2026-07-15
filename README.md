# RTT-Task Analyser

## Project Goal
**RTT-Task Analyser** is a graphical and analytical tool designed for visualizing, profiling, and evaluating task-scheduling algorithms in real-time operating systems (RTOS).

The application processes a live binary event stream sent over the SEGGER RTT protocol from a microcontroller (e.g., an ESP32-P4 running FreeRTOS). Its main output is a real-time Gantt chart for multiple cores, along with efficiency metrics — including the schedule completion time ($C_{max}$) computed using several heuristics (e.g., Baseline, Greedy SPDP, DuLeung).

## Requirements
**Hardware:**
* A microcontroller (e.g., ESP32-P4) flashed with firmware that generates RTT messages (test scenarios).
* A SEGGER J-Link compatible programmer/debugger.
* **Key requirement:** The application works **only** with a live microcontroller connected and an active RTT stream.

**Software:**
* Operating system: Linux.
* Rust toolchain installed (`stable` channel).
* SEGGER J-Link Software and Documentation Pack installed.
* On Linux, the following GUI development packages are required: `libgtk-3-dev`, `libxcb-render0-dev`, `libxcb-shape0-dev`, `libxcb-xfixes0-dev`.

## Installation and Setup
1. Clone the project repository:
```bash
   git clone https://github.com/marcel-gruszecki/RTT_analyzer
   cd RTT_analyzer
```
2. Install the required packages:
```bash
    sudo apt-get update
    sudo apt-get install -y libgtk-3-dev libxcb-render0-dev libxcb-shape0-dev libxcb-xfixes0-dev libxkbcommon-dev
```
3. Make sure you have a microcontroller connected with the appropriate firmware (test-scenario firmware) flashed, and the RTT server running.
## Running the Demo
1. Connect the J-Link debugger and the microcontroller to your computer.
2. Power up the board and initiate RTT transmission on the hardware side.
3. From the root directory of the cloned repository on your computer, run the analytics application:
```bash
cargo run --release
```
## Expected Result
Once the hardware is properly connected and the command has been run, a native graphical application window (built with the `egui` library) will open. On screen you should see:
- A central, live-drawn Gantt chart visualizing tasks on Core 0 and Core 1.
- An analytics panel showing the current completion-time ($C_{max}$) calculations for the test scenario currently being processed.
- An indicator confirming a successful connection to the RTT stream.

## Data
The project does not use any external databases or static batch/log files.
All data required for visualization and analysis is acquired live from the RTT stream generated
by the connected microcontroller. Data access relies on physically flashing the prepared C/FreeRTOS
test scenarios onto the microcontroller.

## Reproducing and Verifying Results
To verify the results, flash the microcontroller with the code representing a given test scenario
(e.g., a scenario with asymmetric load), then read the values computed in the application's Analysis Panel.
Verification consists of comparing the real-time results produced by the application against the reference
data contained in the `wyniki.xlsx` spreadsheet file included in the repository.
This file contains a summary of results and metrics for all supported scenarios. (Scenario execution times
may vary depending on hardware and configuration!)

## Tests
The analyzer's code does not implement standard automated unit tests (e.g. `cargo test`).
Testing and validation are carried out systemically using physical test scenarios.
- Running tests: Flash the chosen edge-case scenario onto the hardware and run the analyzer.
- Test reports: Official results and metrics from the scenario-based tests are documented
in the aforementioned `wyniki.xlsx` file.

## Documentation
The entire source code has been thoroughly documented using the standard `rustdoc` tool.
The documentation serves both as a user guide and as a specification of the software architecture
and maintenance instructions.
- Interactive technical documentation (Architecture and Maintenance): https://marcel-gruszecki.github.io/Dokumentacja_RTT/rtt_task_analyzer/index.html
From the link above you can explore the structure of the key modules:
- app - Graphical interface architecture.
- communication - RTT stream decoding engine and execution-time computation algorithms (heuristics).

### Meaning and Naming Convention of Test Scenarios
All source code implementing FreeRTOS test tasks for the ESP32-P4 microcontroller is located in the dedicated `Scenariusze/` folder. These files are named according to a consistent structural key:
`Zadania_[ScenarioNumber]_[NumberOfTasks].c`

*Example:* A file named `Zadania_1_5.c` means it contains the implementation of **Scenario 1** configured with a pool of **5 unique tasks** ($n=5$).

### Descriptions of the Research/Test Scenarios
Eight unique system-load profiles have been implemented in the scenarios directory to verify the robustness and flexibility of the implemented algorithms:

* **Scenario 1 (Symmetrical Baseline):** Acts as a symmetrical reference point in which tasks have uniform properties and are balanced. Single-core durations are generated within a predictable range ($p_{\text{single}} \in [10, 30]$ ms), while dual-core durations are halved ($p_{\text{dual}} \approx p_{\text{single}}/2$).
* **Scenario 2 (Asymmetrical Workload):** Represents an asymmetric load profile in which one core faces a very heavy computational load from long single-core tasks ($p_{\text{single}} \in [150, 300]$ ms), while the other core only handles single, isolated operations.
* **Scenario 3 (Linear Queue Scaling):** Arranges tasks sequentially on both cores from the shortest to the longest execution time (scaling linearly in the range from $5$ to $150$ ms) in order to analyze the behavior of basic processing queues.
* **Scenario 4 (High Volatility Execution):** Introduces high variance in process lengths. During system operation, very long ($p \in [200, 500]$ ms) and very short tasks ($p \in [1, 5]$ ms) constantly and alternately interleave along the timeline.
* **Scenario 5 (High Parallel Concentration):** Focuses on an environment with a high density of dual-core tasks (making up over $80\%$ of the total load), requiring tight barrier synchronization on both processors simultaneously, with minimal availability of single-core tasks.
* **Scenario 6 (Extreme Core Imbalance):** Models an extremely unbalanced system state, in which a long sequence of gradually increasing tasks (scaling from $10$ to $200$ ms) runs entirely on one core, while the other core remains completely idle.
* **Scenario 7 (Ultra-Short Cluster Overload):** Consists of a dense cluster of very short operations ($p_{\text{single}} \in [0.5, 2]$ ms) that heavily overload a single processor core.

---

### Directory and Source File Structure
```text
.
├── Cargo.lock                    # Cargo dependency version lock file
├── Cargo.toml                    # Rust project configuration and dependency definitions
├── README.md                     # Main project information document
├── esp32_tracer/                 # Firmware component in C: hooks library recording RTOS context switches
│   ├── CMakeLists.txt            # Build definitions for the ESP-IDF system
│   ├── freertos_hooks.h          # FreeRTOS system hooks capturing events
│   ├── README.md                 # Documentation for the firmware diagnostic module
│   ├── SEGGER_RTT.c              # Implementation of the low-level SEGGER RTT library
│   ├── SEGGER_RTT_Conf.h         # Configuration of RTT buffer sizes and channels
│   ├── SEGGER_RTT.h              # SEGGER RTT API header file
│   ├── task_tracer.c             # Event-packet recording and formatting logic
│   └── task_tracer.h             # Exports the tracer_record macro and EVT_IN/EVT_OUT event definitions
├── Scenariusze/                  # Firmware source code (C/FreeRTOS) generating test loads
│   ├── Zadania_1_5.c             # Scenario 1: pool of n = 5 unique tasks
│   ├── Zadania_1_10.c            # Scenario 1: pool of n = 10 unique tasks
│   ├── ...                       # Instance listings for subsequent steps n ∈ {5, 10, 20, 30, 50, 100}
│   ├── Zadania_4_10.c            # Scenario 4: pool of n = 10 unique tasks
│   └── ...                       # Full coverage of profiles from Scenario 1 to Scenario 8
└── src/                          # PC component: client and analytics software (Rust)
    ├── main.rs                   # Entry point, initialization, and launching of the egui window
    ├── app.rs                    # Main application state, UI event loop, and panel logic
    ├── app/
    │   ├── tracer.rs             # Background thread receiving binary packets via probe-rs
    │   └── ui/                   # Implementation and rendering of interface panels (top, task, analysis)
    └── communication/
        ├── protocol.rs           # Definition and parsing of the RTT packet format (MAGIC header, 34 bytes)
        ├── session.rs            # Establishing a session with hardware via probe-rs, reading the ring buffer
        ├── scheduler.rs          # Structural parser converting raw events -> TaskExecution
        └── heuristics.rs         # Analytics engine: implementation of the algorithms (GreedySpdp, SplitOff, DuLeung)
```

## esp32_tracer Diagnostic Library Documentation

The `esp32_tracer` component is the hardware/software foundation of the telemetry system. It is a low-level library written in C, integrated with the FreeRTOS operating system on the ESP32-P4 microcontroller. It is responsible for instantly capturing process state changes and for non-blocking transmission of raw telemetry data over the JTAG/SWD interface to the PC analyzer software.

### Binary Protocol and Telemetry Packet Structure
Diagnostic data transmission is based on fixed-size packets of exactly **34 bytes**. Each frame is tightly packed using the `__attribute__((packed))` attribute, which eliminates compiler padding and guarantees perfect consistency with the decoder's data structures in Rust.

Morphology of the raw binary frame (`tracer_packet_t`):

| Offset (bits/bytes) | Data Type | Field Name | Description |
| :--- | :--- | :--- | :--- |
| `0x00 - 0x01` | `uint8_t[2]` | `magic` | Fixed frame-sync start sequence: `{0xAB, 0xCD}`. |
| `0x02` | `uint8_t` | `type` | RTOS event type (`0x01`: `SWITCHED_IN`, `0x02`: `SWITCHED_OUT`). |
| `0x03` | `uint8_t` | `core_id` | Physical core executing the task: `0` (Core 0) or `1` (Core 1). |
| `0x04` | `uint8_t` | `priority` | Current priority of the FreeRTOS task. |
| `0x05 - 0x08` | `uint32_t` | `task_id` | Unique task identifier (cast address of the TCB pointer). |
| `0x09 - 0x18` | `char[16]` | `name` | Task name string, zero-padded (`\0`). |
| `0x19 - 0x20` | `uint64_t` | `timestamp_us` | Hardware timestamp in microseconds since system startup. |
| `0x21` | `uint8_t` | `checksum` | Bitwise XOR checksum of all preceding 33 bytes. |

### API and Library Function Specification

#### 1. Telemetry Channel Initialization
```c
void tracer_init(void);
```
- Description: Reserves and configures a dedicated SEGGER RTT `Channel 1` named `tracer`. Allocates a static RAM ring buffer sized to a multiple of the frame size ($512 \times 34\text{ B}$).
- Safety mechanism: The channel is initialized in the `SEGGER_RTT_MODE_NO_BLOCK_SKIP` hardware mode. This means that if the ring buffer overflows (e.g., due to JTAG transmission delays), the oldest packets are dropped, which fully prevents blocking the FreeRTOS scheduler and guarantees no deterministic overhead on the embedded application's real-time behavior.
- Requirements: The function must be called exactly once in the startup section of `app_main`, before the FreeRTOS task scheduler is started.

#### 2. RTOS Event Recording
```c
void tracer_record(tracer_evt_t type, const char *name, uint8_t core_id, uint8_t priority);
```
- Description: Builds a binary struct packet in memory, automatically extracts the handle of the current task via `xTaskGetCurrentTaskHandle()`, retrieves a high-resolution microsecond timestamp from `esp_timer_get_time()`, and computes the XOR checksum using the internal helper function `xor_checksum`.
- Concurrency handling: The function is fully multicore-safe and ISR-safe. Writing to the buffer via `SEGGER_RTT_Write` is protected by internal hardware spinlocks at the Hardware Abstraction Layer (HAL) level.


## User Documentation

The application is designed to provide an intuitive, graphical analytical environment for studying system task scheduling. Since the tool has no built-in offline simulation mode from text files, its operation relies exclusively on processing data acquired live from hardware.

### First Steps (Connection and Synchronization)
1. The firmware source code flashed onto the microcontroller (the chosen scenario from the `Scenariusze/` directory) must have the `esp32_tracer` library enabled and compiled in. The `tracer_init()` function must be called at the beginning of the system's initialization function.
2. Run the `rtt_task_analyzer` application.
3. On the top control panel, click the `Connect` button.
4. Successful synchronization with the hardware is signaled by a change in connection status and the appearance of the green `Connected` label.

---

### Charts Panel (the "Charts" button on the top panel)
Once a connection to the microcontroller has been successfully established, the application activates the chart panel, presenting data from the SEGGER RTT buffer in real time. The interface view is divided into functional areas:

* **Left panel:** Contains a list and the types of all unique tasks that the FreeRTOS operating system is currently registering on the microcontroller.
* **Central panel (Gantt Chart):** Shows a timeline on which task-execution blocks are drawn dynamically, precisely split between `Core 0` and `Core 1`. Each task has its own automatically generated color. The interface supports zooming via the mouse wheel and panning the chart.
* **Bottom event table:** Records a chronological, continuously updated list of executed processes. From the table you can read detailed parameters:
    * Task name,
    * Task start time (`Start [us]`),
    * Task end time (`End [us]`),
    * Identifier of the physical core the process ran on (`Core`),
    * Total CPU time allocated to this execution (`Time [us]`).
* **Right metrics panel:** Displays basic, aggregated performance parameters of the microcontroller. Here you can monitor the current percentage load of each core, the total number of identified unique tasks, and the total number of recorded context switches.

---

### Analysis Panel (the "Analysis" button on the top panel)
The analysis panel allows you to run the optimization engine, which evaluates the collected packets and computes alternative process-scheduling plans.

#### Analysis Panel Controls (UI)
In the analysis control panel, the user has two computation modes available:
* ▶ **Run All** — Runs all three implemented mathematical algorithms in parallel and generates a combined comparison table.
* ▶ **Selected Only** — Runs only a single algorithm chosen from the adjacent dropdown list. This option is useful for large task sets, to measure the pure execution time of the exact algorithm without waiting for the heuristics to finish.

After the computation is triggered, the application generates a results table with the following column structure:
* `Algorithm` — Name of the applied scheduling method.
* `Cmax [µs]` — The computed total schedule completion time (makespan).
* `Baseline [µs]` — The actual execution time ($C_{max}$) captured directly from the FreeRTOS trace on the microcontroller (reference point).
* `Improvement` — Percentage performance gain relative to the baseline schedule.
* `Algorithm Time` — The exact time the PC's processor spent performing the optimization computation (expressed dynamically in $ns / \mu s / ms / s$).

Directly below the analysis table, the application generates a static Gantt chart reflecting the structure and preemption layout for the *currently clicked/selected* algorithm from the table.

#### Mathematical Characteristics and Description of the Implemented Algorithms
All implemented methods solve the scheduling problem of class **$P2 \mid pmtn, spdp\text{-}any \mid C_{max}$** in a dual-core environment ($M=2$). The model allows full preemption (interrupting and resuming tasks) and migration between cores. Tasks can operate in one of two modes:
* **Single-core mode (Monolithic - $S_1$):** At a given moment, the task runs on a single, arbitrary core.
* **Dual-core mode (Parallelized/Dual - $S_2$):** A task with parallelism support requires simultaneous occupation of both processor cores, reducing its physical duration on the timeline.

Three algorithmic approaches are implemented in the analytics engine:
##### 1. GreedySpdp
* **Computational complexity:** $O(n^2)$
* **Class:** Local-search heuristic.
* **Operating principle:** The algorithm starts the optimization process assuming that all recorded tasks are in the single-mode set $S_1$ (single-core mode). In each subsequent iteration, the algorithm simulates moving a single task to set $S_2$ (dual-core mode) and computes the impact of that change on the global $C_{max}$ metric. The task whose relocation yields the greatest immediate time reduction is selected. The improvement loop runs until no single mode change is able to shorten the schedule any further.
* **Limitation:** Due to its greedy nature, the algorithm can get stuck in a local mathematical minimum.

##### 2. SplitOff
* **Computational complexity:** $O(n^2)$
* **Class:** Barrier-driven offloading heuristic (McNaughton-guided offload).
* **Operating principle:** The algorithm initially sorts all tasks by duration in descending order and schedules them as single-core tasks. Instead of testing random combinations of changes, the algorithm computes the theoretical lower bound for single-core tasks based on McNaughton's wraparound rule: $\max(p_{\max}, \sum p_i / 2)$. It then precisely identifies the task (the bottleneck) whose execution exceeds the determined core-capacity bound and forces a preemption and workload shift. The identified barrier task is moved to dual-core mode ($S_2$), which shortens the total peak duration. The process repeats iteratively until the global $C_{max}$ stabilizes.

##### 3. DuLeung1989
* **Computational complexity:** $O(n \cdot M^H)$, where $M$ is the number of cores (processors, $M=2$) and $H$ is the number of tasks eligible for dual-core mode. In the worst case ($H = n$) this reduces to $O(n \cdot 2^n)$.
* **Class:** Exact algorithm based on the Du and Leung strategy.
* **Operating principle:** The algorithm guarantees finding the mathematically optimal schedule with an absolutely minimal $C_{max}$. It performs a full enumeration (exhaustive search) of the entire state space, analyzing all $M^H$ possible combinations of splitting the task set into single- ($S_1$) and dual-core ($S_2$) modes. Each unique split is evaluated analytically using Błażewicz's preemptive mathematical formula, and the algorithm returns the configuration with the lowest cost.
* **Limitation:** Due to the exponential growth of complexity, the algorithm is fully practical and stable in terms of timing for systems where the number of tasks is $n \le 22$. If a larger number of processes is detected ($n > 22$), the application automatically triggers a safe fallback mechanism, giving up the exact algorithm in favor of the stable `SplitOff` heuristic, which prevents the user interface from hanging (timeout).
