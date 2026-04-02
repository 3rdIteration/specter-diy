/*
 * Block device configuration for STM32U5G9J-DK2
 *
 * The DK2 uses an Octo-SPI NOR flash (MX25LM51245G) for external storage.
 * This provides the /qspi mount point used by Specter for settings storage.
 *
 * NOTE: Octo-SPI support requires the STM32U5 HAL OSPI driver.
 * For initial bring-up, we use internal flash only.
 * Octo-SPI integration will be added once basic MicroPython boots.
 */

#include "storage.h"

/* Placeholder: Octo-SPI flash block device will be configured here.
 * For now, only internal flash storage is used (MICROPY_HW_ENABLE_INTERNAL_FLASH_STORAGE=1).
 * The secondary filesystem (/qspi) requires porting the OSPI driver. */
