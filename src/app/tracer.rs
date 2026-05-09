use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use crate::communication::{TaskDatabase, TracerSession};

#[derive(Clone, Debug, PartialEq)]
pub enum ConnectionStatus {
    Disconnected,
    Connecting,
    Connected,
    Error(String),
}

pub struct TracerHandle {
    pub db: Arc<Mutex<TaskDatabase>>,
    status: Arc<Mutex<ConnectionStatus>>,
    stop_flag: Arc<AtomicBool>,
}

impl TracerHandle {
    pub fn new() -> Self {
        TracerHandle {
            db: Arc::new(Mutex::new(TaskDatabase::new())),
            status: Arc::new(Mutex::new(ConnectionStatus::Disconnected)),
            stop_flag: Arc::new(AtomicBool::new(false)),
        }
    }

    /// Spawn a background thread that connects to `chip` and streams events
    /// into the shared database.
    pub fn connect(&mut self, chip: &str) {
        self.stop_flag.store(false, Ordering::SeqCst);
        *self.db.lock().unwrap() = TaskDatabase::new();
        *self.status.lock().unwrap() = ConnectionStatus::Connecting;

        let db = Arc::clone(&self.db);
        let status = Arc::clone(&self.status);
        let stop = Arc::clone(&self.stop_flag);
        let chip = chip.to_string();

        thread::spawn(move || {
            match TracerSession::connect(&chip) {
                Err(e) => {
                    *status.lock().unwrap() = ConnectionStatus::Error(e.to_string());
                }
                Ok(mut session) => {
                    *status.lock().unwrap() = ConnectionStatus::Connected;
                    while !stop.load(Ordering::SeqCst) {
                        match session.read_events() {
                            Err(e) => {
                                *status.lock().unwrap() =
                                    ConnectionStatus::Error(e.to_string());
                                break;
                            }
                            Ok(events) => {
                                let had_events = !events.is_empty();
                                let mut db_lock = db.lock().unwrap();
                                for event in events {
                                    db_lock.push_event(event);
                                }
                                drop(db_lock);
                                if !had_events {
                                    thread::sleep(Duration::from_millis(10));
                                }
                            }
                        }
                    }
                }
            }
        });
    }

    /// Signal the background thread to stop and mark status as disconnected.
    pub fn disconnect(&self) {
        self.stop_flag.store(true, Ordering::SeqCst);
        *self.status.lock().unwrap() = ConnectionStatus::Disconnected;
    }

    pub fn status(&self) -> ConnectionStatus {
        self.status.lock().unwrap().clone()
    }
}
