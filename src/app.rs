//! # Główny Kontroler Aplikacji
//!
//! Projekt: RTT-Task Analyser
//! Autor: Marcel Gruszecki (UAM)
//! Moduł: `app`
//! Opis: Zarządza głównym stanem GUI, synchronizuje dane z wątku RTT oraz przekazuje pakiety zadań do algorytmów.


mod ui;

use std::time::Duration;

use eframe::egui;
use crate::communication::{run_all, run_one, SchedulingResult, TaskExecution, TaskScheduleData};

pub(crate) mod tracer;
use tracer::{ConnectionStatus, TracerHandle};

/// Główna struktura przechowująca stan aplikacji graficznej.
pub struct MyEguiApp {
    current_panel: ui::top_panel::PANEL,
    analysis_struct: ui::analysis_panel::AnalysisPanel,
    tracer: TracerHandle,
    chip: String,
    scheduling_results: Vec<SchedulingResult>,
}

impl Default for MyEguiApp {
    fn default() -> Self {
        MyEguiApp {
            current_panel: Default::default(),
            analysis_struct: Default::default(),
            tracer: TracerHandle::new(),
            chip: "esp32p4".to_string(),
            scheduling_results: Vec::new(),
        }
    }
}

impl MyEguiApp {
    pub fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        Self::default()
    }
}

impl eframe::App for MyEguiApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let status = self.tracer.status();

        if matches!(status, ConnectionStatus::Connecting | ConnectionStatus::Connected) {
            ctx.request_repaint_after(Duration::from_millis(100));
        }

        // Pobranie migawki danych z bazy wątku RTT
        let (completed, schedule_data): (Vec<TaskExecution>, Vec<TaskScheduleData>) = {
            let db = self.tracer.db.lock().unwrap();
            (db.completed.clone(), db.schedule_data())
        };

        // Panel górny i akcje połączenia
        let action = ui::top_panel::show_top_panel(ctx, &mut self.current_panel, &mut self.chip, &status);
        match action {
            ui::top_panel::TopPanelAction::Connect => {
                self.tracer.connect(&self.chip.clone());
            }
            ui::top_panel::TopPanelAction::Disconnect => {
                self.tracer.disconnect();
                self.scheduling_results.clear();
            }
            ui::top_panel::TopPanelAction::None => {}
        }

        // Przełączanie widoków głównych
        match self.current_panel {
            ui::top_panel::PANEL::TASK => {
                ui::task_panel::show_task_panel(ctx, &completed, &schedule_data);
            }
            ui::top_panel::PANEL::ANALYSIS => {
                let action = ui::analysis_panel::show_analysis_panel(ctx, &mut self.analysis_struct, &self.scheduling_results);
                match action {
                    ui::analysis_panel::AnalysisAction::RunAll => {
                        if !completed.is_empty() {
                            self.scheduling_results = run_all(&completed);
                        }
                    }
                    ui::analysis_panel::AnalysisAction::RunSelected(idx) => {
                        if !completed.is_empty() {
                            self.scheduling_results = vec![run_one(&completed, idx)];
                        }
                    }
                    ui::analysis_panel::AnalysisAction::None => {}
                }
            }
        }
    }

    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) {
        println!("[egui] Zamknięcie aplikacji, czyszczenie połączeń.");
        self.tracer.disconnect();
    }
}