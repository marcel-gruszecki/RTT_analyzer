//! Algorytmy harmonogramowania dla problemu P | pmtn, spdp-any | Cmax (M = 2).
//!
//! Każde zmierzone wykonanie zadania (`TaskExecution`) traktowane jest jako
//! osobne zadanie j z czasem przetwarzania p_j = duration_us. Mediana per nazwa
//! daje reprezentatywny czas jednego cyklu.
//!
//! Cel: minimalizacja Cmax = czasu zakończenia ostatniego zadania.
//!
//! Każde zadanie ma do wyboru tryb:
//!   - **1-rdzeniowy** (single): biegnie p_j na 1 procesorze
//!   - **2-rdzeniowy** (dual):   biegnie LIN_FACTOR · p_j na obu jednocześnie
//!
//! ## Algorytmy
//!
//! 1. **GreedySpdp** — heurystyk zachłanny (przenosi zadania do S2 dopóki Cmax maleje)
//! 2. **SplitOff**   — heurystyk McNaughton-guided (wraparound task → S2)
//! 3. **DuLeung1989** — OPTYMALNY: brute force po 2ⁿ przypisaniach trybów.
//!    Du J., Leung J.Y-T., "Complexity of Scheduling Parallel Task Systems",
//!    SIAM J. Disc. Math. 2(4), 1989, pp. 473-487 (Theorem 6).

use std::time::Instant;
use super::scheduler::TaskExecution;

const M: usize = 2;

/// Współczynnik liniowy spdp-lin: czas ścienny na 2 rdzeniach = 60% czasu 1-rdz.
const LIN_FACTOR: f64 = 0.6;

// ── Typy wynikowe ─────────────────────────────────────────────────────────────

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
    pub algorithm:    &'static str,
    pub description:  String,
    /// Cmax wyznaczony przez algorytm.
    pub cmax_us:      f64,
    /// Cmax bazowy (obecne przypisanie z pomiarów).
    pub baseline_us:  f64,
    /// (baseline - cmax) / baseline. Dodatnia = poprawa, ujemna = pogorszenie.
    pub improvement:  f64,
    /// Czas wykonania samego algorytmu (preprocessing + optymalizacja) w nanosekundach.
    pub algo_time_ns: u128,
    /// Szczegółowe przypisania.
    pub segments:     Vec<Segment>,
}

impl SchedulingResult {
    pub fn improvement_pct(&self) -> f64 { self.improvement * 100.0 }
}

// ── Agregacja po nazwie zadania ───────────────────────────────────────────────

/// Mediana czasu wykonania per zadanie (nazwa). Posortowane malejąco wg p.
fn aggregate_by_task(execs: &[TaskExecution]) -> Vec<(String, f64)> {
    use std::collections::HashMap;
    let mut map: HashMap<&str, Vec<f64>> = HashMap::new();
    for e in execs {
        map.entry(&e.name).or_default().push(e.duration_us() as f64);
    }
    let mut result: Vec<(String, f64)> = map.into_iter()
        .map(|(name, mut vals)| {
            vals.sort_by(|a, b| a.partial_cmp(b).unwrap());
            let median = vals[vals.len() / 2];
            (name.to_string(), median)
        })
        .collect();
    result.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap());
    result
}

// ── Linia bazowa ──────────────────────────────────────────────────────────────

/// Cmax obserwowany przez OS: każde zadanie zostaje na rdzeniu, na którym
/// zostało pierwszy raz zaobserwowane; sumujemy mediany per rdzeń, bierzemy max.
pub fn baseline_cmax(execs: &[TaskExecution]) -> f64 {
    use std::collections::HashMap;
    let mut map: HashMap<&str, (u8, Vec<f64>)> = HashMap::new();
    for e in execs {
        let entry = map.entry(&e.name).or_insert_with(|| (e.core_id, Vec::new()));
        entry.1.push(e.duration_us() as f64);
    }
    let mut load = [0.0f64; M];
    for (_, (core, mut vals)) in map {
        vals.sort_by(|a, b| a.partial_cmp(b).unwrap());
        let median = vals[vals.len() / 2];
        load[(core as usize) % M] += median;
    }
    load.iter().cloned().fold(0.0f64, f64::max)
}

// ── Algorytm 1: GreedySpdp (heurystyk) ───────────────────────────────────────

