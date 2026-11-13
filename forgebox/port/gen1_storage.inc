// Compiled with the shared secure-element transport in device_diagnostics.c.
enum { GEN1_RETRIES = 8, GEN1_ENROLLING = 1, GEN1_READY, GEN1_WIPING,
       GEN1_PIN_A, GEN1_PIN_B, GEN1_CHANGE_LEGACY, GEN1_CHANGE_A, GEN1_CHANGE_B,
       GEN1_WIPING_PIN, GEN1_PREPARE_PIN };

typedef struct {
    uint8_t marker[8];
    uint8_t remaining;
    uint8_t phase;
    uint8_t salt[GEN1_SALT_SIZE];
} Gen1Record;

typedef struct {
    uint8_t owner[16];
    uint8_t contribution[16];
} Gen1Secret;

_Static_assert(sizeof(Gen1Record) == 32 && sizeof(Gen1Secret) == 32,
               "Gen 1 records must fit one DS28S60 page");

static const uint8_t gen1_blank[32] = {0};

static bool gen1_login_phase(unsigned phase) {
    return phase == GEN1_READY || phase == GEN1_PREPARE_PIN ||
           (phase >= GEN1_PIN_A && phase <= GEN1_CHANGE_B);
}

static bool gen1_flash_blank(bool include_seeds) {
    uint8_t data[PIN_SLOT_DATA_SIZE];
    for (unsigned slot = 0; slot < 2; slot++) {
        if (!pin_flash_read(slot, data)) return false;
        for (unsigned i = 0; i < PIN_SLOT_DATA_SIZE; i++) {
            if (data[i] != 0xff) return false;
        }
        if (include_seeds) {
            unsigned size;
            if (!seed_backup_read(slot, NULL, &size) || size) return false;
        }
    }
    return true;
}

static bool gen1_atecc_open(void) {
    I2CIO_Cfg_t bus;
    uint8_t status, counter[4];
    account_config_ready = false;
    if (!atecc_open(&bus)) return false;
    for (unsigned block = 0; block < 4; block++) {
        if (!atecc_query(&bus, 0x02, 0x80, block * 8, account_config + block * 32, 32))
            goto done;
    }
    if (!atecc_account_policy(account_config) ||
        atecc_check_pairing(&bus, account_config, &status) != PAIRING_MATCH ||
        !atecc_query(&bus, 0x24, 0, 0, counter, sizeof(counter))) goto done;
    uint32_t count = (uint32_t)counter[0] | (uint32_t)counter[1] << 8 |
                     (uint32_t)counter[2] << 16 | (uint32_t)counter[3] << 24;
    account_config_ready = count < 0x1fff00;
done:
    atecc_close(&bus);
    return account_config_ready;
}

static bool gen1_read(const Ds28Identity *identity, uint8_t page, void *record) {
    uint8_t status;
    return ds28s60_authenticated_read(identity, page, &status, record) == PAIRING_MATCH;
}

static bool gen1_commit(const Ds28Identity *identity, uint8_t page,
                        const void *old, const void *next) {
    uint8_t check[32];
    bool ok = ds28s60_attempt_test_write(identity, page, old, next) &&
              gen1_read(identity, page, check) && !memcmp(check, next, sizeof(check));
    clear_bytes(check, sizeof(check));
    return ok;
}

static void gen1_marker(const Ds28Identity *identity, uint8_t *marker) {
#ifdef FORGEBOX_PRODUCTION
    static const uint8_t domain[] = "ForgeBox Specter Gen 1 storage v1 release";
#else
    static const uint8_t domain[] = "ForgeBox Specter Gen 1 storage v1";
#endif
    uint8_t digest[32];
    hmac_sha256_oneline(identity->key, 32, domain, sizeof(domain), digest);
    memcpy(marker, digest, 8);
    clear_bytes(digest, sizeof(digest));
}

