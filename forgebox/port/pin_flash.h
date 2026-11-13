#pragma once
#include <stdbool.h>
#include <stdint.h>

// Legacy encrypted record (145 bytes), followed by the optional PIN word key.
#define PIN_RECORD_SIZE 177
#define PIN_FAILURE_BYTES 8
#define PIN_SLOT_DATA_SIZE (PIN_RECORD_SIZE + 1 + PIN_FAILURE_BYTES)
#define SEED_RECORD_SIZE 113
#define SEED_SLOT_DATA_SIZE (SEED_RECORD_SIZE + 1)
#define SEED_CATALOG_SIZE 993

bool pin_flash_read(unsigned slot, uint8_t *out);
bool pin_flash_write(unsigned slot, const uint8_t *record);
bool pin_flash_retire(unsigned slot);
bool wallet_flash_resume(void);
bool wallet_flash_wipe(void);
bool wallet_flash_is_blank(void);
bool pin_flash_record_failure(unsigned slot, unsigned index);
bool seed_flash_read(unsigned slot, uint8_t *out);
bool seed_flash_write(unsigned slot, const uint8_t *record);
// Read either seed format. Zero length means blank or interrupted write.
bool seed_backup_read(unsigned slot, uint8_t *out, unsigned *size);
bool seed_catalog_write(unsigned slot, const uint8_t *record);
unsigned wallet_spi_capacity(void);
uint32_t wallet_spi_jedec_id(void);
#define SPI_SCAN_BATCH 32
// Bit i is set only when every byte in sector (first + i) is erased.
bool wallet_spi_erased_sectors(unsigned first, unsigned count, uint32_t *mask);
#define DESCRIPTOR_LEGACY_BANK_SIZE 16384
#define DESCRIPTOR_BANK_SIZE 32768
#define DESCRIPTOR_BANKS 4
// -1: I/O error or foreign data, 0: blank/uncommitted, positive: record bytes.
int descriptor_flash_read(unsigned bank, uint8_t *out);
bool descriptor_flash_write(unsigned bank, const uint8_t *record, unsigned size);
