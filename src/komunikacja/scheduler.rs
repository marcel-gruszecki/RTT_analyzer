use std::collections::HashMap;

// ── Events ────────────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventKind {
    SwitchedIn,
    SwitchedOut,
}

#[derive(Debug, Clone)]
pub struct TaskEvent {
    pub task_id:      u32,
    pub name:         String,
    pub core_id:      u8,
    pub priority:     u8,
    pub timestamp_us: u64,
    pub kind:         EventKind,
}

// ── Completed execution window ────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct TaskExecution {
    pub task_id:  u32,
    pub name:     String,
    pub core_id:  u8,
    pub priority: u8,
    pub start_us: u64,
    pub end_us:   u64,
}

impl TaskExecution {
    pub fn duration_us(&self) -> u64 {
        self.end_us.saturating_sub(self.start_us)
    }
}

// ── Scheduling data — input for heuristic algorithms ─────────────────────────

/// Aggregated statistics for one task instance.
///
/// Ready-to-use by scheduling algorithms (RM, EDF, genetic, etc.):
/// - `wcet_us` / `avg_exec_us` / `bcet_us` for execution-time modelling
/// - `period_us` for periodicity-based algorithms (RM, EDF)
/// - `cpu_load` = avg_exec / period for utilisation bounds
#[derive(Debug, Clone)]
pub struct TaskScheduleData {
    pub task_id:      u32,
    pub name:         String,
    pub priority:     u8,
    pub core_id:      u8,

    /// Number of complete IN→OUT windows observed so far.
    pub sample_count: usize,

    /// Worst / best / average execution time in µs.
    pub wcet_us:     u64,
    pub bcet_us:     u64,
    pub avg_exec_us: f64,

    /// Estimated task period (µs) from inter-arrival times of SwitchedIn events.
    /// `None` until at least one inter-arrival gap has been observed.
    pub period_us: Option<f64>,

    /// CPU utilization: `avg_exec_us / period_us`.
    /// `None` when period is unknown.
    pub cpu_load: Option<f64>,
}

// ── Internal per-task accumulator ─────────────────────────────────────────────

struct Accumulator {
    name:           String,
    core_id:        u8,
    priority:       u8,
    durations_us:   Vec<u64>,
    last_in_ts:     Option<u64>,
    inter_arrivals: Vec<u64>,
}

impl Accumulator {
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
            name:         self.name.clone(),
            priority:     self.priority,
            core_id:      self.core_id,
            sample_count: n,
            wcet_us:      wcet,
            bcet_us:      bcet,
            avg_exec_us:  avg,
            period_us:    period,
            cpu_load,
        }
    }
}

// ── Task database ─────────────────────────────────────────────────────────────

/// Stateful tracker that pairs IN/OUT events into executions and accumulates
/// statistics. Feed raw events with `push_event`; query with `schedule_data`.
pub struct TaskDatabase {
    /// `task_id` → (start_us, core_id) of the still-open SwitchedIn event.
    pending:         HashMap<u32, (u64, u8)>,
    /// `core_id` → task_id currently occupying that core.
    /// A single core can run only one task — any new SwitchedIn implicitly
    /// closes whatever was pending on that core (handles dropped OUT events).
    pending_by_core: HashMap<u8, u32>,
    accumulators:    HashMap<u32, Accumulator>,

    /// All completed executions in arrival order.
    pub completed: Vec<TaskExecution>,
}

impl TaskDatabase {
    pub fn new() -> Self {
        Self {
            pending:         HashMap::new(),
            pending_by_core: HashMap::new(),
            accumulators:    HashMap::new(),
            completed:       Vec::new(),
        }
    }

    /// Feed one event. Returns a `TaskExecution` when an IN→OUT pair closes.
    pub fn push_event(&mut self, event: TaskEvent) -> Option<TaskExecution> {
        match event.kind {
            EventKind::SwitchedIn => {
                // Close whatever was running on this core.
                // Handles dropped OUT events: on a single core only one task
                // can run at a time, so a new SwitchedIn implies the previous
                // task switched out at this exact timestamp.
                let prev_id = self.pending_by_core.get(&event.core_id).copied();
                if let Some(prev_id) = prev_id {
                    if prev_id != event.task_id {
                        if let Some((lost_start, lost_core)) = self.pending.remove(&prev_id) {
                            let (name, priority) = self.accumulators
                                .get(&prev_id)
                                .map(|a| (a.name.clone(), a.priority))
                                .unwrap_or_else(|| (String::new(), 0));
                            let exec = TaskExecution {
                                task_id:  prev_id,
                                name,
                                core_id:  lost_core,
                                priority,
                                start_us: lost_start,
                                end_us:   event.timestamp_us,
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
                        name:           event.name.clone(),
                        core_id:        event.core_id,
                        priority:       event.priority,
                        durations_us:   Vec::new(),
                        last_in_ts:     None,
                        inter_arrivals: Vec::new(),
                    });
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
                        name:           event.name.clone(),
                        core_id,
                        priority:       event.priority,
                        durations_us:   Vec::new(),
                        last_in_ts:     None,
                        inter_arrivals: Vec::new(),
                    });
                let exec = TaskExecution {
                    task_id:  event.task_id,
                    name:     event.name,
                    core_id,
                    priority: event.priority,
                    start_us,
                    end_us:   event.timestamp_us,
                };
                acc.durations_us.push(exec.duration_us());
                self.completed.push(exec.clone());
                Some(exec)
            }
        }
    }

    /// Current scheduling statistics for every observed task.
    /// Call this at any time — the data reflects all events pushed so far.
    pub fn schedule_data(&self) -> Vec<TaskScheduleData> {
        self.accumulators
            .iter()
            .map(|(&id, acc)| acc.schedule_data(id))
            .collect()
    }
}