static void gen1_owner(const Ds28Identity *identity, const Gen1Record *record, uint8_t *owner) {
    uint8_t message[8 + GEN1_SALT_SIZE], digest[32];
    memcpy(message, record->marker, 8);
    memcpy(message + 8, record->salt, GEN1_SALT_SIZE);
    hmac_sha256_oneline(identity->key, 32, message, sizeof(message), digest);
    memcpy(owner, digest, 16);
    clear_bytes(digest, sizeof(digest));
}

static bool gen1_open(Ds28Identity *identity, Gen1Record *record) {
    if (!gen1_flash_blank(false)) return false;
    ds28s60_open();
    uint8_t protection[2], status, marker[8];
    for (unsigned block = 1; block <= 2; block++) {
        if (!ds28s60_read_protection(block, protection) ||
            protection[0] != 0x04 || protection[1] != 0x0c) return false;
    }
    if (ds28s60_load_identity(identity, &status) != PAIRING_MATCH ||
        !gen1_read(identity, DS28_TEST_PAGE, record)) return false;
    if (!memcmp(record, gen1_blank, sizeof(*record))) return true;
    gen1_marker(identity, marker);
    return !memcmp(marker, record->marker, sizeof(marker)) &&
           record->phase >= GEN1_ENROLLING && record->phase <= GEN1_PREPARE_PIN &&
           record->remaining <= GEN1_RETRIES;
}

static bool gen1_read_owned_secret(const Ds28Identity *identity, const Gen1Record *record,
                                   Gen1Secret *secret) {
    uint8_t owner[16];
    gen1_owner(identity, record, owner);
    return gen1_read(identity, DS28_RESET_SECRET_PAGE, secret) &&
           !memcmp(owner, secret->owner, sizeof(owner));
}

static bool gen1_wrap_policy(void) {
    uint8_t protection[2];
    return ds28s60_read_protection(DS28_PIN_WRAP_PAGE / 4, protection) &&
           protection[0] == 4 && protection[1] == 0x0c;
}

static void gen1_wrap_key(const uint8_t *atecc, const Gen1Secret *secret, uint8_t *key) {
    static const uint8_t domain[] = "ForgeBox Gen 1 PIN wrapping v1";
    uint8_t mixed[32];
    hmac_sha256_oneline(atecc, 32, secret->contribution, sizeof(secret->contribution), mixed);
    hmac_sha256_oneline(mixed, 32, domain, sizeof(domain), key);
    clear_bytes(mixed, sizeof(mixed));
}

static bool gen1_pin_blob(const Ds28Identity *identity, const Gen1Secret *secret,
                          unsigned bank, const uint8_t *atecc, uint8_t *root, bool save) {
    uint8_t page = DS28_PIN_WRAP_PAGE + bank * 2;
    uint8_t key[32], mask[32], cipher[32], tag[32], expected[32];
    static const uint8_t domain[] = "ForgeBox Gen 1 PIN mask v1";
    bool ok = false;
    if (!gen1_wrap_policy() || !gen1_read(identity, page, cipher) ||
        !gen1_read(identity, page + 1, tag)) goto done;
    gen1_wrap_key(atecc, secret, key);
    hmac_sha256_oneline(key, 32, domain, sizeof(domain), mask);
    if (save) {
        uint8_t next[32];
        for (unsigned i = 0; i < 32; i++) next[i] = root[i] ^ mask[i];
        hmac_sha256_oneline(key, 32, next, 32, expected);
        ok = gen1_commit(identity, page, cipher, next) &&
             gen1_commit(identity, page + 1, tag, expected);
        clear_bytes(next, sizeof(next));
    } else {
        hmac_sha256_oneline(key, 32, cipher, 32, expected);
        unsigned difference = 0;
        for (unsigned i = 0; i < 32; i++) difference |= tag[i] ^ expected[i];
        if (!difference) {
            for (unsigned i = 0; i < 32; i++) root[i] = cipher[i] ^ mask[i];
            ok = true;
        }
    }
done:
    clear_bytes(key, sizeof(key));
    clear_bytes(mask, sizeof(mask));
    return ok;
}

