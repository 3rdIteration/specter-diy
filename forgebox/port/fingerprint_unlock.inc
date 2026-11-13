// Account 0 credential and metadata, on the same DS28S60 pages assigned
// to those purposes by Keystone. Page 82 remains read-only.
typedef struct {
    uint8_t owner[8], nonce[12], tag[12];
} FingerprintMetadata;
_Static_assert(sizeof(FingerprintMetadata) == 32, "fingerprint metadata page size");

enum { FP_EMPTY, FP_PENDING, FP_ACTIVE };

static bool fingerprint_read_storage(const Ds28Identity *identity, FingerprintMetadata *meta,
                                     uint8_t credential[32]) {
    uint8_t policy[2];
    const uint8_t pages[] = {DS28_FP_CREDENTIAL_PAGE, DS28_FP_METADATA_PAGE};
    for (unsigned i = 0; i < sizeof(pages); i++) {
        if (!ds28s60_read_protection(pages[i] / 4, policy) ||
            policy[0] != 0x04 || policy[1] != 0x0c) return false;
    }
    return gen1_read(identity, DS28_FP_METADATA_PAGE, meta) &&
           gen1_read(identity, DS28_FP_CREDENTIAL_PAGE, credential);
}

static void fingerprint_owner(const Ds28Identity *identity, unsigned state, uint8_t out[8]) {
    static const uint8_t pending[] = "ForgeBox fingerprint pending v1";
    static const uint8_t active[] = "ForgeBox fingerprint active v1";
    uint8_t digest[32];
    hmac_sha256_oneline(identity->key, sizeof(identity->key),
        state == FP_PENDING ? pending : active,
        state == FP_PENDING ? sizeof(pending) : sizeof(active), digest);
    memcpy(out, digest, 8);
    clear_bytes(digest, sizeof(digest));
}

static int fingerprint_state(const Ds28Identity *identity, const FingerprintMetadata *meta,
                             const uint8_t credential[32]) {
    uint8_t owner[8];
    if (!memcmp(meta, gen1_blank, 32) && !memcmp(credential, gen1_blank, 32)) return FP_EMPTY;
    fingerprint_owner(identity, FP_PENDING, owner);
    if (!memcmp(meta->owner, owner, sizeof(owner))) return FP_PENDING;
    fingerprint_owner(identity, FP_ACTIVE, owner);
    if (!memcmp(meta->owner, owner, sizeof(owner))) return FP_ACTIVE;
    return -204;
}

static bool fingerprint_revoke(const Ds28Identity *identity, FingerprintMetadata *meta,
                               const uint8_t credential[32], int state) {
    if (state == FP_EMPTY) return true;
    if (state != FP_ACTIVE && state != FP_PENDING) return false;
    FingerprintMetadata pending = {0};
    fingerprint_owner(identity, FP_PENDING, pending.owner);
    if (state == FP_ACTIVE && !gen1_commit(identity, DS28_FP_METADATA_PAGE, meta, &pending))
        return false;
    if (memcmp(credential, gen1_blank, 32) &&
        !gen1_commit(identity, DS28_FP_CREDENTIAL_PAGE, credential, gen1_blank)) return false;
    int count = board_fingerprint_test(FP_TEST_COUNT, NULL);
    if (count == 1) {
        board_fingerprint_test(FP_TEST_REMOVE, NULL);
        count = board_fingerprint_test(FP_TEST_COUNT, NULL);
    }
    // Keep ownership of the revoked template across reboot if cleanup fails.
    return count == 0 && gen1_commit(identity, DS28_FP_METADATA_PAGE,
                                    state == FP_ACTIVE ? &pending : meta, gen1_blank);
}

static bool fingerprint_wallet_wipe(const Ds28Identity *identity) {
    FingerprintMetadata meta;
    uint8_t credential[32];
    bool ok = fingerprint_read_storage(identity, &meta, credential) &&
              fingerprint_revoke(identity, &meta, credential,
                                 fingerprint_state(identity, &meta, credential));
    clear_bytes(&meta, sizeof(meta));
    clear_bytes(credential, sizeof(credential));
    return ok;
}

static bool fingerprint_revoke_for_pin_change(const Ds28Identity *identity) {
    FingerprintMetadata meta;
    uint8_t credential[32];
    return fingerprint_read_storage(identity, &meta, credential) &&
           fingerprint_revoke(identity, &meta, credential, fingerprint_state(identity, &meta, credential));
}

static void fingerprint_wrap_keys(const uint8_t match_key[32], const Gen1Record *record,
                                  const FingerprintMetadata *meta, uint8_t keys[2][32]) {
    uint8_t message[sizeof("ForgeBox fingerprint wrap v1") + GEN1_SALT_SIZE + sizeof(meta->nonce)]
        = "ForgeBox fingerprint wrap v1";
    memcpy(message + sizeof("ForgeBox fingerprint wrap v1"), record->salt, GEN1_SALT_SIZE);
    memcpy(message + sizeof("ForgeBox fingerprint wrap v1") + GEN1_SALT_SIZE,
           meta->nonce, sizeof(meta->nonce));
    for (unsigned i = 0; i < 2; i++) {
        message[sizeof("ForgeBox fingerprint wrap v1") - 1] = i;
        hmac_sha256_oneline(match_key, 32, message, sizeof(message), keys[i]);
    }
}

