# Power Button – Dual-Purpose SW1 (Power-On + Safe Shutdown)

This document describes a small add-on circuit that lets a **single
push-button (SW1)** serve two roles:

1. **Power-on** — the existing diode-OR latch drives PWR\_EN HIGH
   (unchanged).
2. **Safe shutdown** — while the system is running, pressing SW1
   produces a **falling edge on MCU pin B1**, which the firmware
   already monitors to trigger a clean power-off (filesystem sync,
   then PWR\_HOLD LOW).

The circuit requires **no firmware changes** — it is compatible with
both `boot/main/boot.py` and `boot/debug/boot.py`, which configure B1
as:

```python
pyb.ExtInt(pyb.Pin('B1'), pyb.ExtInt.IRQ_FALLING, pyb.Pin.PULL_NONE, pwrcb)
```

---

## Problem

When the system is running, PWR\_HOLD2 keeps PWR\_EN latched HIGH
through D307.  Pressing SW1 again simply drives the same node HIGH
redundantly through D306 — the MCU has **no electrical path** to
observe the press.

The firmware expects B1 to be **held HIGH normally** and to see a
**falling edge** (HIGH → LOW transition) when the button is pressed
(`IRQ_FALLING`, `PULL_NONE`).  SW1, however, is **active-high** — it
connects PWR\_VIN to the circuit when pressed.

We need an **inverter** to convert SW1's active-high pulse into an
active-low signal on B1.

---

## Solution — N-FET Inverter

A single **2N7002 N-channel MOSFET** (Q302) acts as an open-drain
inverter:

* **Gate** is driven by the SW1 node (the junction between SW1 and
  R317) through a current-limiting resistor R321, with R322 pulling
  the gate to GND when SW1 is not pressed.
* **Drain** is connected to MCU B1 and pulled up to 3V3 through R323.
* **Source** is tied to GND.

When SW1 is pressed the gate goes HIGH, Q302 turns ON, and B1 is
pulled LOW — exactly the falling edge the firmware expects.

### Schematic

```
                          3V3 (MCU supply)
                           │
                         [R323 10 kΩ]  ← pull-up keeps B1 HIGH
                           │              while Q302 is OFF
                           │
                           ├─────────── MCU B1 (nButton)
                           │
                         Drain
                           │
              ┌─── Gate ───┤  Q302 (2N7002, N-ch MOSFET)
              │            │
              │          Source
              │            │
              │           GND
              │
SW1 node ──[R321 10 kΩ]───┤
     (PWR_VIN when         │
      button pressed)    [R322 100 kΩ]  ← pull-down keeps gate
                           │               LOW when SW1 is open
                          GND
```

The sense point is the same node where SW1 meets R317 on the
existing power board.  An additional trace or wire from that
node to R321 is the only connection to the main power circuit.

### Optional debounce capacitor

```
              ┌─── Gate ───┤ Q302
              │            :
R321 ─────────┤
              │
            [C301 100 nF]   ← optional, gate to GND
              │
             GND
```

RC time constant with R321 ∥ R322 ≈ 9.1 kΩ → **τ ≈ 0.9 ms**,
which filters contact bounce.  The firmware also debounces via
`micropython.schedule()`, so C301 is optional but recommended.

---

## Operating States

| State | SW1 | Q302 Gate | Q302 | B1 | Result |
|---|---|---|---|---|---|
| **Off, idle** | Open | 0 V (R322→GND) | OFF | Floating (MCU off) | — |
| **Off → Power-on** | Pressed | PWR\_VIN × R322/(R321+R322) ≈ **0.91 × V\_BAT** | ON | LOW (but MCU booting) | PWR\_EN rises via D306; MCU starts, latches PWR\_HOLD |
| **Running, idle** | Open | 0 V (R322→GND) | OFF | **3.3 V** (R323 pull-up) | B1 = HIGH — idle state |
| **Running → Shutdown** | Pressed | ≈ 0.91 × PWR\_VIN | ON | **0 V** (Q302 drain→GND) | **Falling edge** → `pwrcb` fires → `poweroff()` |

