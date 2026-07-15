//! # Algorytmy Szeregowania Zadań (Heurystyczne i Dokładne)
//!
//! Projekt: RTT-Task Analyser
//! Autor: Marcel Gruszecki (UAM)
//! Moduł: `communication::heuristics`
//! Opis: Implementacja algorytmów szeregowania dla problemu $P | pmtn, spdp-any | C_{max}$ przy liczbie rdzeni $M = 2$.
//!       Zawiera heurystyki GreedySpdp i SplitOff, algorytm dokładny DuLeung1989 oraz regułę McNaughtona.

use std::time::Instant;
use std::collections::{HashMap, HashSet};
use super::scheduler::TaskExecution;

const M: usize = 2;

/// Reprezentuje pojedynczy wycinek czasowy przydzielony zadaniu na konkretnym rdzeniu.
#[derive(Debug, Clone)]
pub struct Segment {
    pub name: String,
    pub core: u8,
    pub start_us: f64,
    pub end_us: f64,
}

impl Segment {
    #[allow(dead_code)]
    pub fn duration_us(&self) -> f64 { self.end_us - self.start_us }
}

/// Przechowuje pełny raport z wykonania algorytmu szeregowania.
#[derive(Debug)]
pub struct SchedulingResult {
    pub algorithm: &'static str,
    #[allow(dead_code)]
    pub description: String,
    pub cmax_us: f64,
    pub baseline_us: f64,
    pub improvement: f64,
    pub algo_time_ns: u128,
    pub segments: Vec<Segment>,
}

impl SchedulingResult {
    #[allow(dead_code)]
    pub fn improvement_pct(&self) -> f64 { self.improvement * 100.0 }
}

#[derive(Debug, Clone)]
struct TaskInfo {
    name: String,
    p_single: f64,
    p_dual: f64,
    #[allow(dead_code)]
    dual_measured: bool,
}

// Agreguje surowe pomiary z mikrokontrolera do struktur TaskInfo na bazie pierwszych wystąpień
fn aggregate_tasks(execs: &[TaskExecution]) -> Vec<TaskInfo> {
    let mut single_map: HashMap<String, f64> = HashMap::new();
    let mut dual_c0: HashMap<String, f64> = HashMap::new();
    let mut dual_c1: HashMap<String, f64> = HashMap::new();

    // Zbiory służące do ignorowania pierwszej pętli (artefaktów startowych)
    let mut single_seen = HashSet::new();
    let mut dual_c0_seen = HashSet::new();
    let mut dual_c1_seen = HashSet::new();

    for e in execs {
        let name = e.name.trim().to_string();
        let dur = e.duration_us() as f64;

        if name.ends_with("_0_d") {
            let base = name[..name.len() - 4].to_string();
            if dual_c0_seen.contains(&base) {
                // Zapisujemy dopiero DRUGIE wystąpienie
                dual_c0.entry(base).or_insert(dur);
            } else {
                dual_c0_seen.insert(base);
            }
        } else if name.ends_with("_1_d") {
            let base = name[..name.len() - 4].to_string();
            if dual_c1_seen.contains(&base) {
                dual_c1.entry(base).or_insert(dur);
            } else {
                dual_c1_seen.insert(base);
            }
        } else {
            if single_seen.contains(&name) {
                single_map.entry(name).or_insert(dur);
            } else {
                single_seen.insert(name);
            }
        }
    }

    let mut all_bases = HashSet::new();
    for k in single_map.keys() { all_bases.insert(k.clone()); }
    for k in dual_c0.keys() { all_bases.insert(k.clone()); }

    let mut sorted_bases: Vec<String> = all_bases.into_iter().collect();
    sorted_bases.sort();

    let mut result = Vec::new();
    for base in sorted_bases {
        let p_single = *single_map.get(&base).unwrap_or(&f64::INFINITY);
        let d0 = dual_c0.get(&base);
        let d1 = dual_c1.get(&base);
        let p_dual = if let (Some(&v0), Some(&v1)) = (d0, d1) {
            f64::max(v0, v1)
        } else {
            f64::INFINITY
        };

        let dual_measured = d0.is_some() && d1.is_some();

        // Jeśli zadanie miało komplet pomiarów, rodzi DWA unikalne zadania w puli
        if p_single.is_finite() && p_dual.is_finite() {
            result.push(TaskInfo { name: base.clone(), p_single, p_dual, dual_measured });
            result.push(TaskInfo { name: base.clone(), p_single, p_dual, dual_measured });
        } else {
            result.push(TaskInfo { name: base, p_single, p_dual, dual_measured });
        }
    }

    // Stabilne sortowanie dla LPT
    result.sort_by(|a, b| {
        let cmp = b.p_single.partial_cmp(&a.p_single).unwrap_or(std::cmp::Ordering::Equal);
        if cmp == std::cmp::Ordering::Equal { a.name.cmp(&b.name) } else { cmp }
    });
    result
}

