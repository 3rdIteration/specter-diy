#include <assert.h>
#include <stdio.h>

#define FORGEBOX_HOST 1
#include "../port/pin_flash.c"

static void populate(void) {
    uint8_t pin[PIN_RECORD_SIZE] = {0};
    uint8_t seed[SEED_RECORD_SIZE] = {0};
    memcpy(pin, pin_magic, sizeof(pin_magic));
    memcpy(seed, seed_magic, sizeof(seed_magic));
    for (unsigned slot = 0; slot < 2; slot++) {
        assert(pin_flash_write(slot, pin));
        assert(seed_flash_write(slot, seed));
    }
}

int main(void) {
    uint32_t mask;
    unsigned journal_sector = FLASH_WIPE_JOURNAL / SLOT_SIZE;
    assert(wallet_spi_erased_sectors(journal_sector, 3, &mask) && mask == 7);
    flash[JOURNAL + SLOT_SIZE - 1] = 0;
    assert(wallet_spi_erased_sectors(journal_sector, 3, &mask) && mask == 6);
    assert(flash[JOURNAL + SLOT_SIZE - 1] == 0);
    flash[JOURNAL + SLOT_SIZE - 1] = 0xff;
    assert(wallet_spi_erased_sectors(4064, 32, &mask) && mask == UINT32_MAX);
    assert(!wallet_spi_erased_sectors(4095, 2, &mask));

    uint8_t wallet[32767] = {0}, wallet_readback[32767];
    memcpy(wallet, descriptor_magic, sizeof(descriptor_magic));
    wallet[8] = sizeof(wallet) & 0xff;
    wallet[9] = sizeof(wallet) >> 8;
    memset(wallet + 10, 0x42, sizeof(wallet) - 10);
    uint8_t legacy[] = {'S', 'F', 'W', 'D', 1, 0, 0, 0, 11, 0, 0x42};
    memcpy(flash + descriptor_offset(0), legacy, sizeof(legacy));
    flash[descriptor_offset(0) + DESCRIPTOR_LEGACY_BANK_SIZE - 1] = 0;
    assert(descriptor_flash_read(0, wallet_readback) == sizeof(legacy));
    assert(!memcmp(legacy, wallet_readback, sizeof(legacy)));
    assert(descriptor_flash_write(2, wallet, sizeof(wallet)));
    assert(descriptor_flash_read(2, wallet_readback) == sizeof(wallet));
    assert(!memcmp(wallet, wallet_readback, sizeof(wallet)));
    flash[descriptor_offset(3) + 4096] = 0x42;
    assert(!descriptor_flash_write(3, wallet, sizeof(wallet)));
    assert(wallet_flash_wipe());
    assert(descriptor_flash_read(0, NULL) == 0 && descriptor_flash_read(2, NULL) == 0);
    assert(flash[descriptor_offset(3) + 4096] == 0x42);
    flash[descriptor_offset(3) + 4096] = 0xff;
    assert(descriptor_flash_write(2, wallet, sizeof(wallet)));
    assert(descriptor_flash_write(3, wallet, sizeof(wallet)));
    assert(start_action(ACTION_DESCRIPTOR_3));
    memset(flash + descriptor_offset(3) + DESCRIPTOR_BANK_SIZE - SLOT_SIZE, 0xff, 2);
    assert(wallet_flash_resume() && descriptor_flash_read(3, NULL) == 0);
    assert(descriptor_flash_read(2, wallet_readback) == sizeof(wallet));
    assert(wallet_flash_wipe() && erased(flash, sizeof(flash)));

    uint8_t catalog[SEED_CATALOG_SIZE] = {0}, readback[SEED_CATALOG_SIZE];
    unsigned length;
    memcpy(catalog, catalog_magic, sizeof(catalog_magic));
    assert(seed_catalog_write(0, catalog));
    assert(seed_backup_read(0, readback, &length) && length == sizeof(catalog));
    assert(!memcmp(catalog, readback, length));
    assert(start_action(ACTION_SEED_1));
    assert(wallet_flash_resume() && seed_backup_read(0, readback, &length) && length);
    memcpy(flash + SLOT_SIZE, catalog_magic, 2);
    assert(seed_backup_read(1, readback, &length) && !length);
    assert(seed_catalog_write(1, catalog));
    assert(wallet_flash_wipe() && erased(flash, sizeof(flash)));

    populate();
    flash[SLOT_SIZE + 1024] = 0x00;
    assert(!wallet_flash_wipe());
    assert(!erased(flash, SLOT_SIZE));
    assert(range_erased(JOURNAL, SLOT_SIZE));
    flash[SLOT_SIZE + 1024] = 0xff;

    // Interrupted journal program, before erasing the wallet sectors.
    memcpy(flash + JOURNAL, journal_marker, sizeof(journal_marker) / 2);
    assert(wallet_flash_resume());
    assert(!range_erased(0, JOURNAL) && range_erased(JOURNAL, SLOT_SIZE));

    assert(start_action(ACTION_WIPE));
    // Interrupted PIN-sector erase, leaving a damaged record header.
    memset(flash + 2 * SLOT_SIZE, 0xff, 2);
    assert(wallet_flash_resume());
    assert(erased(flash, sizeof(flash)));

    // Interrupted final journal erase, after the wallet sectors are blank.
    memcpy(flash + JOURNAL + 2, journal_marker + 2, sizeof(journal_marker) - 2);
    assert(wallet_flash_resume());
    assert(erased(flash, sizeof(flash)));

    populate();
    assert(start_action(ACTION_RETIRE_0));
    memset(flash + 2 * SLOT_SIZE, 0xff, 2);
    assert(wallet_flash_resume());
    assert(range_erased(2 * SLOT_SIZE, SLOT_SIZE) && committed_pin(1));
    assert(!range_erased(0, 2 * SLOT_SIZE));
    assert(wallet_flash_wipe());
    assert(erased(flash, sizeof(flash)));
    puts("PASS: wallet wipe, interrupted erase recovery, and foreign sector refusal");
}
