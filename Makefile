TARGET_DIR = bin
BOARD ?= STM32F469DISC
FLAVOR ?= SPECTER
USER_C_MODULES ?= ../../../usermods
MPY_DIR ?= f469-disco/micropython
ifeq ($(shell uname),Linux)
    MPY_CFLAGS ?= -Wno-dangling-pointer -Wno-enum-int-mismatch
else
    MPY_CFLAGS ?=
endif
FROZEN_MANIFEST_DISCO ?= ../../../../manifests/disco.py
FROZEN_MANIFEST_DEBUG ?= ../../../../manifests/debug.py
FROZEN_MANIFEST_UNIX ?= ../../../../manifests/unix.py
DEBUG ?= 0
USE_DBOOT ?= 0
GIT_INFO ?= src/git_info.py
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

# embed git metadata for firmware builds
.PHONY: git-info
git-info:
	./tools/embed_git_info.py $(GIT_INFO)

# disco board with bitcoin library
# Uses stm32f469disc.ld: ISR vector at 0x08000000 (32 KB), code at 0x08008000,
# no padding gap. specter-diy.bin (~500 KB) can be dragged onto DIS_F469NI or
# flashed with st-flash/CubeProgrammer. specter-diy.hex is the sparse Intel HEX.
disco: $(TARGET_DIR) mpy-cross $(MPY_DIR)/ports/stm32 git-info
	@echo Building firmware
	make -C $(MPY_DIR)/ports/stm32 \
        BOARD=$(BOARD) \
        FLAVOR=$(FLAVOR) \
        USE_DBOOT=$(USE_DBOOT) \
        USER_C_MODULES=$(USER_C_MODULES) \
        FROZEN_MANIFEST=$(FROZEN_MANIFEST_DISCO) \
        DEBUG=$(DEBUG) \
        CFLAGS_EXTRA="$(MPY_CFLAGS)" \
        LD_FILES="boards/$(BOARD)/stm32f469disc.ld boards/common_ifs.ld" \
        TEXT0_ADDR=0x08000000 \
        TEXT1_ADDR= && \
	arm-none-eabi-objcopy -O binary \
        $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.elf \
        $(TARGET_DIR)/specter-diy.bin && \
        cp $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.hex \
                $(TARGET_DIR)/specter-diy.hex

# debug build — same compact layout as disco so debug.bin is also drag-and-drop compatible
debug: $(TARGET_DIR) mpy-cross $(MPY_DIR)/ports/stm32 git-info
	@echo Building firmware
	make -C $(MPY_DIR)/ports/stm32 \
        BOARD=$(BOARD) \
        FLAVOR=$(FLAVOR) \
        USE_DBOOT=$(USE_DBOOT) \
        USER_C_MODULES=$(USER_C_MODULES) \
        FROZEN_MANIFEST=$(FROZEN_MANIFEST_DEBUG) \
        DEBUG=$(DEBUG) \
        CFLAGS_EXTRA="$(MPY_CFLAGS)" \
        LD_FILES="boards/$(BOARD)/stm32f469disc.ld boards/common_ifs.ld" \
        TEXT0_ADDR=0x08000000 \
        TEXT1_ADDR= && \
	arm-none-eabi-objcopy -O binary \
        $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.elf \
        $(TARGET_DIR)/debug.bin && \
	cp $(MPY_DIR)/ports/stm32/build-STM32F469DISC/firmware.hex \
        $(TARGET_DIR)/debug.hex


# unixport (simulator)
unix: $(TARGET_DIR) mpy-cross $(MPY_DIR)/ports/unix git-info
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

all: mpy-cross disco unix

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
	rm -rf $(RELEASE_DIR)
	make -C $(MPY_DIR)/mpy-cross clean
	make -C $(MPY_DIR)/ports/unix \
		USER_C_MODULES=$(USER_C_MODULES) \
		FROZEN_MANIFEST=$(FROZEN_MANIFEST_UNIX) clean
	make -C $(MPY_DIR)/ports/stm32 \
		BOARD=$(BOARD) \
		USER_C_MODULES=$(USER_C_MODULES) \
		FROZEN_MANIFEST=$(FROZEN_MANIFEST_DISCO) clean

.PHONY: all clean git-info mpy-cross disco debug unix simulate test bootloader-build release-binaries
