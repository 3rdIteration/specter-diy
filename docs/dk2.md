# Specter-DIY on STM32U5G9J-DK2

This document describes how to build, flash, and run Specter-DIY on the
[STM32U5G9J-DK2](https://www.st.com/en/evaluation-tools/stm32u5g9j-dk2.html)
Discovery Kit.

## Hardware Overview

| Feature | STM32F469I-DISCO (original) | STM32U5G9J-DK2 (new) |
|---------|---------------------------|------------------------|
| MCU | STM32F469NI (Cortex-M4, 180 MHz) | STM32U5G9ZJT6Q (Cortex-M33, 160 MHz) |
| Flash | 2 MB | 4 MB |
| RAM | 384 KB SRAM + 16 MB ext. SDRAM | 3 MB internal SRAM |
| Display | 4" 800×480 DSI | 5" 800×480 parallel RGB |
| Touch | FT6x06 (I2C) | Capacitive (I2C1) |
| Ext. Storage | QSPI Flash (16 MB) | Octo-SPI Flash (128 MB) |
| USB | Micro-B (OTG FS) | Type-C (USB HS) |
| Security | — | TrustZone, HW crypto |

## Building

### Prerequisites

- `arm-none-eabi-gcc` (version 9 or later, with Cortex-M33 support)
- Python 3.9+
- Git (for submodule checkout)

### Quick Build

```bash
# Clone with submodules
git clone --recursive https://github.com/cryptoadvance/specter-diy.git
cd specter-diy

# Build DK2 firmware
make dk2

# Output: bin/specter-diy-dk2.bin and bin/specter-diy-dk2.hex
```

Or use the dedicated build script:

```bash
./build_firmware_dk2.sh
```

### Docker Build

```bash
docker build -t specter-diy .
docker run --rm -v $(pwd):/app specter-diy bash ./build_firmware_dk2.sh
```

## Flashing

### Via ST-Link (built-in on DK2)

```bash
# Using st-flash
st-flash write bin/specter-diy-dk2.bin 0x08000000

# Using STM32CubeProgrammer CLI
STM32_Programmer_CLI -c port=SWD -w bin/specter-diy-dk2.bin 0x08000000
```

### Via OpenOCD

```bash
openocd -f interface/stlink-v3.cfg -f target/stm32u5x.cfg \
    -c "program bin/specter-diy-dk2.bin 0x08000000 verify reset exit"
```

## Pin Mapping

### QR Scanner (Specter Shield)

The QR scanner connects via the Arduino-compatible headers:

| Function | DK2 Pin | Arduino Header |
|----------|---------|----------------|
| Scanner UART TX | PD5 (UART2 TX) | D5 |
| Scanner UART RX | PD6 (UART2 RX) | D6 |
| Scanner Trigger | PF15 | D2 |

The UART is mapped as `"YA"` in MicroPython, matching the F469 configuration.

### Battery Monitor (Specter Shield)

The battery monitor connects via I2C on the Arduino headers:

| Function | DK2 Pin | Arduino Header |
|----------|---------|----------------|
| I2C2 SCL | PF1 | — |
| I2C2 SDA | PF0 | — |

**Note:** I2C1 (PG14/PG13) is used by the DK2's onboard touch controller.
The battery monitor uses I2C2 instead.

### LEDs

| LED | DK2 Pin | Color | Usage |
|-----|---------|-------|-------|
| LED1 | PD2 | Green | Status |
| LED2 | PD4 | Red | SD card activity |

The DK2 has 2 user LEDs vs. 4 on the F469. LED3/LED4 are aliased to LED1/LED2.

### User Button

The user button is on PC13 (active high, rising edge), used as the power/shutdown button.

## Differences from F469-DISCO

### No External SDRAM

The DK2 uses 3 MB internal SRAM instead of 16 MB external SDRAM. The RAM
filesystem (`/ramdisk`) is smaller (512 KB vs ~12 MB), but sufficient for
Specter-DIY operation since transaction data and QR codes are small.

### No Power Hold Circuit

The DK2 is USB-powered and doesn't have the power hold circuit (pin B15)
present on the F469 when used with a battery via the Specter Shield. The
"power off" button press triggers a hard reset instead.

### Display Interface

The DK2 uses a parallel RGB LTDC interface (not MIPI-DSI) for its 5" display.
The display driver is completely different but provides the same Python API.

### Octo-SPI Flash (TODO)

The DK2's 128 MB Octo-SPI flash replaces the F469's 16 MB QSPI flash for
the `/qspi` mount point. Initial bring-up uses internal flash only.
Octo-SPI support will be added in a future update.

## Specter Shield Compatibility

The Specter Shield was designed for the F469-DISCO's Arduino headers. The
DK2 also has Arduino Uno V3-compatible headers, so the shield can physically
connect. However:

1. **Pin mapping differences**: Some Arduino header pins map to different
   MCU pins. The UART and I2C pins are compatible.
2. **Power**: The DK2 runs at 3.3V only, matching the shield's requirements.
3. **Smartcard**: The smartcard interface uses SPI and should work with
   appropriate pin configuration.

## Known Limitations

1. **Octo-SPI flash**: Not yet integrated. Settings are stored in internal
   flash only. The `/qspi` mount point is not available until OSPI support
   is added.
2. **Secure bootloader**: The bootloader has not been ported to STM32U5 yet.
   Firmware signature verification is not available.
3. **TrustZone**: Not yet utilized. Future versions may isolate crypto
   operations in the TrustZone secure world.
4. **USB Type-C**: The DK2 uses USB HS in FS mode. Full HS support may
   require additional configuration.

## Development

### REPL Access

The DK2 provides REPL access via UART1 on the ST-Link VCP (USB connector):

```bash
# Connect at 115200 baud
screen /dev/ttyACM0 115200
```

### Simulator

The Unix simulator (`make simulate`) is unchanged and works for development
and testing regardless of the target board.
