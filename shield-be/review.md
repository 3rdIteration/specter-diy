# Specter Shield-BE (Budget Edition) — Schematic & PCB Design Review

**Board:** SpecterShield-BE-ATDT v1.0.0
**Designer:** Stephen Rothery (Based off Specter Shield 1.0 by CryptoAdvance)
**Reviewer:** Automated KiCad CLI + AI analysis
**Date:** 2026-04-07
**KiCad Version:** 10.0 (file format 20260206)

---

## 1. Board Overview

| Parameter | Value |
|-----------|-------|
| Outer dimensions | 59.0 × 99.0 mm (2.32 × 3.90 in) |
| Largest dimension | 99.0 mm (under 100 mm ✅) |
| Layer count | 4 (F.Cu / In1.Cu / In2.Cu / B.Cu) |
| Thickness | 1.6 mm |
| Stackup | FR4, εr = 4.5, 35 µm copper |
| USB-C cutout | 22.0 × 7.3 mm notch at top |
| Components | 99 total (all one side ✅) |
| Min component size | 0805 predominant ✅ |
| GND zone | 4-layer pour, 0.3 mm clearance, 0.25 mm min thickness |

### Design Goals Assessment

Per the readme, the board targets:
- ✅ 4-layer PCB with largest dimension ≤100 mm (99 mm)
- ✅ All components on one side
- ✅ Generally 0805 size components
- ✅ JLCPCB basic parts prioritized
- ✅ USB-C power input with/without battery
- ✅ Switch-mode PSU (TPS61089) for 3V–5V input
- ✅ Battery charging (TP4056)
- ✅ Power buttons with PWR_HOLD functionality
- ✅ Fully designed in KiCad

---

## 2. Schematic Review

### 2.1 Top-Level Sheet (SpecterShield-BE-ATDT)

The hierarchical schematic has 5 sub-sheets:
1. **STM32F469I-DISCO-Interface** — connector pinout to STM32F469I-Discovery board
2. **Power** — USB-C input, TPS61089 boost converter, TP4056 battery charger, power buttons
3. **SmartCard_Reader** — ST8034ATDT smartcard interface + ISO 7816 connector
4. **QR_Scanner** — FPC connector for QR scanner module (UART interface)
5. **Beeper** — Buzzer driver circuit

### 2.2 Power Section — Findings

**TPS61089 Boost Converter (U301):**
- Input: USB-C 5V or battery 3.0–4.2V
- Output: 5V (set by R316 = 82kΩ and R317 = 330kΩ feedback divider)
- Inductor L301 = 1µH, 1210 size — appropriate for TPS61089
- Input cap: C305 = 22µF, output cap: C306 = 47µF (1206) — meets TPS61089 datasheet requirements
- Bootstrap cap C307 = 100nF — correct per datasheet

**TP4056 Battery Charger (U302):**
- PROG resistor R315 = 3.9kΩ → ~300 mA charge current (conservative, appropriate for many Li-ion cells)
- Thermal pad connected to ground with thermal vias (in SOIC-8-1EP package) ✅
- Charge/charged status LEDs (D302, D303) with 1kΩ current limiting resistors

**Power Path:**
- Q303 (AO3401A P-FET) controls battery vs USB power selection
- Q304 (AO3401A) provides load switch from boost output
- R307 = 100kΩ pullup on gate — note this creates a slow switching transition; may want lower value for crisper switching

**Power Buttons:**
- SW1 (SW301) → Q302 (2N7002 N-FET inverter) → PWR_NBUTTON — generates active-low interrupt for graceful shutdown ✅
- SW2 (SW302) → Q301 (NPN) → controls PWR_EN — hard kill for unresponsive MCU ✅
- 10kΩ pullup/pulldown resistors on button inputs ✅
- PWR_HOLD (B15) latches power on boot via Q305 (AO3401A) ✅

**⚠️ Issues Identified:**

1. **ERC Error: Power pins not driven** — U301 pins VIN(9), VCC(2), BOOT(10), SW(11), VOUT(6) are flagged as "Input Power pin not driven by any Output Power pins." This is a pin-type annotation issue in the symbol — these pins should likely be classified as passive or bidirectional rather than power input. Not a real circuit error, but should be fixed for clean ERC.

