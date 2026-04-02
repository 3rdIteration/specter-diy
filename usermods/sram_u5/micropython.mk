SDRAM_MOD_DIR := $(USERMOD_DIR)

# STM32U5 build - use internal SRAM module
ifeq ($(CMSIS_MCU),STM32U5G9xx)
SRC_USERMOD += $(SDRAM_MOD_DIR)/sdram.c
endif