static int fingerprint_enroll_persistent(const Ds28Identity *identity, const Gen1Record *record,
                                          FingerprintMetadata *meta, const uint8_t *pin_key,
                                          void (*progress)(unsigned, uint8_t)) {
    uint8_t root[32], match_key[32], keys[2][32], tag[32], credential[32];
    const uint8_t iv[16] = {0};  // The random nonce derives a fresh encryption key.
    FingerprintMetadata pending = {0}, active = {0};
    int result = -204;
    if (board_fingerprint_test(FP_TEST_COUNT, NULL) != 0) goto done;
    int checked = board_gen1_unlock(pin_key, root);
    if (checked != -2) {
        result = checked < 0 ? -208 : -210 - checked;
        goto done;
    }
    fingerprint_owner(identity, FP_PENDING, pending.owner);
    if (!gen1_commit(identity, DS28_FP_METADATA_PAGE, meta, &pending)) goto done;
    result = fingerprint_test_action(FP_TEST_ENROLL, progress, match_key);
    if (result) goto done;
    result = -204;
    if (!plausible_otp_key(match_key) || board_fingerprint_test(FP_TEST_COUNT, NULL) != 1 ||
        !board_random(active.nonce, sizeof(active.nonce))) goto done;
    fingerprint_wrap_keys(match_key, record, &active, keys);
    if (fingerprint_aes(true, false, keys[0], iv, pin_key, credential, sizeof(credential)))
        goto done;
    hmac_sha256_oneline(keys[1], 32, credential, sizeof(credential), tag);
    memcpy(active.tag, tag, sizeof(active.tag));
    fingerprint_owner(identity, FP_ACTIVE, active.owner);
    if (gen1_commit(identity, DS28_FP_CREDENTIAL_PAGE, gen1_blank, credential) &&
        gen1_commit(identity, DS28_FP_METADATA_PAGE, &pending, &active)) result = 0;
done:
    clear_bytes(root, sizeof(root));
    clear_bytes(match_key, sizeof(match_key));
    clear_bytes(keys, sizeof(keys));
    clear_bytes(tag, sizeof(tag));
    clear_bytes(credential, sizeof(credential));
    return result;
}

static int fingerprint_unlock_persistent(const Gen1Record *record, const FingerprintMetadata *meta,
                                          const uint8_t credential[32], uint8_t output[64],
                                          void (*progress)(unsigned, uint8_t)) {
    static const uint8_t domain[] = "ForgeBox BIP39 PIN context v1";
    const uint8_t iv[16] = {0};
    uint8_t match_key[32], keys[2][32], tag[32], message[sizeof(domain) + 32];
    uint8_t *pin_key = message + sizeof(domain);
    // The cold-boot and subsequent scan paths both start a fresh module session.
    fingerprint_uart_open();
    fingerprint_restart();
    fingerprint_uart_close();
    int result = fingerprint_test_action(FP_TEST_MATCH, NULL, match_key);
    if (result) goto done;
    result = -200;
    if (!plausible_otp_key(match_key)) goto done;
    fingerprint_wrap_keys(match_key, record, meta, keys);
    hmac_sha256_oneline(keys[1], 32, credential, 32, tag);
    unsigned difference = 0;
    for (unsigned i = 0; i < sizeof(meta->tag); i++) difference |= tag[i] ^ meta->tag[i];
    if (difference || fingerprint_aes(true, true, keys[0], iv, credential, pin_key, 32))
        goto done;
    if (progress) progress(0, 0);
    int checked = board_gen1_unlock(pin_key, output);
    if (checked != -2) {
        result = checked < 0 ? -208 : -210 - checked;
        goto done;
    }
    memcpy(message, domain, sizeof(domain));
    hmac_sha256_oneline(output, 32, message, sizeof(message), output + 32);
    result = 0;
done:
    clear_bytes(match_key, sizeof(match_key));
    clear_bytes(keys, sizeof(keys));
    clear_bytes(tag, sizeof(tag));
    clear_bytes(message, sizeof(message));
    return result;
}

bool board_fp_unlock_test_enabled(void) { return false; }

int board_fp_unlock_test(unsigned action, const uint8_t *pin_key, uint8_t *output,
                         void (*progress)(unsigned, uint8_t)) {
    Ds28Identity identity;
    Gen1Record record;
    FingerprintMetadata meta;
    uint8_t credential[32];
    int result = -204;
    if (!gen1_open(&identity, &record) ||
        (record.phase != GEN1_READY && record.phase != GEN1_PIN_A && record.phase != GEN1_PIN_B) ||
        !record.remaining ||
        !fingerprint_read_storage(&identity, &meta, credential)) goto done;
    int state = fingerprint_state(&identity, &meta, credential);
    switch (action) {
        case 0: result = state; break;
        case 1:
            if (state == FP_EMPTY)
                result = fingerprint_enroll_persistent(&identity, &record, &meta, pin_key, progress);
            break;
        case 2:
            if (state == FP_ACTIVE)
                result = fingerprint_unlock_persistent(&record, &meta, credential, output, progress);
            break;
        case 3:
            result = fingerprint_revoke(&identity, &meta, credential, state) ? 0 : -204;
            break;
    }
done:
    clear_bytes(&identity, sizeof(identity));
    clear_bytes(credential, sizeof(credential));
    return result;
}