static bool gen1_clear_pin_blobs(const Ds28Identity *identity) {
    uint8_t data[32];
    if (!gen1_wrap_policy()) return false;
    for (unsigned i = 0; i < 4; i++) {
        uint8_t page = DS28_PIN_WRAP_PAGE + i;
        if (!gen1_read(identity, page, data) ||
            (memcmp(data, gen1_blank, 32) && !gen1_commit(identity, page, data, gen1_blank)))
            return false;
    }
    return true;
}

static bool gen1_finish_wipe(const Ds28Identity *identity, const Gen1Record *record) {
    Gen1Secret secret;
    uint8_t owner[16];
    gen1_owner(identity, record, owner);
    bool erased = false;
    if (gen1_read(identity, DS28_RESET_SECRET_PAGE, &secret)) {
        if (!memcmp(owner, secret.owner, sizeof(owner)))
            erased = gen1_commit(identity, DS28_RESET_SECRET_PAGE, &secret, gen1_blank);
        else
            erased = !memcmp(&secret, gen1_blank, sizeof(secret));
    }
    clear_bytes(&secret, sizeof(secret));
#ifdef FORGEBOX_FP_UNLOCK
#ifdef FORGEBOX_PRODUCTION
    if (erased) erased = fingerprint_wallet_wipe(identity);
#else
    if (erased) fingerprint_wallet_wipe(identity);
#endif
#endif
    return erased && (record->phase != GEN1_WIPING_PIN || gen1_clear_pin_blobs(identity)) &&
           wallet_flash_wipe() &&
           gen1_commit(identity, DS28_TEST_PAGE, record, gen1_blank);
}

static bool gen1_start_wipe(const Ds28Identity *identity, const Gen1Record *record) {
    Gen1Record pending = *record;
    pending.phase = record->phase >= GEN1_PIN_A ? GEN1_WIPING_PIN : GEN1_WIPING;
    pending.remaining = 0;
    return (record->phase == pending.phase ||
            gen1_commit(identity, DS28_TEST_PAGE, record, &pending)) &&
           gen1_finish_wipe(identity, &pending);
}

static bool gen1_unused(const Ds28Identity *identity) {
    Gen1Secret secret;
    bool blank = gen1_flash_blank(true) && wallet_flash_resume() &&
                 gen1_read(identity, DS28_RESET_SECRET_PAGE, &secret) &&
                 !memcmp(&secret, gen1_blank, sizeof(secret));
#ifdef FORGEBOX_PRODUCTION
    blank = blank && wallet_flash_is_blank();
    // All persistent PIN and fingerprint pages must be unclaimed at enrollment.
    uint8_t page[32];
    if (blank) {
        for (unsigned i = 0; i < 4; i++)
            if (!gen1_read(identity, DS28_PIN_WRAP_PAGE + i, page) ||
                memcmp(page, gen1_blank, sizeof(page))) blank = false;
#ifdef FORGEBOX_FP_UNLOCK
        const uint8_t fingerprint_pages[] = {DS28_FP_CREDENTIAL_PAGE, DS28_FP_METADATA_PAGE};
        for (unsigned i = 0; i < sizeof(fingerprint_pages); i++)
            if (!gen1_read(identity, fingerprint_pages[i], page) ||
                memcmp(page, gen1_blank, sizeof(page))) blank = false;
#endif
    }
    clear_bytes(page, sizeof(page));
#endif
    clear_bytes(&secret, sizeof(secret));
    return blank;
}

static void gen1_root(const uint8_t *atecc, const Gen1Secret *secret, uint8_t *root) {
    hmac_sha256_oneline(atecc, 32, secret->contribution, sizeof(secret->contribution), root);
}

bool board_gen1_enabled(void) { return true; }

