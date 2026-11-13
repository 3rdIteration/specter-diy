// Keystone fingerprint host protocol.
enum {
    FP_COUNT_OK = 1, FP_COUNT_KEY_READ, FP_COUNT_KEY_BLANK, FP_COUNT_RNG,
    FP_COUNT_TX, FP_COUNT_SILENT, FP_COUNT_INVALID, FP_COUNT_REJECTED, FP_COUNT_CRYPTO,
};
enum { FP_AES_ECB = 1, FP_AES_CBC = 2, FP_PAIRING_PAGE = 82 };
enum { FP_ATTEMPT_SIZE = 10, FP_ATTEMPTS_OFFSET = 8 };

static int fingerprint_aes(bool cbc, bool decrypt, const uint8_t *key, const uint8_t *iv,
                           const uint8_t *input, uint8_t *output, unsigned size) {
    mbedtls_aes_context aes;
    uint8_t work_iv[16];
    mbedtls_aes_init(&aes);
    int direction = decrypt ? MBEDTLS_AES_DECRYPT : MBEDTLS_AES_ENCRYPT;
    int rc = decrypt ? mbedtls_aes_setkey_dec(&aes, key, 128)
                     : mbedtls_aes_setkey_enc(&aes, key, 128);
    if (!rc && cbc) {
        memcpy(work_iv, iv, sizeof(work_iv));
        rc = mbedtls_aes_crypt_cbc(&aes, direction, size, work_iv, input, output);
    } else if (!rc) {
        for (unsigned offset = 0; !rc && offset < size; offset += 16)
            rc = mbedtls_aes_crypt_ecb(&aes, direction, input + offset, output + offset);
    }
    mbedtls_aes_free(&aes);
    clear_bytes(work_iv, sizeof(work_iv));
    return rc;
}

static uint8_t fingerprint_secure_send(const uint8_t *key, bool cbc, uint16_t command,
                                       uint8_t argument, uint8_t *host_nonce, uint16_t *received) {
    uint8_t nonce_reply[17], mixed_nonce[16], request[39] = {0xaa, 36, 0};
    uint8_t plain[16] = {0};
    uint8_t result = FP_COUNT_RNG;
    // Use a new module nonce for each mode. Some units need a retry after wake.
    bool got_nonce = false;
    for (unsigned i = 0; i < 3 && !got_nonce; i++)
        got_nonce = fingerprint_query(0xd2, nonce_reply, sizeof(nonce_reply), received) == FP_REPLY &&
                    nonce_reply[0] == 0;
    if (!got_nonce || !board_random(host_nonce, 16)) goto done;
    osDelay(10);  // The retail host waits before sending the encrypted request.

    for (unsigned i = 0; i < 16; i++) mixed_nonce[i] = host_nonce[i] ^ nonce_reply[1 + i];
    plain[4] = command >> 8;
    plain[5] = command;
    plain[9] = 1;
    plain[11] = argument;
    uint32_t crc = fingerprint_crc(plain + 4, 12);
    memcpy(plain, &crc, 4);
    result = FP_COUNT_CRYPTO;
    if (fingerprint_aes(cbc, false, key, key + 16, mixed_nonce, request + 3, 16) ||
        fingerprint_aes(cbc, false, host_nonce, key + 16, plain, request + 19, 16)) goto done;
    crc = fingerprint_crc(request, sizeof(request) - 4);
    memcpy(request + sizeof(request) - 4, &crc, 4);
    *received = 0;
    result = fingerprint_send(request, sizeof(request)) ? FP_COUNT_OK : FP_COUNT_TX;
done:
    clear_bytes(nonce_reply, sizeof(nonce_reply));
    clear_bytes(mixed_nonce, sizeof(mixed_nonce));
    clear_bytes(plain, sizeof(plain));
    clear_bytes(request, sizeof(request));
    return result;
}

