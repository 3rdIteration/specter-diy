# Build

Clone the repository recursively `git clone https://github.com/cryptoadvance/specter-diy.git --recursive`

`bootloader` folder contains a [secure bootloader](https://github.com/cryptoadvance/specter-bootloader) that you can customize with your own firmware signing keys.

## Prerequisites for the Nix build

There are multiple ways to get all necessary tools. The recommended way is to use the Nix flake with direnv.
If that's too complicated for you, you can use the traditional `nix-shell` or install the tools manually (which mighty be tricky to get the dependencies right).

### Install Nix on Ubuntu 24.04

```sh
sudo apt update
sudo apt install nix
```

Enable the required experimental features for flakes and the new CLI:

```sh
sudo mkdir -p /etc/nix
echo "experimental-features = nix-command flakes" | sudo tee -a /etc/nix/nix.conf
```

Add your user to the `nix-users` group so you can access the Nix store:

```sh
sudo usermod -aG nix-users "$USER"
newgrp nix-users
```

### Nix flake (Recommended)

The easiest way to get all necessary tools is to use the Nix flake from the root of the repository. You need to have [Nix](https://nixos.org/) (on Mac use [determinate](https://github.com/DeterminateSystems/nix-installer)) with flakes enabled. This only works with Nix >2.7 (check with `nix --version`).
Install direnv with `brew install direnv` (on Mac) or `sudo apt install direnv` (on Linux). direnv automatically loads and unloads the environment described in `.envrc` when you enter or leave the repository, so the development tooling is ready without extra commands.

Make sure that [flakes are enabled](https://nixos.wiki/wiki/Flakes) in your Nix config. On Linux systems, your user might need to be added to the nix-users group.

```sh
# Enter development shell
nix develop

# Or use with direnv for automatic activation
direnv allow
```


### Nix shell

Alternatively, you can use the traditional `shell.nix`:

```sh
nix-shell
```
You'll need to have [Nix](https://nixos.org/) installed as well.

### Prerequisities (Manually): Board

To compile the firmware for the board you will need `arm-none-eabi-gcc` compiler.

**Debian/Ubuntu**:
```sh
sudo apt-get install build-essential gcc-arm-none-eabi binutils-arm-none-eabi gdb-multiarch openocd
```

**Archlinux**:
```sh
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils openocd base-devel python-case
```
You might need change default gcc flag settings with `CFLAGS_EXTRA="-w"`. Export it or set the variable before of `make`
to avoid warnings being raised as errors.

**MacOS**:
```sh
brew tap ArmMbed/homebrew-formulae
brew install arm-none-eabi-gcc
```

On **Windows**: Install linux subsystem and follow Linux instructions.

### Prerequisities (Manually): Simulator

You may need to install SDL2 library to simulate the screen of the device.

**Linux**:
```sh
sudo apt install libsdl2-dev
```

**MacOS**:
```sh
brew install sdl2
```

**Windows**:
- `sudo apt install libsdl2-dev` on Linux side.
- install and launch [Xming](https://sourceforge.net/projects/xming/) on Windows side
- set `export DISPLAY=:0` on linux part

## Build

All build commands might need a prefix like `nix develop -c` or need to be run from `nix develop` shell. If you use direnv, you don't need to do anything, apart from an initial `direnv allow`.

### Starting the build

After entering the development shell (either with `nix develop` or via direnv), start the default firmware build with:

```sh
make disco
```

This produces two files for use with ST-Link–based programming tools such as
STM32CubeProgrammer, OpenOCD, or `st-flash`:

| File | Use |
|------|-----|
| `bin/specter-diy.hex` | Flash with STM32CubeProgrammer or OpenOCD (sparse Intel HEX, recommended) |
| `bin/specter-diy.bin` | Flash with `st-flash write bin/specter-diy.bin 0x8000000` |

> **Note:** `bin/specter-diy.bin` contains a 112 KB padding gap between the ISR
> vector and the application code. This makes it unsuitable for drag-and-drop
> programming via the board's virtual mass-storage drive.

### Drag-and-drop onto the virtual drive (DIS_F469NI)

To flash the firmware by dragging a file onto the board's USB mass-storage drive
(`DIS_F469NI`), build the compact binary instead:

```sh
make disco-dnd
```

This uses a contiguous flash layout (ISR at `0x08000000`, code immediately
following at `0x08008000`) and produces `bin/specter-diy-dnd.bin` — a ~500 KB
binary with no padding gap that programs cleanly via the ST-LINK mass storage.

To flash, connect the board via the **miniUSB** cable on the top, wait for the
`DIS_F469NI` disk to appear, and copy `bin/specter-diy-dnd.bin` to its root.

You can build both targets together with:

```sh
make disco disco-dnd
```

To build a simulator run `make unix` - it will compile a micropython simulator for mac/unix and store it under `bin/micropython_unix`.

To launch a simulator either run `bin/micropython_unix simulate.py` or simly run `make simulate`.

If something is not working you can clean up with `make clean`

### Bootloader-based release binaries

To build a release package with the secure bootloader (initial installation binary
+ SD-card upgrade binary), run:

```sh
make release-binaries
```

This requires the `bootloader` submodule to be initialised. It produces:

| File | Use |
|------|-----|
| `release/initial_firmware.bin` | Initial flash: drag onto `DIS_F469NI` **or** `st-flash write ... 0x8000000` |
| `release/specter_upgrade.bin` | Firmware upgrade: copy to the root of an SD card |

For custom signing keys see the [bootloader self-signed firmware guide](https://github.com/cryptoadvance/specter-bootloader/blob/master/doc/selfsigned.md).

## Run Unittests

Currently unittests work only on linuxport, and there are... not many... Contributions are very welcome!

```
make test
```
