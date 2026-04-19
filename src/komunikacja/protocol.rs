// Packet format (must match task_tracer.h on the ESP32 side):
// magic(2) + type(1) + core(1) + prio(1) + task_id(4) + name(16) + ts(8) + chk(1) = 34 bytes

pub const MAGIC: [u8; 2] = [0xAB, 0xCD];
pub const PKT_LEN: usize = 34;

pub const EVT_IN:  u8 = 0x01;
pub const EVT_OUT: u8 = 0x02;

pub struct RawPacket {
    pub event_type:   u8,
    pub core_id:      u8,
    pub priority:     u8,
    pub task_id:      u32,
    pub name_raw:     [u8; 16],
    pub timestamp_us: u64,
}

pub fn parse_packet(buf: &[u8; PKT_LEN]) -> Option<RawPacket> {
    if buf[0] != MAGIC[0] || buf[1] != MAGIC[1] {
        return None;
    }
    let chk: u8 = buf[..PKT_LEN - 1].iter().fold(0u8, |a, &x| a ^ x);
    if chk != buf[PKT_LEN - 1] {
        return None;
    }
    Some(RawPacket {
        event_type:   buf[2],
        core_id:      buf[3],
        priority:     buf[4],
        task_id:      u32::from_le_bytes(buf[5..9].try_into().unwrap()),
        name_raw:     buf[9..25].try_into().unwrap(),
        timestamp_us: u64::from_le_bytes(buf[25..33].try_into().unwrap()),
    })
}

pub fn task_name(raw: &[u8; 16]) -> String {
    let end = raw.iter().position(|&b| b == 0).unwrap_or(16);
    String::from_utf8_lossy(&raw[..end]).into_owned()
}

// ── FrameReader — accumulates RTT bytes, emits complete packets ───────────────

pub struct FrameReader {
    buf: Vec<u8>,
}

impl FrameReader {
    pub fn new() -> Self {
        Self { buf: Vec::with_capacity(PKT_LEN * 8) }
    }

    pub fn push(&mut self, data: &[u8]) {
        self.buf.extend_from_slice(data);
    }

    /// Returns the next complete, checksum-valid packet, or None if not enough
    /// bytes have arrived yet.
    pub fn next_packet(&mut self) -> Option<RawPacket> {
        loop {
            let pos = self.buf.windows(2).position(|w| w == MAGIC)?;
            if pos > 0 {
                self.buf.drain(..pos);
            }
            if self.buf.len() < PKT_LEN {
                return None;
            }
            let raw: [u8; PKT_LEN] = self.buf[..PKT_LEN].try_into().unwrap();
            self.buf.drain(..PKT_LEN);
            if let Some(pkt) = parse_packet(&raw) {
                return Some(pkt);
            }
            // Bad checksum — the 0xAB was a false positive; push 0xCD back
            // so the next scan can find a real magic sequence starting there.
            self.buf.insert(0, MAGIC[1]);
        }
    }
}
