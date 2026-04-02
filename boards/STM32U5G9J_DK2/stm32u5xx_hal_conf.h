/* This file is part of the MicroPython project, http://micropython.org/
 * The MIT License (MIT)
 * Copyright (c) 2024 Specter-DIY contributors
 */
#ifndef MICROPY_INCLUDED_STM32U5XX_HAL_CONF_H
#define MICROPY_INCLUDED_STM32U5XX_HAL_CONF_H

/* DK2 board has a 16 MHz HSE crystal */
#define HSE_VALUE (16000000)
#define LSE_VALUE (32768)
#define EXTERNAL_CLOCK_VALUE (12288000)

/* Oscillator timeouts in ms */
#define HSE_STARTUP_TIMEOUT (100)
#define LSE_STARTUP_TIMEOUT (5000)

/* Enable HAL modules needed for DK2 display and peripherals */
#define HAL_DMA2D_MODULE_ENABLED
#define HAL_LTDC_MODULE_ENABLED
#define HAL_OSPI_MODULE_ENABLED
#define HAL_SMARTCARD_MODULE_ENABLED

#include "boards/stm32u5xx_hal_conf_base.h"

#endif /* MICROPY_INCLUDED_STM32U5XX_HAL_CONF_H */
