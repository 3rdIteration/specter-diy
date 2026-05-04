TARGET_DIR = bin
BOARD ?= STM32F469DISC
FLAVOR ?= SPECTER
USER_C_MODULES ?= ../../../usermods
MPY_DIR ?= f469-disco/micropython
MPY_CFLAGS ?= -Wno-dangling-pointer -Wno-enum-int-mismatch
FROZEN_MANIFEST_DISCO ?= ../../../../manifests/disco.py
FROZEN_MANIFEST_DEBUG ?= ../../../../manifests/debug.py
FROZEN_MANIFEST_UNIX ?= ../../../../manifests/unix.py
DEBUG ?= 0
USE_DBOOT ?= 0
BOOTLOADER_DIR ?= bootloader
RELEASE_DIR ?= release

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

$(RELEASE_DIR):
	mkdir -p $(RELEASE_DIR)

# check submodules
$(MPY_DIR)/mpy-cross/Makefile:
	git submodule update --init --recursive

# cross-compiler
mpy-cross: $(TARGET_DIR) $(MPY_DIR)/mpy-cross/Makefile
	@echo Building cross-compiler
	make -C $(MPY_DIR)/mpy-cross \
        DEBUG=$(DEBUG) \
        CFLAGS_EXTRA="$(MPY_CFLAGS)" && \
	cp $(MPY_DIR)/mpy-cross/mpy-cross $(TARGET_DIR)

# disco board with bitcoin library
# Produces specter-diy.hex (for CubeProgrammer / OpenOCD / st-flash) and
# specter-diy.bin (flat binary with a 112 KB gap at 0x08004000–0x0801FFFF;
# use specter-diy.hex for ST-Link tools and disco-dnd for drag-and-drop).
disco: $(TARGET_DIR) mpy-cross $(MPY_DIR)/ports/stm32
	@echo Building firmware
	make -C $(MPY_DIR)/ports/stm32 \
        BOARD=$(BOARD) \
        FLAVOR=$(FLAVOR) \
        USE_DBOOT=$(USE_DBOOT) \
        USER_C_MODULES=$(USER_C_MODULES) \
        FROZEN_MANIFEST=$(FROZEN_MANIFEST_DISCO) \
        DEBUG=$(DEBUG) \
        CFLAGS_EXTRA="$(MPY_CFLAGS)" && \
	arm-none-eabi-objcopy -O binary \
        $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.elf \
        $(TARGET_DIR)/specter-diy.bin && \
        cp $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.hex \
                $(TARGET_DIR)/specter-diy.hex

# Compact firmware for drag-and-drop onto the board's virtual drive (DIS_F469NI).
# Uses stm32f469disc.ld so the ISR vector occupies sectors 0-1 (0x08000000, 32 KB)
# and application code immediately follows at 0x08008000 — no gap, no padding.
# The resulting binary is ~500 KB and programs cleanly via the ST-LINK mass storage.
disco-dnd: $(TARGET_DIR) mpy-cross $(MPY_DIR)/ports/stm32
	@echo Building compact firmware for drag-and-drop
	make -C $(MPY_DIR)/ports/stm32 \
        BOARD=$(BOARD) \
        BUILD=build-$(BOARD)-dnd \
        FLAVOR=$(FLAVOR) \
        USER_C_MODULES=$(USER_C_MODULES) \
        FROZEN_MANIFEST=$(FROZEN_MANIFEST_DISCO) \
        DEBUG=$(DEBUG) \
        CFLAGS_EXTRA="$(MPY_CFLAGS)" \
        LD_FILES="boards/$(BOARD)/stm32f469disc.ld boards/common_ifs.ld" \
        TEXT0_ADDR=0x08000000 \
        TEXT1_ADDR= && \
	arm-none-eabi-objcopy -O binary \
        $(MPY_DIR)/ports/stm32/build-$(BOARD)-dnd/firmware.elf \
        $(TARGET_DIR)/specter-diy-dnd.bin

# disco board with bitcoin library
debug: $(TARGET_DIR) mpy-cross $(MPY_DIR)/ports/stm32
	@echo Building firmware
	make -C $(MPY_DIR)/ports/stm32 \
        BOARD=$(BOARD) \
        FLAVOR=$(FLAVOR) \
        USE_DBOOT=$(USE_DBOOT) \
        USER_C_MODULES=$(USER_C_MODULES) \
        FROZEN_MANIFEST=$(FROZEN_MANIFEST_DEBUG) \
        DEBUG=$(DEBUG) \
        CFLAGS_EXTRA="$(MPY_CFLAGS)" && \
	arm-none-eabi-objcopy -O binary \
        $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.elf \
        $(TARGET_DIR)/debug.bin && \
	cp $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.hex \
        $(TARGET_DIR)/debug.hex


