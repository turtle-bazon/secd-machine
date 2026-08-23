# Split Keyboard Communication Protocols

## Overview

A split keyboard has two halves. One half (usually right, or whichever has
the USB receiver) is the **master** — it connects to the PC and sends HID
reports. The other half is the **slave** — it scans its own matrix and
sends key events to the master.

The master combines both halves' inputs into a single HID report.

## Protocol options

### 1. UART over TRRS cable (wired)

Traditional QMK-style approach. Simple, zero latency, no radio needed.

```
LEFT HALF                    RIGHT HALF (master)
┌──────────┐   TRRS cable    ┌──────────────┐
│ Matrix    │──TX ────────RX──│              │
│ Scan      │──RX ◄────────TX─│  USB to PC   │
└──────────┘                  └──────────────┘
```

- **Speed**: instant (hardware UART)
- **Latency**: <1ms
- **Reliability**: perfect (wired)
- **Downside**: cable between halves defeats wireless purpose

---

### 2. BLE Nordic UART Service (NUS) — nRF52840

Standard Nordic protocol. Slave advertises NUS, master connects as central.

```
SLAVE (peripheral)           MASTER (central + HID peripheral)
┌──────────────┐   BLE NUS   ┌──────────────┐
│ Matrix scan  │◄───────────►│ USB or       │
│ Key events   │  ~7-15ms    │ BLE HID      │──► PC
└──────────────┘             └──────────────┘
```

- **Latency**: 7.5–15ms per connection interval
- **Throughput**: ~20KB/s practical
- **Protocol**: custom GATT characteristic for key events
- **Power**: moderate (BLE connection maintained)
- **Complexity**: medium — SoftDevice central+peripheral multiprotocol

Master runs BOTH roles:
- Central: connects to slave's NUS characteristic
- Peripheral: advertises HID to PC

This requires SoftDevice multiprotocol support (S140 supports this natively).

---

### 3. ESB (Enhanced ShockBurst) — nRF52840 only

Nordic's proprietary low-latency radio protocol. No BLE stack needed
for inter-half communication — just direct radio packets.

```
SLAVE                        MASTER (runs BLE HID)
┌──────────────┐   ESB       ┌──────────────┐
│ Matrix scan  │◄───────────►│ BLE HID      │──► PC
│ Key events   │  <1ms       │ to computer  │
└──────────────┘             └──────────────┘
```

- **Latency**: <1ms per packet
- **Throughput**: 1–2Mbps raw
- **Protocol**: address + payload + CRC, auto-ACK optional
- **Power**: lowest (radio on only during transmission)
- **Complexity**: low — simple register-level API
- **Downside**: proprietary to Nordic, no standard

ESB and BLE can coexist on the same radio with careful timeslot
management, but it's tricky. Usually: slave uses pure ESB,
master switches between ESB (listening for slave) and BLE (HID to PC).

This is what Nice!Nano-based wireless splits use in ZMK firmware.

---

### 4. ESP-NOW — ESP32-S3/C3 only

Espressif's connectionless WiFi protocol. Peer-to-peer, no AP needed.

```
SLAVE (ESP32)                 MASTER (ESP32)
┌──────────────┐  ESP-NOW     ┌──────────────┐
│ Matrix scan  │◄───────────►│ NimBLE HID   │──► PC
│ Key events   │  ~1-3ms     │ to computer  │
└──────────────┘             └──────────────┘
```

- **Latency**: 1–3ms typical
- **Throughput**: 250KB/s max payload
- **Protocol**: MAC-addressed datagrams, up to 250 bytes each
- **Power**: moderate (WiFi radio active)
- **Complexity**: low — simple send/receive callbacks
- **Downside**: ESP32-only, needs WiFi init even though no AP

Can coexist with BLE HID since ESP-NOW and BLE use different time slots.

---

### 5. SPI over TRRS/flex cable

SPI is faster than UART but needs more wires (MOSI, MISO, SCK, CS).
Some high-end splits use this for near-zero latency.

- **Latency**: <0.1ms
- **Wires**: 4+ (vs 2 for UART)
- **Use case**: when you need very fast scanning of large matrices

---

## Comparison table

| Protocol | Latency | Wireless | Platform | Complexity |
|----------|---------|----------|----------|------------|
| UART cable | <1ms | No | Any | Trivial |
| BLE NUS | 7-15ms | Yes | nRF52840 | Medium |
| ESB | <1ms | Yes | nRF52840 | Low |
| ESP-NOW | 1-3ms | Yes | ESP32-S3/C3 | Low |
| SPI cable | <0.1ms | No | Any | Low |

---

## Recommended by platform

### nRF52840 split
**ESB for inter-half + BLE HID for PC.**
Lowest latency, proven by ZMK firmware ecosystem.
Slave = pure ESB transmitter. Master = ESB receiver + BLE HID peripheral.

### ESP32-S3 split
**ESP-NOW for inter-half + NimBLE HID for PC.**
No cable needed, good enough latency for typing.
Both halves run NimBLE; master also runs ESP-NOW listener.

### Wired fallback
UART over TRRS works on every platform and is trivially reliable.
Good for development and testing before wireless is implemented.