2. **ERC Error: U302 V_CC pin not driven** — Same pin-type annotation issue for TP4056.

3. **No dedicated TVS on USB-C VBUS** — D301 (SMF6.0CA) is a 6V TVS which is good for ESD, but the placement should be verified as close as possible to the USB-C connector for compliance.

4. **USB-C CC resistors** — R301, R302 = 5.1kΩ to GND on CC1/CC2 — correctly identifies as UFP (sink) for USB-C power delivery ✅

### 2.3 SmartCard Reader Section

**ST8034ATDT (U401):**
- Full ISO 7816 smartcard interface IC
- Clock from MCU via SC_CLK, IO via SC_IO
- VCC selection headers (J202 pins) for 3V/5V card support
- ESD protection diodes D401, D402 (1N4148) on IO lines

**⚠️ Issues:**
1. **ERC Error: U401 pins CMDVCC(5), VDD(INTF)(3), VDDP(13) not driven** — Again, pin-type annotation issues.
2. **ERC Error: J401 (card slot) VCC(C1) and GND(C5) not driven** — Pin type issue on the ISO 7816 connector symbol.
3. **DRC: 4 shorting items** — Nets like SC_VCC_SEL1/SC_VCC_SEL1_1 appear to be intentional jumper/header pad shorts on J202/J204 where through-hole and SMD pads overlap. Should be verified intentional.

### 2.4 QR Scanner Section

- Simple FPC connector (SFV12R-2STE1HLF, J501) with UART TX/RX + trigger pin
- 100nF decoupling cap C501 ✅
- QR_TRIG active-low trigger with pull-up

### 2.5 Beeper Section

- Q501 (SS8050 NPN) drives buzzer BZ501 from UC_BEEP signal
- 1kΩ base resistor R501 ✅
- Flyback diode D501 across buzzer ✅

### 2.6 STM32F469I-DISCO Interface

- 4 pin headers (J201–J204) matching Discovery board layout
- Signal routing for SPI, I2C, UART, power, GPIO
- Multiple N/C (not connected) pins properly labeled

---

## 3. PCB Layout Review

### 3.1 Stackup & Layer Usage

| Layer | Usage |
|-------|-------|
| F.Cu | Signal routing + component pads + GND pour |
| In1.Cu | GND pour (solid plane) ✅ |
| In2.Cu | GND pour (solid plane) ✅ |
| B.Cu | Signal routing + GND pour |

**Assessment:** Having In1 and In2 as dedicated ground planes is excellent for:
- Low-impedance return paths
- EMI shielding between front and back signal layers
- Thermal dissipation
- Signal integrity

### 3.2 Trace Widths

| Width | Count | Usage |
|-------|-------|-------|
| 0.2 mm | 638 | Signal traces (default) |
| 0.25 mm | 14 | Signal traces |
| 0.3 mm | 3 | Signal traces |
| 0.4 mm | 10 | Moderate current |
| 0.45 mm | 1 | Moderate current |
| 0.6 mm | 6 | Power traces |
| 0.8 mm | 29 | Power traces |
| 1.0 mm | 19 | High-current power |
| 1.2 mm | 21 | High-current power |
| 1.4 mm | 14 | High-current power |

**Assessment:**
- 0.2 mm minimum signal trace is fine for 4-layer JLCPCB (min 0.09 mm) ✅
- Power traces use progressively wider widths up to 1.4 mm for VBUS/battery paths ✅
- For 1A continuous at the boost converter output, 1.0–1.4 mm traces on outer layers (35µm copper) can handle ~1.5–2A — adequate ✅

### 3.3 Vias

- All vias: 0.5 mm diameter, 0.3 mm drill
- 235 total vias
- 0.3 mm drill meets JLCPCB minimum (0.3 mm for standard, matching the readme target) ✅

### 3.4 DRC Results Summary