# unixport (simulator)
unix: $(TARGET_DIR) mpy-cross $(MPY_DIR)/ports/unix
	@echo Building binary with frozen files
	make -C $(MPY_DIR)/ports/unix \
        USER_C_MODULES=$(USER_C_MODULES) \
        FROZEN_MANIFEST=$(FROZEN_MANIFEST_UNIX) \
        CFLAGS_EXTRA="$(MPY_CFLAGS)" && \
	cp $(MPY_DIR)/ports/unix/micropython $(TARGET_DIR)/micropython_unix

simulate: unix
	$(TARGET_DIR)/micropython_unix simulate.py

test: unix
	cd test && ../$(TARGET_DIR)/micropython_unix run_tests.py

all: mpy-cross disco disco-dnd unix

# Build the Specter bootloader
bootloader-build:
	@echo Building bootloader
	$(MAKE) -C $(BOOTLOADER_DIR) stm32f469disco READ_PROTECTION=1 WRITE_PROTECTION=1

# Build firmware with USE_DBOOT=1 and package both release binaries:
#   release/initial_firmware.bin  — for ST-Link / OpenOCD (startup + bootloader + firmware)
#   release/specter_upgrade.bin   — for drag-and-drop via Specter bootloader
release-binaries: $(RELEASE_DIR) $(TARGET_DIR) mpy-cross $(MPY_DIR)/ports/stm32 bootloader-build
	@echo Building firmware with bootloader support \(USE_DBOOT=1\)
	$(MAKE) -C $(MPY_DIR)/ports/stm32 \
        BOARD=$(BOARD) \
        FLAVOR=$(FLAVOR) \
        USE_DBOOT=1 \
        USER_C_MODULES=$(USER_C_MODULES) \
        FROZEN_MANIFEST=$(FROZEN_MANIFEST_DISCO) \
        DEBUG=$(DEBUG) \
        CFLAGS_EXTRA="$(MPY_CFLAGS)"
	arm-none-eabi-objcopy -O binary \
        $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.elf \
        $(TARGET_DIR)/specter-diy.bin
	cp $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.hex \
        $(TARGET_DIR)/specter-diy.hex
	@echo Assembling ST-Link binary \(startup + bootloader + firmware\)
	python3 $(BOOTLOADER_DIR)/tools/make-initial-firmware.py \
        -s $(BOOTLOADER_DIR)/build/stm32f469disco/startup/release/startup.hex \
        -b $(BOOTLOADER_DIR)/build/stm32f469disco/bootloader/release/bootloader.hex \
        -f $(TARGET_DIR)/specter-diy.hex \
        -bin $(RELEASE_DIR)/initial_firmware.bin
	@echo Generating drag-and-drop upgrade file
	python3 $(BOOTLOADER_DIR)/tools/upgrade-generator.py gen \
        -f $(TARGET_DIR)/specter-diy.hex \
        -p stm32f469disco \
        $(RELEASE_DIR)/specter_upgrade.bin
	@echo "ST-Link binary:        $(RELEASE_DIR)/initial_firmware.bin"
	@echo "Drag-and-drop binary:  $(RELEASE_DIR)/specter_upgrade.bin"

clean:
	rm -rf $(TARGET_DIR)
	make -C $(MPY_DIR)/mpy-cross clean
	make -C $(MPY_DIR)/ports/unix \
		USER_C_MODULES=$(USER_C_MODULES) \
		FROZEN_MANIFEST=$(FROZEN_MANIFEST_UNIX) clean
	make -C $(MPY_DIR)/ports/stm32 \
		BOARD=$(BOARD) \
		USER_C_MODULES=$(USER_C_MODULES) \
		FROZEN_MANIFEST=$(FROZEN_MANIFEST_DISCO) clean
	make -C $(MPY_DIR)/ports/stm32 \
		BOARD=$(BOARD) \
		BUILD=build-$(BOARD)-dnd \
		USER_C_MODULES=$(USER_C_MODULES) \
		FROZEN_MANIFEST=$(FROZEN_MANIFEST_DISCO) clean

.PHONY: all clean mpy-cross disco disco-dnd debug unix simulate test bootloader-build release-binaries
