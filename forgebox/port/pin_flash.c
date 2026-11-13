#include <stddef.h>
#include <string.h>
#include "pin_flash.h"

#define SLOT_SIZE 4096
#define COMMIT_OFFSET 256
#define FAILURES_OFFSET 512
#define JOURNAL (4 * SLOT_SIZE)
#define DESCRIPTOR_BASE (5 * SLOT_SIZE)
#define DESCRIPTOR_EXPANDED (DESCRIPTOR_BASE + 2 * DESCRIPTOR_LEGACY_BANK_SIZE)
#define FLASH_STORAGE_BASE 0x00EAE000u
#define FLASH_WIPE_JOURNAL 0x00EAC000u
#define FLASH_DESCRIPTOR_BASE 0x00EA0000u
#define FLASH_DESCRIPTOR_EXPANDED 0x00E60000u
_Static_assert(PIN_RECORD_SIZE <= COMMIT_OFFSET, "PIN record overlaps commit marker");
static const uint8_t pin_magic[] = {'S', 'F', 'P', '1'};
static const uint8_t seed_magic[] = {'S', 'F', 'S', '1'};
#ifdef FORGEBOX_PRODUCTION
static const uint8_t catalog_magic[] = {'F', 'S', '0', '1'};
static const uint8_t descriptor_magic[] = {'F', 'W', '0', '1'};
static const uint8_t journal_marker[] = "FW01 ForgeBox Specter storage";
#else
static const uint8_t catalog_magic[] = {'S', 'F', 'S', '2'};
static const uint8_t descriptor_magic[] = {'S', 'F', 'W', '2'};
static const uint8_t journal_marker[] = "SFW1 ForgeBox Specter storage";
#endif
static const uint8_t descriptor_legacy_magic[] = {'S', 'F', 'W', 'D'};
enum { CATALOG_COMMIT = 1024 };
_Static_assert(SEED_CATALOG_SIZE < CATALOG_COMMIT, "catalog overlaps commit marker");
// Equal numbers of programmed bits make these codes distinguishable even
// if a NOR program or erase is interrupted halfway through.
enum { ACTION_WIPE = 0xfc, ACTION_RETIRE_0 = 0xf3, ACTION_RETIRE_1 = 0xcf,
       ACTION_SEED_0 = 0x3f, ACTION_SEED_1 = 0xfa,
       ACTION_DESCRIPTOR_0 = 0xf6, ACTION_DESCRIPTOR_1 = 0xf5,
       ACTION_DESCRIPTOR_2 = 0xee, ACTION_DESCRIPTOR_3 = 0xed };
static const uint8_t descriptor_actions[] = {
    ACTION_DESCRIPTOR_0, ACTION_DESCRIPTOR_1, ACTION_DESCRIPTOR_2, ACTION_DESCRIPTOR_3,
};

#if FORGEBOX_HOST
static uint8_t flash[DESCRIPTOR_EXPANDED + 2 * DESCRIPTOR_BANK_SIZE];
static bool ready;
static bool flash_init(void) {
    if (!ready) memset(flash, 0xff, sizeof(flash));
    return ready = true;
}
uint32_t wallet_spi_jedec_id(void) { return 0xc84018; }
static bool flash_read(uint32_t offset, uint8_t *out, size_t size) {
    memcpy(out, flash + offset, size);
    return true;
}
static bool flash_program(uint32_t offset, const uint8_t *in, size_t size) {
    for (size_t i = 0; i < size; i++) flash[offset + i] &= in[i];
    return true;
}
static bool flash_erase(uint32_t offset) {
    memset(flash + offset, 0xff, SLOT_SIZE);
    return true;
}
static bool flash_read_physical(uint32_t address, uint8_t *out, size_t size) {
    if (address >= FLASH_STORAGE_BASE && address < FLASH_STORAGE_BASE + 4 * SLOT_SIZE)
        return flash_read(address - FLASH_STORAGE_BASE, out, size);
    if (address >= FLASH_WIPE_JOURNAL && address < FLASH_WIPE_JOURNAL + SLOT_SIZE)
        return flash_read(JOURNAL + address - FLASH_WIPE_JOURNAL, out, size);
    if (address >= FLASH_DESCRIPTOR_BASE &&
        address < FLASH_DESCRIPTOR_BASE + 2 * DESCRIPTOR_LEGACY_BANK_SIZE)
        return flash_read(DESCRIPTOR_BASE + address - FLASH_DESCRIPTOR_BASE, out, size);
    if (address >= FLASH_DESCRIPTOR_EXPANDED &&
        address < FLASH_DESCRIPTOR_EXPANDED + 2 * DESCRIPTOR_BANK_SIZE)
        return flash_read(DESCRIPTOR_EXPANDED + address - FLASH_DESCRIPTOR_EXPANDED, out, size);
    memset(out, 0xff, size);
    return true;
}
#else
#include "drv_gd25qxx.h"
// Four sectors in the gap before NFT storage in Keystone's published SPI map.
// The ForgeBox factory map is unpublished. Refuse sectors with foreign data.
_Static_assert(FLASH_DESCRIPTOR_BASE >= 0x00EA0000u &&
               FLASH_DESCRIPTOR_BASE + 2 * DESCRIPTOR_LEGACY_BANK_SIZE <= FLASH_WIPE_JOURNAL,
               "descriptor banks must stay between Trezor banks and the wipe journal");
