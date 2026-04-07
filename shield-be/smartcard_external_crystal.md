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

## BOM Impact

| Ref | Part | Action | LCSC | Notes |
|-----|------|--------|------|-------|
| Y401 | 4 MHz Crystal SMD3225 | **ADD** | C70587 | JLCPCB Basic Part |
| C_L1 | 33 pF 0805 | **ADD** | — | Load cap for crystal |
| C_L2 | 33 pF 0805 | **ADD** | — | Load cap for crystal |
| R403 | 22Ω 0805 | **REMOVE** | — | SC_CLK series resistor (no longer needed) |

Net component change: +2 components (crystal + 2 caps - 1 resistor).
