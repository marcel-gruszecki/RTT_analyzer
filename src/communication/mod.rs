//! # Moduł Komunikacji i Algorytmów
//!
//! Projekt: Heurystyczne szeregowanie zadań w modelu Spdp-Any
//! Autor: Marcel Gruszecki (UAM)
//! Moduł: `communication`
//! Opis: Agreguje podmoduły odpowiedzialne za parsowanie protokołu JTAG/RTT,
//!       zarządzanie bazą wykonanych zadań oraz uruchamianie algorytmów szeregowania.

pub mod heuristics;
pub mod protocol;
pub mod scheduler;
pub mod session;

pub use heuristics::{run_all, run_one, SchedulingResult};
pub use scheduler::{TaskDatabase, TaskExecution, TaskScheduleData};
pub use session::TracerSession;