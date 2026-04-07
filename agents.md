# Agents Instructions

This document provides instructions for AI agents working with this repository, particularly for hardware design review tasks.

---

## KiCad PCB & Schematic Review

### Overview

The `shield-be/` directory contains KiCad 10 hardware design files for the Specter Shield-BE (Budget Edition) PCB. Agents can render these files as images and run automated checks to perform design reviews.

### Prerequisites — Installing KiCad CLI

The KiCad files use **KiCad 10.0 format** (file version 20260206). The default `kicad` package on Ubuntu 24.04 is v7 and **cannot open these files**. You must install the nightly build:

```bash
sudo add-apt-repository -y ppa:kicad/kicad-dev-nightly
sudo apt-get update -qq
sudo apt-get install -y -qq kicad-nightly librsvg2-bin
```

This provides:
- `kicad-cli-nightly` (v10.99+) — command-line tool for exporting and checking designs
- `rsvg-convert` — converts SVG exports to PNG for visual inspection

### Rendering Images

#### Export PCB as SVG

```bash
cd /home/runner/work/specter-diy/specter-diy

# All layers combined
kicad-cli-nightly pcb export svg \
  --layers "F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,F.Fab,B.Fab,Edge.Cuts" \
  --page-size-mode 2 \
  -o /tmp/kicad-renders/pcb_all_layers.svg \
  shield-be/SpecterShield-BE-ATDT.kicad_pcb

# Front copper only
kicad-cli-nightly pcb export svg \
  --layers "F.Cu,F.Silkscreen,Edge.Cuts" \
  --page-size-mode 2 \
  -o /tmp/kicad-renders/pcb_front.svg \
  shield-be/SpecterShield-BE-ATDT.kicad_pcb

# Back copper (mirrored for readability)
kicad-cli-nightly pcb export svg \
  --layers "B.Cu,B.Silkscreen,Edge.Cuts" \
  --page-size-mode 2 --mirror \
  -o /tmp/kicad-renders/pcb_back.svg \
  shield-be/SpecterShield-BE-ATDT.kicad_pcb

# Inner layers (ground planes)
kicad-cli-nightly pcb export svg \
  --layers "In1.Cu,Edge.Cuts" --page-size-mode 2 \
  -o /tmp/kicad-renders/pcb_inner1.svg \
  shield-be/SpecterShield-BE-ATDT.kicad_pcb

kicad-cli-nightly pcb export svg \
  --layers "In2.Cu,Edge.Cuts" --page-size-mode 2 \
  -o /tmp/kicad-renders/pcb_inner2.svg \
  shield-be/SpecterShield-BE-ATDT.kicad_pcb
```

#### Export Schematics as SVG

This exports all hierarchical schematic sheets:

```bash
kicad-cli-nightly sch export svg \
  -o /tmp/kicad-renders/ \
  shield-be/SpecterShield-BE-ATDT.kicad_sch
```

This produces one SVG per sheet:
- `SpecterShield-BE-ATDT.svg` — top-level hierarchy
- `SpecterShield-BE-ATDT-Power.svg` — power section
- `SpecterShield-BE-ATDT-SmartCard_Reader.svg` — smartcard interface
- `SpecterShield-BE-ATDT-STM32F469I-DISCO-Interface.svg` — MCU interface
- `SpecterShield-BE-ATDT-QR_Scanner.svg` — QR module
- `SpecterShield-BE-ATDT-Beeper.svg` — buzzer driver

#### Convert SVG → PNG for Viewing

```bash
cd /tmp/kicad-renders
for f in *.svg; do
  rsvg-convert -w 2400 "$f" -o "${f%.svg}.png"
done
```

Then use the `view` tool on each PNG file to visually inspect the design.

### Running Automated Checks

#### Design Rule Check (DRC) — PCB

```bash
kicad-cli-nightly pcb drc \
  --severity-all \
  -o /tmp/kicad-renders/drc_report.json \
  --format json \
  shield-be/SpecterShield-BE-ATDT.kicad_pcb
```

#### Electrical Rule Check (ERC) — Schematic

```bash
kicad-cli-nightly sch erc \
  --severity-all \
  -o /tmp/kicad-renders/erc_report.json \
  --format json \
  shield-be/SpecterShield-BE-ATDT.kicad_sch
```

#### Analyzing Reports

The JSON reports can be parsed with Python:

```python
import json
from collections import Counter

# DRC analysis
with open('/tmp/kicad-renders/drc_report.json') as f:
    drc = json.load(f)

violations = drc.get('violations', [])
types = Counter(v.get('type') for v in violations)
severities = Counter(v.get('severity') for v in violations)

# ERC analysis (violations are nested per sheet)
with open('/tmp/kicad-renders/erc_report.json') as f:
    erc = json.load(f)

all_erc = []
for sheet in erc.get('sheets', []):
    all_erc.extend(sheet.get('violations', []))
```

