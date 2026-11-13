#include <assert.h>
#include <stdio.h>
#include "hmac.h"

#define FORGEBOX_HOST 1
#define FORGEBOX_GEN1_STORAGE 1
#include "../port/device_diagnostics.h"
#include "../port/pin_flash.c"

typedef int I2CIO_Cfg_t;
typedef struct { uint8_t key[32]; } Ds28Identity;
enum { DS28_TEST_PAGE = 5, DS28_RESET_SECRET_PAGE = 11, DS28_PIN_WRAP_PAGE = 44 };
static uint8_t account_config[128];
static bool account_config_ready;
static uint8_t pages[48][32], expected_pin[32], epoch;
static int unavailable_page = -1;
static bool enroll_fails, auth_write_fails, lose_auth_ack;
static unsigned writes;

static void clear_bytes(void *out, unsigned size) { memset(out, 0, size); }
static bool board_random(uint8_t *out, unsigned size) { memset(out, epoch + 1, size); return true; }
static bool atecc_open(I2CIO_Cfg_t *bus) { *bus = 0; return true; }
static void atecc_close(const I2CIO_Cfg_t *bus) { (void)bus; }
static bool atecc_query(const I2CIO_Cfg_t *bus, uint8_t op, uint8_t mode,
                        unsigned arg, uint8_t *out, unsigned size) {
    assert(!*bus);
    assert((op == 2 && mode == 0x80 && arg <= 24 && size == 32) ||
           (op == 0x24 && !mode && !arg && size == 4));
    memset(out, 0, size);
    return true;
}
static bool atecc_account_policy(const uint8_t *cfg) { return cfg == account_config; }
static PairingStatus atecc_check_pairing(const I2CIO_Cfg_t *bus, const uint8_t *cfg,
                                         uint8_t *status) {
    assert(!*bus && cfg == account_config);
    *status = 0;
    return PAIRING_MATCH;
}
static bool atecc_account_enroll_input(const uint8_t *pin, unsigned size) {
    assert(account_config_ready && size == sizeof(expected_pin));
    if (enroll_fails) return false;
    epoch++;
    memcpy(expected_pin, pin, size);
    return true;
}
static PairingStatus atecc_account_kdf_inputs(const uint8_t *auth, const uint8_t *pin,
                                               unsigned size, uint8_t *out) {
    assert(account_config_ready && size == sizeof(expected_pin));
    if (memcmp(auth, expected_pin, size)) return PAIRING_REJECTED;
    hmac_sha256_oneline(&epoch, 1, pin, size, out);
    return PAIRING_MATCH;
}
static bool gen1_rewrite_auth(const uint8_t *pin) {
    if (auth_write_fails) return false;
    memcpy(expected_pin, pin, sizeof(expected_pin));
    return !lose_auth_ack;
}
static void ds28s60_open(void) {}
static bool ds28s60_read_protection(uint8_t block, uint8_t *out) {
    assert(block == 1 || block == 2 || block == 11);
    out[0] = 0x04;
    out[1] = 0x0c;
    return true;
}
static PairingStatus ds28s60_load_identity(Ds28Identity *identity, uint8_t *status) {
    memset(identity->key, 0x44, sizeof(identity->key));
    *status = 0xaa;
    return PAIRING_MATCH;
}
static PairingStatus ds28s60_authenticated_read(const Ds28Identity *identity, uint8_t page,
                                                uint8_t *status, uint8_t *out) {
    assert(identity->key[0] == 0x44 && (page == 5 || page == 11 || (page >= 44 && page < 48)));
    if (page == unavailable_page) return PAIRING_REPLY;
    memcpy(out, pages[page], 32);
    *status = 0xaa;
    return PAIRING_MATCH;
}
static bool ds28s60_attempt_test_write(const Ds28Identity *identity, uint8_t page,
                                       const uint8_t *old, const uint8_t *next) {
    assert(identity->key[0] == 0x44 && (page == 5 || page == 11 || (page >= 44 && page < 48)));
    if (memcmp(old, pages[page], 32)) return false;
    memcpy(pages[page], next, 32);
    writes++;
    return true;
}

#include "../port/gen1_storage.inc"

