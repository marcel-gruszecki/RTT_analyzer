mod tracer;
mod ui;

use std::time::Duration;

use eframe::egui;
use crate::komunikacja::{run_all, SchedulingResult, TaskExecution, TaskScheduleData};

use tracer::{ConnectionStatus, TracerHandle};

pub struct MyEguiApp {
    current_panel:       ui::top_panel::PANEL,
    analysis_struct:     ui::analysis_panel::AnalysisPanel,
    tracer:              TracerHandle,
    chip:                String,
    scheduling_results:  Vec<SchedulingResult>,
}

impl Default for MyEguiApp {
    fn default() -> Self {
        MyEguiApp {
            current_panel:      Default::default(),
            analysis_struct:    Default::default(),
            tracer:             TracerHandle::new(),
            chip:               "esp32p4".to_string(),
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

        // Keep repainting while connected so the live view stays fresh.
        if matches!(status, ConnectionStatus::Connecting | ConnectionStatus::Connected) {
            ctx.request_repaint_after(Duration::from_millis(100));
        }

        // Brief lock — snapshot the data we need for this frame.
        let (completed, schedule_data): (Vec<TaskExecution>, Vec<TaskScheduleData>) = {
            let db = self.tracer.db.lock().unwrap();
            (db.completed.clone(), db.schedule_data())
        };

        // Top panel returns the action triggered by the user.
        let action = ui::top_panel::show_top_panel(
            ctx,
            &mut self.current_panel,
            &mut self.chip,
            &status,
        );
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

        match self.current_panel {
            ui::top_panel::PANEL::TASK => {
                ui::task_panel::show_task_panel(ctx, &completed, &schedule_data);
            }
            ui::top_panel::PANEL::ANALYSIS => {
                let run_clicked = ui::analysis_panel::show_analysis_panel(
                    ctx,
                    &mut self.analysis_struct,
                    &self.scheduling_results,
                );
                if run_clicked && !completed.is_empty() {
                    self.scheduling_results = run_all(&completed);
                }
            }
        }
    }
}