**49 Errors:**
| Type | Count | Severity |
|------|-------|----------|
| Silkscreen overlap | 28 | Cosmetic — reference designators overlapping. Fix for manufacturing readability. |
| Footprint type mismatch | 6 | Warning — J201-J204, J303, U301 marked TH but have SMD pads. Fix symbol/footprint metadata. |
| Shorting items | 4 | Investigate — SC_VCC_SEL nets have duplicate net names on header pads. Likely intentional jumpers, but should use explicit net ties. |
| Copper-edge clearance | 4 | **Important** — J401 (smartcard slot) pads at 0.0 mm from board edge. The slot extends to the board edge by design, but manufacturer may reject. Add board edge cutout or rule exception. |
| Courtyard overlap | 2 | J202/J204 overlapping J401 — verify physical clearance for assembly. |
| Starved thermal | 2 | C404/C405 GND pads only have 1 thermal spoke (min 2). May cause soldering issues — add relief or adjust pad connection. |
| Silk over copper | 3 | Minor — silkscreen printing over exposed copper. |
| Silk-edge clearance | 2 | Minor — silkscreen too close to board edge. |
| Non-mirrored text on back | 1 | Minor — text should be mirrored for back layer. |

**103 Warnings:**
- 96 lib_footprint_issues — footprints don't match library. Typical for custom/modified footprints.
- 4 lib_footprint_mismatch — minor library sync issues.
- 3 silk_over_copper — cosmetic.

### 3.5 Visual Layout Observations

**From PCB front copper view:**
- Components are well-organized in functional groups
- Pin headers along left and right edges for Discovery board connection ✅
- USB-C connector centered at top with proper notch cutout ✅
- Battery connector (JST PH) at top right ✅
- Smartcard slot (J401) at right edge with edge-mount design
- Power section components (U301 boost, U302 charger) in upper region
- QR scanner FPC connector at bottom right
- Mounting holes at 4 corners ✅

**From inner layers (In1.Cu, In2.Cu):**
- Both layers show solid GND pour with clearances only around vias and through-hole pads ✅
- Excellent ground plane continuity — no significant splits visible
- Good thermal relief patterns on GND connections

---

## 4. Compliance & Certification Considerations

### 4.1 FCC/CE EMC (Electromagnetic Compatibility)

| Requirement | Status | Notes |
|-------------|--------|-------|
| Ground planes | ✅ | Dual internal GND planes provide excellent shielding |
| Decoupling caps | ✅ | 100nF caps on all IC power pins |
| USB-C ESD protection | ⚠️ | TVS (SMF6.0CA) present, verify placement proximity to connector |
| Clock signal routing | ⚠️ | SC_CLK trace should be checked for length/coupling — smartcard clock is a potential EMI source |
| No antenna structures | ✅ | No wireless — reduces EMC risk significantly |
| Board edge clearance | ⚠️ | Copper at board edge on J401 — potential EMI coupling point |

**Recommendations:**
1. Ensure the SC_CLK trace is routed adjacent to ground (on inner layer) or has guard traces
2. Add ground stitching vias around the board perimeter (every 5–10 mm)
3. Verify TVS D301 is placed as close as physically possible to USB-C connector pins
4. Consider adding a common-mode choke on USB data lines if pursuing formal FCC testing (though this is power-only USB-C)

### 4.2 USB-C Compliance (USB-IF)

| Requirement | Status | Notes |
|-------------|--------|-------|
| CC1/CC2 pull-down 5.1kΩ | ✅ | R301/R302 = 5.1kΩ to GND — correct for UFP/sink |
| VBUS decoupling | ✅ | Input capacitance present |
| ESD on VBUS | ✅ | SMF6.0CA TVS diode |
| Power-only 6-pin connector | ✅ | No data lines — simplified compliance |

### 4.3 Battery Safety (UN 38.3 / IEC 62133)

| Requirement | Status | Notes |
|-------------|--------|-------|
| Charge IC with thermal regulation | ✅ | TP4056 has internal thermal foldback |
| Charge current limiting | ✅ | ~300 mA via R315 = 3.9kΩ |
| Over-discharge protection | ⚠️ | No dedicated battery protection IC visible. Recommend adding DW01A + FS8205 or similar for over-discharge, over-charge, and short-circuit protection |
| Reverse polarity protection | ⚠️ | Verify JST connector is keyed (JST-PH is keyed ✅), but no circuit-level reverse polarity protection |
| Charge status indication | ✅ | Charge/charged LEDs present |

