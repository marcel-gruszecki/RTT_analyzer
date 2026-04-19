# Wire Protocol

Every event is a fixed-size 30-byte packet. No length framing needed.

## Packet layout (little-endian, packed)

| Offset | Size | Field         | Description                          |
|--------|------|---------------|--------------------------------------|
| 0      | 1    | magic[0]      | 0xAB (sync byte)                     |
| 1      | 1    | magic[1]      | 0xCD (sync byte)                     |
| 2      | 1    | type          | 0x01 = switched-in, 0x02 = switched-out |
| 3      | 1    | core_id       | 0 or 1                               |
| 4      | 1    | priority      | FreeRTOS priority (0 = lowest)       |
| 5      | 1    | _pad          | reserved, always 0                   |
| 6      | 16   | name          | null-terminated task name (UTF-8)    |
| 22     | 8    | timestamp_us  | µs since boot (uint64_t LE)          |
| 30 - 1 | 1   | checksum      | XOR of bytes [0..28]                 |

Total: **30 bytes**

## Transport: JTAG via OpenOCD

Data flows: ESP32-P4 → JTAG → OpenOCD → TCP socket → Rust app

### 1. Enable in sdkconfig (menuconfig)
```
Component config → Application Level Tracing
  [*] Enable application tracing
  Data Destination: JTAG
```

### 2. Start OpenOCD and open TCP trace port
```bash
openocd -f board/esp32p4-evk.cfg \
  -c "init; esp apptrace start tcp://localhost:3335 0; reset run"
```
`0` = no packet limit (stream until stopped). The Rust app connects to port 3335.

### 3. Stop tracing (OpenOCD telnet on port 4444)
```
esp apptrace stop
```

---

## Rust parsing sketch

```rust
use std::io::Read;
use std::net::TcpStream;

const MAGIC: [u8; 2] = [0xAB, 0xCD];
const PKT_LEN: usize = 30;

#[repr(C, packed)]
#[derive(Copy, Clone)]
struct Packet {
    magic:        [u8; 2],
    event_type:   u8,   // 1 = switched-in, 2 = switched-out
    core_id:      u8,
    priority:     u8,
    _pad:         u8,
    name:         [u8; 16],
    timestamp_us: u64,  // little-endian
    checksum:     u8,
}

fn sync_and_read(stream: &mut TcpStream) -> Option<Packet> {
    let mut buf = [0u8; PKT_LEN];
    let mut b = [0u8; 1];
    loop {
        stream.read_exact(&mut b).ok()?;
        if b[0] != MAGIC[0] { continue; }
        stream.read_exact(&mut b).ok()?;
        if b[0] != MAGIC[1] { continue; }
        buf[0] = MAGIC[0];
        buf[1] = MAGIC[1];
        stream.read_exact(&mut buf[2..]).ok()?;
        let chk: u8 = buf[..PKT_LEN - 1].iter().fold(0, |a, &x| a ^ x);
        if chk == buf[PKT_LEN - 1] {
            return Some(unsafe { std::mem::transmute(buf) });
        }
    }
}

fn main() {
    let mut stream = TcpStream::connect("127.0.0.1:3335").unwrap();
    while let Some(pkt) = sync_and_read(&mut stream) {
        let name = std::str::from_utf8(&pkt.name)
            .unwrap_or("?")
            .trim_end_matches('\0');
        let ts = u64::from_le_bytes(pkt.timestamp_us.to_ne_bytes());
        println!("[core{}] {:?} '{}' prio={} t={}µs",
            pkt.core_id,
            if pkt.event_type == 1 { "IN " } else { "OUT" },
            name, pkt.priority, ts);
    }
}
```
