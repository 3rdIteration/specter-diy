#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void board_delay(unsigned ms);
unsigned board_ticks(void);
void board_reset(void);
bool board_power_off(void);
bool board_random(uint8_t *data, size_t len);
bool board_battery_read(uint32_t *millivolts, unsigned *percent, bool *charging);
bool board_sd_init(void);
bool board_sd_present(void);
bool board_sd_io(bool write, uint32_t block, uint8_t *data, size_t len);
uint32_t board_sd_blocks(void);
void board_display_init(void);
void board_display_update(unsigned dt);
void board_backlight(bool on);
uintptr_t board_scratch(void);
void board_startup_failed(void);
bool board_usb_serial_available(void);
bool board_boot_flash_available(void);
bool board_boot_flash_read(unsigned offset, uint32_t *out, unsigned size);
void board_usb_serial_enable(bool enable);
bool board_usb_serial_connected(void);
unsigned board_usb_serial_read(uint8_t *out);
bool board_usb_serial_write(const uint8_t *data, unsigned size);
#if FORGEBOX_HOST
void board_test_sd_insert(bool present);
unsigned board_test_sd_writes(void);
#endif
