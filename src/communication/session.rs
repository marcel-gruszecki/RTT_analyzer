//! # Zarządzanie Sesją Sprzętową JTAG/RTT
//!
//! Projekt: RTT-Task Analyser
//! Autor: Marcel Gruszecki (UAM)
//! Moduł: `communication::session`
//! Opis: Odpowiada za inicjalizację połączenia JTAG za pośrednictwem biblioteki `probe-rs`.
//!       Skanuje pamięć HP SRAM mikrokontrolera ESP32-P4 w celu lokalizacji bloku kontrolnego RTT,
//!       a następnie w sposób nieblokujący pobiera strumień bajtów z dedykowanego kanału telemetrii.

use std::error::Error;
use std::time::Duration;
use std::thread;

use probe_rs::{Permissions, config::TargetSelector, probe::list::Lister};
use probe_rs::rtt::{Rtt, ScanRegion};

use super::protocol::{FrameReader, PKT_LEN, EVT_IN, EVT_OUT, task_name};
use super::scheduler::{TaskEvent, EventKind};

// Adresy pamięci High-Performance SRAM dla ESP32-P4 (768 KB)
const RTT_SCAN_START: u64 = 0x4ff0_0000;
const RTT_SCAN_END:   u64 = 0x4ffc_0000;
const RTT_CHANNEL:    usize = 1;

/// Struktura reprezentująca aktywną sesję debugowania sprzętowego.
pub struct TracerSession {
    session: probe_rs::Session,
    rtt:     Rtt,
    framer:  FrameReader,
}

impl Drop for TracerSession {
    /// Bezpiecznie zamyka sesję, czyści sprzętowe punkty przerwań i wznawia pracę procesora.
    fn drop(&mut self) {
        println!("\n[JTAG] Zamykanie sesji, czyszczenie rejestrów i wznawianie rdzeni...");
        if let Ok(mut core) = self.session.core(0) {
            let _ = core.clear_all_hw_breakpoints();
            let _ = core.run();
        }
    }
}

impl TracerSession {
    /// Łączy się z programatorem JTAG, inicjalizuje procesor i lokalizuje blok pamięci RTT.
    pub fn connect(chip: &str) -> Result<Self, Box<dyn Error>> {
        let lister = Lister::new();
        let probes = lister.list_all();
        if probes.is_empty() {
            return Err("nie znaleziono programatora JTAG (sprawdź podłączenie USB)".into());
        }

        println!("Znalezione programatory:");
        for (i, info) in probes.iter().enumerate() {
            println!("  [{}] {:?}", i, info);
        }

        let probe = probes[0].open()?;
        let mut session = probe.attach(
            TargetSelector::Unspecified(chip.to_string()),
            Permissions::default(),
        )?;

        // Uruchomienie rdzenia i oczekiwanie na start oprogramowania układowego
        {
            let mut core = session.core(0)?;
            core.run()?;
        }
        println!("Rdzeń uruchomiony, czekam 500ms na init firmware...");
        thread::sleep(Duration::from_millis(500));

        // Skanowanie wskazanego regionu HP SRAM w poszukiwaniu bloku RTT
        let scan = ScanRegion::range(RTT_SCAN_START..RTT_SCAN_END);
        println!("Szukam bloku RTT w HP SRAM ({:#010x}..{:#010x})...", RTT_SCAN_START, RTT_SCAN_END);

        let rtt = loop {
            let mut core = session.core(0)?;
            match Rtt::attach_region(&mut core, &scan) {
                Ok(r) => {
                    println!("Znaleziono blok RTT pod adresem: {:#010x}", r.ptr());
                    core.run()?;
                    break r;
                }
                Err(e) => {
                    eprintln!("\r  [{:?}] próbuję ponownie...", e);
                    thread::sleep(Duration::from_millis(200));
                }
            }
        };

        Ok(Self { session, rtt, framer: FrameReader::new() })
    }

    /// Pobiera dostępne bajty z bufora kołowego RTT i dekoduje je na wektor zdarzeń.
    pub fn read_events(&mut self) -> Result<Vec<TaskEvent>, Box<dyn Error>> {
        // Zwiększony bufor do 512 pakietów zapobiega gubieniu danych przy szybkich przełączeniach kontekstu
        let mut raw_buf = [0u8; PKT_LEN * 512];

        let session = &mut self.session;
        let rtt     = &mut self.rtt;

        let mut core = session.core(0)?;
        if core.core_halted()? {
            eprintln!("[warn] rdzeń był zatrzymany — wznawiam");
            core.run()?;
        }

        // Odczyt surowego strumienia bajtów z kanału telemetrycznego przez interfejs JTAG
        let n = rtt
            .up_channels()
            .get_mut(RTT_CHANNEL)
            .ok_or("brak kanału RTT 1 — czy tracer_init() zostało wywołane?")?
            .read(&mut core, &mut raw_buf)?;

        drop(core);

        let mut events = Vec::new();
        if n > 0 {
            self.framer.push(&raw_buf[..n]);
            // Pętla dekodująca kompletne pakiety binarne
            while let Some(pkt) = self.framer.next_packet() {
                let kind = match pkt.event_type {
                    EVT_IN  => EventKind::SwitchedIn,
                    EVT_OUT => EventKind::SwitchedOut,
                    other   => {
                        eprintln!("[warn] Uszkodzona ramka RTT lub nieznany typ: {other:#x}");
                        continue;
                    }
                };
                events.push(TaskEvent {
                    task_id:      pkt.task_id,
                    name:         task_name(&pkt.name_raw),
                    core_id:      pkt.core_id,
                    priority:     pkt.priority,
                    timestamp_us: pkt.timestamp_us,
                    kind,
                });
            }
        }
        Ok(events)
    }
}