// A session-only envelope. The module may persist a test template until
// single-slot cleanup, but the wallet credential never goes to flash.
typedef struct {
    uint8_t iv[16], ciphertext[32], mac[32];
} FingerprintTestEnvelope;
static FingerprintTestEnvelope fp_envelope;
static bool fp_envelope_ready, fp_test_started;

static void fingerprint_test_keys(const uint8_t *match_key, uint8_t *cipher_key, uint8_t *mac_key) {
    static const uint8_t cipher_domain[] = "ForgeBox fingerprint test cipher v1";
    static const uint8_t mac_domain[] = "ForgeBox fingerprint test MAC v1";
    hmac_sha256_oneline(match_key, 32, cipher_domain, sizeof(cipher_domain), cipher_key);
    hmac_sha256_oneline(match_key, 32, mac_domain, sizeof(mac_domain), mac_key);
}

static void fingerprint_test_mac(const uint8_t *key, uint8_t *mac) {
    hmac_sha256_oneline(key, 32, (const uint8_t *)&fp_envelope,
                       sizeof(fp_envelope.iv) + sizeof(fp_envelope.ciphertext), mac);
}

static int fingerprint_wrap_pin(const uint8_t *pin_key, void (*progress)(unsigned, uint8_t)) {
    uint8_t match_key[32], root[32], cipher_key[32], mac_key[32];
    int result = -204;
    if (fp_test_started || board_fingerprint_test(FP_TEST_COUNT, NULL) != 0) goto done;
    int checked = board_gen1_unlock(pin_key, root);
    if (checked != -2) {
        result = checked < 0 ? -208 : -210 - checked;
        goto done;
    }
    fp_test_started = true;
    result = fingerprint_test_action(FP_TEST_ENROLL, progress, match_key);
    if (result) goto done;
    result = -204;
    if (board_fingerprint_test(FP_TEST_COUNT, NULL) != 1 || !plausible_otp_key(match_key))
        goto done;
    fingerprint_test_keys(match_key, cipher_key, mac_key);
    result = -FP_COUNT_CRYPTO;
    if (!board_random(fp_envelope.iv, sizeof(fp_envelope.iv)) ||
        fingerprint_aes(true, false, cipher_key, fp_envelope.iv, pin_key,
                        fp_envelope.ciphertext, sizeof(fp_envelope.ciphertext))) goto done;
    fingerprint_test_mac(mac_key, fp_envelope.mac);
    fp_envelope_ready = true;
    result = 0;
done:
    clear_bytes(match_key, sizeof(match_key));
    clear_bytes(root, sizeof(root));
    clear_bytes(cipher_key, sizeof(cipher_key));
    clear_bytes(mac_key, sizeof(mac_key));
    return result;
}

static int fingerprint_unwrap_and_unlock(uint8_t *output) {
    static const uint8_t domain[] = "ForgeBox BIP39 PIN context v1";
    uint8_t match_key[32], cipher_key[32], mac_key[32], expected[32];
    uint8_t context_message[sizeof(domain) + 32];
    uint8_t *pin_key = context_message + sizeof(domain);
    int result = -204;
    if (!fp_envelope_ready) goto done;
    result = fingerprint_test_action(FP_TEST_MATCH, NULL, match_key);
    if (result) goto done;
    result = -200;
    if (!plausible_otp_key(match_key)) goto done;
    fingerprint_test_keys(match_key, cipher_key, mac_key);
    fingerprint_test_mac(mac_key, expected);
    unsigned difference = 0;
    for (unsigned i = 0; i < sizeof(expected); i++) difference |= expected[i] ^ fp_envelope.mac[i];
    if (difference) goto done;
    result = -FP_COUNT_CRYPTO;
    if (fingerprint_aes(true, true, cipher_key, fp_envelope.iv, fp_envelope.ciphertext,
                        pin_key, sizeof(fp_envelope.ciphertext))) goto done;
    int checked = board_gen1_unlock(pin_key, output);
    if (checked != -2) {
        result = checked < 0 ? -208 : -210 - checked;
        goto done;
    }
    memcpy(context_message, domain, sizeof(domain));
    hmac_sha256_oneline(output, 32, context_message, sizeof(context_message), output + 32);
    result = 0;
done:
    clear_bytes(match_key, sizeof(match_key));
    clear_bytes(cipher_key, sizeof(cipher_key));
    clear_bytes(mac_key, sizeof(mac_key));
    clear_bytes(expected, sizeof(expected));
    clear_bytes(context_message, sizeof(context_message));
    return result;
}

static int fingerprint_cleanup(bool allow_leftover) {
    fp_envelope_ready = false;
    clear_bytes(&fp_envelope, sizeof(fp_envelope));
    if (!fp_test_started && !allow_leftover) return 0;
    int count = board_fingerprint_test(FP_TEST_COUNT, NULL);
    if (count == 1) {
        board_fingerprint_test(FP_TEST_REMOVE, NULL);
        count = board_fingerprint_test(FP_TEST_COUNT, NULL);
    }
    if (!count) fp_test_started = false;
    return count;
}

bool board_fp_unlock_test_enabled(void) { return true; }

int board_fp_unlock_test(unsigned action, const uint8_t *pin_key, uint8_t *result,
                         void (*progress)(unsigned, uint8_t)) {
    switch (action) {
        case 0:
            fingerprint_uart_open();
            fingerprint_restart();
            fingerprint_uart_close();
            return board_fingerprint_test(FP_TEST_COUNT, NULL);
        case 1: return fingerprint_wrap_pin(pin_key, progress);
        case 2: return fingerprint_unwrap_and_unlock(result);
        case 3: return fingerprint_cleanup(false);
        case 4: return fingerprint_cleanup(true);
        default: return -204;
    }
}
