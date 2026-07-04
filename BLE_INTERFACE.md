# BLE Interface — BK6LS cadence/speed sensor

Integration guide for a **BLE central / client** (phone, web-bluetooth, or e-bike
controller) connecting to this device. Self-contained — you do **not** need the
firmware source to implement a client from this document.

The device is a custom-firmware nRF52810 cadence/speed sensor. It exposes the
**standard Cycling Speed and Cadence (CSC) profile** (so off-the-shelf cycling
apps work) **plus a custom service** that streams live, *signed* RPM every 100 ms.

---

## 1. Discovery / advertising

- **Advertised name:** `BK6LS-Cadence`
- **Appearance:** `1157` (0x0485, "Cycling: Cadence Sensor")
- **Advertised 16-bit service UUIDs:** `0x1816` (CSC), `0x180F` (Battery)
- Connectable, single connection, no bonding/pairing required (just connect).

**Find it by** advertised name `BK6LS-Cadence` **or** by advertised service UUID
`0x1816`. Do not hard-code the MAC (it's a random static address).

> ⚠️ **The device stops advertising when parked.** See §3 — if you don't see it,
> it's probably asleep; move/shake it and it re-advertises within ~5 s.

---

## 2. GATT table

| Service | UUID | Characteristic | UUID | Properties |
|---|---|---|---|---|
| Cycling Speed & Cadence | `0x1816` | CSC Measurement | `0x2A5B` | Notify |
| | | CSC Feature | `0x2A5C` | Read |
| | | Sensor Location | `0x2A5D` | Read |
| | | SC Control Point | `0x2A55` | Write, Indicate |
| **RPM (custom)** | `cad00001-eb1c-4f1e-9b2a-6f1c0de0cade` | **Live RPM** | `cad00002-eb1c-4f1e-9b2a-6f1c0de0cade` | **Notify, Read** |
| Battery | `0x180F` | Battery Level | `0x2A19` | Read, Notify |
| Device Information | `0x180A` | (standard DIS strings) | | Read |

To receive notifications, write `0x0001` to the characteristic's **CCC descriptor
(`0x2902`)** (most BLE stacks do this automatically when you "subscribe").

All multi-byte values are **little-endian**.

---

## 3. Connection behavior (IMPORTANT for the client)

This is a coin-cell sensor with aggressive power management:

- **While connected:** it stays awake and pushes notifications:
  - Live RPM (`cad00002…`): every **100 ms**
  - CSC Measurement (`0x2A5B`): every **~1 s**
  - Battery (`0x2A19`): every **~30 s**
- **When disconnected and not moving for ~20 s:** it **stops advertising** and
  sleeps. It is then invisible to scans.
- **On motion:** it wakes within ~5 s and starts advertising again.

**Client implications:**
1. If a scan finds nothing, the device is asleep — prompt the user to spin the
   crank / move the sensor, then it will appear.
2. Handle disconnects gracefully and **auto-reconnect / re-scan** — the device may
   drop off when the bike is parked and reappear when riding resumes.
3. Don't assume a persistent connection; treat reappearance as normal.

---

## 4. Data formats

### 4.1 Live RPM — custom characteristic `cad00002-…` (recommended for live speed)

The simplest way to get instantaneous rotation speed.

- **Value:** `int16` little-endian = **centi-RPM** (RPM × 100), **signed**.
- **Sign = direction of rotation** (forward = positive, reverse = negative).
- **Update rate:** 100 ms (on notify).
- Range: ±327.67 RPM.

```
bytes (LE):  [b0] [b1]
value = (int16)(b0 | (b1 << 8))
rpm   = value / 100.0
```

Examples:
| Bytes (hex) | int16 | RPM |
|---|---|---|
| `00 00` | 0 | 0.00 (stopped) |
| `66 21` | 8550 | +85.50 |
| `F4 FE` | -268 | -2.68 (reverse) |

```js
// Web Bluetooth example
const v = await char.startNotifications();
char.addEventListener('characteristicvaluechanged', e => {
  const rpm = e.target.value.getInt16(0, /*littleEndian=*/true) / 100;
  console.log('RPM', rpm);
});
```

### 4.2 CSC Measurement — `0x2A5B` (standard cadence profile)

Standard BLE CSC format. This device reports **crank (cadence)** data.

Layout (variable, depends on flags byte):
```
[flags:1] [if wheel: cumulative_wheel_revs:u32, last_wheel_event_time:u16]
          [if crank: cumulative_crank_revs:u16, last_crank_event_time:u16]
```
- `flags` bit0 = wheel data present, bit1 = crank data present.
- **This device sends crank only:** `flags = 0x02`, then `cumulative_crank_revs`
  (`uint16`), then `last_crank_event_time` (`uint16`, units **1/1024 s**, wraps
  every 64 s). Total 5 bytes: `02 <ccr_lo> <ccr_hi> <lcet_lo> <lcet_hi>`.

**Compute cadence (RPM)** from two consecutive notifications `a` then `b`
(handle uint16 wrap-around):
```
dRev  = (uint16)(b.ccr  - a.ccr)
dTime = (uint16)(b.lcet - a.lcet)        // 1/1024 s ticks
if (dTime == 0) cadence_rpm = 0          // no new crank event
else            cadence_rpm = dRev * 1024 * 60 / dTime
```
> Note: CSC cumulative revs are **unsigned/monotonic** (no direction). For
> direction, use the custom RPM characteristic (§4.1).

### 4.3 Battery Level — `0x2A19`

- **Value:** `uint8` = battery percent `0..100`.
- Read on demand, and notified ~every 30 s.
- (Derived from supply voltage: ≈3.0 V → 100 %, ≈2.0 V → 0 %.)

### 4.4 CSC Feature — `0x2A5C` (read)
`uint16` LE bitmask. This device returns `0x0007` = wheel + crank + multiple
sensor locations supported.

### 4.5 Sensor Location — `0x2A5D` (read)
`uint8` enum. Default `5` = Left Crank. (`12` = Rear Wheel in wheel mode.)

### 4.6 SC Control Point — `0x2A55` (write + indicate)
Standard CSC control point. Subscribe to indications before writing. Op codes:
- `0x01` Set Cumulative Value — payload `uint32` new cumulative wheel revs.
- `0x03` Update Sensor Location — payload `uint8` location.
- `0x04` Request Supported Sensor Locations.
- Response: `0x10 <req_op> <status> [data]` (status `0x01` = success).
Most clients don't need this for basic cadence/RPM reading.

---

## 5. Quick start for a client

1. **Scan** for name `BK6LS-Cadence` (or service `0x1816`). If absent, ask the
   user to move the sensor (it's asleep), then scan again.
2. **Connect** (no pairing needed).
3. **Subscribe** to the characteristics you need:
   - Live signed RPM → `cad00002-eb1c-4f1e-9b2a-6f1c0de0cade` (easiest, 100 ms).
   - Standard cadence → `0x2A5B` (for CSC-compatible logic).
   - Battery → `0x2A19`.
4. **Parse** per §4 (everything little-endian).
5. **Auto-reconnect** on disconnect (device sleeps/wakes — §3).

---

## 6. Gotchas checklist
- [ ] Everything is little-endian.
- [ ] The live-RPM value is **signed** (`int16`); don't read it as unsigned.
- [ ] Device disappears from scans when parked; reappears on motion. Auto-reconnect.
- [ ] CSC cumulative counters wrap at `uint16`; the event time wraps every 64 s —
      use modular subtraction.
- [ ] Enable the CCC (`0x2902`) / "subscribe" for each notify characteristic.
- [ ] No bonding required; if your stack forces pairing, "Just Works" is fine.
