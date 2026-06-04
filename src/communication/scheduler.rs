//! # Baza Danych i Akumulator Statystyk Zadań
//!
//! Projekt: RTT-Task Analyser
//! Autor: Marcel Gruszecki (UAM)
//! Moduł: `communication::scheduler`
//! Opis: Odpowiada za śledzenie stanu wykonania zadań w systemie. Paruje binarne zdarzenia
//!       wejścia/wyjścia w pełne okna wykonania oraz oblicza parametry
//!       statystyczne systemów czasu rzeczywistego.

use std::collections::HashMap;

/// Rodzaj zdarzenia przełączenia kontekstu (Context Switch).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventKind {
    SwitchedIn,
    SwitchedOut,
}

/// Reprezentuje pojedyncze surowe zdarzenie przełączenia wyemitowane przez mikrokontroler.
#[derive(Debug, Clone)]
pub struct TaskEvent {
    pub task_id: u32,
    pub name: String,
    pub core_id: u8,
    pub priority: u8,
    pub timestamp_us: u64,
    pub kind: EventKind,
}

/// Zamknięte okno czasowe wykonania danego zadania na rdzeniu.
#[derive(Debug, Clone)]
pub struct TaskExecution {
    pub task_id: u32,
    pub name: String,
    pub core_id: u8,
    #[allow(dead_code)]
    pub priority: u8,
    pub start_us: u64,
    pub end_us: u64,
}

impl TaskExecution {
    pub fn duration_us(&self) -> u64 {
        self.end_us.saturating_sub(self.start_us)
    }
}

/// Zintegrowane dane statystyczne zadania, stanowiące dane wejściowe dla algorytmów szeregowania.
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct TaskScheduleData {
    pub task_id: u32,
    pub name: String,
    pub priority: u8,
    pub core_id: u8,
    pub sample_count: usize,
    pub wcet_us: u64,
    pub bcet_us: u64,
    pub avg_exec_us: f64,
    pub period_us: Option<f64>,
    pub cpu_load: Option<f64>,
}

// Wewnętrzny akumulator czasu trwania i odstępów czasowych dla pojedynczego zadania
struct Accumulator {
    name: String,
    core_id: u8,
    priority: u8,
    durations_us: Vec<u64>,
    last_in_ts: Option<u64>,
    inter_arrivals: Vec<u64>,
}

impl Accumulator {
    // Agreguje zebrane próbki i generuje strukturę danych dla harmonogramu
    fn schedule_data(&self, task_id: u32) -> TaskScheduleData {
        let n = self.durations_us.len();
        let (wcet, bcet, avg) = if n == 0 {
            (0, 0, 0.0)
        } else {
            let w = *self.durations_us.iter().max().unwrap();
            let b = *self.durations_us.iter().min().unwrap();
            let a = self.durations_us.iter().sum::<u64>() as f64 / n as f64;
            (w, b, a)
        };

        let period = if !self.inter_arrivals.is_empty() {
            let sum: u64 = self.inter_arrivals.iter().sum();
            Some(sum as f64 / self.inter_arrivals.len() as f64)
        } else {
            None
        };

        let cpu_load = period.map(|p| if p > 0.0 { avg / p } else { 0.0 });

        TaskScheduleData {
            task_id,
            name: self.name.clone(),
            priority: self.priority,
            core_id: self.core_id,
            sample_count: n,
            wcet_us: wcet,
            bcet_us: bcet,
            avg_exec_us: avg,
            period_us: period,
            cpu_load,
        }
    }
}

/// Monitor stanowy, który paruje zdarzenia i buduje bazę wykonanych zadań w czasie rzeczywistym.
pub struct TaskDatabase {
    pending: HashMap<u32, (u64, u8)>,
    pending_by_core: HashMap<u8, u32>,
    accumulators: HashMap<u32, Accumulator>,
    pub completed: Vec<TaskExecution>,
}

impl TaskDatabase {
    pub fn new() -> Self {
        Self {
            pending: HashMap::new(),
            pending_by_core: HashMap::new(),
            accumulators: HashMap::new(),
            completed: Vec::new(),
        }
    }

    /// Przyjmuje nowe zdarzenie. Jeśli zamknie ono parę IN->OUT, zwraca obiekt wykonania zadania.
    pub fn push_event(&mut self, event: TaskEvent) -> Option<TaskExecution> {
        match event.kind {
            EventKind::SwitchedIn => {
                // Automatyczne domykanie zadań na tym samym rdzeniu (obsługa zgubionych pakietów OUT)
                let prev_id = self.pending_by_core.get(&event.core_id).copied();
                if let Some(prev_id) = prev_id {
                    if prev_id != event.task_id {
                        if let Some((lost_start, lost_core)) = self.pending.remove(&prev_id) {
                            let (name, priority) = self.accumulators
                                .get(&prev_id)
                                .map(|a| (a.name.clone(), a.priority))
                                .unwrap_or_else(|| (String::new(), 0));

                            let exec = TaskExecution {
                                task_id: prev_id,
                                name,
                                core_id: lost_core,
                                priority,
                                start_us: lost_start,
                                end_us: event.timestamp_us,
                            };
                            let dur = exec.duration_us();
                            self.completed.push(exec);
                            if let Some(a) = self.accumulators.get_mut(&prev_id) {
                                a.durations_us.push(dur);
                            }
                        }
                    }
                }

                self.pending_by_core.insert(event.core_id, event.task_id);
                self.pending.insert(event.task_id, (event.timestamp_us, event.core_id));

                let acc = self.accumulators
                    .entry(event.task_id)
                    .or_insert_with(|| Accumulator {
                        name: event.name.clone(),
                        core_id: event.core_id,
                        priority: event.priority,
                        durations_us: Vec::new(),
                        last_in_ts: None,
                        inter_arrivals: Vec::new(),
                    });

                // Obliczanie czasu inter-arrival (okresu napływu zadań)
                if let Some(prev) = acc.last_in_ts {
                    acc.inter_arrivals.push(event.timestamp_us.saturating_sub(prev));
                }
                acc.last_in_ts = Some(event.timestamp_us);
                None
            }

            EventKind::SwitchedOut => {
                let (start_us, core_id) = self.pending.remove(&event.task_id)?;
                if self.pending_by_core.get(&core_id).copied() == Some(event.task_id) {
                    self.pending_by_core.remove(&core_id);
                }

                let acc = self.accumulators
                    .entry(event.task_id)
                    .or_insert_with(|| Accumulator {
                        name: event.name.clone(),
                        core_id,
                        priority: event.priority,
                        durations_us: Vec::new(),
                        last_in_ts: None,
                        inter_arrivals: Vec::new(),
                    });

                let exec = TaskExecution {
                    task_id: event.task_id,
                    name: event.name,
                    core_id,
                    priority: event.priority,
                    start_us,
                    end_us: event.timestamp_us,
                };

                acc.durations_us.push(exec.duration_us());
                self.completed.push(exec.clone());
                Some(exec)
            }
        }
    }

    /// Pobiera bieżący wektor statystyk dla wszystkich zarejestrowanych zadań.
    pub fn schedule_data(&self) -> Vec<TaskScheduleData> {
        self.accumulators
            .iter()
            .map(|(&id, acc)| acc.schedule_data(id))
            .collect()
    }
}