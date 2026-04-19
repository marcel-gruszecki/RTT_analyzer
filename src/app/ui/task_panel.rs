use eframe::egui;
use egui_plot::{Bar, BarChart, Plot, PlotBounds, PlotPoint, Text};

use crate::komunikacja::{TaskExecution, TaskScheduleData};

/// Visible time window on the Gantt (5 seconds).
const WINDOW_US: u64 = 5_000_000;

/// Blue shades — distinguishable per task, all in the blue family.
const BLUES: &[(u8, u8, u8)] = &[
    ( 30, 120, 220),
    ( 70, 170, 255),
    ( 15,  75, 175),
    (100, 200, 255),
    ( 50, 100, 200),
    ( 10, 150, 210),
    ( 80, 140, 255),
    ( 20,  60, 150),
];

fn task_color(name: &str) -> egui::Color32 {
    let idx = name.bytes().fold(0usize, |a, b| a.wrapping_add(b as usize)) % BLUES.len();
    let (r, g, b) = BLUES[idx];
    egui::Color32::from_rgb(r, g, b)
}

pub fn show_task_panel(
    ctx: &egui::Context,
    execs: &[TaskExecution],
    schedule_data: &[TaskScheduleData],
) {
    show_task_left_panel(ctx, schedule_data);
    show_task_right_panel(ctx, execs, schedule_data);
    show_central_panel_task(ctx, execs);
}

pub fn show_task_left_panel(ctx: &egui::Context, schedule_data: &[TaskScheduleData]) {
    egui::SidePanel::left("side_panel_left")
        .min_width(110.0)
        .show(ctx, |ui| {
            ui.label(egui::RichText::new("Zadania").strong());
            ui.separator();
            if schedule_data.is_empty() {
                ui.label("(brak danych)");
                return;
            }
            let mut seen = std::collections::HashSet::new();
            for s in schedule_data {
                if seen.insert(s.name.as_str()) {
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new("●").color(task_color(&s.name)));
                        ui.label(&s.name);
                    });
                }
            }
        });
}

pub fn show_task_right_panel(
    ctx: &egui::Context,
    execs: &[TaskExecution],
    schedule_data: &[TaskScheduleData],
) {
    egui::SidePanel::right("side_panel_right")
        .min_width(150.0)
        .show(ctx, |ui| {
            ui.label(egui::RichText::new("System").strong());
            ui.separator();

            let unique_tasks: std::collections::HashSet<&str> =
                schedule_data.iter().map(|s| s.name.as_str()).collect();
            ui.label(format!("Zadania:   {}", unique_tasks.len()));
            ui.label(format!("Wykonania: {}", execs.len()));

            ui.separator();

            let min_start = execs.iter().map(|e| e.start_us).min().unwrap_or(0);
            let max_end   = execs.iter().map(|e| e.end_us).max().unwrap_or(0);
            let wall_us   = max_end.saturating_sub(min_start);

            if wall_us > 0 {
                let mut busy = [0u64; 2];
                for e in execs {
                    busy[(e.core_id as usize) % 2] =
                        busy[(e.core_id as usize) % 2].saturating_add(e.duration_us());
                }
                for (i, &b) in busy.iter().enumerate() {
                    let pct = (b as f64 / wall_us as f64 * 100.0).min(100.0);
                    ui.label(format!("CPU rdzen {}: {:.1}%", i, pct));
                }
            } else {
                ui.label("CPU rdzen 0: —");
                ui.label("CPU rdzen 1: —");
            }
        });
}