/// **GreedySpdp — zachłanny wybór trybu (P|pmtn,spdp-any|Cmax)**
///
/// Iteracyjnie przenosi zadanie z S1 do S2, jeśli daje to lepsze Cmax.
/// Kończy gdy żaden ruch nie poprawia. Złożoność: O(n²).
///
/// **Heurystyk** — może utknąć w lokalnym minimum (DuLeung1989 daje optimum).
pub fn greedy_spdp(execs: &[TaskExecution]) -> SchedulingResult {
    let t0 = Instant::now();
    let baseline = baseline_cmax(execs);

    let tasks = aggregate_by_task(execs);
    let n = tasks.len();

    let cmax_of = |s1: &[usize], s2: &[usize]| -> f64 {
        let y: f64       = s2.iter().map(|&i| tasks[i].1 * LIN_FACTOR).sum();
        let s1_sum: f64  = s1.iter().map(|&i| tasks[i].1).sum();
        let s1_pmax: f64 = s1.iter().map(|&i| tasks[i].1).fold(0.0f64, f64::max);
        y + f64::max(s1_pmax, s1_sum / M as f64)
    };

    let mut s1: Vec<usize> = (0..n).collect();
    let mut s2: Vec<usize> = Vec::new();
    let mut cur = cmax_of(&s1, &s2);

    loop {
        let mut best_gain = 0.0f64;
        let mut best_pos  = None;

        for (pos, &idx) in s1.iter().enumerate() {
            let new_s1: Vec<usize> = s1.iter().enumerate()
                .filter(|&(p, _)| p != pos)
                .map(|(_, &i)| i)
                .collect();
            let mut new_s2 = s2.clone();
            new_s2.push(idx);
            let gain = cur - cmax_of(&new_s1, &new_s2);
            if gain > best_gain {
                best_gain = gain;
                best_pos  = Some(pos);
            }
        }

        let Some(pos) = best_pos else { break };
        let idx = s1[pos];
        s1.remove(pos);
        s2.push(idx);
        cur = cmax_of(&s1, &s2);
    }

    let s1_tasks: Vec<(String, f64)> = s1.iter().map(|&i| (tasks[i].0.clone(), tasks[i].1)).collect();
    let s2_tasks: Vec<(String, f64)> = s2.iter().map(|&i| (tasks[i].0.clone(), tasks[i].1)).collect();
    let (segs, cmax) = build_spdp_schedule(&s1_tasks, &s2_tasks);

    let algo_time_ns = t0.elapsed().as_nanos();
    SchedulingResult {
        algorithm:    "GreedySpdp",
        description:  format!("Heurystyk zachłanny; τ(Ti,2)={:.0}%·τ(Ti,1)", LIN_FACTOR * 100.0),
        cmax_us:      cmax,
        baseline_us:  baseline,
        improvement:  (baseline - cmax) / baseline,
        algo_time_ns,
        segments:     segs,
    }
}

// ── Algorytm 2: SplitOff (heurystyk) ─────────────────────────────────────────