**Recommendations:**
1. **Add a battery protection IC** (e.g., DW01A + dual MOSFET) between the battery connector and the rest of the circuit. This is essential for any product with a Li-ion battery — protects against over-charge (>4.25V), over-discharge (<2.5V), and short-circuit.
2. Consider adding a PTC resettable fuse on the battery input as additional protection.

### 4.4 Smart Card (ISO 7816 / EMV)

| Requirement | Status | Notes |
|-------------|--------|-------|
| ST8034 interface IC | ✅ | Handles voltage regulation and protocol |
| Clock crystal/source | ✅ | SC_CLK from MCU |
| IO line protection | ✅ | ESD diodes D401/D402 |
| Card detect | ⚠️ | Verify card-detect switch on J401 is properly debounced |

### 4.5 RoHS / REACH

- All standard components (0805 resistors, capacitors, SOT-23 transistors) are widely available in RoHS-compliant variants ✅
- Verify that specific part numbers ordered from JLCPCB are RoHS-compliant on the BOM

### 4.6 IPC Standards

| Standard | Requirement | Status |
|----------|-------------|--------|
| IPC-2221B | Min trace/space | ✅ 0.2mm/0.2mm exceeds Class 2 minimums |
| IPC-2221B | Via aspect ratio | ✅ 1.6mm/0.3mm = 5.3:1 (within 10:1 limit) |
| IPC-7351C | Land patterns | ✅ Hand-solder extended pads used throughout |
| IPC-A-610 | Thermal relief | ⚠️ 2 starved thermals on GND connections (C404/C405) |

---

## 5. Priority Action Items

### Critical (Must Fix Before Fabrication)
1. **Investigate 4 shorting DRC errors** on SC_VCC_SEL and SC_CLKDIV nets — confirm intentional or add net ties
2. **Copper-edge clearance** on J401 smartcard pads — add manufacturer exception or adjust edge cut
3. **Starved thermal reliefs** on C404/C405 — adjust pad connections for reliable soldering

### High Priority (Recommended Before Production)
4. **Add battery protection circuit** (DW01A + FS8205) for over-discharge/over-charge/short-circuit
5. **Fix ERC pin type annotations** on TPS61089, TP4056, ST8034 symbols to clear false-error pin-not-driven warnings
6. **Add ground stitching vias** around board perimeter for improved EMC
7. **Review power FET gate resistor** R307 (100kΩ) — consider lowering for faster switching

### Medium Priority (Cleanup)
8. Fix 28 silkscreen overlap issues for manufacturing readability
9. Fix 6 footprint type mismatches in component metadata
10. Fix non-mirrored back-layer text
11. Address 96 library footprint warnings (sync footprints with library)

### Low Priority (Nice to Have)
12. Add reverse polarity protection on battery input
13. Consider adding a PTC fuse on battery line
14. Add common-mode filtering if pursuing formal EMC certification

---

## 6. BOM Summary

| Type | Count | Key Values |
|------|-------|------------|
| Resistors (R) | 48 | 10kΩ ×15, 100kΩ ×9, 1kΩ ×6, 200Ω ×4, various |
| Capacitors (C) | 20 | 100nF ×9, 22µF ×5, 10µF ×3, 47µF ×1 |
| Transistors (Q) | 8 | AO3401A ×3, 2N7002 ×2, NPN(SOT-89) ×2, SS8050 ×1 |
| Diodes (D) | 8 | LED ×3, 1N4148 ×2, TVS ×1, Schottky ×1, generic ×1 |
| Connectors (J) | 8 | Pin headers ×4, USB-C ×1, JST-PH ×1, SC slot ×1, FPC ×1 |
| ICs (U) | 3 | TPS61089 (boost), TP4056 (charger), ST8034 (smartcard) |
| Switches (SW) | 2 | Tactile push buttons |
| Inductor (L) | 1 | 1µH 1210 |
| Buzzer (BZ) | 1 | SMT piezo |
| **Total** | **99** | |

All passives in 0805 size for hand-soldering ✅
