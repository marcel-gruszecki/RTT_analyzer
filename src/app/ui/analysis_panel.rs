use eframe::egui;
use egui_plot::{Bar, BarChart, Plot};

use crate::komunikacja::SchedulingResult;

/// Which algorithm's Gantt to display.
#[derive(Clone, PartialEq, Eq, Debug, Default)]
pub enum ALGO {
    #[default]
    McNaughton,
    LPT,
    SplitSpdp,
}

impl ALGO {
    fn label(&self) -> &'static str {
        match self {
            ALGO::McNaughton => "McNaughton",
            ALGO::LPT        => "LPT",
            ALGO::SplitSpdp  => "Split spdp-any",
        }
    }

    fn index(&self) -> usize {
        match self {
            ALGO::McNaughton => 0,
            ALGO::LPT        => 1,
            ALGO::SplitSpdp  => 2,
        }
    }
}

#[derive(Debug, Default)]
pub struct AnalysisPanel {
    radio: ALGO,
}

/// Returns `true` when the user clicks "Uruchom analizę".
pub fn show_analysis_panel(
    ctx: &egui::Context,
    state: &mut AnalysisPanel,
    results: &[SchedulingResult],
) -> bool {
    let mut run_clicked = false;

    egui::CentralPanel::default().show(ctx, |ui| {
        ui.label(egui::RichText::new("Harmonogramowanie — wyniki").strong());
        ui.separator();

        // ── Control row ───────────────────────────────────────────────────────
        ui.horizontal(|ui| {
            if ui.button("▶ Uruchom analizę").clicked() {
                run_clicked = true;
            }
            ui.separator();
            egui::ComboBox::from_label("Algorytm Gantt")
                .selected_text(state.radio.label())
                .show_ui(ui, |ui| {
                    ui.selectable_value(&mut state.radio, ALGO::McNaughton, "McNaughton");
                    ui.selectable_value(&mut state.radio, ALGO::LPT,        "LPT");
                    ui.selectable_value(&mut state.radio, ALGO::SplitSpdp,  "Split spdp-any");
                });
        });

        ui.separator();

        if results.is_empty() {
            ui.label("Brak wynikow. Zbierz dane i kliknij 'Uruchom analize'.");
            return;
        }

        // ── Results table ─────────────────────────────────────────────────────
        let col_w = (ui.available_width() / 4.0).max(90.0);
        egui::Grid::new("results_table").striped(true).show(ui, |ui| {
            for h in &["Algorytm", "Cmax [µs]", "Bazowy [µs]", "Poprawa"] {
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
                ui.add_sized(
                    [col_w, 20.0],
                    egui::Label::new(format!("{:.0}", r.cmax_us)),
                );
                ui.add_sized(
                    [col_w, 20.0],
                    egui::Label::new(format!("{:.0}", r.baseline_us)),
                );
                ui.add_sized(
                    [col_w, 20.0],
                    egui::Label::new(
                        egui::RichText::new(format!("{:+.1}%", imp)).color(color),
                    ),
                );
                ui.end_row();
            }
        });

        ui.separator();

        // ── Gantt for selected algorithm ──────────────────────────────────────
        let idx = state.radio.index();
        if let Some(result) = results.get(idx) {
            ui.label(format!(
                "Diagram Gantta -- {} (Cmax ~{:.0} us)",
                result.algorithm, result.cmax_us
            ));

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
        }
    });

    run_clicked
}

fn name_color(name: &str) -> egui::Color32 {
    let hash = name
        .bytes()
        .fold(0x811c9dc5u64, |acc, b| acc.wrapping_mul(0x01000193) ^ b as u64);
    let r = (((hash >> 16) & 0xff) as u8).max(60);
    let g = (((hash >> 8) & 0xff) as u8).max(60);
    let b = ((hash & 0xff) as u8).max(60);
    egui::Color32::from_rgb(r, g, b)
}
