//! Algorytmy harmonogramowania dla problemu P | pmtn, spdp-any | Cmax (M = 2).
//! Zadania z sufiksem `_d` w nazwie traktowane są jako pomiary dwurdzeniowe:
//! sparowane wykonania na rdzeniu 0 i rdzeniu 1 dają zmierzony czas ścienny
//! `p_dual = max(dur_core0, dur_core1)` (oba rdzenie zajęte jednocześnie).
//! Dla zadań bez pomiaru dwurdzeniowego: `p_dual = p_single * LIN_FACTOR`.
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

/// Fallback: czas ścienny na 2 rdzeniach = 60% czasu 1-rdz., gdy brak danych.
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

// ── Reprezentacja zadania ─────────────────────────────────────────────────────

#[derive(Debug, Clone)]
struct TaskInfo {
    name:           String,
    p_single:       f64,
    p_dual:         f64,
    dual_measured:  bool,
}

// ── Agregacja zadań z wykrywaniem _d ─────────────────────────────────────────

/// Buduje listę `TaskInfo` z surowych wykonań.
///
/// Zadania `*_d` (sufix `_d`) są wykonaniami dwurdzeniowymi: parujemy wykonania
/// core-0 i core-1 pozycyjnie (EventGroupSync = jednoczesny start), mierzymy
/// `max(dur_c0, dur_c1)` dla każdej pary i bierzemy medianę par jako `p_dual`.
///
/// Dla każdego zadania `foo_d` szukamy odpowiadającego `foo` (bez sufixu) by
/// uzyskać `p_single`. Jeżeli brak — `p_single = p_dual / LIN_FACTOR`.
///
/// Zadania o nazwie kończącej się `_d` nie pojawiają się samodzielnie na liście
/// wynikowej (są wbudowane w odpowiadające im `foo`).
fn aggregate_tasks(execs: &[TaskExecution]) -> Vec<TaskInfo> {
    use std::collections::HashMap;

    // Rozdziel: _d → grupy per rdzeń; reszta → single_map
    let mut single_map: HashMap<&str, Vec<f64>> = HashMap::new();
    let mut dual_c0:    HashMap<&str, Vec<(u64, f64)>> = HashMap::new(); // (start_us, dur)
    let mut dual_c1:    HashMap<&str, Vec<(u64, f64)>> = HashMap::new();

    for e in execs {
        if e.name.ends_with("_d") {
            let base = e.name.trim_end_matches("_d");
            let dur  = e.duration_us() as f64;
            if e.core_id == 0 {
                dual_c0.entry(base).or_default().push((e.start_us, dur));
            } else {
                dual_c1.entry(base).or_default().push((e.start_us, dur));
            }
        } else {
            single_map.entry(&e.name).or_default().push(e.duration_us() as f64);
        }
    }

    // Dla każdego zadania z pomiarem dual: sparuj c0/c1 pozycyjnie po start_us
    let mut dual_median: HashMap<String, f64> = HashMap::new();
    let mut dual_base_names: std::collections::HashSet<String> = std::collections::HashSet::new();

    for (base, mut c0_vec) in dual_c0 {
        if let Some(mut c1_vec) = dual_c1.remove(base) {
            c0_vec.sort_by_key(|(t, _)| *t);
            c1_vec.sort_by_key(|(t, _)| *t);
            let pairs = c0_vec.len().min(c1_vec.len());
            if pairs == 0 { continue; }
            let mut pair_durs: Vec<f64> = (0..pairs)
                .map(|i| c0_vec[i].1.max(c1_vec[i].1))
                .collect();
            pair_durs.sort_by(|a, b| a.partial_cmp(b).unwrap());
            let median = pair_durs[pair_durs.len() / 2];
            dual_median.insert(base.to_string(), median);
            dual_base_names.insert(base.to_string());
        }
    }

    // Zbuduj wynikową listę TaskInfo z zadań single_map
    let mut result: Vec<TaskInfo> = single_map.into_iter()
        .map(|(name, mut vals)| {
            vals.sort_by(|a, b| a.partial_cmp(b).unwrap());
            let p_single = vals[vals.len() / 2];
            let (p_dual, dual_measured) = if let Some(&pd) = dual_median.get(name) {
                (pd, true)
            } else {
                (p_single * LIN_FACTOR, false)
            };
            TaskInfo { name: name.to_string(), p_single, p_dual, dual_measured }
        })
        .collect();

    // Zadania, które mają tylko _d (brak odpowiednika single): dodaj je też
    for base in &dual_base_names {
        if !result.iter().any(|t| &t.name == base) {
            let pd = dual_median[base];
            result.push(TaskInfo {
                name:          base.clone(),
                p_single:      pd / LIN_FACTOR,
                p_dual:        pd,
                dual_measured: true,
            });
        }
    }

    result.sort_by(|a, b| b.p_single.partial_cmp(&a.p_single).unwrap());
    result
}