_Static_assert(FLASH_DESCRIPTOR_EXPANDED + 2 * DESCRIPTOR_BANK_SIZE <= 0x00E7F000u,
               "expanded descriptor banks overlap old Trezor storage");
_Static_assert(FLASH_STORAGE_BASE + 4 * SLOT_SIZE <= 0x00EB2000u, "storage overlaps NFT storage");
_Static_assert(FLASH_WIPE_JOURNAL + SLOT_SIZE <= 0x00EAD000u, "journal overlaps Trezor storage");
_Static_assert(SLOT_SIZE == GD25QXX_SECTOR_SIZE, "unexpected erase geometry");
uint32_t wallet_spi_jedec_id(void) {
    static bool attempted;
    static uint32_t id;
    if (!attempted) {
        attempted = true;
        Gd25FlashInit();
        id = Gd25FlashReadID();
    }
    return id;
}
static bool flash_init(void) {
    uint32_t id = wallet_spi_jedec_id();
    return id == 0xC84018u || id == 0x852018u;
}
static uint32_t flash_address(uint32_t offset) {
    if (offset >= DESCRIPTOR_EXPANDED) return FLASH_DESCRIPTOR_EXPANDED + offset - DESCRIPTOR_EXPANDED;
    if (offset >= DESCRIPTOR_BASE) return FLASH_DESCRIPTOR_BASE + offset - DESCRIPTOR_BASE;
    return offset >= JOURNAL ? FLASH_WIPE_JOURNAL + offset - JOURNAL : FLASH_STORAGE_BASE + offset;
}
static bool flash_read_physical(uint32_t address, uint8_t *out, size_t size) {
    return Gd25FlashReadBuffer(address, out, size) == (int32_t)size;
}
static bool flash_read(uint32_t offset, uint8_t *out, size_t size) {
    return flash_read_physical(flash_address(offset), out, size);
}
static bool flash_program(uint32_t offset, const uint8_t *in, size_t size) {
    return Gd25FlashWriteBuffer(flash_address(offset), in, size) == (int32_t)size;
}
static bool flash_erase(uint32_t offset) {
    return Gd25FlashSectorErase(flash_address(offset)) == 0;
}
#endif

unsigned wallet_spi_capacity(void) {
    return flash_init() ? 16u * 1024 * 1024 : 0;
}

static bool erased(const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (data[i] != 0xff) return false;
    return true;
}

bool wallet_spi_erased_sectors(unsigned first, unsigned count, uint32_t *mask) {
    unsigned sectors = wallet_spi_capacity() / SLOT_SIZE;
    if (!count || count > SPI_SCAN_BATCH || first >= sectors || count > sectors - first)
        return false;
    uint8_t page[COMMIT_OFFSET];
    uint32_t result = 0;
    for (unsigned i = 0; i < count; i++) {
        uint32_t base = (first + i) * SLOT_SIZE;
        unsigned offset;
        for (offset = 0; offset < SLOT_SIZE; offset += sizeof(page)) {
            if (!flash_read_physical(base + offset, page, sizeof(page))) return false;
            if (!erased(page, sizeof(page))) break;
        }
        if (offset == SLOT_SIZE) result |= UINT32_C(1) << i;
    }
    *mask = result;
    return true;
}

