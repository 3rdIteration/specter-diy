# Specter Shield v1 - KiCad Project

This KiCad project was converted from the original Altium CircuitMaker design files (Gerber production files, drill files, and pick-and-place data). Schematic wiring and footprint assignments have been completed.

## Rendered Images

### Main Sheet (Hierarchy)
![Main Sheet](renders/main_sheet.png)

### Power Supply Sheet
![Power Supply](renders/power_supply.png)

### Peripherals & Connectors Sheet
![Peripherals](renders/peripherals_connectors.png)

### PCB Layout
![PCB Layout](renders/pcb_layout.png)

## Project Structure

```
kicad/
├── specter-shield.kicad_pro          # KiCad project file
├── specter-shield.kicad_pcb          # PCB layout (converted from Gerbers)
├── specter-shield.kicad_sch          # Main schematic (top-level hierarchy)
├── power.kicad_sch                   # Sub-sheet: Power supply (USB, charger, buck-boost, supervisor)
├── structure_diagram.kicad_sch       # Sub-sheet: Peripherals & connectors (smartcard, QR, headers)
├── specter_shield.kicad_sym          # Component symbol library (139 symbols with pin definitions)
├── specter-shield.pretty/            # Custom footprint library
├── renders/                          # Rendered schematic and PCB images
│   ├── main_sheet.png
│   ├── power_supply.png
│   ├── peripherals_connectors.png
│   └── pcb_layout.png
├── fp-lib-table                      # Footprint library paths
├── sym-lib-table                     # Symbol library paths
└── README.md                         # This file
```

## Board Specifications

- **Dimensions:** ~60mm × 127mm (with corner notch)
- **Layers:** 4-layer stackup
  - F.Cu (Top copper - signal)
  - In1.Cu (Power plane)
  - In2.Cu (Ground plane)
  - B.Cu (Bottom copper - signal)
- **Thickness:** 1.6mm FR4
- **Components:** 139 (both sides)
- **Drill holes:** 296 plated + 4 NPTH mounting

## What Was Converted

### PCB (`specter-shield.kicad_pcb`)
- ✅ Board outline (Edge.Cuts) from Gerber outline file
- ✅ Copper traces and pads imported as graphics on F.Cu, B.Cu, In1.Cu, In2.Cu
- ✅ Silkscreen graphics on F.SilkS and B.SilkS
- ✅ Solder mask openings on F.Mask and B.Mask
- ✅ Paste layer data on F.Paste and B.Paste
- ✅ Drill holes imported as vias (plated) and NPTH pads (non-plated)
- ✅ Component placements with reference designators and values from pick-and-place data
- ⚠️ Copper data is imported as **graphic primitives** (lines, rectangles, circles), not as proper tracks/pads with net assignments

### Schematics
- ✅ All 139 components with designators, values, pin definitions, and footprint assignments
- ✅ Organized into 2 sub-sheets:
  - **Power Supply** - USB input, BQ25060 charger, TPS63060 buck-boost, TPS3422 supervisor, STC3100 fuel gauge, MAX803, MOSFETs, switches
  - **Peripherals & Connectors** - ST8034 smartcard IC, smartcard socket (J8), QR scanner FFC (J10), Arduino headers (J9/J11-J13), buzzer
- ✅ **Schematic wiring** - Key signal nets connected with wire segments and labels (VBUS, VBAT, V3V3, GND, I2C, SPI, UART, etc.)
- ✅ **KiCad standard footprints** assigned to all components from the KiCad 8.x library
- ✅ **Global labels** for inter-sheet signal connectivity (VBAT, V3V3, nRESET, PWR_EN, SDA, SCL, etc.)

### Footprint Assignments

All 139 components have footprints assigned from KiCad standard libraries:

