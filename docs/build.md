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

This produces `bin/specter-diy.bin`, which can be flashed by dragging the file onto the board's virtual mass-storage drive or by
using programming tools such as STM32CubeProgrammer.

To build custom bootloader and firmware that you will be able to sign check out the bootloader doc on [self-signed firmware](https://github.com/cryptoadvance/specter-bootloader/blob/master/doc/selfsigned.md). To wipe flash and remove protections on the device with the secure bootloader check out [this doc](https://github.com/cryptoadvance/specter-bootloader/blob/master/doc/remove_protection.md).

To build an open firmware (no bootloader and signature verifications) run `make disco`. It also produces the `bin/specter-diy.bin` image ready for flashing via the board's virtual drive or external programming tools.

### Flashing the firmware with OpenOCD

You can flash both official release images and locally built development binaries with the ST-LINK debugger that is built into the STM32F469 Discovery board. The examples below use the OpenOCD board configuration that ships with OpenOCD (`board/stm32f469i-disco.cfg`).

1. Connect the Discovery board to your computer using the ST-LINK USB port and ensure that no other debug session is active.
2. Start OpenOCD from the root of the repository (or the directory where the firmware image is located):

   ```sh
   # Flash an official release image that you have downloaded
   openocd -f board/stm32f469i-disco.cfg \
           -c "program /path/to/specter-diy-vX.Y.Z.bin 0x08000000 verify reset exit"

   # Flash a development build produced by `make disco`
   openocd -f board/stm32f469i-disco.cfg \
           -c "program bin/specter-diy.bin 0x08000000 verify reset exit"
   ```

   The `0x08000000` address is the start of the internal flash memory where the firmware resides. The `verify` option checks that the contents were written correctly, `reset` restarts the MCU, and `exit` terminates OpenOCD once flashing is complete.

3. Wait for OpenOCD to report a successful `verified` status. The board will reboot automatically and start the newly flashed firmware.

If you encounter permission issues on Linux, ensure your user is in the `dialout` (or distribution-specific) group that grants access to USB devices, or run the command with elevated privileges.

### How to build and run the simulator

Build the simulator with `make unix`—this compiles the MicroPython simulator for macOS and Linux and stores it under `bin/micropython_unix`.

Launch the simulator by running `bin/micropython_unix simulate.py`, or simply run `make simulate`.

If something is not working you can clean up with `make clean`.

## Run Unittests

Currently unittests work only on linuxport, and there are... not many... Contributions are very welcome!

```
make test
```