static bool range_erased(uint32_t offset, size_t size) {
    uint8_t block[64];
    while (size) {
        size_t count = size < sizeof(block) ? size : sizeof(block);
        if (!flash_read(offset, block, count) || !erased(block, count)) return false;
        offset += count;
        size -= count;
    }
    return true;
}

static bool read_record(uint32_t base, uint8_t *out, size_t size,
                        size_t failures, const uint8_t *magic) {
    if (!flash_init() ||
        !flash_read(base, out, size) ||
        !flash_read(base + COMMIT_OFFSET, out + size, 1) ||
        (failures && !flash_read(base + FAILURES_OFFSET, out + size + 1, failures)))
        return false;
    if (erased(out, size + 1 + failures)) return range_erased(base, SLOT_SIZE);
    if (memcmp(out, magic, 4)) return false;
    return range_erased(base + size, COMMIT_OFFSET - size) &&
           range_erased(base + COMMIT_OFFSET + 1, FAILURES_OFFSET - COMMIT_OFFSET - 1) &&
           range_erased(base + FAILURES_OFFSET + failures, SLOT_SIZE - FAILURES_OFFSET - failures);
}

static bool program_checked(uint32_t offset, const uint8_t *data, size_t size) {
    uint8_t check[PIN_RECORD_SIZE];
    return flash_program(offset, data, size) && flash_read(offset, check, size) &&
           !memcmp(data, check, size);
}

static bool write_record(uint32_t base, const uint8_t *record, size_t size,
                         size_t failures, const uint8_t *magic) {
    uint8_t previous[PIN_SLOT_DATA_SIZE];
    const uint8_t committed = 0;
    if (memcmp(record, magic, 4) || !read_record(base, previous, size, failures, magic))
        return false;
    if (!erased(previous, size + 1 + failures) &&
        (!flash_erase(base) || !range_erased(base, SLOT_SIZE))) return false;
    return program_checked(base, magic, 4) &&
           program_checked(base + 4, record + 4, size - 4) &&
           program_checked(base + COMMIT_OFFSET, &committed, 1);
}

bool pin_flash_read(unsigned slot, uint8_t *out) {
    return slot < 2 && read_record((slot + 2) * SLOT_SIZE, out, PIN_RECORD_SIZE,
                                  PIN_FAILURE_BYTES, pin_magic);
}

bool pin_flash_write(unsigned slot, const uint8_t *record) {
    return slot < 2 && write_record((slot + 2) * SLOT_SIZE, record, PIN_RECORD_SIZE,
                                   PIN_FAILURE_BYTES, pin_magic);
}

static bool erase_checked(uint32_t base) {
    return range_erased(base, SLOT_SIZE) ||
           (flash_erase(base) && range_erased(base, SLOT_SIZE));
}

bool seed_flash_read(unsigned slot, uint8_t *out) {
    return slot < 2 && read_record(slot * SLOT_SIZE, out, SEED_RECORD_SIZE, 0, seed_magic);
}

bool seed_flash_write(unsigned slot, const uint8_t *record) {
    return slot < 2 && write_record(slot * SLOT_SIZE, record, SEED_RECORD_SIZE, 0, seed_magic);
}