#### Export BOM

```bash
kicad-cli-nightly sch export python-bom \
  -o /tmp/kicad-renders/bom.xml \
  shield-be/SpecterShield-BE-ATDT.kicad_sch
```

### PCB Data Extraction

For deeper analysis, parse the `.kicad_pcb` file directly with Python regex:

```python
import re

with open('shield-be/SpecterShield-BE-ATDT.kicad_pcb') as f:
    content = f.read()

# Trace widths
widths = re.findall(
    r'\(segment\s+\(start[^)]+\)\s+\(end[^)]+\)\s+\(width\s+([\d.]+)\)',
    content
)

# Via sizes
vias = re.findall(
    r'\(via\s+.*?\(size\s+([\d.]+)\)\s+\(drill\s+([\d.]+)\)',
    content, re.DOTALL
)

# Board outline from Edge.Cuts gr_line elements
edge_lines = re.findall(
    r'\(gr_line\s+\(start\s+([\d.]+)\s+([\d.]+)\)\s+\(end\s+([\d.]+)\s+([\d.]+)\).*?\(layer\s+"Edge\.Cuts"\)',
    content, re.DOTALL
)
```

### What to Review

When reviewing a KiCad PCB and schematic, check the following:

#### Schematic Review Checklist
- [ ] **Power supply**: Correct voltage regulators, adequate decoupling capacitors, proper feedback divider values
- [ ] **ESD protection**: TVS diodes on external interfaces (USB, smartcard)
- [ ] **Pin connections**: All IC pins properly connected or explicitly marked N/C
- [ ] **Net naming**: Consistent, descriptive net labels
- [ ] **Component values**: Resistor/capacitor values match datasheet recommendations
- [ ] **ERC results**: Address all errors, review warnings

#### PCB Layout Review Checklist
- [ ] **Board dimensions**: Meet manufacturing and enclosure constraints
- [ ] **Layer stackup**: Ground planes on inner layers for 4-layer designs
- [ ] **Trace widths**: Adequate for current requirements (≥0.3mm for power, ≥0.15mm for signals)
- [ ] **Via sizes**: Meet manufacturer minimums (JLCPCB: ≥0.3mm drill)
- [ ] **Component placement**: Logical grouping, adequate clearances
- [ ] **Ground planes**: Continuous, no large splits under sensitive signals
- [ ] **Thermal management**: Adequate copper area for heat-dissipating components
- [ ] **Silkscreen**: Readable, no overlaps on critical reference designators
- [ ] **DRC results**: All errors resolved, warnings reviewed

#### Compliance & Certification Checklist
- [ ] **EMC (FCC/CE)**: Ground plane continuity, decoupling, no unintended antenna structures, ground stitching vias at board edges
- [ ] **USB-C**: Correct CC pull-down resistors (5.1kΩ), ESD protection, proper connector footprint
- [ ] **Battery safety (UN 38.3 / IEC 62133)**: Over-charge/over-discharge/short-circuit protection IC, charge current limiting, thermal protection
- [ ] **ISO 7816 (smartcard)**: Proper interface IC, ESD protection on IO pins, correct clock/data routing
- [ ] **RoHS/REACH**: Verify component availability in compliant variants
- [ ] **IPC-2221B**: Trace width/spacing, via aspect ratios, copper-edge clearances
- [ ] **Manufacturability (DFM)**: Component placement for pick-and-place, solder paste considerations, panel clearances

### Output

After completing a review, create or update `shield-be/review.md` with:
1. Board overview (dimensions, layer count, component count)
2. Per-section schematic findings
3. PCB layout observations
4. DRC/ERC results summary with categorization
5. Compliance assessment against relevant standards
6. Prioritized action items (Critical / High / Medium / Low)
7. BOM summary

---

## Other Agent Tasks

### Building Firmware

See the main README and `docs/build.md`. Quick reference:
- `make disco` — Build for STM32F469I-DISCO
- `make dk2` — Build for STM32U5G9J-DK2
- `make unix` — Build simulator
- `make test` — Run unit tests

### Repository Structure

- `src/` — Main Python application code
- `boot/` — Board-specific boot scripts
- `shield-be/` — KiCad hardware design files (Budget Edition shield)
- `shield/` — Original Specter Shield design files
- `boards/` — MicroPython board definitions
- `f469-disco/` — MicroPython submodule
- `docs/` — Documentation