### Gate voltage check (worst case)

| Supply | V\_gate = V × 100/(10+100) | 2N7002 V\_GS(th) |
|---|---|---|
| 3.0 V (min Li-Ion) | **2.73 V** | 1.0–2.5 V → **ON** ✓ |
| 4.2 V (full Li-Ion) | **3.82 V** | **ON** ✓ |
| 5.0 V (USB) | **4.55 V** | **ON** ✓ (well below 20 V max) |

### B1 voltage when Q302 is ON

Even at minimum gate drive (V\_GS = 2.73 V) the 2N7002 R\_DS(on)
is well under 1 kΩ.  With the 10 kΩ pull-up:

    V_B1 = 3.3 V × R_DS(on) / (10 kΩ + R_DS(on))

At R\_DS(on) = 100 Ω → **V\_B1 ≈ 0.03 V** — solidly LOW.

---

## Power-on / boot sequence (no false trigger)

1. User presses SW1 → PWR\_VIN flows through D306 → PWR\_EN rises →
   regulator starts → 3V3 rises.
2. Simultaneously Q302 gate goes HIGH → drain (B1) pulled LOW.  But
   the MCU is still booting — the interrupt has **not been registered
   yet**, so no shutdown is triggered.
3. `boot.py` runs: sets PWR\_HOLD HIGH (line 13–14), then registers
   the B1 interrupt (line 50).  At this point B1 is still LOW (SW1
   held), but `IRQ_FALLING` only fires on a HIGH→LOW **transition**,
   not on a static LOW level.
4. User releases SW1 → gate drops → Q302 OFF → B1 rises to 3.3 V
   (rising edge — ignored by `IRQ_FALLING`).
5. B1 is now HIGH (idle).  Next press will produce the falling edge
   that triggers a safe shutdown.

---

## Safe shutdown sequence

1. User presses SW1 → Q302 ON → B1 falls → `IRQ_FALLING` fires.
2. `pwrcb()` schedules `poweroff()` on the main loop.
3. `poweroff()` toggles LEDs, stops I2C battery IC, calls `os.sync()`,
   waits 300 ms, then drives PWR\_HOLD (B15) LOW.
4. PWR\_HOLD LOW → D307 stops conducting → R315 pulls PWR\_EN to
   GND → regulator shuts down → device off.

> **Note:** If the user is still holding SW1 when PWR\_HOLD goes LOW,
> PWR\_EN stays HIGH through D306 (the power-on path).  The MCU will
> continue running in its post-`pwr.off()` sleep.  As soon as the
> button is released, power drops.  This is the same behaviour as the
> original power button design.

---

## BOM (3 components + 1 optional)

All parts from the **JLCPCB Basic Parts Library** for lowest cost.

| Ref | Part | Value | Package | LCSC |
|---|---|---|---|---|
| Q302 | 2N7002 N-ch MOSFET | V\_GS(th) 1–2.5 V | SOT-23 | **C8545** |
| R321 | Chip resistor | 10 kΩ | 0402 | **C25744** |
| R322 | Chip resistor | 100 kΩ | 0402 | **C25741** |
| R323 | Chip resistor | 10 kΩ | 0402 | **C25744** |
| C301 | MLCC (optional) | 100 nF | 0402 | **C1525** |

> Q302 and R321–R323 are the same values already used elsewhere on the
> shield (battery ADC circuit), so no new unique parts are introduced.

---

## Connections to existing board

Only **one signal** needs to be tapped from the existing power
circuit — the SW1 node (junction of SW1 and R317):

| From | To | Notes |
|---|---|---|
| SW1–R317 junction | R321 (gate circuit input) | Short trace / wire |
| 3V3 rail | R323 (top) | MCU supply rail |
| GND | R322 bottom, Q302 source, C301 | Ground plane |
| Q302 drain / R323 bottom | MCU B1 | Arduino header or direct pad |

No existing traces need to be cut.  The add-on can be built as a
small daughter-board or integrated into the next shield revision.
