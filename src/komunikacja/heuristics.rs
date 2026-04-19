//! Heuristic scheduling algorithms for problem P | pmtn, spdp-any | Cmax.
//!
//! Każde zmierzone wykonanie zadania (`TaskExecution`) traktowane jest jako
//! osobne zadanie j z czasem przetwarzania p_j = duration_us.
//!
//! M = 2 maszyny (rdzenie ESP32-P4).
//! Cel: minimalizacja Cmax = czasu zakończenia ostatniego zadania.

use std::collections::{HashMap, HashSet};

use super::scheduler::TaskExecution;

const M: usize = 2;

/// Domyślny udział równoległy gdy brak danych wielordzeniowych.
/// 0.5 = zakładamy że 50% zadania można wykonać równolegle (podejście zachowawcze).
const DEFAULT_PAR: f64 = 0.5;

// ── Typy wynikowe ─────────────────────────────────────────────────────────────

/// Fragment zadania przydzielony do konkretnego rdzenia i przedziału czasu.
#[derive(Debug, Clone)]
pub struct Segment {
    pub name:     String,
    pub core:     u8,
    pub start_us: f64,
    pub end_us:   f64,
}

impl Segment {
    pub fn duration_us(&self) -> f64 { self.end_us - self.start_us }
}

#[derive(Debug)]
pub struct SchedulingResult {
    pub algorithm:   &'static str,
    pub description: String,
    /// Cmax wyznaczony przez algorytm.
    pub cmax_us:     f64,
    /// Cmax bazowy (obecne przypisanie z pomiarów).
    pub baseline_us: f64,
    /// (baseline - cmax) / baseline.  Dodatnia = poprawa, ujemna = pogorszenie.
    pub improvement: f64,
    /// Szczegółowe przypisania (może być pusta dla dużych zbiorów).
    pub segments:    Vec<Segment>,
}

impl SchedulingResult {
    pub fn improvement_pct(&self) -> f64 { self.improvement * 100.0 }
}

// ── Linia bazowa ──────────────────────────────────────────────────────────────

/// Cmax obecnego przypisania z pomiarów — każde wykonanie zostaje na rdzeniu
/// na którym zostało zaobserwowane, kolejkowane sekwencyjnie na tym rdzeniu.
pub fn baseline_cmax(execs: &[TaskExecution]) -> f64 {
    let mut load = [0.0f64; M];
    for e in execs {
        load[(e.core_id as usize) % M] += e.duration_us() as f64;
    }
    load.iter().cloned().fold(0.0f64, f64::max)
}

// ── Pomocnicza: udział równoległy per nazwa zadania ───────────────────────────

/// Zwraca szacowany udział równoległy f ∈ (0,1) per nazwa zadania.
/// Zadania obserwowane na obu rdzeniach → f = 0.85 (już parallelizowane).
/// Pozostałe → DEFAULT_PAR.
fn par_fracs(execs: &[TaskExecution]) -> HashMap<String, f64> {
    let mut name_cores: HashMap<&str, HashSet<u8>> = HashMap::new();
    for e in execs {
        name_cores.entry(e.name.as_str()).or_default().insert(e.core_id);
    }
    let mut result: HashMap<String, f64> = HashMap::new();
    for e in execs {
        result.entry(e.name.clone()).or_insert_with(|| {
            let multi = name_cores.get(e.name.as_str()).map_or(false, |s| s.len() > 1);
            if multi { 0.85 } else { DEFAULT_PAR }
        });
    }
    result
}

// ── Algorytm 1: McNaughton ────────────────────────────────────────────────────

/// **McNaughton (P | pmtn | Cmax) — algorytm optymalny**
///
/// Dla M identycznych maszyn z preempcją wyznacza dolną granicę Cmax:
///
///   Cmax* = max(max p_j,  Σp_j / M)
///
/// Następnie wypełnia maszynę 0 od lewej do Cmax*, a nadmiar "zawija" na
/// maszynę 1 (wrap-around). Każde zadanie jest dzielone **co najwyżej raz**.
/// Złożoność: O(n log n) dla sortowania, O(n) dla przydziału.
pub fn mcnaughton(execs: &[TaskExecution]) -> SchedulingResult {
    let baseline = baseline_cmax(execs);
    let total: f64 = execs.iter().map(|e| e.duration_us() as f64).sum();
    let pmax:  f64 = execs.iter().map(|e| e.duration_us() as f64).fold(0.0f64, f64::max);
    let cmax = f64::max(pmax, total / M as f64);

    let mut segs = Vec::new();
    let mut core = 0usize;
    let mut pos  = 0.0f64;

    let mut sorted: Vec<&TaskExecution> = execs.iter().collect();
    sorted.sort_by(|a, b| b.duration_us().cmp(&a.duration_us()));

    for e in sorted {
        let mut rem = e.duration_us() as f64;
        while rem > 1e-9 {
            let slice = rem.min(cmax - pos);
            segs.push(Segment {
                name: e.name.clone(), core: core as u8,
                start_us: pos, end_us: pos + slice,
            });
            pos += slice;
            rem -= slice;
            if rem > 1e-9 {
                core = (core + 1) % M;
                pos  = 0.0;
            }
        }
    }

    SchedulingResult {
        algorithm:   "McNaughton",
        description: "P | pmtn | Cmax — optymalny; Cmax* = max(max p_j, Σp_j/M)".into(),
        cmax_us: cmax, baseline_us: baseline,
        improvement: (baseline - cmax) / baseline,
        segments: segs,
    }
}

