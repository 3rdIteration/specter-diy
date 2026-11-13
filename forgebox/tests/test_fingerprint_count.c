#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "hmac.h"

enum { FP_FRAME_MAX = 263, FP_REPLY = 1, PAIRING_MATCH = 1 };
typedef int PairingStatus;
typedef struct { uint8_t key[32]; } Ds28Identity;
static const char *requests[] = {
    "aa2400954f64f2e4e86e9eee82d202166848996be545c888837139b970dc8ddd1b6fdb06e1d9c4",
    "aa24000a940bb5416ef045f1c39458c653ea5a9e57829c0e4afcbf6cb6efe61a35cea02eab9362",
};
static const char *responses[] = {
    "aa24000be8e4c6d2d692f73f750768cd10c768ae3a71384013479e5a259218e4df8cbfca099399",
    "aa2400412a38371d9fc312fff1c524177a1b0348defb254577e73682497e535e0488f3cb861076",
};
static const char *test_requests[] = {
    "aa2400954f64f2e4e86e9eee82d202166848998f464f7703e32efbf4c35f1e46579e1e90487f37",
    "aa2400954f64f2e4e86e9eee82d20216684899c27e2d1533c2f095b4d90302815ce22fd78acf97",
    "aa2400954f64f2e4e86e9eee82d20216684899c27e2d1533c2f095b4d90302815ce22fd78acf97",
    "aa2400954f64f2e4e86e9eee82d20216684899e4175640e942fdd5603d88242399ed99a3ed0b26",
};
static const char *test_responses[] = {
    "aa44009265d14932770abd1b75a89684e95ccbef319bb37ca4bc8ee549819d3b96c43ef39ab3b177cd904b00cc2db4ba0c4995d1e3fa00a2e0d555f8638004fbe5bae71de11653",
    "aa24009a9783a5ac7d5ff760875bf36c121bc0ae3a71384013479e5a259218e4df8cbf189e61b5",
    "aa24009a25f46111b572807e157e4dbace4f4cae3a71384013479e5a259218e4df8cbf9822f28c",
    "aa2400a474cddfb5c55eefdb49ecafa74f8d4fae3a71384013479e5a259218e4df8cbf76794731",
};
static unsigned mode, reads;
static int test_case = -1;
static bool wrong_response;
static bool unlock_fixture;
static unsigned unlock_command, match_fixture, mock_templates, gen1_calls;
static const uint8_t test_pin[32] = {0x42};
static const char *unlock_responses[] = {
    "aa2400617f1b72cc001185e077dc29e7db2526ae3a71384013479e5a259218e4df8cbf0ddeb619",
    "aa2400f4877a3c6a68c889c9944839b7d07e8fae3a71384013479e5a259218e4df8cbfe82f48c4",
    "aa4400daabbe4c672e6c53b8525385c17800fb229ccfde4fc066c4007b8ae1b182d3e6f39ab3b177cd904b00cc2db4ba0c4995d1e3fa00a2e0d555f8638004fbe5bae7f7027c3b",
    "aa440056fc0c9af6d3d16ad7113b6519f9baa8919eb615426803c8d7eba1655b6368c21b088c9fd98d632d53643f3b1ac50d832c652a115ea85b5e65478a53ab63cc6709cce449",
};

static void hex_bytes(const char *hex, uint8_t *out) {
    for (unsigned i = 0; hex[2 * i]; i++) {
        unsigned value;
        assert(sscanf(hex + 2 * i, "%2x", &value) == 1);
        out[i] = value;
    }
}

