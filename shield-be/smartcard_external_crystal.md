# Smartcard Interface: External Crystal Oscillator

## Current Design — MCU Clock Signal

The current Shield-BE design uses the **STM32F469 MCU's USART2_CK pin (PA4)** to provide the clock signal to the ST8034ATDT smartcard interface IC. The clock enters the ST8034 via its **XTAL1 pin (pin 1)**, with **XTAL2 (pin 2) left unconnected**.

### Current Clock Frequency

The clock frequency is determined by the STM32's USART smartcard-mode prescaler, defined in the firmware at [`f469-disco/usermods/scard/scard.h`](https://github.com/diybitcoinhardware/f469-disco/blob/master/usermods/scard/scard.h):

```c
#define SCARD_MAX_CLK_FREQUENCY_HZ  (5000000LU)  // 5 MHz max target
#define SCARD_ETU                   (372U)        // ISO 7816 elementary time unit
```

The actual output frequency calculation (from [`f469-disco/usermods/scard/ports/stm32/scard_io.c`](https://github.com/diybitcoinhardware/f469-disco/blob/master/usermods/scard/ports/stm32/scard_io.c)):

```c
uint32_t clk_in = get_usart_clock(usart_id);  // APB1 = 45 MHz for USART2
uint32_t prescaler = (clk_in + 2U * SCARD_MAX_CLK_FREQUENCY_HZ - 1U) /
                     (2U * SCARD_MAX_CLK_FREQUENCY_HZ);
// prescaler = ceil(45,000,000 / 10,000,000) = 5
uint32_t card_clk = clk_in / (2U * prescaler);
// card_clk = 45,000,000 / (2 × 5) = 4,500,000 Hz
```

| Parameter | Value |
|-----------|-------|
| MCU System Clock | 180 MHz |
| APB1 Peripheral Clock (PCLK1) | 45 MHz |
| USART2 Input Clock | 45 MHz |
| Prescaler (GTPR.PSC) | 5 |
| **SC_CLK Output Frequency** | **4.5 MHz** |
| USART Baudrate | 4,500,000 / 372 ≈ **12,097 baud** |

The smartcard reader Python configuration is in [`src/keystore/javacard/util.py`](../src/keystore/javacard/util.py):

```python
reader = sc.Reader(
    name="Specter card reader",
    ifaceId=2,            # USART2
    ioPin=Pin.cpu.A2,     # PA2 = USART2_TX (data)
    clkPin=Pin.cpu.A4,    # PA4 = USART2_CK (clock → ST8034 XTAL1)
    rstPin=Pin.cpu.G10,
    presPin=Pin.cpu.C2,
    pwrPin=Pin.cpu.C5,
)
```

### Signal Path (Current)
```
MCU PA4 (USART2_CK) ──[SC_CLK @ 4.5 MHz]──→ R403 (22Ω) ──→ ST8034 XTAL1 (pin 1)
                                                              ST8034 XTAL2 (pin 2) → NC
```

---

## Proposed Design — External Crystal Oscillator

The ST8034ATDT supports an external crystal connected between **XTAL1 (pin 1)** and **XTAL2 (pin 2)**, using its internal oscillator amplifier. This eliminates the need for the MCU to generate the clock signal.

### Recommended Crystal

A **4 MHz passive crystal** in an **SMD3225 (3.2×2.5mm) 4-pin package** is recommended:

| Parameter | Value |
|-----------|-------|
| Frequency | 4 MHz |
| Package | SMD3225-4P (3.2×2.5mm) |
| LCSC Part Number | **C70587** |
| MFR Part Number | X322520MPB4SI (YXC) |
| JLCPCB Status | **Basic/Preferred Part** |
| Load Capacitance | 20 pF (typical) |
| Frequency Tolerance | ±10 ppm |
| Operating Temp | -40°C to +85°C |

> **Note:** 4 MHz is a standard ISO 7816 smartcard clock frequency, well within the ST8034's supported range (up to 20 MHz). With CLKDIV1 = LOW (divider = 1, as in the current design), the full 4 MHz is passed to the card's CLK pin.

### Load Capacitors

The crystal requires two load capacitors (C_L1, C_L2) from each crystal pin to GND. The values depend on the crystal's specified load capacitance (C_L) and PCB stray capacitance (C_stray, typically ~2-5 pF):

```
C_L1 = C_L2 = 2 × (C_L - C_stray)
```

For C_L = 20 pF and C_stray ≈ 3 pF: **C_L1 = C_L2 ≈ 34 pF** (use standard 33 pF, 0402/0805).

### Connection Diagram

![External Crystal Oscillator Connection](smartcard_external_crystal.png)

### Signal Path (Proposed)
```
MCU PA4 (USART2_CK) ──── DISCONNECTED (not routed to ST8034)

                  C_L1 (33pF)
                    │
ST8034 XTAL1 (pin 1) ──┤──── Y401 (4 MHz Crystal) ────┤── ST8034 XTAL2 (pin 2)
                    │                                   │
                   GND                                 GND
                                                        │
                                                    C_L2 (33pF)
```

### Hardware Changes Summary

1. **Remove** the SC_CLK trace from R403 to ST8034 XTAL1
2. **Add** 4 MHz crystal Y401 (C70587) between ST8034 XTAL1 (pin 1) and XTAL2 (pin 2)
3. **Add** load capacitors C_L1 and C_L2 (33 pF each) from XTAL1 and XTAL2 to GND
4. R403 (22Ω series resistor on SC_CLK) can be **removed** or left unpopulated

---

## Software Changes Required

**Yes, the firmware needs changes if the MCU clock is disconnected and an external crystal is used.**

### Why Changes Are Needed

The STM32's USART in smartcard mode currently ties its **data baudrate** to the **clock prescaler output**. Both are derived from the same source:

```c
// Current code in scard_io.c init_smartcard():
uint32_t card_clk = clk_in / (2U * prescaler);           // 4.5 MHz CK output
uint32_t baudrate = (card_clk + SCARD_ETU / 2U) / SCARD_ETU;  // ≈ 12,097 baud
```

When the MCU generates the clock, the data timing and card clock are inherently synchronized because they come from the same USART peripheral. With an external 4 MHz crystal, the card operates at 4 MHz, but the MCU's USART would still internally calculate baudrate from its own prescaler (4.5 MHz), causing a **~12.5% frequency mismatch** that would corrupt smartcard communication.

### Required Firmware Changes

#### 1. Baudrate Calculation (`f469-disco/usermods/scard/ports/stm32/scard_io.c`)

The baudrate must be calculated from the **external crystal frequency** instead of the USART prescaler:

```c
// New: define external crystal frequency
#define SCARD_EXTERNAL_CLK_HZ  (4000000LU)  // 4 MHz crystal

static bool init_smartcard(SMARTCARD_HandleTypeDef* sc_handle,
                           USART_TypeDef* usart_handle, uint8_t usart_id) {
    uint32_t clk_in = get_usart_clock(usart_id);

    // Prescaler still needed (USART requires valid value), but CK pin is unused
    uint32_t prescaler = 1U;  // Minimum valid value

    // Baudrate must match the EXTERNAL crystal frequency, not the USART CK
    uint32_t baudrate = (SCARD_EXTERNAL_CLK_HZ + SCARD_ETU / 2U) / SCARD_ETU;
    // = 4,000,000 / 372 ≈ 10,753 baud

    sc_handle->Init.Prescaler = prescaler;
    sc_handle->Init.BaudRate  = baudrate;
    // ... rest unchanged
}
```

| Parameter | Current (MCU Clock) | Proposed (4 MHz Crystal) |
|-----------|-------------------|------------------------|
| Card CLK frequency | 4.5 MHz | 4.0 MHz |
| USART baudrate | ~12,097 baud | ~10,753 baud |
| USART prescaler | 5 | 1 (or any valid value) |
| CK pin function | Drives ST8034 XTAL1 | Unused (still toggles) |

#### 2. CLK Pin Configuration (Optional)

The `clk_pin` parameter and its USART alternate-function configuration can optionally be **kept as-is**. The USART_CK pin will still toggle at its own frequency, but since it's physically disconnected from the ST8034, this is harmless. Removing the pin configuration is a cosmetic improvement but not strictly necessary.

If you want to make the CLK pin fully optional:

```c
// In init_pins():
static bool init_pins(mp_obj_t io_pin, mp_obj_t clk_pin, uint8_t usart_id) {
    bool ok = mp_hal_pin_config_alt(
        pin_find(io_pin), MP_HAL_PIN_MODE_ALT_OPEN_DRAIN, MP_HAL_PIN_PULL_UP,
        AF_FN_USART, usart_id);

    // Only configure CK pin if provided (allow None for external crystal designs)
    if(clk_pin != mp_const_none) {
        ok = ok && mp_hal_pin_config_alt(
            pin_find(clk_pin), MP_HAL_PIN_MODE_ALT, MP_HAL_PIN_PULL_UP,
            AF_FN_USART, usart_id);
    }
    return ok;
}
```

#### 3. Python Configuration (`src/keystore/javacard/util.py`)

If the CLK pin is made optional in the C layer:

```python
reader = sc.Reader(
    name="Specter card reader",
    ifaceId=2,
    ioPin=Pin.cpu.A2,
    clkPin=None,           # No MCU clock output needed (external crystal)
    rstPin=Pin.cpu.G10,
    presPin=Pin.cpu.C2,
    pwrPin=Pin.cpu.C5,
)
```

### Minimum Viable Change

The **absolute minimum** firmware change to support an external crystal is a **single-line edit** to the baudrate calculation — changing the frequency used from the derived USART CK frequency to the known external crystal frequency. The USART prescaler and CK pin can remain configured (they just become functionally unused).

---

## Using ST8034TDT Instead of ST8034ATDT (Zero Software Changes)

The **ST8034TDT** has better availability than the ST8034ATDT but has a different clock divider behavior. The key difference is:

| Device | CLKDIV = LOW | CLKDIV = HIGH |
|--------|-------------|---------------|
| **ST8034ATDT** | fXTAL (÷1) | fXTAL / 2 |
| **ST8034TDT** | fXTAL / 4 | fXTAL / 2 |

The current Shield-BE design uses the ST8034ATDT with **CLKDIV1 = LOW** (÷1 mode), so the card receives the full XTAL1 input frequency (4.5 MHz from the MCU). The ST8034TDT's **minimum divider is ÷2** (CLKDIV = HIGH), not ÷1.

### The Problem

If you simply swap ST8034ATDT → ST8034TDT without changes:
- MCU outputs 4.5 MHz on USART2_CK → enters ST8034 XTAL1
- ST8034TDT with CLKDIV=HIGH divides by 2 → card receives **2.25 MHz**
- But the firmware baudrate is based on 4.5 MHz → **12,097 baud**
- Card expects baudrate at 2.25 MHz / 372 = **6,048 baud**
- **2× mismatch → communication failure**

### The Solution: 9 MHz External Crystal

Use a **9 MHz external crystal** on the ST8034TDT's XTAL1/XTAL2 pins, with **CLKDIV = HIGH** (÷2 divider):

```
9 MHz crystal ÷ 2 = 4.5 MHz to card
```

This produces **exactly 4.5 MHz** at the card — the same frequency the current MCU clock provides. The firmware's baudrate calculation yields:

```c
// Firmware (unchanged):
clk_in = 45,000,000 Hz (APB1)
prescaler = 5
card_clk = 45,000,000 / (2 × 5) = 4,500,000 Hz
baudrate = 4,500,000 / 372 ≈ 12,097 baud

// Card (with 9 MHz crystal, ÷2):
card_clk = 9,000,000 / 2 = 4,500,000 Hz
expected_baudrate = 4,500,000 / 372 ≈ 12,097 baud  ✓ EXACT MATCH
```

**No software changes are needed.** The MCU's USART2_CK pin (PA4) still toggles at 4.5 MHz but is physically disconnected from the ST8034. The USART data baudrate (12,097) exactly matches what the card expects from its 4.5 MHz clock. Both sides agree on the timing.

### Hardware Changes for ST8034TDT

1. **Replace** U401: ST8034ATDT → **ST8034TDT** (pin-compatible, same SO-16 package)
2. **Disconnect** SC_CLK trace from MCU PA4 to ST8034 XTAL1
3. **Add** Y401: **9 MHz crystal** (SMD3225-4P) between XTAL1 (pin 1) and XTAL2 (pin 2)
4. **Add** load capacitors C_L1, C_L2 from XTAL1/XTAL2 to GND
5. **Change CLKDIV1 = HIGH** (currently LOW) — tie to VCC via pull-up resistor on PCB
6. **Remove** R403 (22Ω SC_CLK series resistor, no longer needed)

### CLKDIV Pin Configuration Change

The current schematic has `SC_CLKDIV1 = LOW` for the ST8034ATDT's ÷1 mode. For the ST8034TDT, this must change to **HIGH** for the ÷2 divider:

| Pin | Current (ST8034ATDT) | New (ST8034TDT) | Effect |
|-----|---------------------|-----------------|--------|
| CLKDIV (pin 6) | LOW (÷1) | **HIGH (÷2)** | 9 MHz / 2 = 4.5 MHz |

On the MCU interface sheet, SC_CLKDIV1 is routed to an MCU GPIO. For zero firmware changes, **tie CLKDIV1 to VCC** via a pull-up resistor on the shield PCB (hardware-only change).

### Signal Path (ST8034TDT with 9 MHz Crystal)
```
MCU PA4 (USART2_CK) ──── DISCONNECTED (not routed to ST8034)

                  C_L1
                    │
ST8034TDT XTAL1 ───┤──── Y401 (9 MHz Crystal) ────┤── ST8034TDT XTAL2
                    │                                │
                   GND                              GND
                                                     │
                                                   C_L2

CLKDIV (pin 6) ──── HIGH (VCC via pull-up)

Card CLK = 9 MHz / 2 = 4.5 MHz  ← identical to current design
USART baudrate = 12,097 baud     ← unchanged firmware
```

### 9 MHz Crystal Selection

9 MHz is **not commonly available as a JLCPCB basic part** in SMD3225. Options:

| Option | Frequency | JLCPCB Status | LCSC | Notes |
|--------|-----------|---------------|------|-------|
| **Option A** | 9 MHz SMD3225 | Extended part | — | Check LCSC for availability; extended parts have ~$3 setup fee |
| **Option B** | 8 MHz SMD3225 | **Basic part** | C115962 | Card CLK = 4 MHz; requires firmware baudrate change |

**If 9 MHz must be sourced as an extended part**, the additional cost is minimal ($3 one-time per order). This approach requires zero software changes but uses a non-standard crystal frequency.

**The 8 MHz option is now the ★ recommended approach** — see the next section. With one small firmware define (`SCARD_CARD_CLK_HZ`), both ATDT and TDT work identically with a JLCPCB basic-part crystal.

---

## ★ Recommended: 8 MHz Crystal + 4 MHz Card Clock (Both ATDT and TDT)

The best overall solution is to use an **8 MHz crystal** with **CLKDIV = HIGH (÷2)** on both ST8034 variants, giving a standard **4 MHz card clock**. This requires a single, small firmware change but delivers the most benefits:

- **8 MHz crystal is a JLCPCB basic part** (C115962) — cheapest sourcing
- **4 MHz is the standard ISO 7816 smartcard clock** — maximum compatibility
- **Both ST8034ATDT and ST8034TDT work identically** — no autodetection, interchangeable ICs
- **Single firmware change** — one define, no runtime logic

### How It Works

Both ST8034 variants with CLKDIV = HIGH divide by 2:

| Device | CLKDIV = HIGH | 8 MHz Crystal | Card CLK |
|--------|---------------|---------------|----------|
| ST8034ATDT | ÷2 | 8 MHz | **4 MHz** |
| ST8034TDT | ÷2 | 8 MHz | **4 MHz** |

The card receives exactly 4 MHz regardless of which IC is populated. ISO 7816 baudrate at 4 MHz:

```
baudrate = 4,000,000 / 372 ≈ 10,753 baud
```

### Firmware Change

**One define** in [`f469-disco/usermods/scard/scard.h`](https://github.com/diybitcoinhardware/f469-disco/blob/master/usermods/scard/scard.h):

```c
/// Frequency of card clock provided to smart card via external crystal + divider
/// When defined, baudrate is derived from this value instead of the USART prescaler.
/// This decouples the data timing from the MCU's CK pin output.
#define SCARD_CARD_CLK_HZ               (4000000LU)  // 8 MHz crystal ÷ 2
```

And a small change in [`f469-disco/usermods/scard/ports/stm32/scard_io.c`](https://github.com/diybitcoinhardware/f469-disco/blob/master/usermods/scard/ports/stm32/scard_io.c) in `init_smartcard()`:

```c
  // Calculate clock prescaler, programmed into USART_GTPR.PSC
  uint32_t clk_in = get_usart_clock(usart_id);
  uint32_t prescaler = (clk_in + 2U * SCARD_MAX_CLK_FREQUENCY_HZ - 1U) /
                       (2U * SCARD_MAX_CLK_FREQUENCY_HZ);
  if(prescaler < 1U) {
    prescaler = 1U;
  } else if(prescaler > 31U) {
    return false;
  }

  // Calculate baudrate depending on smart card clock and etu
#ifdef SCARD_CARD_CLK_HZ
  // External crystal: baudrate must match the actual card clock frequency,
  // not the USART prescaler output (which is disconnected from the card)
  uint32_t baudrate = (SCARD_CARD_CLK_HZ + SCARD_ETU / 2U) / SCARD_ETU;
#else
  // MCU-clocked: baudrate derived from prescaler (original behavior)
  uint32_t card_clk = clk_in / (2U * prescaler);
  uint32_t baudrate = (card_clk + SCARD_ETU / 2U) / SCARD_ETU;
#endif
```

**That's it.** When `SCARD_CARD_CLK_HZ` is defined, baudrate is calculated from the known external crystal frequency. When undefined, the original behavior is preserved for backward compatibility with MCU-clocked designs.

### Why Not Just Change SCARD_MAX_CLK_FREQUENCY_HZ?

Changing `SCARD_MAX_CLK_FREQUENCY_HZ` from 5 MHz to 4 MHz won't work because the USART prescaler is integer-only:

```
APB1 = 45 MHz
prescaler = ceil(45,000,000 / 8,000,000) = 6
MCU CK output = 45,000,000 / (2 × 6) = 3,750,000 Hz (not 4 MHz!)
derived baudrate = 3,750,000 / 372 ≈ 10,081 baud
```

But the card running from an 8 MHz ÷ 2 crystal expects:
```
card clock = 4,000,000 Hz
expected baudrate = 4,000,000 / 372 ≈ 10,753 baud
```

That's a **6.7% mismatch** — UART communication requires <3% tolerance, so this would **fail**. The baudrate must be decoupled from the prescaler when using an external crystal; the `SCARD_CARD_CLK_HZ` define does exactly that.

### Timing Verification

| Parameter | Value |
|-----------|-------|
| External crystal | 8 MHz |
| CLKDIV setting | HIGH (÷2) |
| **Card CLK** | **4,000,000 Hz** |
| SCARD_CARD_CLK_HZ | 4,000,000 |
| **USART baudrate** | **4,000,000 / 372 ≈ 10,753 baud** |
| Card expected baudrate | 4,000,000 / 372 ≈ 10,753 baud |
| **Mismatch** | **0% — exact match** ✓ |

### Hardware Configuration

Both ATDT and TDT use identical hardware:

```
                  C_L1 (per crystal spec)
                    │
ST8034 XTAL1 ──────┤──── Y401 (8 MHz Crystal) ────┤── ST8034 XTAL2
                    │                                │
                   GND                              GND
                                                     │
                                                   C_L2 (per crystal spec)

CLKDIV (pin 6) ──── HIGH (VCC via 10kΩ pull-up)

MCU PA4 (USART2_CK) ──── DISCONNECTED (not routed to ST8034)

Card CLK = 8 MHz / 2 = 4.0 MHz (standard ISO 7816)
USART baudrate = 10,753 baud (firmware-configured)
```

### BOM for 8 MHz Crystal Approach (★ Recommended)

| Ref | Part | Action | LCSC | Notes |
|-----|------|--------|------|-------|
| U401 | ST8034ATDT or ST8034TDT | **Either works** | — | Interchangeable with this design |
| Y401 | 8 MHz Crystal SMD3225 | **ADD** | **C115962** | **JLCPCB Basic Part** ✓ |
| C_L1 | Load cap 0402 (per crystal spec) | **ADD** | — | Typically 15-33 pF |
| C_L2 | Load cap 0402 (per crystal spec) | **ADD** | — | Typically 15-33 pF |
| R_PU | 10kΩ 0402 pull-up | **ADD** | C25804 | CLKDIV1 pull-up to VCC |
| R403 | 22Ω 0805 | **REMOVE** | — | SC_CLK series resistor (no longer needed) |

Net component change: +3 components (crystal + 2 caps + pull-up - 1 resistor).

---

## Summary Comparison (All Options)

| Configuration | IC | Crystal | CLKDIV | Card CLK | Firmware Change? | Crystal Cost |
|---------------|-----|---------|--------|----------|-----------------|-------------|
| Current design | ATDT | None (MCU CK) | LOW (÷1) | 4.5 MHz | None (baseline) | N/A |
| ATDT + 4 MHz crystal | ATDT | 4 MHz | LOW (÷1) | 4.0 MHz | **Yes** (baudrate) | Basic |
| TDT + 9 MHz crystal | TDT | 9 MHz | HIGH (÷2) | 4.5 MHz | None | Extended |
| ★ **8 MHz crystal (either IC)** | **Either** | **8 MHz** | **HIGH (÷2)** | **4.0 MHz** | **Yes** (1 define) | **Basic** ✓ |

**★ Recommended: 8 MHz crystal with CLKDIV=HIGH.** One small firmware change (`SCARD_CARD_CLK_HZ = 4000000`) enables both ST8034ATDT and ST8034TDT to be used interchangeably with a JLCPCB basic-part crystal, delivering the standard ISO 7816 4 MHz card clock. No autodetection needed — the hardware is identical regardless of which IC variant is populated.

---

## Previous BOM Tables (For Reference)

### For ST8034ATDT with 4 MHz Crystal

| Ref | Part | Action | LCSC | Notes |
|-----|------|--------|------|-------|
| Y401 | 4 MHz Crystal SMD3225 | **ADD** | C70587 | JLCPCB Basic Part |
| C_L1 | 33 pF 0805 | **ADD** | — | Load cap for crystal |
| C_L2 | 33 pF 0805 | **ADD** | — | Load cap for crystal |
| R403 | 22Ω 0805 | **REMOVE** | — | SC_CLK series resistor (no longer needed) |

### For ST8034TDT with 9 MHz Crystal (zero firmware changes)

| Ref | Part | Action | LCSC | Notes |
|-----|------|--------|------|-------|
| U401 | ST8034TDT | **REPLACE** | — | Better availability than ATDT |
| Y401 | 9 MHz Crystal SMD3225 | **ADD** | — | JLCPCB Extended Part (check LCSC) |
| C_L1 | Load cap (per crystal spec) | **ADD** | — | Load cap for crystal |
| C_L2 | Load cap (per crystal spec) | **ADD** | — | Load cap for crystal |
| R_PU | 10kΩ 0402 pull-up | **ADD** | C25804 | CLKDIV1 pull-up to VCC |
| R403 | 22Ω 0805 | **REMOVE** | — | SC_CLK series resistor (no longer needed) |