int main(void) {
    uint8_t salt[GEN1_SALT_SIZE] = {0x42}, read_salt[GEN1_SALT_SIZE];
    uint8_t pin[32] = {0x77}, wrong[32] = {0x67}, initial[32], root[32];
#ifdef FORGEBOX_PRODUCTION
    uint8_t seed[SEED_CATALOG_SIZE] = {0};
    memcpy(seed, catalog_magic, sizeof(catalog_magic));
#else
    uint8_t seed[SEED_RECORD_SIZE] = {0};
    memcpy(seed, seed_magic, sizeof(seed_magic));
#endif
    uint8_t legacy[PIN_RECORD_SIZE] = {0};
    memcpy(legacy, pin_magic, sizeof(pin_magic));

    assert(board_gen1_state(read_salt) == 0);
    assert(pin_flash_write(0, legacy));
    assert(board_gen1_state(read_salt) < 0 && !board_gen1_wipe() && !writes);
    assert(wallet_flash_wipe());
    pages[11][0] = 1;
    assert(!board_gen1_enroll(salt, pin, root) && !writes);
    pages[11][0] = 0;

    enroll_fails = true;
    assert(!board_gen1_enroll(salt, pin, root));
    assert(board_gen1_state(read_salt) == 0);
    enroll_fails = false;
#ifdef FORGEBOX_PRODUCTION
    flash[descriptor_offset(2) + SLOT_SIZE] = 0x42;
    assert(board_gen1_state(read_salt) < 0);
    assert(!board_gen1_enroll(salt, pin, initial));
    assert(flash[descriptor_offset(2) + SLOT_SIZE] == 0x42);
    flash[descriptor_offset(2) + SLOT_SIZE] = 0xff;
#endif
    assert(board_gen1_enroll(salt, pin, initial));
    assert(board_gen1_state(read_salt) == 8 && !memcmp(salt, read_salt, sizeof(salt)));
#ifdef FORGEBOX_PRODUCTION
    assert(seed_catalog_write(0, seed));
#else
    assert(seed_flash_write(0, seed));
#endif
    assert(board_gen1_unlock(wrong, root) == 7);
    assert(board_gen1_state(read_salt) == 7);
    assert(board_gen1_unlock(pin, root) == -2 && !memcmp(initial, root, sizeof(root)));
    assert(board_gen1_state(read_salt) == 8);

    for (int remaining = 7; remaining > 0; remaining--) {
        assert(board_gen1_unlock(wrong, root) == remaining);
        assert(board_gen1_state(read_salt) == remaining);
    }
    unavailable_page = 11;
    assert(board_gen1_unlock(wrong, root) == -1);
    assert(board_gen1_state(read_salt) == -1 && !range_erased(0, SLOT_SIZE));
    unavailable_page = -1;
    assert(board_gen1_state(read_salt) == 0 && range_erased(0, SLOT_SIZE));
    assert(!memcmp(pages[5], gen1_blank, 32) && !memcmp(pages[11], gen1_blank, 32));
    assert(board_gen1_enroll(salt, pin, root) && memcmp(initial, root, sizeof(root)));
    assert(board_gen1_wipe() && board_gen1_state(read_salt) == 0);
    assert(board_gen1_enroll(salt, pin, initial));
#ifdef FORGEBOX_PRODUCTION
    assert(seed_catalog_write(0, seed));
#else
    assert(seed_flash_write(0, seed));
#endif
    pages[44][0] = 0x42;
    assert(board_gen1_change_pin(pin, wrong) == -1 && pages[44][0] == 0x42);
    pages[44][0] = 0;
    auth_write_fails = true;
    assert(board_gen1_change_pin(pin, wrong) == -1);
    assert(board_gen1_state(read_salt) == 8);
    assert(board_gen1_unlock(pin, root) == -2 && !memcmp(root, initial, 32));
    auth_write_fails = false;
    lose_auth_ack = true;
    assert(board_gen1_change_pin(pin, wrong) == -2);
    assert(board_gen1_unlock(pin, root) == 7);
    assert(board_gen1_unlock(wrong, root) == -2 && !memcmp(root, initial, 32));
    assert(board_gen1_change_pin(wrong, pin) == -2);
    assert(board_gen1_unlock(pin, root) == -2 && !memcmp(root, initial, 32));
    assert(!range_erased(0, SLOT_SIZE));
    assert(board_gen1_wipe() && board_gen1_state(read_salt) == 0);
    assert(!memcmp(pages[44], gen1_blank, 32) && !memcmp(pages[47], gen1_blank, 32));

    puts("PASS: Gen 1 enrollment, persistent retries, wipe recovery and key rotation");
}
