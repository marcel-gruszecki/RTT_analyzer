//! # Panel Analizy Porównawczej Algorytmów
//!
//! Projekt: RTT-Task Analyser
//! Autor: Marcel Gruszecki (UAM)
//! Moduł: `app::ui::analysis_panel`
//! Opis: Implementuje widok analityczny aplikacji. Odpowiada za renderowanie tabeli
//!       wyników (Makespan, zysk procentowy, czas obliczeniowy) oraz wizualizację
//!       wyjściowych harmonogramów na interaktywnym wykresie Gantta.

use eframe::egui;
use egui_plot::{Bar, BarChart, Plot};

use crate::communication::SchedulingResult;

/// Warianty algorytmów szeregowania obsługiwane przez interfejs.
#[derive(Clone, PartialEq, Eq, Debug, Default)]
pub enum ALGO {
    #[default]
    GreedySpdp,
    SplitOff,
    DuLeung1989,
}

impl ALGO {
    fn label(&self) -> &'static str {
        match self {
            ALGO::GreedySpdp  => "GreedySpdp",
            ALGO::SplitOff    => "SplitOff",
            ALGO::DuLeung1989 => "DuLeung1989",
        }
    }

    pub fn index(&self) -> usize {
        match self {
            ALGO::GreedySpdp  => 0,
            ALGO::SplitOff    => 1,
            ALGO::DuLeung1989 => 2,
        }
    }
}

/// Akcje wyboru operacji matematyczno-obliczeniowych przez użytkownika.
#[derive(Clone, Debug)]
pub enum AnalysisAction {
    None,
    RunAll,
    RunSelected(usize),
}

/// Struktura przechowująca stan komponentów wyboru w panelu analizy.
#[derive(Debug, Default)]
pub struct AnalysisPanel {
    radio: ALGO,
}

/// Główna funkcja renderująca panel wyników i wykresy Gantta.
pub fn show_analysis_panel(
    ctx: &egui::Context,
    state: &mut AnalysisPanel,
    results: &[SchedulingResult],
) -> AnalysisAction {
    let mut action = AnalysisAction::None;

    egui::CentralPanel::default().show(ctx, |ui| {
        ui.label(egui::RichText::new("Harmonogramowanie — wyniki").strong());
        ui.separator();

        // Menu kontrolne: Przyciski uruchamiania i lista wyboru algorytmu
        ui.horizontal(|ui| {
            if ui.button("▶ Uruchom wszystkie").clicked() {
                action = AnalysisAction::RunAll;
            }
            if ui.button("▶ Tylko wybrany").clicked() {
                action = AnalysisAction::RunSelected(state.radio.index());
            }
            ui.separator();
            egui::ComboBox::from_label("Algorytm")
                .selected_text(state.radio.label())
                .show_ui(ui, |ui| {
                    ui.selectable_value(&mut state.radio, ALGO::GreedySpdp,  "GreedySpdp");
                    ui.selectable_value(&mut state.radio, ALGO::SplitOff,    "SplitOff");
                    ui.selectable_value(&mut state.radio, ALGO::DuLeung1989, "DuLeung1989 (opt)");
                });
        });

        ui.separator();

        if results.is_empty() {
            ui.label("Brak wyników. Zbierz dane i kliknij 'Uruchom wszystkie' lub 'Tylko wybrany'.");
            return;
        }

        // Siatka (Grid) prezentująca zbiorcze zestawienie metryk wydajnościowych
        let col_w = (ui.available_width() / 5.0).max(90.0);
        egui::Grid::new("results_table").striped(true).show(ui, |ui| {
            for h in &["Algorytm", "Cmax [µs]", "Bazowy [µs]", "Poprawa", "Czas algorytmu"] {
                ui.add_sized(
                    [col_w, 20.0],
                    egui::Label::new(egui::RichText::new(*h).strong()),
                );
            }
            ui.end_row();

            for r in results {
                let imp = r.improvement_pct();
                let color = if imp >= 0.0 {
                    egui::Color32::GREEN
                } else {
                    egui::Color32::RED
                };
                ui.add_sized([col_w, 20.0], egui::Label::new(r.algorithm));
                ui.add_sized([col_w, 20.0], egui::Label::new(format!("{:.0}", r.cmax_us)));
                ui.add_sized([col_w, 20.0], egui::Label::new(format!("{:.0}", r.baseline_us)));
                ui.add_sized(
                    [col_w, 20.0],
                    egui::Label::new(egui::RichText::new(format!("{:+.1}%", imp)).color(color)),
                );
                ui.add_sized([col_w, 20.0], egui::Label::new(format_duration_ns(r.algo_time_ns)));
                ui.end_row();
            }
        });

        ui.separator();

        // Renderowanie geometrycznego diagramu Gantta dla aktualnie wskazanego algorytmu
        let selected_label = state.radio.label();
        if let Some(result) = results.iter().find(|r| r.algorithm == selected_label) {
            ui.label(format!(
                "Diagram Gantta — {} (Cmax ~{:.0} µs, czas algo {})",
                result.algorithm,
                result.cmax_us,
                format_duration_ns(result.algo_time_ns)
            ));

            // Mapowanie wycinków czasowych (Segment) na poziome słupki wykresu
            let bars: Vec<Bar> = result
                .segments
                .iter()
                .map(|seg| {
                    Bar::new(seg.core as f64, seg.end_us - seg.start_us)
                        .base_offset(seg.start_us)
                        .horizontal()
                        .fill(name_color(&seg.name))
                        .name(&seg.name)
                        .width(0.6)
                })
                .collect();

            let chart = BarChart::new(result.algorithm, bars).horizontal();

            egui::ScrollArea::vertical()
                .id_salt("gantt_analysis_scroll")
                .max_height(220.0)
                .show(ui, |ui| {
                    Plot::new("gantt_analysis")
                        .include_x(0.0)
                        .include_x(result.cmax_us)
                        .include_y(-0.5)
                        .include_y(1.5)
                        .y_axis_formatter(|mark, _range| match mark.value.round() as i32 {
                            0 => "Core 0".to_string(),
                            1 => "Core 1".to_string(),
                            _ => String::new(),
                        })
                        .show(ui, |plot_ui| {
                            plot_ui.bar_chart(chart);
                        });
                });
        } else {
            ui.label(format!(
                "Brak wyniku dla '{}'. Kliknij 'Uruchom wszystkie' lub 'Tylko wybrany'.",
                selected_label
            ));
        }
    });

    action
}

// Formatowanie jednostek czasu wykonania kodu (od nanosekund do sekund)
fn format_duration_ns(ns: u128) -> String {
    if ns < 1_000 {
        format!("{} ns", ns)
    } else if ns < 1_000_000 {
        format!("{:.2} µs", ns as f64 / 1_000.0)
    } else if ns < 1_000_000_000 {
        format!("{:.2} ms", ns as f64 / 1_000_000.0)
    } else {
        format!("{:.2} s", ns as f64 / 1_000_000_000.0)
    }
}

// Deterministyczny generator unikalnego koloru RGB na bazie haszowania nazwy zadania
fn name_color(name: &str) -> egui::Color32 {
    let hash = name
        .bytes()
        .fold(0x811c9dc5u64, |acc, b| acc.wrapping_mul(0x01000193) ^ b as u64);
    let r = (((hash >> 16) & 0xff) as u8).max(60);
    let g = (((hash >> 8) & 0xff) as u8).max(60);
    let b = ((hash & 0xff) as u8).max(60);
    egui::Color32::from_rgb(r, g, b)
}