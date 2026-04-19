use eframe::egui;

use crate::app::tracer::ConnectionStatus;

#[derive(Clone, Debug, Default)]
pub enum PANEL {
    #[default]
    TASK,
    ANALYSIS,
}

pub enum TopPanelAction {
    None,
    Connect,
    Disconnect,
}

pub fn show_top_panel(
    ctx: &egui::Context,
    last_panel: &mut PANEL,
    chip: &mut String,
    status: &ConnectionStatus,
) -> TopPanelAction {
    let mut action = TopPanelAction::None;

    egui::TopBottomPanel::top("top_panel").show(ctx, |ui| {
        ui.horizontal(|ui| {
            if ui.button("Wykresy").clicked() {
                *last_panel = PANEL::TASK;
            }
            if ui.button("Analiza").clicked() {
                *last_panel = PANEL::ANALYSIS;
            }

            ui.separator();

            let is_busy = matches!(
                status,
                ConnectionStatus::Connecting | ConnectionStatus::Connected
            );
            ui.label("Chip:");
            ui.add_enabled(
                !is_busy,
                egui::TextEdit::singleline(chip).desired_width(80.0),
            );

            match status {
                ConnectionStatus::Disconnected => {
                    if ui.button("Połącz").clicked() {
                        action = TopPanelAction::Connect;
                    }
                }
                ConnectionStatus::Connecting => {
                    ui.spinner();
                    ui.label("Łączenie...");
                    if ui.button("Anuluj").clicked() {
                        action = TopPanelAction::Disconnect;
                    }
                }
                ConnectionStatus::Connected => {
                    ui.colored_label(egui::Color32::from_rgb(46, 164, 79), "● Połączono");
                    if ui.button("Rozłącz").clicked() {
                        action = TopPanelAction::Disconnect;
                    }
                }
                ConnectionStatus::Error(msg) => {
                    ui.colored_label(
                        egui::Color32::RED,
                        format!("✗ {}", msg),
                    );
                    if ui.button("Ponów").clicked() {
                        action = TopPanelAction::Connect;
                    }
                }
            }
        });
    });

    action
}
