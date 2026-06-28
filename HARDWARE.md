# Hardware reverse-engineering notes — BK6LS-0026006 cadence/speed sensor

## MCU (confirmed via J-Link / FICR)
- **nRF52810**, variant **AAD1**, package **QC = QFN32 (5×5 mm)**, **24 KB RAM, 192 KB flash**, no FPU.
- FICR INFO @0x10000100: PART=`0x00052810`, VARIANT=`0x41414431`("AAD1"), PACKAGE=`0x00002003`(QC),
  RAM=`0x18`(24), FLASH=`0xC0`(192).
- BLE factory address (FICR DEVICEADDR) = **F6:2F:79:C2:67:2B** (static random) — matches nRF Connect screenshot.
- **APPROTECT = DISABLED** (UICR 0x10001208 = 0xFFFFFFFF; SWD memory reads succeed). The stock firmware was
  therefore **fully readable** — premise of "presumed locked" was wrong.

## Active J-Link probe
- Serial **69658030** (always pass `--snr 69658030`; a phantom serial `440223164` also enumerates but has no target).

## Stock firmware
- SoftDevice-based Nordic SDK build (MBR@0x0, SoftDevice@0x1000, app @~0x22000). Used flash 0x0..0x2FFFF (~192 KB).
- **Full factory backup saved:** `stock-backup/stock_full.hex` (code + UICR + FICR). Restorable via
  `nrfjprog --snr 69658030 --family NRF52 --program stock-backup/stock_full.hex --chiperase --verify --reset`.
- Strings: device name `BK6L…`, versions `V1.1.0`, `V1.3.8`, `V2.1.0`, `BLE06.01.01`, `nRF5x` SDK marker.
  No reliable IMU part-name string; I2C byte-freq inconclusive but address **0x68 is the likely IMU address**.

## Pinout — extracted from LIVE stock-firmware peripheral registers (not a blind scan)
Read GPIO `PIN_CNF[0..31]` @0x50000700, `DIR`/`OUT`/`IN` @0x50000500, TWI/SPI PSEL, GPIOTE, while stock FW ran.
PIN_CNF reset default = `0x02` (input, buffer disconnected) = unused.

| Pin | PIN_CNF | Decoded | Inferred role |
|---|---|---|---|
| **P0.28** | `0x060C` | input + pull-up + **open-drain (S0D1)**, idle-high | **I²C SDA or SCL** (bit-banged) |
| **P0.30** | `0x060C` | input + pull-up + **open-drain (S0D1)**, idle-high | **I²C SDA or SCL** (bit-banged) |
| **P0.15** | `0x04`   | input + **pull-down** (waits for high) | **IMU INT** (active-high) — wake-on-motion candidate |
| **P0.25** | `0x0C`   | input + **pull-up** (waits for low), idle-high | **IMU INT2 / reed** (active-low) candidate |
| P0.04 | `0x03` | push-pull output, low | LED / control |
| P0.05 | `0x03` | push-pull output, low | LED / control |
| P0.12 | `0x03` | push-pull output, low | LED / control |
| P0.16 | `0x03` | push-pull output, **HIGH** | LED-on or sensor power-enable |
| P0.18 | `0x03` | push-pull output, **HIGH** | LED-on or sensor power-enable |
| P0.06 | `0x0603` | open-drain output, no pull | IMU control or open-drain LED |
| P0.09 | `0x0603` | open-drain output, no pull (NFC pin repurposed) | IMU control or open-drain LED |
| P0.10 | `0x0603` | open-drain output, no pull (NFC pin repurposed) | IMU control or open-drain LED |
| all others | `0x02` | unused/default | — |

Notes:
- **I²C is bit-banged** (both TWIM/SPIM instances were DISABLED with PSEL disconnected) → confirms a software
  I²C on P0.28/P0.30. SDA-vs-SCL ordering still to confirm empirically.
- **GPIOTE had no channels** → INT uses the low-power GPIO PORT/DETECT SENSE mechanism (matches P0.15 pull-down,
  P0.25 pull-up waiting states).
- P0.00/01 (LFXO) and P0.21 (RESET) show default — consistent with 32.768 kHz crystal fitted + reset pin reserved.

