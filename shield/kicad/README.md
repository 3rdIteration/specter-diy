# Specter Shield v1 - KiCad Project

This KiCad project was converted from the original Altium CircuitMaker design files (Gerber production files, drill files, and pick-and-place data).

## Project Structure

```
kicad/
├── specter-shield.kicad_pro          # KiCad project file
├── specter-shield.kicad_pcb          # PCB layout (converted from Gerbers)
├── specter-shield.kicad_sch          # Main schematic (top-level)
├── structure_diagram.kicad_sch       # Sub-sheet: Connectors & interfaces
├── power.kicad_sch                   # Sub-sheet: Power management
├── peripherals.kicad_sch             # Sub-sheet: Passive components & peripherals
├── specter_shield.kicad_sym          # Component symbol library
├── specter-shield.pretty/            # Component footprint library (empty - needs population)
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
- ✅ All 139 components listed with designators, values, and footprint references
- ✅ Organized into 3 sub-sheets matching the original PDF schematics:
  - **Structure Diagram** - Connectors (J1-J13) and switches (SW1-SW2)
  - **Power** - ICs (TPS63060, BQ25060, TPS3422, STC3100, MAX803), inductors, diodes, MOSFETs, ferrite beads
  - **Peripherals** - ST8034 smartcard IC, resistors, capacitors, buzzer
- ⚠️ **No wiring/connections** - Components are placed but not connected. Use the original PDF schematics as reference to add wires.

## What Needs Manual Completion

### High Priority
1. **Schematic wiring** - Connect components using the original PDF schematics (`01_Structure_Diagram.pdf`, `02_Power.pdf`, `03_Peripherals.pdf`) as reference
2. **Footprint library** - Replace placeholder footprints with proper KiCad footprints from the KiCad standard library or custom libraries
3. **Net assignment** - Once schematics are wired, import the netlist into the PCB to assign nets to copper features

### Medium Priority
4. **Symbol refinement** - Replace generic rectangle symbols with proper schematic symbols (e.g., from KiCad's built-in libraries)
5. **DRC clean-up** - Resolve any design rule violations after net assignment
6. **Copper trace refinement** - Convert imported graphic copper data to proper KiCad tracks and filled zones

### Low Priority
7. **3D models** - Assign 3D models to component footprints
8. **BOM fields** - Add manufacturer part numbers and supplier info from the original BOM (`specter-shield_v1 BOM.xlsx`)

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
