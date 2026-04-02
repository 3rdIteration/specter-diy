# STM32F746G-Discovery Board Support

This document describes how to build and use Specter-DIY on the STM32F746G-Discovery board.

## Board Overview

The [STM32F746G-Discovery](https://www.st.com/en/evaluation-tools/32f746gdiscovery.html) is an evaluation board featuring:

- **MCU**: STM32F746NGH6 (ARM Cortex-M7, 216 MHz max, 1 MB Flash, 340 KB SRAM)
- **Display**: 4.3" TFT LCD, 480×272 pixels (RK043FN48H), capacitive touchscreen (FT5336)
- **External SDRAM**: IS42S32400F, 128 Mbit (16 MB)
- **External QSPI Flash**: N25Q128A, 128 Mbit (16 MB)
- **SD Card**: microSD card slot
- **USB**: USB OTG FS (Micro-AB connector)
- **Debug**: Integrated ST-LINK/V2-1

## Differences from the STM32F469I-Discovery

| Feature | STM32F469I-DISCO | STM32F746G-DISCO |
|---------|-----------------|-----------------|
| MCU Core | Cortex-M4 (180 MHz) | Cortex-M7 (216 MHz) |
| Internal Flash | 2 MB | 1 MB |
| Display | 4" 800×480 (DSI) | 4.3" 480×272 (LTDC/RGB) |
| Touch Controller | FT6x06 | FT5336 |
| HSE Crystal | 8 MHz | 25 MHz |
| Battery/Power Hold | Yes (Specter Shield) | No |
| LEDs | 4 | 1 (green) |

**Note**: The F746G-Discovery has a smaller display resolution (480×272 vs 800×480). The GUI will render at the lower resolution. Some UI elements may need adaptation for the smaller screen.

## Building

### Prerequisites

Same as the standard build — you need `arm-none-eabi-gcc` and related tools. See [build.md](build.md) for details.

### Build the Firmware

```sh
# Clone with submodules
git clone --recursive https://github.com/cryptoadvance/specter-diy.git
cd specter-diy

# Build for STM32F746G-Discovery
make f746g
```

This produces:
- `bin/specter-diy-f746g.bin` — Binary firmware image
- `bin/specter-diy-f746g.hex` — Intel HEX firmware image

### Flash the Firmware

1. Connect the STM32F746G-Discovery board via the ST-LINK USB connector (CN14).
2. The board appears as a USB mass storage device (`DIS_F746NG`).
3. Copy `bin/specter-diy-f746g.bin` to the board's virtual drive.
4. The board will automatically reset and run the firmware.

Alternatively, use STM32CubeProgrammer or OpenOCD:

```sh
# Using st-flash
st-flash write bin/specter-diy-f746g.bin 0x08000000

# Using OpenOCD
openocd -f board/stm32f7discovery.cfg -c "program bin/specter-diy-f746g.bin 0x08000000 verify reset exit"
```

## Hardware Notes

### No Power Management

Unlike the F469I-Discovery with the Specter Shield, the F746G-Discovery does not have:
- A power hold circuit (the board is always powered via USB or external supply)
- A battery management IC
- A dedicated power button

The user button (blue, PI11) is available but does not control power.

### Display

The 480×272 LCD is natively landscape orientation. The display module supports both portrait and landscape modes, but portrait mode requires software rotation and reduces effective resolution to 272×480.

### SD Card

The microSD card slot is on the back of the board. SD card detection is on pin PC13.

### USB

USB OTG FS is available on the Micro-AB connector (CN13). Note that USB VBUS detection requires software polling (pin PJ12) since PA9 is used for the ST-LINK VCP.

### QSPI Flash

The N25Q128A 16 MB QSPI flash is used for the `/qspi` filesystem, storing wallet data and settings (same as the F469I-Discovery).

## File Structure

The F746G port adds the following files:

```
specter-diy/
├── Makefile                          # Added f746g target
├── boot/f746g/boot.py                # F746G-specific boot script
├── manifests/f746g.py                # F746G frozen module manifest
├── src/platform.py                   # Updated with F746G fallbacks
└── f469-disco/                       # (submodule)
    ├── manifests/disco.py            # Shared manifest (common libs)
    └── micropython/ports/stm32/boards/
        └── STM32F7DISC/              # MicroPython board definition (upstream)
```