int board_gen1_state(uint8_t *salt) {
    Ds28Identity identity;
    Gen1Record record;
    int result = -1;
    if (!gen1_open(&identity, &record)) goto done;
    if (!record.phase) {
        if (gen1_unused(&identity) && gen1_atecc_open()) result = 0;
    } else if (!gen1_login_phase(record.phase) || !record.remaining) {
        if (gen1_start_wipe(&identity, &record) && gen1_atecc_open()) result = 0;
    } else if (gen1_atecc_open()) {
        memcpy(salt, record.salt, GEN1_SALT_SIZE);
        result = record.remaining;
    }
done:
    clear_bytes(&identity, sizeof(identity));
    return result;
}

bool board_gen1_enroll(const uint8_t *salt, const uint8_t *pin_key, uint8_t *root) {
    Ds28Identity identity;
    Gen1Record record, next = {.remaining = GEN1_RETRIES, .phase = GEN1_ENROLLING};
    Gen1Secret secret;
    uint8_t atecc[32];
    bool result = false;
    if (!gen1_open(&identity, &record) || record.phase ||
        !gen1_unused(&identity) || !gen1_atecc_open()) goto done;
    gen1_marker(&identity, next.marker);
    memcpy(next.salt, salt, GEN1_SALT_SIZE);
    gen1_owner(&identity, &next, secret.owner);
    if (!board_random(secret.contribution, sizeof(secret.contribution)) ||
        !gen1_commit(&identity, DS28_TEST_PAGE, &record, &next) ||
        !atecc_account_enroll_input(pin_key, 32) ||
        atecc_account_kdf_inputs(pin_key, pin_key, 32, atecc) != PAIRING_MATCH ||
        !gen1_commit(&identity, DS28_RESET_SECRET_PAGE, gen1_blank, &secret)) goto done;
    record = next;
    next.phase = GEN1_READY;
    if (!gen1_commit(&identity, DS28_TEST_PAGE, &record, &next)) goto done;
    gen1_root(atecc, &secret, root);
    result = true;
done:
    clear_bytes(&secret, sizeof(secret));
    clear_bytes(atecc, sizeof(atecc));
    clear_bytes(&identity, sizeof(identity));
    return result;
}

int board_gen1_unlock(const uint8_t *pin_key, uint8_t *root) {
    Ds28Identity identity;
    Gen1Record record, next;
    Gen1Secret secret;
    uint8_t atecc[32];
    int result = -1;
    if (!gen1_open(&identity, &record) || !gen1_login_phase(record.phase) ||
        !record.remaining || !gen1_atecc_open()) goto done;
    next = record;
    next.remaining--;
    if (!gen1_commit(&identity, DS28_TEST_PAGE, &record, &next)) goto done;
    PairingStatus status = atecc_account_kdf_inputs(pin_key, pin_key, 32, atecc);
    if (status == PAIRING_REJECTED) {
        if (next.remaining || gen1_start_wipe(&identity, &next)) result = next.remaining;
        goto done;
    }
    if (status != PAIRING_MATCH || !gen1_read_owned_secret(&identity, &next, &secret)) goto done;
    unsigned phase = record.phase;
    if (phase == GEN1_PREPARE_PIN) {
        if (!gen1_clear_pin_blobs(&identity)) goto done;
        phase = GEN1_READY;
    }
    if (phase >= GEN1_CHANGE_LEGACY) {
        unsigned target = phase == GEN1_CHANGE_A ? 1 : 0;
        if (gen1_pin_blob(&identity, &secret, target, atecc, root, false))
            phase = GEN1_PIN_A + target;
        else if (phase == GEN1_CHANGE_LEGACY) {
            uint8_t legacy_root[32];
            gen1_root(atecc, &secret, legacy_root);
            bool verified = gen1_pin_blob(&identity, &secret, 1, atecc, root, false) &&
                            !memcmp(legacy_root, root, 32);
            clear_bytes(legacy_root, sizeof(legacy_root));
            if (!verified) goto done;
            phase = GEN1_PIN_B;
        } else
            phase = GEN1_PIN_B - target;
    }
    if (phase == GEN1_READY) gen1_root(atecc, &secret, root);
    else if (!gen1_pin_blob(&identity, &secret, phase - GEN1_PIN_A, atecc, root, false)) goto done;
    record = next;
    record.phase = phase;
    record.remaining = GEN1_RETRIES;
    if (!gen1_commit(&identity, DS28_TEST_PAGE, &next, &record)) goto done;
    result = -2;
done:
    clear_bytes(&secret, sizeof(secret));
    clear_bytes(atecc, sizeof(atecc));
    clear_bytes(&identity, sizeof(identity));
    return result;
}

