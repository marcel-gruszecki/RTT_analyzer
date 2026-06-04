//! # Warstwa Interfejsu Użytkownika (UI)
//!
//! Projekt: RTT-Task Analyser
//! Autor: Marcel Gruszecki (UAM)
//! Moduł: `app::ui`
//! Opis: Główny punkt dostępowy dla modułów graficznych aplikacji.
//!       Agreguje i re-eksportuje komponenty odpowiedzialne za renderowanie paska górnego,
//!       osi czasu rzeczywistego zadań oraz panelu analitycznego algorytmów szeregowania.

pub mod task_panel;
pub mod analysis_panel;
pub mod top_panel;