bool seed_backup_read(unsigned slot, uint8_t *out, unsigned *size) {
    uint32_t base = slot * SLOT_SIZE;
    uint8_t magic[4], commit;
    *size = 0;
    if (slot >= 2 || !flash_init() || !flash_read(base, magic, sizeof(magic))) return false;
#ifndef FORGEBOX_PRODUCTION
    if (!memcmp(magic, seed_magic, sizeof(magic))) {
        uint8_t legacy[SEED_SLOT_DATA_SIZE];
        if (!seed_flash_read(slot, legacy)) return false;
        if (legacy[SEED_RECORD_SIZE] == 0) {
            *size = SEED_RECORD_SIZE;
            if (out) memcpy(out, legacy, *size);
        }
        return legacy[SEED_RECORD_SIZE] == 0 || legacy[SEED_RECORD_SIZE] == 0xff;
    }
#endif
    if (memcmp(magic, catalog_magic, sizeof(magic))) {
        // A cut during the initial magic write can only leave bits unprogrammed.
        for (unsigned i = 0; i < sizeof(magic); i++)
            if ((magic[i] & catalog_magic[i]) != catalog_magic[i]) return false;
        return range_erased(base + sizeof(magic), SLOT_SIZE - sizeof(magic));
    }
    if (!flash_read(base + CATALOG_COMMIT, &commit, 1) ||
        !range_erased(base + SEED_CATALOG_SIZE, CATALOG_COMMIT - SEED_CATALOG_SIZE) ||
        !range_erased(base + CATALOG_COMMIT + 1, SLOT_SIZE - CATALOG_COMMIT - 1)) return false;
    if (commit != 0) return true;
    *size = SEED_CATALOG_SIZE;
    return !out || flash_read(base, out, *size);
}

static bool start_action(uint8_t action);

static uint32_t descriptor_offset(unsigned bank) {
    return bank < 2 ? DESCRIPTOR_BASE + bank * DESCRIPTOR_LEGACY_BANK_SIZE :
                      DESCRIPTOR_EXPANDED + (bank - 2) * DESCRIPTOR_BANK_SIZE;
}

static unsigned descriptor_bank_size(unsigned bank) {
    return bank < 2 ? DESCRIPTOR_LEGACY_BANK_SIZE : DESCRIPTOR_BANK_SIZE;
}

static bool erase_descriptor(unsigned bank) {
    uint32_t base = descriptor_offset(bank);
    for (unsigned i = descriptor_bank_size(bank); i != 0; i -= SLOT_SIZE)
        if (!erase_checked(base + i - SLOT_SIZE)) return false;
    return true;
}

int descriptor_flash_read(unsigned bank, uint8_t *out) {
    uint32_t base = descriptor_offset(bank);
    unsigned limit = descriptor_bank_size(bank) - 1;
    const uint8_t *magic = bank < 2 ? descriptor_legacy_magic : descriptor_magic;
    uint8_t header[10], commit;
    if (bank >= DESCRIPTOR_BANKS || !flash_init() || !flash_read(base, header, sizeof(header)) ||
        !flash_read(base + limit, &commit, 1)) return -1;
    if (memcmp(header, magic, sizeof(descriptor_magic))) {
        for (unsigned i = 0; i < sizeof(descriptor_magic); i++)
            if ((header[i] & magic[i]) != magic[i]) return -1;
        return range_erased(base + 4, limit - 3) ? 0 : -1;
    }
    if (commit != 0) return 0;
    unsigned size = header[8] | header[9] << 8;
    if (size < sizeof(header) || size > limit ||
        !range_erased(base + size, limit - size)) return -1;
    if (out && !flash_read(base, out, size)) return -1;
    return size;
}

bool descriptor_flash_write(unsigned bank, const uint8_t *record, unsigned size) {
    uint32_t base = descriptor_offset(bank);
    const uint8_t committed = 0;
    if (bank < 2 || bank >= DESCRIPTOR_BANKS || size < 10 || size >= DESCRIPTOR_BANK_SIZE ||
        memcmp(record, descriptor_magic, sizeof(descriptor_magic)) ||
        size != ((unsigned)record[8] | (unsigned)record[9] << 8) ||
        !wallet_flash_resume() || descriptor_flash_read(bank, NULL) < 0) return false;
    if (!range_erased(base, DESCRIPTOR_BANK_SIZE) &&
        (!start_action(descriptor_actions[bank]) ||
         !erase_descriptor(bank) || !erase_checked(JOURNAL))) return false;
    if (!program_checked(base, descriptor_magic, sizeof(descriptor_magic))) return false;
    for (unsigned offset = sizeof(descriptor_magic); offset < size;) {
        unsigned count = size - offset;
        unsigned page_remaining = COMMIT_OFFSET - offset % COMMIT_OFFSET;
        if (count > page_remaining) count = page_remaining;
        if (count > PIN_RECORD_SIZE) count = PIN_RECORD_SIZE;
        if (!program_checked(base + offset, record + offset, count)) return false;
        offset += count;
    }
    return program_checked(base + DESCRIPTOR_BANK_SIZE - 1, &committed, 1);
}