/// **SplitOff — McNaughton-guided dual-core offloading (heurystyk)**
///
/// Iteracyjnie wyznacza zadanie "split" w wraparound McNaughtona i próbuje
/// przenieść je do S2. Kończy gdy ruch nie poprawia Cmax. Złożoność: O(n²).
///
/// **Heurystyk** — może utknąć w lokalnym minimum.
pub fn splitoff(execs: &[TaskExecution]) -> SchedulingResult {
    let t0 = Instant::now();
    let baseline = baseline_cmax(execs);

    let tasks = aggregate_by_task(execs);
    let n = tasks.len();

    let cmax_of = |s1: &[usize], s2: &[usize]| -> f64 {
        let y: f64       = s2.iter().map(|&i| tasks[i].1 * LIN_FACTOR).sum();
        let s1_sum: f64  = s1.iter().map(|&i| tasks[i].1).sum();
        let s1_pmax: f64 = s1.iter().map(|&i| tasks[i].1).fold(0.0f64, f64::max);
        y + f64::max(s1_pmax, s1_sum / M as f64)
    };

    let mut s1: Vec<usize> = (0..n).collect();
    let mut s2: Vec<usize> = Vec::new();
    let mut cur = cmax_of(&s1, &s2);

    loop {
        let s1_sum: f64  = s1.iter().map(|&i| tasks[i].1).sum();
        let s1_pmax: f64 = s1.iter().map(|&i| tasks[i].1).fold(0.0f64, f64::max);
        let cmax_s1 = f64::max(s1_pmax, s1_sum / M as f64);

        let mut cumsum = 0.0f64;
        let mut split_pos: Option<usize> = None;
        for (pos, &idx) in s1.iter().enumerate() {
            cumsum += tasks[idx].1;
            if cumsum >= cmax_s1 - 1e-9 {
                split_pos = Some(pos);
                break;
            }
        }

        let Some(pos) = split_pos else { break };

        let split_idx = s1[pos];
        let new_s1: Vec<usize> = s1.iter().enumerate()
            .filter(|&(p, _)| p != pos)
            .map(|(_, &i)| i)
            .collect();
        let mut new_s2 = s2.clone();
        new_s2.push(split_idx);
        let new_cmax = cmax_of(&new_s1, &new_s2);

        if new_cmax < cur - 1e-9 {
            s1 = new_s1;
            s2 = new_s2;
            cur = new_cmax;
        } else {
            break;
        }
    }

    let s1_tasks: Vec<(String, f64)> = s1.iter().map(|&i| (tasks[i].0.clone(), tasks[i].1)).collect();
    let s2_tasks: Vec<(String, f64)> = s2.iter().map(|&i| (tasks[i].0.clone(), tasks[i].1)).collect();
    let (segs, cmax) = build_spdp_schedule(&s1_tasks, &s2_tasks);

    let algo_time_ns = t0.elapsed().as_nanos();
    SchedulingResult {
        algorithm:    "SplitOff",
        description:  format!("Heurystyk McNaughton-guided; τ(Ti,2)={:.0}%·τ(Ti,1)", LIN_FACTOR * 100.0),
        cmax_us:      cmax,
        baseline_us:  baseline,
        improvement:  (baseline - cmax) / baseline,
        algo_time_ns,
        segments:     segs,
    }
}

// ── Algorytm 3: DuLeung1989 (OPTYMALNY) ──────────────────────────────────────

/// **DuLeung1989 — optymalny dla P|pmtn,spdp-any|Cmax (M=2)**
///
/// Du J., Leung J.Y-T. "Complexity of Scheduling Parallel Task Systems",
/// SIAM J. Disc. Math. 2(4), 1989, pp. 473-487 (Theorem 6).
///
/// Każdy harmonogram PTS odpowiada przypisaniu k_j ∈ {1,2} każdemu zadaniu.
/// Dla ustalonego przypisania optymalny preempcyjny Cmax (Blazewicz):
///
///   Y       = Σ_{j∈S2} LIN_FACTOR · p_j
///   Cmax_S1 = max(pmax_S1, ΣS1/M)
///   Cmax    = Y + Cmax_S1
///
/// Algorytm enumeruje wszystkie 2ⁿ podzbiorów S2. Złożoność: O(n · 2ⁿ).
/// Dla n ≤ 22 praktyczne (do 100M ewaluacji).
pub fn duleung(execs: &[TaskExecution]) -> SchedulingResult {
    let t0 = Instant::now();
    let baseline = baseline_cmax(execs);

    let tasks = aggregate_by_task(execs);
    let n = tasks.len();

    if n == 0 {
        return SchedulingResult {
            algorithm:    "DuLeung1989",
            description:  "brak danych".into(),
            cmax_us: 0.0, baseline_us: baseline, improvement: 0.0,
            algo_time_ns: t0.elapsed().as_nanos(), segments: vec![],
        };
    }

    if n > 22 {
        let mut r = splitoff(execs);
        r.algorithm    = "DuLeung1989";
        r.description  = format!("n={} > 22 — fallback do SplitOff (brute force pominięty)", n);
        r.algo_time_ns = t0.elapsed().as_nanos();
        return r;
    }

    let cmax_of_mask = |mask: u32| -> f64 {
        let mut y       = 0.0f64;
        let mut s1_sum  = 0.0f64;
        let mut s1_pmax = 0.0f64;
        for i in 0..n {
            let p = tasks[i].1;
            if (mask >> i) & 1 == 1 {
                y += p * LIN_FACTOR;
            } else {
                s1_sum += p;
                if p > s1_pmax { s1_pmax = p; }
            }
        }
        y + f64::max(s1_pmax, s1_sum / M as f64)
    };

    let mut best_mask: u32 = 0;
    let mut best_cmax = cmax_of_mask(0);
    let total = 1u32 << n;
    for mask in 1..total {
        let c = cmax_of_mask(mask);
        if c < best_cmax {
            best_cmax = c;
            best_mask = mask;
        }
    }

    let s1_tasks: Vec<(String, f64)> = (0..n)
        .filter(|&i| (best_mask >> i) & 1 == 0)
        .map(|i| (tasks[i].0.clone(), tasks[i].1))
        .collect();
    let s2_tasks: Vec<(String, f64)> = (0..n)
        .filter(|&i| (best_mask >> i) & 1 == 1)
        .map(|i| (tasks[i].0.clone(), tasks[i].1))
        .collect();
    let (segs, cmax) = build_spdp_schedule(&s1_tasks, &s2_tasks);

    let algo_time_ns = t0.elapsed().as_nanos();
    SchedulingResult {
        algorithm:    "DuLeung1989",
        description:  format!("OPTYMALNY (Du-Leung 1989); brute force 2^{}={} ewaluacji; |S2|={}",
                              n, total, s2_tasks.len()),
        cmax_us:      cmax,
        baseline_us:  baseline,
        improvement:  (baseline - cmax) / baseline,
        algo_time_ns,
        segments:     segs,
    }
}