// ── Linia bazowa ──────────────────────────────────────────────────────────────

/// Cmax obserwowany przez OS: każde zadanie zostaje na rdzeniu, na którym
/// zostało pierwszy raz zaobserwowane; sumujemy mediany per rdzeń, bierzemy max.
/// Ignoruje wykonania `_d` (to pomiary dwurdzeniowe, nie są częścią baseline).
pub fn baseline_cmax(execs: &[TaskExecution]) -> f64 {
    use std::collections::HashMap;
    let mut map: HashMap<&str, (u8, Vec<f64>)> = HashMap::new();
    for e in execs {
        if e.name.ends_with("_d") { continue; }
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

// ── Pomocnicza: Cmax dla danego podziału S1/S2 ───────────────────────────────

#[inline]
fn cmax_of_split(tasks: &[TaskInfo], s1: &[usize], s2: &[usize]) -> f64 {
    let y: f64       = s2.iter().map(|&i| tasks[i].p_dual).sum();
    let s1_sum: f64  = s1.iter().map(|&i| tasks[i].p_single).sum();
    let s1_pmax: f64 = s1.iter().map(|&i| tasks[i].p_single).fold(0.0f64, f64::max);
    y + f64::max(s1_pmax, s1_sum / M as f64)
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

    let tasks = aggregate_tasks(execs);
    let n = tasks.len();

    let mut s1: Vec<usize> = (0..n).collect();
    let mut s2: Vec<usize> = Vec::new();
    let mut cur = cmax_of_split(&tasks, &s1, &s2);

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
            let gain = cur - cmax_of_split(&tasks, &new_s1, &new_s2);
            if gain > best_gain {
                best_gain = gain;
                best_pos  = Some(pos);
            }
        }

        let Some(pos) = best_pos else { break };
        let idx = s1[pos];
        s1.remove(pos);
        s2.push(idx);
        cur = cmax_of_split(&tasks, &s1, &s2);
    }

    let measured = s2.iter().any(|&i| tasks[i].dual_measured);
    let s1_refs: Vec<&TaskInfo> = s1.iter().map(|&i| &tasks[i]).collect();
    let s2_refs: Vec<&TaskInfo> = s2.iter().map(|&i| &tasks[i]).collect();
    let (segs, cmax) = build_spdp_schedule(&s1_refs, &s2_refs);

    let algo_time_ns = t0.elapsed().as_nanos();
    SchedulingResult {
        algorithm:   "GreedySpdp",
        description: format!(
            "Heurystyk zachłanny; {}; |S2|={}",
            if measured { "p_dual zmierzony".to_string() }
            else        { format!("τ(Ti,2)={:.0}%·τ(Ti,1) (fallback)", LIN_FACTOR * 100.0) },
            s2.len()
        ),
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

    let tasks = aggregate_tasks(execs);
    let n = tasks.len();

    let mut s1: Vec<usize> = (0..n).collect();
    let mut s2: Vec<usize> = Vec::new();
    let mut cur = cmax_of_split(&tasks, &s1, &s2);

    loop {
        let s1_sum: f64  = s1.iter().map(|&i| tasks[i].p_single).sum();
        let s1_pmax: f64 = s1.iter().map(|&i| tasks[i].p_single).fold(0.0f64, f64::max);
        let cmax_s1 = f64::max(s1_pmax, s1_sum / M as f64);

        // Szukaj zadania wrap-around w aktualnej kolejności s1 (bez sortowania)
        let mut cumsum = 0.0f64;
        let mut split_pos: Option<usize> = None;
        for (pos, &idx) in s1.iter().enumerate() {
            cumsum += tasks[idx].p_single;
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
        let new_cmax = cmax_of_split(&tasks, &new_s1, &new_s2);

        if new_cmax < cur - 1e-9 {
            s1 = new_s1;
            s2 = new_s2;
            cur = new_cmax;
        } else {
            break;
        }
    }

    let measured = s2.iter().any(|&i| tasks[i].dual_measured);
    let s1_refs: Vec<&TaskInfo> = s1.iter().map(|&i| &tasks[i]).collect();
    let s2_refs: Vec<&TaskInfo> = s2.iter().map(|&i| &tasks[i]).collect();
    let (segs, cmax) = build_spdp_schedule(&s1_refs, &s2_refs);

    let algo_time_ns = t0.elapsed().as_nanos();
    SchedulingResult {
        algorithm:   "SplitOff",
        description: format!(
            "Heurystyk McNaughton-guided; {}; |S2|={}",
            if measured { "p_dual zmierzony".to_string() }
            else        { format!("τ(Ti,2)={:.0}%·τ(Ti,1) (fallback)", LIN_FACTOR * 100.0) },
            s2.len()
        ),
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
///   Y       = Σ_{j∈S2} p_dual_j
///   Cmax_S1 = max(pmax_S1, ΣS1/M)
///   Cmax    = Y + Cmax_S1
///
/// Algorytm enumeruje wszystkie 2ⁿ podzbiorów S2. Złożoność: O(n · 2ⁿ).
/// Dla n ≤ 22 praktyczne (do 100M ewaluacji).
pub fn duleung(execs: &[TaskExecution]) -> SchedulingResult {
    let t0 = Instant::now();
    let baseline = baseline_cmax(execs);

    let tasks = aggregate_tasks(execs);
    let n = tasks.len();

    if n == 0 {
        return SchedulingResult {
            algorithm:    "DuLeung1989",
            description:  "".into(),
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
            if (mask >> i) & 1 == 1 {
                y += tasks[i].p_dual;
            } else {
                let p = tasks[i].p_single;
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

    let s1_refs: Vec<&TaskInfo> = (0..n)
        .filter(|&i| (best_mask >> i) & 1 == 0)
        .map(|i| &tasks[i])
        .collect();
    let s2_refs: Vec<&TaskInfo> = (0..n)
        .filter(|&i| (best_mask >> i) & 1 == 1)
        .map(|i| &tasks[i])
        .collect();

    let measured = s2_refs.iter().any(|t| t.dual_measured);
    let (segs, cmax) = build_spdp_schedule(&s1_refs, &s2_refs);

    let algo_time_ns = t0.elapsed().as_nanos();
    SchedulingResult {
        algorithm:   "DuLeung1989",
        description: format!(
            "OPTYMALNY (Du-Leung 1989); {}; brute force 2^{}={} ewaluacji; |S2|={}",
            if measured { "p_dual zmierzony".to_string() }
            else        { format!("τ(Ti,2)={:.0}%·τ(Ti,1) (fallback)", LIN_FACTOR * 100.0) },
            n, total, s2_refs.len()
        ),
        cmax_us:      cmax,
        baseline_us:  baseline,
        improvement:  (baseline - cmax) / baseline,
        algo_time_ns,
        segments:     segs,
    }
}

// ── Pomocnicza: budowanie harmonogramu 2-fazowego spdp ───────────────────────

fn build_spdp_schedule(
    s1_tasks: &[&TaskInfo],
    s2_tasks: &[&TaskInfo],
) -> (Vec<Segment>, f64) {
    let mut segs = Vec::new();

    // Faza 1: zadania 2-rdzeniowe (oba rdzenie zajęte jednocześnie)
    let mut t = 0.0f64;
    for task in s2_tasks {
        let dur = task.p_dual;
        if dur < 1e-9 { continue; }
        let lbl = format!("{}[×2]", task.name);
        segs.push(Segment { name: lbl.clone(), core: 0, start_us: t, end_us: t + dur });
        segs.push(Segment { name: lbl,         core: 1, start_us: t, end_us: t + dur });
        t += dur;
    }
    let y = t;

    // Faza 2: zadania 1-rdzeniowe — McNaughton wrap-around od offsetu y
    let s1_sum: f64  = s1_tasks.iter().map(|t| t.p_single).sum();
    let s1_pmax: f64 = s1_tasks.iter().map(|t| t.p_single).fold(0.0f64, f64::max);
    let cmax_s1 = f64::max(s1_pmax, s1_sum / M as f64);

    let mut s1_sorted = s1_tasks.to_vec();
    s1_sorted.sort_by(|a, b| b.p_single.partial_cmp(&a.p_single).unwrap());

    let mut core      = 0usize;
    let mut phase_pos = 0.0f64;

    for task in &s1_sorted {
        let mut rem = task.p_single;
        while rem > 1e-9 {
            let space = cmax_s1 - phase_pos;
            let slice = rem.min(space);
            if slice > 1e-9 {
                segs.push(Segment {
                    name: task.name.clone(), core: core as u8,
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