pub fn show_central_panel_task(ctx: &egui::Context, execs: &[TaskExecution]) {
    egui::CentralPanel::default().show(ctx, |ui| {
        ui.label(format!("Zakonczone wykonania: {}", execs.len()));
        ui.separator();

        // ── Sliding Gantt — 5-second rolling window ───────────────────────────
        let t_now   = execs.iter().map(|e| e.end_us).max().unwrap_or(WINDOW_US);
        let t_start = t_now.saturating_sub(WINDOW_US);

        // Keep bars from a slightly wider range so the user can scroll back a bit.
        let fetch_start = t_now.saturating_sub(WINDOW_US * 3);
        let visible: Vec<&TaskExecution> = execs
            .iter()
            .filter(|e| e.end_us >= fetch_start)
            .collect();

        let bars: Vec<Bar> = visible.iter().map(|e| {
            Bar::new(e.core_id as f64, e.duration_us() as f64)
                .base_offset(e.start_us as f64)
                .horizontal()
                .fill(task_color(&e.name))
                .stroke(egui::Stroke::new(1.0, egui::Color32::from_black_alpha(80)))
                .name(&e.name)
                .width(0.6)
        }).collect();

        let chart = BarChart::new("Gantt wykonan", bars).horizontal();

        let t_start_f = t_start as f64;
        Plot::new("gantt_task")
            .height(150.0)
            .allow_drag(egui::Vec2b::new(true, false))
            .allow_scroll(egui::Vec2b::new(true, false))
            .allow_zoom(egui::Vec2b::new(true, false))
            .allow_boxed_zoom(false)
            .include_y(-0.5)
            .include_y(1.5)
            .y_axis_formatter(|mark, _range| match mark.value.round() as i32 {
                0 => "Rdzen 0".to_string(),
                1 => "Rdzen 1".to_string(),
                _ => String::new(),
            })
            .x_axis_formatter(move |mark, _range| {
                let rel_ms = (mark.value - t_start_f) / 1_000.0;
                if rel_ms < -500.0 { String::new() } else { format!("{:.0}ms", rel_ms) }
            })
            .show(ui, |plot_ui| {
                // Pin the view to the latest 5-second window every frame.
                plot_ui.set_plot_bounds(PlotBounds::from_min_max(
                    [t_start as f64, -0.5],
                    [t_now   as f64,  1.5],
                ));
                plot_ui.bar_chart(chart);
                for e in &visible {
                    let cx = e.start_us as f64 + e.duration_us() as f64 / 2.0;
                    plot_ui.text(
                        Text::new(
                            format!("lbl_{}_{}", e.task_id, e.start_us),
                            PlotPoint::new(cx, e.core_id as f64),
                            e.name.as_str(),
                        )
                        .color(egui::Color32::WHITE),
                    );
                }
            });

        ui.separator();

        // ── Execution log table ───────────────────────────────────────────────
        let col_w = ui.available_width() / 5.0;

        egui::Grid::new("exec_table_header").show(ui, |ui| {
            for h in &["Zadanie", "Start [us]", "Koniec [us]", "Rdzen", "Czas [us]"] {
                ui.add_sized(
                    [col_w, 20.0],
                    egui::Label::new(egui::RichText::new(*h).strong()),
                );
            }
            ui.end_row();
        });

        egui::ScrollArea::vertical()
            .id_salt("exec_table_body")
            .max_height(ui.available_height() - 4.0)
            .stick_to_bottom(true)
            .show(ui, |ui| {
                egui::Grid::new("exec_table_rows")
                    .striped(true)
                    .show(ui, |ui| {
                        for e in execs {
                            ui.add_sized(
                                [col_w, 18.0],
                                egui::Label::new(
                                    egui::RichText::new(&e.name).color(task_color(&e.name)),
                                ),
                            );
                            ui.add_sized([col_w, 18.0], egui::Label::new(e.start_us.to_string()));
                            ui.add_sized([col_w, 18.0], egui::Label::new(e.end_us.to_string()));
                            ui.add_sized([col_w, 18.0], egui::Label::new(e.core_id.to_string()));
                            ui.add_sized(
                                [col_w, 18.0],
                                egui::Label::new(e.duration_us().to_string()),
                            );
                            ui.end_row();
                        }
                    });
            });
    });
}