// ── Pomocnicza: budowanie harmonogramu 2-fazowego spdp ───────────────────────

fn build_spdp_schedule(
    s1_tasks: &[(String, f64)],
    s2_tasks: &[(String, f64)],
) -> (Vec<Segment>, f64) {
    let mut segs = Vec::new();

    // Faza 1: zadania 2-rdzeniowe (oba rdzenie zajęte jednocześnie)
    let mut t = 0.0f64;
    for (name, p) in s2_tasks {
        let dur = p * LIN_FACTOR;
        if dur < 1e-9 { continue; }
        let lbl = format!("{}[×2]", name);
        segs.push(Segment { name: lbl.clone(), core: 0, start_us: t, end_us: t + dur });
        segs.push(Segment { name: lbl,         core: 1, start_us: t, end_us: t + dur });
        t += dur;
    }
    let y = t;

    // Faza 2: zadania 1-rdzeniowe — McNaughton wrap-around od offsetu y
    let s1_sum: f64  = s1_tasks.iter().map(|(_, p)| *p).sum();
    let s1_pmax: f64 = s1_tasks.iter().map(|(_, p)| *p).fold(0.0f64, f64::max);
    let cmax_s1 = f64::max(s1_pmax, s1_sum / M as f64);

    let mut s1_sorted = s1_tasks.to_vec();
    s1_sorted.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap());

    let mut core      = 0usize;
    let mut phase_pos = 0.0f64;

    for (name, p) in &s1_sorted {
        let mut rem = *p;
        while rem > 1e-9 {
            let space = cmax_s1 - phase_pos;
            let slice = rem.min(space);
            if slice > 1e-9 {
                segs.push(Segment {
                    name: name.clone(), core: core as u8,
                    start_us: y + phase_pos, end_us: y + phase_pos + slice,
                });
            }
            phase_pos += slice;
            rem -= slice;
            if rem > 1e-9 {
                core      = (core + 1) % M;
                phase_pos = 0.0;
            }
        }
    }

    (segs, y + cmax_s1)
}

// ── Punkty wejścia ────────────────────────────────────────────────────────────

/// Uruchamia wszystkie 3 algorytmy. Kolejność wyników odpowiada wariantom
/// `ALGO` w analysis_panel:
///   0 GreedySpdp, 1 SplitOff, 2 DuLeung1989.
pub fn run_all(execs: &[TaskExecution]) -> Vec<SchedulingResult> {
    vec![
        greedy_spdp(execs),
        splitoff(execs),
        duleung(execs),
    ]
}

/// Uruchamia pojedynczy algorytm wskazany przez `algo_idx` (zgodnie z `run_all`).
pub fn run_one(execs: &[TaskExecution], algo_idx: usize) -> SchedulingResult {
    match algo_idx {
        0 => greedy_spdp(execs),
        1 => splitoff(execs),
        2 => duleung(execs),
        _ => panic!("invalid algorithm index: {}", algo_idx),
    }
}