bool seed_catalog_write(unsigned slot, const uint8_t *record) {
    unsigned size;
    uint32_t base = slot * SLOT_SIZE;
    const uint8_t committed = 0;
    // Check ownership before erasing. Avoid reading the large catalog here.
    if (memcmp(record, catalog_magic, sizeof(catalog_magic)) ||
        !wallet_flash_resume() || !seed_backup_read(slot, NULL, &size)) return false;
    if (!range_erased(base, SLOT_SIZE) &&
        (!start_action(slot == 0 ? ACTION_SEED_0 : ACTION_SEED_1) ||
         !erase_checked(base) || !erase_checked(JOURNAL))) return false;
    if (!program_checked(base, catalog_magic, sizeof(catalog_magic))) return false;
    for (unsigned offset = sizeof(catalog_magic); offset < SEED_CATALOG_SIZE;) {
        unsigned count = SEED_CATALOG_SIZE - offset;
        unsigned page_remaining = COMMIT_OFFSET - offset % COMMIT_OFFSET;
        if (count > page_remaining) count = page_remaining;
        if (count > PIN_RECORD_SIZE) count = PIN_RECORD_SIZE;
        if (!program_checked(base + offset, record + offset, count)) return false;
        offset += count;
    }
    return program_checked(base + CATALOG_COMMIT, &committed, 1);
}

bool wallet_flash_is_blank(void) {
    if (!flash_init() || !range_erased(0, JOURNAL)) return false;
    for (unsigned bank = 0; bank < DESCRIPTOR_BANKS; bank++)
        if (!range_erased(descriptor_offset(bank), descriptor_bank_size(bank))) return false;
    return true;
}

static bool wallet_sectors_owned(void) {
    uint8_t data[PIN_SLOT_DATA_SIZE];
    for (unsigned slot = 0; slot < 2; slot++) {
        unsigned size;
        if (!pin_flash_read(slot, data) || !seed_backup_read(slot, NULL, &size)) return false;
    }
    return true;
}

static bool finish_wipe(void) {
    // Erase both copies of the backup key before erasing seed ciphertext.
    if (!erase_checked(2 * SLOT_SIZE) || !erase_checked(3 * SLOT_SIZE) ||
        !erase_checked(0) || !erase_checked(SLOT_SIZE)) return false;
    // Do not erase space belonging to other firmware. Erase each known bank
    // backwards so its ownership marker survives an interruption until last.
#ifdef FORGEBOX_PRODUCTION
    unsigned first_bank = 2;
#else
    unsigned first_bank = 0;
#endif
    for (unsigned bank = first_bank; bank < DESCRIPTOR_BANKS; bank++) {
        uint32_t base = descriptor_offset(bank);
        uint8_t magic[4];
        if (!flash_read(base, magic, sizeof(magic))) return false;
        if (memcmp(magic, bank < 2 ? descriptor_legacy_magic : descriptor_magic, sizeof(magic))) continue;
        if (!erase_descriptor(bank)) return false;
    }
    return erase_checked(JOURNAL);
}

static bool start_action(uint8_t action) {
    return program_checked(JOURNAL, journal_marker, sizeof(journal_marker)) &&
           program_checked(JOURNAL + sizeof(journal_marker), &action, 1);
}

static bool committed_pin(unsigned slot) {
    uint8_t data[PIN_SLOT_DATA_SIZE];
    return pin_flash_read(slot, data) && data[PIN_RECORD_SIZE] == 0;
}

static bool finish_retire(unsigned slot) {
    return committed_pin(1 - slot) && erase_checked((slot + 2) * SLOT_SIZE) &&
           erase_checked(JOURNAL);
}

