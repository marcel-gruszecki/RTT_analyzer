use std::error::Error;
use std::time::Duration;
use std::thread;

use probe_rs::{Permissions, config::TargetSelector, probe::list::Lister};
use probe_rs::rtt::{Rtt, ScanRegion};

use super::protocol::{FrameReader, PKT_LEN, EVT_IN, EVT_OUT, task_name};
use super::scheduler::{TaskEvent, EventKind};

// HP SRAM on ESP32-P4: 0x4ff00000 .. 0x4ffc0000 (768 KB)
const RTT_SCAN_START: u64 = 0x4ff0_0000;
const RTT_SCAN_END:   u64 = 0x4ffc_0000;
const RTT_CHANNEL:    usize = 1;   // channel 0 = console, channel 1 = tracer

pub struct TracerSession {
    session: probe_rs::Session,
    rtt:     Rtt,
    framer:  FrameReader,
}

impl TracerSession {
    /// Connects to the first available JTAG probe, attaches to `chip`,
    /// waits for firmware to initialise and locates the RTT control block.
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

        // Boot the core and give the firmware time to call tracer_init().
        {
            let mut core = session.core(0)?;
            core.run()?;
        }
        println!("Rdzeń uruchomiony, czekam 500ms na init firmware...");
        thread::sleep(Duration::from_millis(500));

        // Scan for the RTT control block. Retry until firmware has written it.
        let scan = ScanRegion::range(RTT_SCAN_START..RTT_SCAN_END);
        println!("Szukam bloku RTT w HP SRAM ({:#010x}..{:#010x})...",
            RTT_SCAN_START, RTT_SCAN_END);

        let rtt = loop {
            let mut core = session.core(0)?;
            match Rtt::attach_region(&mut core, &scan) {
                Ok(r) => {
                    println!("Znaleziono blok RTT pod adresem: {:#010x}", r.ptr());
                    core.run()?;
                    break r;
                }
                Err(e) => {
                    eprint!("\r  [{:?}] próbuję ponownie...", e);
                    thread::sleep(Duration::from_millis(200));
                }
            }
        };

        Ok(Self { session, rtt, framer: FrameReader::new() })
    }

    /// Reads whatever bytes are available in the RTT channel and returns all
    /// complete, valid task events decoded from them.
    /// Non-blocking: returns an empty Vec if no new data has arrived.
    pub fn read_events(&mut self) -> Result<Vec<TaskEvent>, Box<dyn Error>> {
        let mut raw_buf = [0u8; PKT_LEN * 64];

        // Explicit field borrows let the borrow checker see that `session`
        // and `rtt` are independent, so `core` (from session) and the channel
        // (from rtt) can coexist.
        let session = &mut self.session;
        let rtt     = &mut self.rtt;

        let mut core = session.core(0)?;
        if core.core_halted()? {
            eprintln!("[warn] rdzeń był zatrzymany — wznawiam");
            core.run()?;
        }

        let n = rtt
            .up_channels()
            .get_mut(RTT_CHANNEL)
            .ok_or("brak kanału RTT 1 — czy tracer_init() zostało wywołane?")?
            .read(&mut core, &mut raw_buf)?;

        drop(core); // release borrow on session before touching self.framer

        let mut events = Vec::new();
        if n > 0 {
            self.framer.push(&raw_buf[..n]);
            while let Some(pkt) = self.framer.next_packet() {
                let kind = match pkt.event_type {
                    EVT_IN  => EventKind::SwitchedIn,
                    EVT_OUT => EventKind::SwitchedOut,
                    other   => {
                        eprintln!("[warn] nieznany typ zdarzenia: {other:#x}");
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