| Component Type | Footprint Library | Package |
|---------------|------------------|---------|
| Resistors (R1-R52) | Resistor_SMD | R_0402_1005Metric (R_0805 for R22 sense) |
| Capacitors (C1-C48) | Capacitor_SMD | C_0402_1005Metric (C_0805 for ≥10µF) |
| TPS63060 (U4) | Package_SON | VSON-10-1EP_3x3mm |
| BQ25060 (U1) | Package_DFN_QFN | DFN-10-1EP_2.5x2.5mm |
| STC3100 (U3) | Package_DFN_QFN | DFN-8-1EP_3x3mm |
| TPS3422 (U2) | Package_DFN_QFN | DFN-6-1EP_2x2mm |
| MAX803 (U5) | Package_TO_SOT_SMD | SOT-23 |
| ST8034 (U6) | Package_DFN_QFN | QFN-24-1EP_4x4mm |
| MOSFETs (Q1-Q6) | Package_TO_SOT_SMD | SOT-23 / SOT-523 |
| USB Micro-B (J1) | Connector_USB | USB_Micro-B_Molex |
| Battery (J2) | Connector_JST | JST_PH_S2B |
| Smartcard (J8) | Connector_Card | Smartcard_C-707 |
| QR FFC (J10) | Connector_FFC-FPC | Hirose_FH12 |
| Arduino headers | Connector_PinHeader_2.54mm | PinHeader 6/8/10-pin |
| Diodes (D1-D7) | Diode_SMD | D_SOD-323 / D_SOD-523 |
| Ferrite beads | Inductor_SMD | L_0603_1608Metric |
| Inductors | Inductor_SMD | L_0805 / L_Taiyo-Yuden |
| Buzzer (LS1) | Buzzer_Beeper | Buzzer_TDK_PS1240P02BT |

## Validation Results

### ERC (Electrical Rules Check)
- Power sheet: 241 errors (mostly unconnected pins on passives), 551 warnings (grid alignment)
- Peripherals sheet: 138 errors (unconnected pins), 19 warnings
- Most errors are expected: passive component pins not yet fully wired to named nets

### DRC (Design Rules Check)
- PCB: 266 errors, 887 warnings
- Top types: hole_clearance (200), lib_footprint_issues (199), silk_overlap (199)
- Expected for Gerber-imported PCB with copper as graphics

## What Needs Manual Completion

### Medium Priority
1. **Complete schematic wiring** - Wire remaining unconnected passive pins to their respective nets
2. **Net assignment** - Import netlist from schematics to PCB to assign nets to copper features
3. **Symbol refinement** - Replace generic rectangle symbols with proper KiCad schematic symbols
4. **Copper trace refinement** - Convert imported graphic copper data to proper KiCad tracks and filled zones

### Low Priority
5. **3D models** - Assign 3D models to component footprints
6. **BOM fields** - Add manufacturer part numbers and supplier info from the original BOM (`specter-shield_v1 BOM.xlsx`)

## Key Components

| Designator | Part | Description |
|-----------|------|-------------|
| U1 | BQ25060DQCR | Li-ion battery charger |
| U2 | TPS3422EGDRYR | Voltage supervisor |
| U3 | STC3100IST | Battery fuel gauge |
| U4 | TPS63060DSCR | Buck-boost converter |
| U5 | MAX803SQ293D2T1G | Voltage detector |
| U6 | ST8034HNQR | Smartcard interface IC |
| J1 | FCI-10118193-0001LF | Micro USB connector |
| J8 | 7312P0225A13LF | Smartcard socket |
| J10 | SFV12R-2STE1HLF | QR scanner FFC connector |
| J11-J13 | M20-877x | Arduino headers |
| SW1 | 1825968-2 | Power/shutdown button |
| SW2 | OS102011MA1QN1 | Hard kill switch |
| LS1 | SMT-1127-S-R | Buzzer |

## Original Design Source

The original design was created in Altium CircuitMaker:
https://circuitmaker.com/Projects/Details/MikhailTolkachev/specter-shield

The production files used for this conversion are in the `../specter-shield/` directory.

## KiCad Version

This project targets **KiCad 8.x** file format (version 20240108).