bool wallet_flash_resume(void) {
    uint8_t marker[sizeof(journal_marker) + 1];
    if (!flash_init() || !flash_read(JOURNAL, marker, sizeof(marker))) return false;
    if (!range_erased(JOURNAL + sizeof(marker), SLOT_SIZE - sizeof(marker))) return false;
    if (erased(marker, sizeof(marker))) return true;
    uint8_t action = marker[sizeof(journal_marker)];
    if (memcmp(marker, journal_marker, sizeof(journal_marker)) == 0) {
        switch (action) {
        case ACTION_WIPE: return finish_wipe();
        case ACTION_RETIRE_0: return finish_retire(0);
        case ACTION_RETIRE_1: return finish_retire(1);
        case ACTION_SEED_0: return erase_checked(0) && erase_checked(JOURNAL);
        case ACTION_SEED_1: return erase_checked(SLOT_SIZE) && erase_checked(JOURNAL);
        case ACTION_DESCRIPTOR_0: return erase_descriptor(0) && erase_checked(JOURNAL);
        case ACTION_DESCRIPTOR_1: return erase_descriptor(1) && erase_checked(JOURNAL);
        case ACTION_DESCRIPTOR_2: return erase_descriptor(2) && erase_checked(JOURNAL);
        case ACTION_DESCRIPTOR_3: return erase_descriptor(3) && erase_checked(JOURNAL);
        }
    }
    // With no committed action the wallet sectors must stay untouched.
    // Only clean a recognizable torn journal. Unrecognized data fails closed.
    for (size_t i = 0; i < sizeof(journal_marker); i++) {
        if ((marker[i] & journal_marker[i]) != journal_marker[i]) return false;
    }
    if ((action & ACTION_WIPE) != ACTION_WIPE &&
        (action & ACTION_RETIRE_0) != ACTION_RETIRE_0 &&
        (action & ACTION_RETIRE_1) != ACTION_RETIRE_1 &&
        (action & ACTION_SEED_0) != ACTION_SEED_0 &&
        (action & ACTION_SEED_1) != ACTION_SEED_1 &&
        (action & ACTION_DESCRIPTOR_0) != ACTION_DESCRIPTOR_0 &&
        (action & ACTION_DESCRIPTOR_1) != ACTION_DESCRIPTOR_1 &&
        (action & ACTION_DESCRIPTOR_2) != ACTION_DESCRIPTOR_2 &&
        (action & ACTION_DESCRIPTOR_3) != ACTION_DESCRIPTOR_3) return false;
    if (memcmp(marker, journal_marker, 4) == 0 || range_erased(0, JOURNAL))
        return erase_checked(JOURNAL);
    for (unsigned slot = 0; slot < DESCRIPTOR_BANKS; slot++) {
        uint8_t desc_action = descriptor_actions[slot];
        if ((action & desc_action) == desc_action &&
            range_erased(descriptor_offset(slot), descriptor_bank_size(slot))) return erase_checked(JOURNAL);
    }
    for (unsigned slot = 0; slot < 2; slot++) {
        uint8_t seed_action = slot == 0 ? ACTION_SEED_0 : ACTION_SEED_1;
        if ((action & seed_action) == seed_action && range_erased(slot * SLOT_SIZE, SLOT_SIZE))
            return erase_checked(JOURNAL);
        uint8_t code = slot == 0 ? ACTION_RETIRE_0 : ACTION_RETIRE_1;
        if ((action & code) == code && range_erased((slot + 2) * SLOT_SIZE, SLOT_SIZE) &&
            committed_pin(1 - slot)) return erase_checked(JOURNAL);
    }
    return false;
}

bool wallet_flash_wipe(void) {
    if (!wallet_flash_resume() || !wallet_sectors_owned()) return false;
    return start_action(ACTION_WIPE) && finish_wipe();
}

bool pin_flash_retire(unsigned slot) {
    uint8_t data[PIN_SLOT_DATA_SIZE];
    if (slot >= 2 || !wallet_flash_resume() || !pin_flash_read(slot, data)) return false;
    if (erased(data, sizeof(data))) return true;
    return committed_pin(1 - slot) &&
           start_action(slot == 0 ? ACTION_RETIRE_0 : ACTION_RETIRE_1) && finish_retire(slot);
}

bool pin_flash_record_failure(unsigned slot, unsigned index) {
    uint8_t current[PIN_SLOT_DATA_SIZE];
    const uint8_t failed = 0;
    if (index >= PIN_FAILURE_BYTES || !pin_flash_read(slot, current) ||
        current[PIN_RECORD_SIZE] != 0 || current[PIN_RECORD_SIZE + 1 + index] != 0xff)
        return false;
    return program_checked((slot + 2) * SLOT_SIZE + FAILURES_OFFSET + index, &failed, 1);
}
