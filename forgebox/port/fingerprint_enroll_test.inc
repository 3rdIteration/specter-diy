// Test-only commands, for a board with an existing ECB pairing.
enum { FP_TEST_SLOT = 0, FP_ENROLL_STEPS = 8 };

static int fingerprint_run_command(const uint8_t *key, uint16_t command,
                                   void (*progress)(unsigned, uint8_t), uint8_t *match_key) {
    uint8_t host_nonce[16], response[FP_FRAME_MAX], plain[FP_FRAME_MAX];
    uint16_t received = 0;
    // Keystone's A7FF argument 1 requests the key-bearing reply.
    uint8_t argument = command == 0xa7ff && match_key ? 1 : FP_TEST_SLOT;
    int result = -(int)fingerprint_secure_send(key, false, command, argument,
                                               host_nonce, &received);
    if (result != -FP_COUNT_OK) goto done;

    uint32_t start = osKernelGetTickCount();
    unsigned total, steps = 0, events = 0;
    unsigned timeout = command == 0xa000 ? 90000 : command == 0xa7ff ? 15000 : 2500;
    result = -FP_COUNT_SILENT;
    while ((total = fingerprint_receive(start, timeout, response, &received)) != 0) {
        if (total == 23) continue;  // ACK frame, as in the retail receiver.
        unsigned size = total - 7;
        if (size < 16 || size % 16) continue;
        if (fingerprint_aes(false, true, host_nonce, key + 16, response + 3, plain, size)) {
            result = -FP_COUNT_CRYPTO;
            break;
        }
        if (plain[4] != command >> 8 || plain[5] != (uint8_t)command ||
            plain[10] || !plain[9] || plain[9] > size - 11) continue;
        uint8_t status = plain[11];
        if (command != 0xa000) {
            if (command == 0xa7ff && !status && plain[9] < 2) continue;
            result = status;
            if (command == 0xa7ff && !status && match_key) {
                if (plain[9] != 38) result = -100 - plain[9];
                else memcpy(match_key, plain + 17, 32);
            }
            break;
        }
        if (!status) {
            if (plain[9] < 3 || plain[12] != FP_TEST_SLOT || plain[13] > FP_ENROLL_STEPS) {
                result = -FP_COUNT_INVALID;
                break;
            }
            steps = plain[13];
        }
        if (progress) progress(steps, status);
        if (steps == FP_ENROLL_STEPS) {
            result = 0;
            if (match_key) {
                if (plain[9] != 38) result = -100 - plain[9];
                else memcpy(match_key, plain + 17, 32);
            }
            break;
        }
        if (++events >= 18) break;
    }
    if (result == -FP_COUNT_SILENT && received) result = -FP_COUNT_INVALID;
done:
    clear_bytes(host_nonce, sizeof(host_nonce));
    clear_bytes(response, sizeof(response));
    clear_bytes(plain, sizeof(plain));
    return result;
}

static int fingerprint_test_action(FingerprintTestAction action, void (*progress)(unsigned, uint8_t),
                                   uint8_t *match_key) {
    uint8_t key[32];
    int result = -FP_COUNT_KEY_READ;
    if (fingerprint_read_key(key) != PAIRING_MATCH) goto done;
    if (!plausible_otp_key(key)) {
        result = -FP_COUNT_KEY_BLANK;
        goto done;
    }
    fingerprint_uart_open();
    if (action == FP_TEST_COUNT) {
        uint8_t report[8] = {0}, attempt[FP_ATTEMPT_SIZE] = {0};
        int status = fingerprint_count_query(key, false, report, attempt);
        // A timed-out capture can leave the module busy. Reset only its
        // volatile session before trying the read-only query once more.
        if (status != FP_COUNT_OK) {
            fingerprint_restart();
            status = fingerprint_count_query(key, false, report, attempt);
        }
        result = status == FP_COUNT_OK ? report[1] : -status;
    } else if (action == FP_TEST_ENROLL) {
        result = fingerprint_run_command(key, 0xa000, progress, match_key);
    } else if (action == FP_TEST_MATCH) {
        result = fingerprint_run_command(key, 0xa7ff, NULL, match_key);
    } else if (action == FP_TEST_REMOVE) {
        result = fingerprint_run_command(key, 0xa400, NULL, NULL);
    }
    fingerprint_uart_close();
done:
    clear_bytes(key, sizeof(key));
    return result;
}

int board_fingerprint_test(FingerprintTestAction action, void (*progress)(unsigned, uint8_t)) {
    return fingerprint_test_action(action, progress, NULL);
}
