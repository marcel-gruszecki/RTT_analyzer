//! # RTT-Task Analyser
//!
//! Projekt: RTT-Task Analyser
//! Autor: Marcel Gruszecki (Uniwersytet im. Adama Mickiewicza w Poznaniu)
//! Moduł: Główny program uruchomieniowy
//! Description: Inicjalizuje środowisko powiązane ze sprzętem (ESP32-P4).
//!              Konfiguruje i uruchamia natywny kontekst graficzny `eframe` oraz montuje
//!              główną maszynę stanów aplikacji `MyEguiApp` do wizualizacji strumienia danych RTT.

mod app;
mod communication;

fn main() {
    // Przygotowanie standardowej konfiguracji okna aplikacji dla systemu desktopowego
    let native_options = eframe::NativeOptions::default();

    // Tworzenie natywnego okna i uruchomienie cyklu życia aplikacji egui
    let _ = eframe::run_native(
        "RTT-Task Analyser",
        native_options,
        Box::new(|cc| Ok(Box::new(app::MyEguiApp::new(cc)))),
    );
}