int board_gen1_change_pin(const uint8_t *old_pin, const uint8_t *new_pin) {
    Ds28Identity identity;
    Gen1Record record, pending;
    Gen1Secret secret;
    uint8_t root[32], next_root[32], atecc[32], page[32];
    int result = board_gen1_unlock(old_pin, root);
    if (result != -2) goto done;
    result = -1;
    if (!gen1_open(&identity, &record) || !gen1_read_owned_secret(&identity, &record, &secret) ||
        !gen1_wrap_policy()) goto done;
    unsigned target = record.phase == GEN1_PIN_A;
    if (record.phase == GEN1_READY) {
        for (unsigned i = 0; i < 4; i++)
            if (!gen1_read(&identity, DS28_PIN_WRAP_PAGE + i, page) ||
                memcmp(page, gen1_blank, 32)) goto done;
    }
#ifdef FORGEBOX_FP_UNLOCK
    if (!fingerprint_revoke_for_pin_change(&identity)) goto done;
#endif
    if (atecc_account_kdf_inputs(old_pin, new_pin, 32, atecc) != PAIRING_MATCH) goto done;
    // Journal before touching the new bank. The current authorization still
    // works if power is lost before the ATECC switches to the new PIN.
    pending = record;
    pending.phase = record.phase == GEN1_READY ? GEN1_CHANGE_LEGACY :
                    target ? GEN1_CHANGE_A : GEN1_CHANGE_B;
    if (record.phase == GEN1_READY) {
        Gen1Record preparing = record;
        preparing.phase = GEN1_PREPARE_PIN;
        if (!gen1_commit(&identity, DS28_TEST_PAGE, &record, &preparing)) goto done;
        record = preparing;
        uint8_t old_atecc[32];
        bool backed_up = atecc_account_kdf_inputs(old_pin, old_pin, 32, old_atecc) == PAIRING_MATCH &&
                         gen1_pin_blob(&identity, &secret, 1, old_atecc, root, true);
        clear_bytes(old_atecc, sizeof(old_atecc));
        if (!backed_up) goto done;
    }
    if (!gen1_commit(&identity, DS28_TEST_PAGE, &record, &pending) ||
        !gen1_pin_blob(&identity, &secret, target, atecc, root, true)) goto done;
    gen1_rewrite_auth(new_pin);
    if (atecc_account_kdf_inputs(new_pin, new_pin, 32, atecc) != PAIRING_MATCH ||
        !gen1_pin_blob(&identity, &secret, target, atecc, next_root, false) ||
        memcmp(root, next_root, 32)) goto done;
    record.phase = GEN1_PIN_A + target;
    if (gen1_commit(&identity, DS28_TEST_PAGE, &pending, &record)) result = -2;
done:
    clear_bytes(&identity, sizeof(identity));
    clear_bytes(&secret, sizeof(secret));
    clear_bytes(root, sizeof(root));
    clear_bytes(next_root, sizeof(next_root));
    clear_bytes(atecc, sizeof(atecc));
    return result;
}

bool board_gen1_wipe(void) {
    Ds28Identity identity;
    Gen1Record record;
    bool result = gen1_open(&identity, &record) &&
                  (record.phase ? gen1_start_wipe(&identity, &record) : gen1_unused(&identity));
    clear_bytes(&identity, sizeof(identity));
    return result;
}