## CONFIRMED empirically (pinscan firmware #1, via RTT)
- **I²C bus: SCL = P0.30, SDA = P0.28** (the other ordering gives no ACK).
- **Motion sensor = 3-axis ACCELEROMETER at I²C address 0x4C** (NOT a gyro IMU). Register map is
  **Bosch BMA-compatible**: `0x00`=chip-id-ish `0xF0`, `0x01`=`0x01`, `0x0F`=`0x43`; **XYZ data = regs
  0x02..0x07, 16-bit little-endian signed, LSB bit0 = new-data flag**. Static reading ≈ (X −25, Y −9, Z +122)
  → ~1 g on Z (device flat); values update live. 0x30–0x3F = OTP trim.
  - Exact part is a BMA-map clone (MiraMEMS DA / STK / GMA family); precise ID deferred — can be lifted from
    the stock-FW init sequence in `stock-backup/stock_full.hex` if the wake-on-motion INT config needs it.
  - **Implication for design:** cadence/speed comes from the **rotating gravity vector** — track
    θ = atan2(accel_b, accel_a) of the two in-plane axes; +360° = one revolution; RPM = dθ/dt. The
    "battery-reinsert mode change" = switching which axis pair is the rotation plane (crank vs wheel mounting).
- INT candidate levels at rest: P0.15 = 1, P0.25 = 1 (both idle-high). Which one is the accel motion INT
  (and its active edge) still to confirm by tapping the sensor and watching the pin.

## Full register dump of 0x4C (reference fingerprint)
```
00: F0 01 E7 FF F7 FF 7A 00 15 00 00 00 00 00 00 43
10: 05 07 00 00 00 34 00 00 71 02 00 00 00 00 00 00
20: 00 80 00 00 00 00 00 00 00 00 3A 7D C7 7C 42 06
30: 6D 61 9D 41 30 25 25 1D 1D 4C 4E 1F 5D 0A 6F 2E
```

## LEDs — CONFIRMED (driven via debugger, observed on USB microscope)
Single 2-color LED package (silkscreen "LED1"), **active-LOW** (common-anode to VCC; GPIO LOW = on):
- **P0.16 = BLUE LED** (drive LOW = on, HIGH/hi-Z = off).
- **P0.18 = RED LED**  (drive LOW = on, HIGH/hi-Z = off).
(Other output candidates P0.04/05/06/09/10/12 lit nothing → not LEDs; likely accel control/unused.)

## Accelerometer identity & init (CONFIRMED)
- **MiraMEMS DA-family** 3-axis accel (BMA-register-compatible), I²C 0x4C. Datasheet sibling: DA215
  (DS_da215). Data 14-bit @ 0x02–0x07 (left-justified). RANGE 0x0F, ODR 0x10, MODE_BW 0x11 (bit7 PWR_OFF:
  0=normal). Fixed/OTP-ish regs: 0x18=0x71, 0x19=0x02. **Reg 0x00 must NOT be written — writing it wedges
  the chip** (config stops latching; reads stick at 0xF0). The stock FW never touches 0x00.
- The chip powers up in a non-sampling default; it **must be configured** or all data regs read 0
  (this is why a battery/power glitch "kills" cadence until re-init).
- **Exact stock init sequence** (reverse-engineered from stock_full.hex @0x24fca, write fn @0x29074),
  replicated in `src/accel.c` `accel_init()`:
  `0x09=00, 0x0F=42, 0x20=01, 0x21=80, 0x28=00, 0x1A=00, 0x20=01, 0x21=00, 0x21=81, 0x20=00, 0x20=00,
   0x21=00, 0x21=80, 0x10=01, 0x11=07, 0x15=34, 0x0F=42, 0x10=05`. The 0x20/0x21/0x28/0x1A writes set up
  the active/motion interrupt (reuse for wake-on-motion). After this, X/Y/Z are live (|g| ≈ 480 LSB).

## Still to confirm
1. Accel motion INT pin: **P0.15** (input+pulldown → active-high) vs **P0.25** (input+pullup → active-low),
   for hardware wake-on-motion (System OFF) — the stock init already enables the active interrupt.
2. Rotation-plane calibration (crank vs wheel) — needs the user to rotate the device.
