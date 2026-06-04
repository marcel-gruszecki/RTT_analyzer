//! # Górny Pasek Nawigacji i Statusu Połączenia
//!
//! Projekt: RTT-Task Analyser
//! Autor: Marcel Gruszecki (UAM)
//! Moduł: `app::ui::top_panel`
//! Opis: Implementuje komponent górnego paska narzędziowego. Odpowiada za globalną nawigację
//!       pomiędzy panelami (Zadania / Analiza) oraz obsługę i wizualizację stanów połączenia
//!       sprzętowego z mikrokontrolerem (w tym obsługę błędów, ładowania i rozłączania).

use eframe::egui;

use crate::app::tracer::ConnectionStatus;

/// Identyfikatory widoków paneli głównych aplikacji.
#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub enum PANEL {
    #[default]
    TASK,
    ANALYSIS,
}

/// Zdarzenia sterujące stanem wątku telemetrycznego przekazywane do pętli głównej.
pub enum TopPanelAction {
    None,
    Connect,
    Disconnect,
}

/// Funkcja renderująca górny panel interfejsu oraz kontrolery połączenia sprzętowego.
pub fn show_top_panel(
    ctx: &egui::Context,
    last_panel: &mut PANEL,
    chip: &mut String,
    status: &ConnectionStatus,
) -> TopPanelAction {
    let mut action = TopPanelAction::None;

    egui::TopBottomPanel::top("top_panel").show(ctx, |ui| {
        ui.horizontal(|ui| {
            // Przyciski nawigacyjne głównych paneli roboczych
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

            // Maszyna stanów UI dopasowująca widgety i akcje do statusu połączenia JTAG
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