static uint8_t fingerprint_count_query(const uint8_t *key, bool cbc, uint8_t *out,
                                      uint8_t *attempt) {
    uint8_t host_nonce[16], plain[FP_FRAME_MAX], response[FP_FRAME_MAX];
    uint16_t received = 0;
    uint8_t result = fingerprint_secure_send(key, cbc, 0xa500, 0, host_nonce, &received);
    if (result != FP_COUNT_OK) goto done;
    uint32_t start = osKernelGetTickCount();
    unsigned total;
    while ((total = fingerprint_receive(start, 1000, response, &received)) != 0) {
        if (attempt[1] != UINT8_MAX) attempt[1]++;
        // Retail treats 23-byte frames for A500 as acknowledgements.
        if (total == 23) {
            if (attempt[2] != UINT8_MAX) attempt[2]++;
            if (response[7] == 0xa5 && !response[8]) attempt[4] |= 1;
            continue;
        }
        attempt[3] = total < UINT8_MAX ? total : UINT8_MAX;
        if (response[7] == 0xa5 && !response[8] && !response[13] &&
            response[12] <= total - 18) {
            attempt[4] |= 2;
            attempt[5] = response[12];
        }
        unsigned encrypted = total - 7;
        if (encrypted < 16 || encrypted % 16) continue;
        if (fingerprint_aes(cbc, true, host_nonce, key + 16, response + 3, plain, encrypted)) {
            result = FP_COUNT_CRYPTO;
            goto done;
        }
        if (plain[4] != 0xa5 || plain[5] != 0) continue;
        attempt[4] |= 4;
        if (plain[10] != 0 || plain[9] < 1 || plain[9] > encrypted - 11) continue;
        attempt[4] |= 8;
        attempt[6] = plain[9];
        if (plain[11]) {
            out[5] = plain[11];
            result = FP_COUNT_REJECTED;
            goto done;
        }
        // A zero-length acknowledgement cannot prove an encrypted reply.
        if (plain[9] < 2) continue;
        out[1] = plain[12];
        out[2] = cbc ? FP_AES_CBC : FP_AES_ECB;
        result = FP_COUNT_OK;
        goto done;
    }
    result = received ? FP_COUNT_INVALID : FP_COUNT_SILENT;
done:
    attempt[0] = result;
    memcpy(attempt + 8, &received, sizeof(received));
    memcpy(out + 6, &received, sizeof(received));
    clear_bytes(host_nonce, sizeof(host_nonce));
    clear_bytes(plain, sizeof(plain));
    clear_bytes(response, sizeof(response));
    return result;
}

static PairingStatus fingerprint_read_key(uint8_t *key) {
    Ds28Identity identity;
    uint8_t status;
    ds28s60_open();
    PairingStatus read = ds28s60_load_identity(&identity, &status);
    if (read == PAIRING_MATCH)
        read = ds28s60_authenticated_read(&identity, FP_PAIRING_PAGE, &status, key);
    clear_bytes(&identity, sizeof(identity));
    return read;
}

static void fingerprint_count_probe(uint8_t *out, bool version_known, uint8_t major) {
    uint8_t key[32];
    PairingStatus read = fingerprint_read_key(key);
    out[4] = read;
    if (read != PAIRING_MATCH) {
        out[0] = FP_COUNT_KEY_READ;
    } else if (!plausible_otp_key(key)) {
        out[0] = FP_COUNT_KEY_BLANK;
    } else {
        bool cbc = !version_known || major == 1;
        out[3] = cbc ? FP_AES_CBC : FP_AES_ECB;
        uint8_t *ecb = out + FP_ATTEMPTS_OFFSET;
        uint8_t *cbc_stats = ecb + FP_ATTEMPT_SIZE;
        out[0] = fingerprint_count_query(key, cbc, out, cbc ? cbc_stats : ecb);
        if (!version_known && (out[0] == FP_COUNT_SILENT || out[0] == FP_COUNT_INVALID)) {
            out[3] |= FP_AES_ECB;
            out[0] = fingerprint_count_query(key, false, out, ecb);
        }
    }
    clear_bytes(key, sizeof(key));
}