// ── Algorytm 2: LPT ──────────────────────────────────────────────────────────

/// **LPT — Longest Processing Time First (P || Cmax)**
///
/// Zachłanny heurystyk bez preempcji:
/// 1. Posortuj zadania malejąco według p_j.
/// 2. Przydziel każde do najlżej obciążonego rdzenia.
///
/// Gwarancja aproksymacji: Cmax_LPT / Cmax* ≤ 4/3 − 1/(3M).
/// Dla M=2 oznacza to co najwyżej ~17% powyżej optimum.
/// Złożoność: O(n log n).
pub fn lpt(execs: &[TaskExecution]) -> SchedulingResult {
    let baseline = baseline_cmax(execs);

    let mut sorted: Vec<&TaskExecution> = execs.iter().collect();
    sorted.sort_by(|a, b| b.duration_us().cmp(&a.duration_us()));

    let mut load = [0.0f64; M];
    let mut segs  = Vec::new();

    for e in sorted {
        let core = if load[0] <= load[1] { 0 } else { 1 };
        let p = e.duration_us() as f64;
        segs.push(Segment {
            name: e.name.clone(), core: core as u8,
            start_us: load[core], end_us: load[core] + p,
        });
        load[core] += p;
    }

    let cmax = load.iter().cloned().fold(0.0f64, f64::max);
    SchedulingResult {
        algorithm:   "LPT",
        description: "P || Cmax — zachłanny LPT bez preempcji, aproks. ≤ 4/3".into(),
        cmax_us: cmax, baseline_us: baseline,
        improvement: (baseline - cmax) / baseline,
        segments: segs,
    }
}

// ── Algorytm 3: Split spdp-any ────────────────────────────────────────────────

/// **Split spdp-any (P | pmtn, spdp-any | Cmax)**
///
/// Rozszerzenie problemu: zadania mogą być wykonywane na wielu rdzeniach
/// jednocześnie (modeled jako „malleable jobs"). Udział równoległy f wyznacza
/// jak dużą część zadania można rozłożyć na M rdzeni.
///
/// Efektywny czas wykonania (prawo Amdahla dla M rdzeni):
///
///   p_eff = p × (1 − f/M)
///
/// Dla f = 0.5, M = 2:  p_eff = p × 0.75  (25% szybciej niż bez podziału).
/// Dla f = 0.85, M = 2: p_eff = p × 0.575 (42% szybciej).
///
/// Po wyznaczeniu p_eff stosuje wrap-around McNaughtona.
pub fn split_spdp(execs: &[TaskExecution]) -> SchedulingResult {
    let baseline = baseline_cmax(execs);
    let fracs    = par_fracs(execs);

    let eff: Vec<(&TaskExecution, f64, f64)> = execs.iter().map(|e| {
        let f    = fracs[&e.name];
        let peff = e.duration_us() as f64 * (1.0 - f / M as f64);
        (e, f, peff)
    }).collect();

    let total: f64 = eff.iter().map(|(_, _, pe)| pe).sum();
    let pmax:  f64 = eff.iter().map(|(_, _, pe)| *pe).fold(0.0f64, f64::max);
    let cmax = f64::max(pmax, total / M as f64);

    let mut segs = Vec::new();
    let mut core = 0usize;
    let mut pos  = 0.0f64;

    let mut sorted = eff;
    sorted.sort_by(|a, b| b.2.partial_cmp(&a.2).unwrap());

    for (e, f, peff) in sorted {
        let label = format!("{}[∥{:.0}%]", e.name, f * 100.0);
        let mut rem = peff;
        while rem > 1e-9 {
            let slice = rem.min(cmax - pos);
            segs.push(Segment {
                name: label.clone(), core: core as u8,
                start_us: pos, end_us: pos + slice,
            });
            pos += slice;
            rem -= slice;
            if rem > 1e-9 {
                core = (core + 1) % M;
                pos  = 0.0;
            }
        }
    }

    SchedulingResult {
        algorithm:   "Split spdp-any",
        description: format!(
            "P | pmtn, spdp-any | Cmax — Amdahl, domyślny udział równoległy: {:.0}%",
            DEFAULT_PAR * 100.0
        ),
        cmax_us: cmax, baseline_us: baseline,
        improvement: (baseline - cmax) / baseline,
        segments: segs,
    }
}

// ── Uruchom wszystkie ─────────────────────────────────────────────────────────

pub fn run_all(execs: &[TaskExecution]) -> Vec<SchedulingResult> {
    vec![mcnaughton(execs), lpt(execs), split_spdp(execs)]
}