static uint32_t fingerprint_crc(const uint8_t *data, unsigned size) {
    uint32_t crc = UINT32_MAX;
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
static void clear_bytes(void *out, unsigned size) { memset(out, 0, size); }
static uint32_t osKernelGetTickCount(void) { return 0; }
static void osDelay(unsigned ticks) { assert(ticks == 10); }
static void ds28s60_open(void) {}
static PairingStatus ds28s60_load_identity(Ds28Identity *identity, uint8_t *status) {
    memset(identity, 0, sizeof(*identity));
    *status = 0xaa;
    return PAIRING_MATCH;
}
static PairingStatus ds28s60_authenticated_read(const Ds28Identity *identity, uint8_t page,
                                                uint8_t *status, uint8_t *key) {
    assert(!identity->key[0] && *status == 0xaa && page == 82);
    for (unsigned i = 0; i < 32; i++) key[i] = i;
    return PAIRING_MATCH;
}
static bool plausible_otp_key(const uint8_t *key) { return key[1] != key[0]; }
static bool board_random(uint8_t *out, unsigned size) {
    assert(size == 16 || size == 12);
    for (unsigned i = 0; i < size; i++) out[i] = 32 + i;
    return true;
}
static uint8_t fingerprint_query(uint8_t command, uint8_t *out, unsigned size, uint16_t *received) {
    assert(command == 0xd2 && size == 17);
    *received = 39;
    out[0] = 0;
    for (unsigned i = 0; i < 16; i++) out[i + 1] = 48 + i;
    return FP_REPLY;
}
static bool fingerprint_send(const uint8_t *request, unsigned size) {
    uint8_t expected[39];
    if (unlock_fixture) {
        const char *commands[] = {
            requests[0], test_requests[0],
            "aa2400954f64f2e4e86e9eee82d202166848995ecf1290f1097718f68fd0416d720cb124de4b85",
            test_requests[3],
        };
        assert(size == sizeof(expected));
        for (unlock_command = 0; unlock_command < 4; unlock_command++) {
            hex_bytes(commands[unlock_command], expected);
            if (!memcmp(request, expected, size)) break;
        }
        assert(unlock_command < 4);
        if (unlock_command == 1) mock_templates = 1;
        if (unlock_command == 3) mock_templates = 0;
        reads = 0;
        return true;
    }
    hex_bytes(test_case < 0 ? requests[mode] : test_requests[test_case], expected);
    assert(size == sizeof(expected) && !memcmp(request, expected, size));
    return true;
}
static unsigned fingerprint_receive(uint32_t start, uint32_t timeout, uint8_t *out,
                                    uint16_t *received) {
    if (unlock_fixture) {
        if (reads++) return 0;
        const char *reply;
        switch (unlock_command) {
            case 0: reply = unlock_responses[mock_templates]; break;
            case 1: reply = test_responses[0]; break;
            case 2: reply = match_fixture < 2 ? test_responses[match_fixture + 1]
                                             : unlock_responses[match_fixture]; break;
            default: reply = test_responses[3]; break;
        }
        hex_bytes(reply, out);
        *received = strlen(reply) / 2;
        return *received;
    }
    static const unsigned test_timeouts[] = {90000, 15000, 15000, 2500};
    assert(!start && timeout == (test_case < 0 ? 1000 : test_timeouts[test_case]));
    if (reads++) return 0;
    hex_bytes(test_case < 0 ? responses[mode ^ wrong_response] : test_responses[test_case], out);
    *received = test_case == 0 ? 71 : 39;
    return *received;
}

#include "../port/fingerprint_count.inc"

typedef enum {
    FP_TEST_COUNT, FP_TEST_ENROLL, FP_TEST_MATCH, FP_TEST_REMOVE,
} FingerprintTestAction;
static void fingerprint_uart_open(void) {}
static void fingerprint_uart_close(void) {}
static void fingerprint_restart(void) {}
#include "../port/fingerprint_enroll_test.inc"

static int board_gen1_unlock(const uint8_t *pin_key, uint8_t *root) {
    gen1_calls++;
    assert(!memcmp(pin_key, test_pin, sizeof(test_pin)));
    memset(root, 0x55, 32);
    return -2;
}
#include "../port/fingerprint_unlock_test.inc"

enum { DS28_FP_CREDENTIAL_PAGE = 72, DS28_FP_METADATA_PAGE = 84,
       GEN1_READY = 2, GEN1_PIN_A = 4, GEN1_PIN_B = 5, GEN1_SALT_SIZE = 22 };
typedef struct { uint8_t phase, remaining, salt[GEN1_SALT_SIZE]; } Gen1Record;
static const uint8_t gen1_blank[32];
static uint8_t fingerprint_pages[2][32], wallet_generation = 1;
static bool fail_credential_write;

static bool ds28s60_read_protection(unsigned block, uint8_t policy[2]) {
    assert(block == 18 || block == 21);
    policy[0] = 4;
    policy[1] = 12;
    return true;
}

static uint8_t *fake_page(unsigned page) {
    assert(page == DS28_FP_CREDENTIAL_PAGE || page == DS28_FP_METADATA_PAGE);
    return fingerprint_pages[page == DS28_FP_METADATA_PAGE];
}

static bool gen1_read(const Ds28Identity *identity, unsigned page, void *out) {
    memcpy(out, fake_page(page), 32);
    return true;
}

static bool gen1_commit(const Ds28Identity *identity, unsigned page, const void *old,
                        const void *next) {
    if (page == DS28_FP_CREDENTIAL_PAGE && fail_credential_write) return false;
    assert(!memcmp(fake_page(page), old, 32));
    memcpy(fake_page(page), next, 32);
    return true;
}

static bool gen1_open(Ds28Identity *identity, Gen1Record *record) {
    memset(identity, 0, sizeof(*identity));
    *record = (Gen1Record){.phase = GEN1_READY, .remaining = 8, .salt = {wallet_generation}};
    return true;
}

#define board_fp_unlock_test_enabled persistent_test_flag
#define board_fp_unlock_test persistent_fingerprint
#include "../port/fingerprint_unlock.inc"
#undef board_fp_unlock_test
#undef board_fp_unlock_test_enabled

static unsigned enrolled_steps;
static void progress(unsigned steps, uint8_t status) {
    assert(status == 0);
    enrolled_steps = steps;
}

int main(void) {
    for (mode = 0; mode <= 1; mode++) {
        uint8_t report[28] = {0};
        fingerprint_count_probe(report, true, mode);
        assert(report[0] == FP_COUNT_OK && report[1] == 2 && report[2] == mode + 1);
        assert(report[4] == PAIRING_MATCH && report[6] == 39);
        const uint8_t *attempt = report + 8 + 10 * mode;
        assert(attempt[0] == FP_COUNT_OK && attempt[1] == 1 && attempt[3] == 39);
        assert(attempt[4] == 12 && attempt[6] == 2);
        wrong_response = true;
        reads = 0;
        memset(report, 0, sizeof(report));
        fingerprint_count_probe(report, true, mode);
        assert(report[0] == FP_COUNT_INVALID);
        wrong_response = false;
        reads = 0;
    }
    puts("PASS: A500 encrypted request and template reply fixtures in ECB and CBC");
    static const FingerprintTestAction actions[] = {
        FP_TEST_ENROLL, FP_TEST_MATCH, FP_TEST_MATCH, FP_TEST_REMOVE,
    };
    for (test_case = 0; test_case < 4; test_case++) {
        reads = 0;
        assert(board_fingerprint_test(actions[test_case], progress) == (test_case == 2 ? 0x81 : 0));
    }
    assert(enrolled_steps == 8);
    puts("PASS: slot-0 enrollment, matching, rejection and single-slot deletion packets");

    unlock_fixture = true;
    uint8_t result[64], context[32];
    assert(board_fp_unlock_test(0, NULL, result, NULL) == 0);
    assert(board_fp_unlock_test(1, test_pin, result, progress) == 0 && gen1_calls == 1);
    assert(mock_templates == 1);
    assert(board_fp_unlock_test(2, NULL, result, NULL) == -102 && gen1_calls == 1);
    match_fixture = 1;
    assert(board_fp_unlock_test(2, NULL, result, NULL) == 0x81 && gen1_calls == 1);
    match_fixture = 3;
    assert(board_fp_unlock_test(2, NULL, result, NULL) == -200 && gen1_calls == 1);
    match_fixture = 2;
    assert(board_fp_unlock_test(2, NULL, result, NULL) == 0 && gen1_calls == 2);
    for (unsigned i = 0; i < 32; i++) assert(result[i] == 0x55);
    static const uint8_t domain[] = "ForgeBox BIP39 PIN context v1";
    uint8_t message[sizeof(domain) + 32];
    memcpy(message, domain, sizeof(domain));
    memcpy(message + sizeof(domain), test_pin, 32);
    hmac_sha256_oneline(result, 32, message, sizeof(message), context);
    assert(!memcmp(context, result + 32, 32));
    assert(board_fp_unlock_test(3, NULL, result, NULL) == 0 && mock_templates == 0);
    assert(board_fp_unlock_test(2, NULL, result, NULL) == -204 && gen1_calls == 2);
    puts("PASS: key-bearing match unwraps the Gen 1 credential, rejects mismatch, and cleans up");

    mock_templates = 1;
    assert(persistent_fingerprint(4, NULL, result, NULL) == 1);
    assert(persistent_fingerprint(1, test_pin, result, progress) == -205);
    assert(gen1_calls == 2 && mock_templates == 1);
    assert(persistent_fingerprint(5, NULL, result, progress) == -204);
    assert(gen1_calls == 2 && mock_templates == 1);
    assert(persistent_fingerprint(5, test_pin, result, progress) == 0);
    assert(mock_templates == 1 && gen1_calls == 3);
    assert(persistent_fingerprint(3, NULL, result, NULL) == 0 && mock_templates == 0);
    gen1_calls = 2;

    assert(persistent_fingerprint(0, NULL, result, NULL) == FP_EMPTY);
    fingerprint_pages[0][0] = 0x41;
    assert(persistent_fingerprint(1, test_pin, result, progress) == -204 && gen1_calls == 2);
    fingerprint_pages[0][0] = 0;
    fail_credential_write = true;
    assert(persistent_fingerprint(1, test_pin, result, progress) == -204 && gen1_calls == 3);
    assert(persistent_fingerprint(0, NULL, result, NULL) == FP_PENDING);
    assert(persistent_fingerprint(2, NULL, result, NULL) == -204);
    fail_credential_write = false;
    assert(persistent_fingerprint(3, NULL, result, NULL) == 0 && !mock_templates);
    assert(persistent_fingerprint(1, test_pin, result, progress) == 0 && gen1_calls == 4);
    assert(persistent_fingerprint(0, NULL, result, NULL) == FP_ACTIVE);
    assert(!fp_envelope_ready);  // Only the mocked DS28S60 pages survive a restart.
    match_fixture = 1;
    assert(persistent_fingerprint(2, NULL, result, NULL) == 0x81 && gen1_calls == 4);
    match_fixture = 2;
    assert(persistent_fingerprint(2, NULL, result, NULL) == 0 && gen1_calls == 5);
    assert(!memcmp(context, result + 32, 32));
    fingerprint_pages[0][0] ^= 1;
    assert(persistent_fingerprint(2, NULL, result, NULL) == -200 && gen1_calls == 5);
    fingerprint_pages[0][0] ^= 1;
    wallet_generation++;
    assert(persistent_fingerprint(2, NULL, result, NULL) == -200 && gen1_calls == 5);
    Ds28Identity identity = {0};
    fingerprint_wallet_wipe(&identity);
    assert(!mock_templates && persistent_fingerprint(0, NULL, result, NULL) == FP_EMPTY);
    puts("PASS: persistent enroll, interrupted enrollment, occupied page, cold unlock, wrong finger, tamper, wallet binding and wipe");
}