/// Wyznacza bazowy czas trwania (Makespan) pierwszej sekwencyjnej pętli pomiarowej.
pub fn baseline_cmax(execs: &[TaskExecution]) -> f64 {
    if execs.is_empty() { return 0.0; }

    let mut seen_tasks = HashSet::new();
    let mut min_start = f64::INFINITY;
    let mut max_end = 0.0f64;

    for e in execs {
        let name = e.name.trim().to_string();

        if seen_tasks.contains(&name) {
            break;
        }
        seen_tasks.insert(name);

        let s = e.start_us as f64;
        let end = e.end_us as f64;
        if s < min_start { min_start = s; }
        if end > max_end { max_end = end; }
    }

    if max_end > min_start && min_start.is_finite() {
        max_end - min_start
    } else {
        0.0
    }
}

// Wylicza teoretyczną wartość Cmax dla zadanego podziału zbiorów S1 i S2
#[inline]
fn cmax_of_split(tasks: &[TaskInfo], s1: &[usize], s2: &[usize]) -> f64 {
    let mut y = 0.0f64;
    for &i in s2 { y += tasks[i].p_dual; }

    let mut s1_sum = 0.0f64;
    let mut s1_pmax = 0.0f64;
    for &i in s1 {
        let p = tasks[i].p_single;
        s1_sum += p;
        s1_pmax = f64::max(s1_pmax, p);
    }

    y + f64::max(s1_pmax, s1_sum / 2.0)
}

