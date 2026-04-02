DISPLAY_MOD_DIR := $(USERMOD_DIR)

# STM32U5G9J-DK2 display driver
ifeq ($(CMSIS_MCU),STM32U5G9xx)

# The module itself
SRC_USERMOD += $(DISPLAY_MOD_DIR)/display.c

# LVGL STM32 HAL (LTDC-based display + I2C touch)
SRC_USERMOD += $(DISPLAY_MOD_DIR)/lv_stm_hal/lv_stm_hal.c

# lvgl support (shared with F469 build)
LVGL_DIR := $(DISPLAY_MOD_DIR)/../udisplay_f469
include $(LVGL_DIR)/lvgl/lvgl.mk
SRC_USERMOD += $(CSRCS)
CFLAGS_USERMOD += $(CFLAGS)

# Fonts (shared with F469 build)
SRC_USERMOD += $(LVGL_DIR)/fonts/square.c
SRC_USERMOD += $(LVGL_DIR)/fonts/font_roboto_mono_28.c
SRC_USERMOD += $(LVGL_DIR)/fonts/font_roboto_mono_22.c
SRC_USERMOD += $(LVGL_DIR)/fonts/font_roboto_mono_16.c
SRC_USERMOD += $(LVGL_DIR)/fonts/font_roboto_mono_12.c

# Pixel art class (shared with F469 build)
SRC_USERMOD += $(LVGL_DIR)/pixelart/px_img.c

# Dirs with header files
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)
CFLAGS_USERMOD += -I$(DISPLAY_MOD_DIR)/lv_stm_hal
CFLAGS_USERMOD += -I$(LVGL_DIR)/lvgl
CFLAGS_USERMOD += -I$(LVGL_DIR)
CFLAGS_USERMOD += -I$(LVGL_DIR)/pixelart

endif

# Unix port (simulator) - reuse the F469 unix driver
ifneq ($(UNAME_S),)
include $(DISPLAY_MOD_DIR)/../udisplay_f469/micropython.mk
endif
