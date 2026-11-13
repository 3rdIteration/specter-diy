include ../vendor/specter/f469-disco/micropython/py/mkenv.mk
CROSS_COMPILE = arm-none-eabi-
BUILD = ../build/vm
ifeq ($(HOST),1)
CROSS_COMPILE =
BUILD = ../build/host
FROZEN_MANIFEST = host_manifest.py
endif
DISCO = ../vendor/specter/f469-disco
GRAPHICS = $(DISCO)/usermods/udisplay_f469
USER_C_MODULES = ../build/usermods
QSTR_DEFS = qstrdefsport.h
FROZEN_MANIFEST ?= manifest.py
MICROPY_VFS_FAT = 1
include $(TOP)/py/py.mk

INC += -I. -I$(TOP) -I$(BUILD) -I$(GRAPHICS) -I$(GRAPHICS)/lvgl
INC += -I$(TOP)/lib/mbedtls/include
CFLAGS += $(INC) $(CFLAGS_MOD) -Os -std=gnu99 -Wall -Wno-error -Wno-unused-function
ifeq ($(HOST),1)
CFLAGS += -DFORGEBOX_HOST=1
else
CFLAGS += -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16
endif
ifeq ($(PRODUCTION),1)
CFLAGS += -DFORGEBOX_PRODUCTION=1
endif
CFLAGS += -fdata-sections -ffunction-sections -DMODULE_DISPLAY_ENABLED=1
CFLAGS += -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"'

SRC_C = modforgebox.c moddisplay.c modos.c modtime.c mphalport.c \
    lib/utils/pyexec.c lib/utils/stdout_helpers.c lib/mp-readline/readline.c \
    lib/mbedtls/library/aes.c lib/mbedtls/library/platform_util.c \
    lib/timeutils/timeutils.c extmod/uzlib/crc32.c
SRC_S = lib/utils/gchelper_m3.s
ifeq ($(HOST),1)
SRC_S =
SRC_C += host_main.c camera_preview.c pin_flash.c
endif
SRC_QSTR += $(SRC_C) $(SRC_MOD)
OBJ = $(PY_O) $(addprefix $(BUILD)/,$(SRC_C:.c=.o)) $(addprefix $(BUILD)/,$(SRC_S:.s=.o))
OBJ += $(addprefix $(BUILD)/,$(SRC_MOD:.c=.o))
all: $(BUILD)/libmicropython.a
ifeq ($(HOST),1)
all: $(BUILD)/specter-host
$(BUILD)/specter-host: $(OBJ) ../build/host_graphics.a
	$(Q)$(CC) -Wl,--gc-sections -o $@ $^ -lm
endif
$(BUILD)/libmicropython.a: $(OBJ)
	$(Q)$(AR) rcs $@ $^
include $(TOP)/py/mkrules.mk