/// Implementacja zachłannego algorytmu heurystycznego GreedySpdp.
pub fn greedy_spdp(execs: &[TaskExecution]) -> SchedulingResult {
    let t0 = Instant::now();
    let baseline = baseline_cmax(execs);
    //let tasks = aggregate_tasks_for_algo(execs);
    let tasks = aggregate_tasks(execs);

    println!("=== aggregate_tasks output ({} tasks) ===", tasks.len());
    for (i, t) in tasks.iter().enumerate() {
        println!("  [{}] name={:?} p_single={:.1} p_dual={:.1} dual_measured={}",
                 i, t.name, t.p_single, t.p_dual, t.dual_measured);
    }
    println!("  baseline={:.1}", baseline);

    let n = tasks.len();

    let mut s1: Vec<usize> = (0..n).filter(|&i| !tasks[i].p_single.is_infinite()).collect();
    let mut s2: Vec<usize> = (0..n).filter(|&i| tasks[i].p_single.is_infinite()).collect();
    let mut cur = cmax_of_split(&tasks, &s1, &s2);

    loop {
        let mut best_gain = 0.0f64;
        let mut best_pos  = None;

        for (pos, &idx) in s1.iter().enumerate() {
            if tasks[idx].p_dual.is_infinite() { continue; }

            let new_s1: Vec<usize> = s1.iter().enumerate().filter(|&(p, _)| p != pos).map(|(_, &i)| i).collect();
            let mut new_s2 = s2.clone();
            new_s2.push(idx);

            let new_cmax = cmax_of_split(&tasks, &new_s1, &new_s2);
            let gain = cur - new_cmax;

            if gain > best_gain || (gain.abs() < 1e-9 && best_pos.is_none()) {
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

    let s1_refs: Vec<&TaskInfo> = s1.iter().map(|&i| &tasks[i]).collect();
    let s2_refs: Vec<&TaskInfo> = s2.iter().map(|&i| &tasks[i]).collect();

    println!("=== greedy_spdp split ===");
    println!("  S1: {:?}", s1.iter().map(|&i| (&tasks[i].name, tasks[i].p_single)).collect::<Vec<_>>());
    println!("  S2: {:?}", s2.iter().map(|&i| (&tasks[i].name, tasks[i].p_dual)).collect::<Vec<_>>());
    println!("  cmax theoretical: {:.1}", cur);

    let (segs, cmax) = build_spdp_schedule(&s1_refs, &s2_refs);

    SchedulingResult {
        algorithm: "GreedySpdp",
        description: "".into(),
        cmax_us: cmax,
        baseline_us: baseline,
        improvement: if baseline > 0.0 { (baseline - cmax) / baseline } else { 0.0 },
        algo_time_ns: t0.elapsed().as_nanos(),
        segments: segs,
    }
}

/// Implementacja strukturalnego algorytmu heurystycznego SplitOff.
pub fn splitoff(execs: &[TaskExecution]) -> SchedulingResult {
    let t0 = Instant::now();
    let baseline = baseline_cmax(execs);
    //let tasks = aggregate_tasks_for_algo(execs);
    let tasks = aggregate_tasks(execs);

    println!("=== aggregate_tasks output ({} tasks) ===", tasks.len());
    for (i, t) in tasks.iter().enumerate() {
        println!("  [{}] name={:?} p_single={:.1} p_dual={:.1} dual_measured={}",
                 i, t.name, t.p_single, t.p_dual, t.dual_measured);
    }
    println!("  baseline={:.1}", baseline);

    let n = tasks.len();

    let mut s1: Vec<usize> = (0..n).filter(|&i| !tasks[i].p_single.is_infinite()).collect();
    let mut s2: Vec<usize> = (0..n).filter(|&i| tasks[i].p_single.is_infinite()).collect();
    let mut cur = cmax_of_split(&tasks, &s1, &s2);

    loop {
        let s1_sum: f64  = s1.iter().map(|&i| tasks[i].p_single).sum();
        let s1_pmax: f64 = s1.iter().map(|&i| tasks[i].p_single).fold(0.0f64, f64::max);
        let cmax_s1 = f64::max(s1_pmax, s1_sum / 2.0);

        let mut cumsum = 0.0f64;
        let mut split_pos = None;
        for (pos, &idx) in s1.iter().enumerate() {
            cumsum += tasks[idx].p_single;
            if cumsum >= cmax_s1 - 1e-9 {
                split_pos = Some(pos);
                break;
            }
        }

        let Some(pos) = split_pos else { break };
        let idx = s1[pos];

        if tasks[idx].p_dual.is_infinite() { break; }

        let new_s1: Vec<usize> = s1.iter().enumerate().filter(|&(p, _)| p != pos).map(|(_, &i)| i).collect();
        let mut new_s2 = s2.clone();
        new_s2.push(idx);

        let new_cmax = cmax_of_split(&tasks, &new_s1, &new_s2);
        if new_cmax <= cur + 1e-5 {
            s1 = new_s1;
            s2 = new_s2;
            cur = new_cmax;
        } else {
            break;
        }
    }

    let s1_refs: Vec<&TaskInfo> = s1.iter().map(|&i| &tasks[i]).collect();
    let s2_refs: Vec<&TaskInfo> = s2.iter().map(|&i| &tasks[i]).collect();
    let (segs, cmax) = build_spdp_schedule(&s1_refs, &s2_refs);

    SchedulingResult {
        algorithm: "SplitOff",
        description: "".into(),
        cmax_us: cmax,
        baseline_us: baseline,
        improvement: if baseline > 0.0 { (baseline - cmax) / baseline } else { 0.0 },
        algo_time_ns: t0.elapsed().as_nanos(),
        segments: segs,
    }
}

/// Implementacja algorytmu optymalnego (dokładnego) opartego na przeglądzie zupełnym masek bitowych.
pub fn duleung(execs: &[TaskExecution]) -> SchedulingResult {
    let t0 = Instant::now();
    let baseline = baseline_cmax(execs);
    //let tasks = aggregate_tasks_for_algo(execs);
    let tasks = aggregate_tasks(execs);

    println!("=== aggregate_tasks output ({} tasks) ===", tasks.len());
    for (i, t) in tasks.iter().enumerate() {
        println!("  [{}] name={:?} p_single={:.1} p_dual={:.1} dual_measured={}",
                 i, t.name, t.p_single, t.p_dual, t.dual_measured);
    }
    println!("  baseline={:.1}", baseline);

    let n = tasks.len();

    if n > 30 {
        return SchedulingResult {
            algorithm: "DuLeung1989", description: "".into(),
            cmax_us: baseline, baseline_us: baseline, improvement: 0.0,
            algo_time_ns: 0, segments: vec![],
        };
    }

    let mut best_cmax = f64::INFINITY;
    let mut best_s1: Vec<usize> = Vec::new();
    let mut best_s2: Vec<usize> = Vec::new();

    for mask in 0..(1 << n) {
        let mut s1 = Vec::new();
        let mut s2 = Vec::new();
        let mut valid_mask = true;

        for i in 0..n {
            if (mask >> i) & 1 == 1 {
                if tasks[i].p_dual.is_infinite() { valid_mask = false; break; }
                s2.push(i);
            } else {
                if tasks[i].p_single.is_infinite() { valid_mask = false; break; }
                s1.push(i);
            }
        }

        if !valid_mask { continue; }

        let c = cmax_of_split(&tasks, &s1, &s2);
        if c < best_cmax - 1e-9 || ((c - best_cmax).abs() < 1e-9 && s2.len() > best_s2.len()) {
            best_cmax = c;
            best_s1 = s1;
            best_s2 = s2;
        }
    }

    let s1_refs: Vec<&TaskInfo> = best_s1.iter().map(|&i| &tasks[i]).collect();
    let s2_refs: Vec<&TaskInfo> = best_s2.iter().map(|&i| &tasks[i]).collect();
    let (segs, cmax) = build_spdp_schedule(&s1_refs, &s2_refs);

    SchedulingResult {
        algorithm: "DuLeung1989",
        description: "".into(),
        cmax_us: cmax,
        baseline_us: baseline,
        improvement: if baseline > 0.0 { (baseline - cmax) / baseline } else { 0.0 },
        algo_time_ns: t0.elapsed().as_nanos(),
        segments: segs,
    }
}

// Konstruuje geometryczne segmenty wykresu Gantta przy użyciu reguły owijania McNaughtona
fn build_spdp_schedule(s1_tasks: &[&TaskInfo], s2_tasks: &[&TaskInfo]) -> (Vec<Segment>, f64) {
    let mut segs = Vec::new();

    // Szeregowanie podzbioru zadań równoległych (S2) na obu rdzeniach jednocześnie
    let mut t = 0.0f64;
    for task in s2_tasks {
        let dur = task.p_dual;
        if dur < 1e-9 || dur.is_infinite() { continue; }
        let lbl = format!("{}[×2]", task.name);
        segs.push(Segment { name: lbl.clone(), core: 0, start_us: t, end_us: t + dur });
        segs.push(Segment { name: lbl,         core: 1, start_us: t, end_us: t + dur });
        t += dur;
    }
    let y = t;

    // Szeregowanie podzbioru zadań sekwencyjnych (S1) metodą owijania McNaughtona
    let s1_sum: f64  = s1_tasks.iter().filter(|t| !t.p_single.is_infinite()).map(|t| t.p_single).sum();
    let s1_pmax: f64 = s1_tasks.iter().filter(|t| !t.p_single.is_infinite()).map(|t| t.p_single).fold(0.0f64, f64::max);
    let cmax_s1 = f64::max(s1_pmax, s1_sum / 2.0);

    let mut s1_sorted = s1_tasks.to_vec();
    s1_sorted.sort_by(|a, b| b.p_single.partial_cmp(&a.p_single).unwrap_or(std::cmp::Ordering::Equal));

    let mut core_time = vec![y; M];
    let mut current_core = 0usize;

    for task in &s1_sorted {
        let mut rem = task.p_single;
        if rem.is_infinite() { continue; }

        while rem > 1e-9 {
            let space = (y + cmax_s1) - core_time[current_core];
            let slice = rem.min(space);

            if slice > 1e-9 {
                segs.push(Segment {
                    name: task.name.clone(),
                    core: current_core as u8,
                    start_us: core_time[current_core],
                    end_us: core_time[current_core] + slice,
                });
                core_time[current_core] += slice;
                rem -= slice;
            }

            if rem > 1e-9 {
                current_core = (current_core + 1) % M;
            }
        }
    }

    (segs, y + cmax_s1)
}

/// Punkty wejścia: uruchamia cały pakiet algorytmów.
pub fn run_all(execs: &[TaskExecution]) -> Vec<SchedulingResult> {
    vec![ greedy_spdp(execs), splitoff(execs), duleung(execs) ]
}

/// Punkty wejścia: uruchamia pojedynczy, wskazany indeksem algorytm szeregowania.
pub fn run_one(execs: &[TaskExecution], algo_idx: usize) -> SchedulingResult {
    match algo_idx {
        0 => greedy_spdp(execs),
        1 => splitoff(execs),
        2 => duleung(execs),
        _ => panic!("Nieprawidłowy indeks algorytmu: {}", algo_idx),
    }
}