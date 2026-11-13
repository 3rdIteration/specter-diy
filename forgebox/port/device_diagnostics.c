#include <string.h>
#include "device_diagnostics.h"
#include "pin_flash.h"
#include "mhscpu.h"
#include "drv_i2c_io.h"
#include "drv_spi_io.h"
#include "hardware_version.h"
#include "cmsis_os.h"
#include "user_delay.h"
#if defined(FORGEBOX_SE_PAIRING) || defined(FORGEBOX_GEN1_STORAGE) || defined(FORGEBOX_BOOT_PROBE)
#include "board_api.h"
#include "hmac.h"
#include "mhscpu_otp.h"
#include "sha2.h"
#endif
#if defined(FORGEBOX_BOOT_PROBE) || defined(FORGEBOX_FP_UNLOCK_TEST) || defined(FORGEBOX_FP_UNLOCK)
#include "mbedtls/aes.h"
#endif

static void pin_mode(GPIO_TypeDef *port, uint16_t pin, GPIO_ModeTypeDef mode) {
    GPIO_InitTypeDef config = {
        .GPIO_Mode = mode, .GPIO_Pin = pin, .GPIO_Remap = GPIO_Remap_1,
    };
    GPIO_Init(port, &config);
}

static const SPIIO_Cfg_t ds_spi = {
        .MISO_PORT = GPIOD, .MISO_PIN = GPIO_Pin_11,
        .MOSI_PORT = GPIOD, .MOSI_PIN = GPIO_Pin_10,
        .CLK_PORT = GPIOD, .CLK_PIN = GPIO_Pin_8,
};

static void ds28s60_open(void) {
    GPIO_SetBits(GPIOD, GPIO_Pin_9);
    pin_mode(GPIOD, GPIO_Pin_9, GPIO_Mode_Out_PP);
    SpiIoInit(&ds_spi);
    GPIO_SetBits(GPIOG, GPIO_Pin_0);
    pin_mode(GPIOG, GPIO_Pin_0, GPIO_Mode_Out_PP);
    osDelay(10);
}

static bool ds28s60_exchange(const uint8_t *command, unsigned command_size,
                             uint8_t *out, unsigned size, unsigned polls,
                             uint8_t *status) {
#if defined(FORGEBOX_SE_PAIRING) || defined(FORGEBOX_GEN1_STORAGE) || defined(FORGEBOX_BOOT_PROBE)
    // Match the retail driver's 50 ms polling interval and leave CS high
    // between commands. Back-to-back protection reads were intermittent.
    UserDelayUs(100);
#endif
    GPIO_ResetBits(GPIOD, GPIO_Pin_9);
    SpiIoSendData(&ds_spi, command, command_size);
    uint8_t length = 0;
    for (unsigned attempt = 0; attempt < polls; attempt++) {
#if defined(FORGEBOX_SE_PAIRING) || defined(FORGEBOX_GEN1_STORAGE) || defined(FORGEBOX_BOOT_PROBE)
        osDelay(50);
#else
        osDelay(10);
#endif
        SpiIoRecvData(&ds_spi, &length, 1);
        if (length != 0 && length != 0xff) break;
    }
    *status = 0xff;
    if (length != 0 && length != 0xff) SpiIoRecvData(&ds_spi, status, 1);
    bool success = length == size + 1 && *status == 0xaa;
    if (success && size) SpiIoRecvData(&ds_spi, out, size);
    GPIO_SetBits(GPIOD, GPIO_Pin_9);
    return success;
}

static bool ds28s60_read_protection(uint8_t block, uint8_t *protection) {
    const uint8_t command[] = {0xaa, 1, block};
    uint8_t status;
    return ds28s60_exchange(command, sizeof(command), protection, 2, 5, &status);
}

static uint16_t atecc_crc(const uint8_t *data, unsigned count) {
    uint16_t crc = 0;
    for (unsigned i = 0; i < count; i++) {
        for (unsigned bit = 1; bit <= 0x80; bit <<= 1) {
            bool feedback = !!(crc & 0x8000) != !!(data[i] & bit);
            crc <<= 1;
            if (feedback) crc ^= 0x8005;
        }
    }
    return crc;
}

#define ATECC_ADDRESS 0x60
static bool atecc_wake(const I2CIO_Cfg_t *bus) {
    // Transient wake pulse.
    GPIO_ResetBits(bus->SDA_PORT, bus->SDA_PIN);
    UserDelayUs(90);
    GPIO_SetBits(bus->SDA_PORT, bus->SDA_PIN);
    osDelay(3);
    uint8_t wake[4];
    const uint8_t expected[] = {4, 0x11, 0x33, 0x43};
    return I2CIO_ReceiveData(bus, ATECC_ADDRESS, wake, sizeof(wake)) == 0 &&
           !memcmp(wake, expected, sizeof(wake));
}

static bool atecc_open(I2CIO_Cfg_t *bus) {
    const bool evt0 = GetHardwareVersion() == VERSION_EVT0;
    uint16_t scl = evt0 ? GPIO_Pin_11 : GPIO_Pin_12;
    uint16_t sda = evt0 ? GPIO_Pin_12 : GPIO_Pin_11;
    I2CIO_Init(bus, GPIOG, scl, GPIOG, sda);
    return atecc_wake(bus);
}

static void atecc_close(const I2CIO_Cfg_t *bus) {
    const uint8_t sleep = 0x01;
    I2CIO_SendData(bus, ATECC_ADDRESS, &sleep, 1);
}

static bool atecc_exchange(const I2CIO_Cfg_t *bus, uint8_t opcode, uint8_t mode,
                           uint8_t address, const uint8_t *input, unsigned input_size,
                           uint8_t *out, unsigned size, unsigned wait_ms) {
    uint8_t command[85] = {0x03, input_size + 7, opcode, mode, address, 0};
    uint8_t response[67];  // An encrypted KDF reply is 64 bytes plus framing.
    if (input_size > sizeof(command) - 8 || size > sizeof(response) - 3) return false;
#if defined(FORGEBOX_SE_COMBINED_TEST) || defined(FORGEBOX_GEN1_STORAGE)
    // Refresh the watchdog window between commands as CryptoAuthLib does.
    // Idle retains the volatile state needed by the authenticated sequence.
    const uint8_t idle = 0x02;
    if (I2CIO_SendData(bus, ATECC_ADDRESS, &idle, 1) || !atecc_wake(bus)) return false;
#endif
    if (input_size) memcpy(command + 6, input, input_size);
    uint16_t crc = atecc_crc(command + 1, input_size + 5);
    command[input_size + 6] = crc;
    command[input_size + 7] = crc >> 8;
    if (I2CIO_SendData(bus, ATECC_ADDRESS, command, input_size + 8))
        return false;
    osDelay(wait_ms);
    if (I2CIO_ReceiveData(bus, ATECC_ADDRESS, response, size + 3) ||
        response[0] != size + 3 ||
        atecc_crc(response, size + 1) != (response[size + 1] | response[size + 2] << 8))
        return false;
    memcpy(out, response + 1, size);
    return true;
}

static bool atecc_query(const I2CIO_Cfg_t *bus, uint8_t opcode, uint8_t mode,
                        uint8_t address, uint8_t *out, unsigned size) {
    return atecc_exchange(bus, opcode, mode, address, NULL, 0, out, size, 12);
}

#if defined(FORGEBOX_SE_PAIRING) || defined(FORGEBOX_GEN1_STORAGE) || defined(FORGEBOX_BOOT_PROBE)
// Keystone retail OTP layout. This code only enables the OTP read window.
#define OTP_ATECC_AUTH_KEY 0x40009560u
#define OTP_ATECC_ENCRYPT_KEY 0x40009580u
#define OTP_DS28_BINDING   0x400095a0u
#define ATECC_NONCE       0x16
#define NONCE_NO_SEED_UPDATE 0x01
#define ATECC_CHECKMAC    0x28
#define CHECKMAC_RANDOM_TEMPKEY 0x01
#define DS28_BINDING_PAGE 91
#define DS28_COMPUTE_SECRET 0x3c  // Derivation input only. Never sent to the chip.
#define DS28_PAGE_AUTH 0xa5

static void clear_bytes(void *data, unsigned size) {
    volatile uint8_t *bytes = data;
    while (size--) *bytes++ = 0;
}

static bool plausible_otp_key(const uint8_t *key) {
    unsigned differences = 0;
    for (unsigned i = 1; i < 32; i++) differences |= key[i] ^ key[0];
    return differences != 0;
}

// 0 = unavailable, 1 = all zero, 2 = all FF, 3 = other data.
static uint8_t classify_block(const uint8_t *data) {
    unsigned any_set = 0, any_clear = 0;
    for (unsigned i = 0; i < 32; i++) {
        any_set |= data[i];
        any_clear |= data[i] ^ 0xff;
    }
    return !any_set ? 1 : !any_clear ? 2 : 3;
}

static PairingStatus atecc_tempkey(const I2CIO_Cfg_t *bus, uint8_t *out) {
    uint8_t nonce[55];
    PairingStatus result = PAIRING_RNG;
    if (board_random(nonce + 32, 20)) {
        result = PAIRING_CHALLENGE;
        if (atecc_exchange(bus, ATECC_NONCE, NONCE_NO_SEED_UPDATE, 0,
                           nonce + 32, 20, nonce, 32, 60)) {
            nonce[52] = ATECC_NONCE;
            nonce[53] = NONCE_NO_SEED_UPDATE;
            nonce[54] = 0;
            sha256_Raw(nonce, sizeof(nonce), out);
            result = PAIRING_MATCH;
        }
    }
    clear_bytes(nonce, sizeof(nonce));
    return result;
}

static PairingStatus atecc_check_key(const I2CIO_Cfg_t *bus, const uint8_t *config,
                                     uint8_t slot, const uint8_t *key, uint8_t *status) {
    PairingStatus result = PAIRING_RNG;
    uint8_t msg[88] = {0};
    uint8_t checkmac[77] = {0};
    *status = 0xff;
    memcpy(msg, key, 32);
    if (!board_random(checkmac + 64, 13)) goto done;
    result = atecc_tempkey(bus, msg + 32);
    if (result != PAIRING_MATCH) goto done;

    // CheckMac message layout from CryptoAuthLib atcah_check_mac.
    const uint8_t *other = checkmac + 64;
    memcpy(msg + 64, other, 4);
    memcpy(msg + 76, other + 4, 3);
    msg[79] = config[12];  // SN[8]
    memcpy(msg + 80, other + 7, 4);
    memcpy(msg + 84, config, 2);  // SN[0:2]
    memcpy(msg + 86, other + 11, 2);
    sha256_Raw(msg, sizeof(msg), checkmac + 32);
    result = PAIRING_REPLY;
    if (atecc_exchange(bus, ATECC_CHECKMAC, CHECKMAC_RANDOM_TEMPKEY, slot,
                       checkmac, sizeof(checkmac), status, 1, 60)) {
        if (*status == 0) result = PAIRING_MATCH;
        else if (*status == 1) result = PAIRING_REJECTED;
    }
done:
    clear_bytes(msg, sizeof(msg));
    clear_bytes(checkmac, sizeof(checkmac));
    return result;
}

static PairingStatus atecc_check_pairing(const I2CIO_Cfg_t *bus, const uint8_t *config,
                                        uint8_t *status) {
    // Locked zones and retail slot 1 policy (secret, random nonce, no LimitedUse).
    if (config[6] != 0x60 || config[86] != 0 || config[87] != 0 ||
        config[22] != 0x80 || config[23] != 0x80 ||
        config[98] != 0x7c || config[99] != 0)
        return PAIRING_POLICY;
    uint8_t key[32];
    OTP_PowerOn();
    memcpy(key, (const void *)OTP_ATECC_AUTH_KEY, sizeof(key));
    PairingStatus result = plausible_otp_key(key)
        ? atecc_check_key(bus, config, 1, key, status) : PAIRING_OTP;
    clear_bytes(key, sizeof(key));
    return result;
}

static PairingStatus atecc_transport_mask(const I2CIO_Cfg_t *bus, const uint8_t *config,
                                          uint8_t *mask) {
    const uint8_t gendig[] = {0x15, 2, 2, 0};  // GenDig, data zone, slot 2
    uint8_t transcript[96] = {0}, status;
    PairingStatus result = PAIRING_OTP;
    memcpy(transcript, (const void *)OTP_ATECC_ENCRYPT_KEY, 32);
    if (!plausible_otp_key(transcript)) goto done;
    result = atecc_tempkey(bus, transcript + 64);
    if (result != PAIRING_MATCH) goto done;
    result = PAIRING_GENDIG;
    if (!atecc_exchange(bus, gendig[0], gendig[1], gendig[2],
                        gendig, sizeof(gendig), &status, 1, 60) || status != 0) goto done;
    memcpy(transcript + 32, gendig, sizeof(gendig));
    transcript[36] = config[12];  // SN[8]
    memcpy(transcript + 37, config, 2);  // SN[0:2]
    sha256_Raw(transcript, sizeof(transcript), mask);
    result = PAIRING_MATCH;
done:
    clear_bytes(transcript, sizeof(transcript));
    return result;
}

static PairingStatus atecc_read_scratch(const I2CIO_Cfg_t *bus, const uint8_t *config,
                                        uint8_t slot, uint8_t *data) {
    if ((slot != 8 && slot != 13) ||
        config[18] != 0 || config[24] != 0x80 || config[25] != 0x80 ||
        config[100] != 0xfc || config[101] != 0x01 ||
        config[20 + 2 * slot] != 0xc2 || config[21 + 2 * slot] != 0x42 ||
        config[96 + 2 * slot] != 0x5c || config[97 + 2 * slot] != 0) return PAIRING_POLICY;
    uint8_t mask[32];
    PairingStatus result = atecc_transport_mask(bus, config, mask);
    if (result == PAIRING_MATCH) {
        if (atecc_query(bus, 0x02, 0x82, slot << 3, data, 32)) {
            for (unsigned i = 0; i < 32; i++) data[i] ^= mask[i];
        } else result = PAIRING_ENCRYPTED_READ;
    }
    clear_bytes(mask, sizeof(mask));
    return result;
}

static PairingStatus atecc_probe_scratch(I2CIO_Cfg_t *bus, const uint8_t *config, uint8_t slot,
                                         uint8_t *blank_state) {
    uint8_t blocks[2][32];
    PairingStatus result = PAIRING_NO_CHIP;
    for (unsigned i = 0; i < 2; i++) {
        // Reopen and reauthenticate for an independent encryption nonce.
        if (!atecc_open(bus)) {
            result = PAIRING_NO_CHIP;
            break;
        }
        uint8_t status;
        result = atecc_check_pairing(bus, config, &status);
        if (result == PAIRING_MATCH) result = atecc_read_scratch(bus, config, slot, blocks[i]);
        atecc_close(bus);
        if (result != PAIRING_MATCH) break;
    }
    if (result == PAIRING_MATCH) {
        if (memcmp(blocks[0], blocks[1], sizeof(blocks[0]))) result = PAIRING_INCONSISTENT;
        else *blank_state = classify_block(blocks[0]);
    }
    clear_bytes(blocks, sizeof(blocks));
    return result;
}

#if defined(FORGEBOX_SE_RESET_TEST) || defined(FORGEBOX_GEN1_STORAGE)
static bool atecc_encrypted_kdf(const I2CIO_Cfg_t *bus, uint8_t slot,
                                 const uint8_t *input, uint8_t *out) {
    uint8_t command[36] = {2, 0, 0, 32};  // HKDF details: input, 32 bytes.
    uint8_t response[64], io[48], mask[32];
    memcpy(command + 4, input, 32);
    memcpy(io, (const void *)0x40009540u, 32);  // Factory I/O protection key.
    bool success = plausible_otp_key(io) &&
        atecc_exchange(bus, 0x56, 0x56, slot, command, sizeof(command),
                       response, sizeof(response), 100);
    if (success) {
        memcpy(io + 32, response + 32, 16);
        sha256_Raw(io, sizeof(io), mask);
        for (unsigned i = 0; i < 32; i++) out[i] = response[i] ^ mask[i];
    }
    clear_bytes(command, sizeof(command));
    clear_bytes(response, sizeof(response));
    clear_bytes(io, sizeof(io));
    clear_bytes(mask, sizeof(mask));
    return success;
}

static uint8_t atecc_check_encrypted_kdf(I2CIO_Cfg_t *bus, const uint8_t *config) {
    uint8_t key[32], tempkey[32], expected[32], response[32], input[32], status;
    uint8_t result = 2;
    if (!atecc_open(bus)) return result;
    if (atecc_check_pairing(bus, config, &status) != PAIRING_MATCH ||
        atecc_read_scratch(bus, config, 8, key) != PAIRING_MATCH ||
        !board_random(input, sizeof(input)) ||
        atecc_tempkey(bus, tempkey) != PAIRING_MATCH ||
        !atecc_encrypted_kdf(bus, 8, input, response)) goto done;
    hmac_sha256_oneline(key, sizeof(key), input, sizeof(input), expected);
    unsigned different = 0;
    for (unsigned i = 0; i < 32; i++) different |= expected[i] ^ response[i];
    result = different == 0 ? 1 : 3;
done:
    atecc_close(bus);
    clear_bytes(key, sizeof(key));
    clear_bytes(tempkey, sizeof(tempkey));
    clear_bytes(expected, sizeof(expected));
    clear_bytes(response, sizeof(response));
    clear_bytes(input, sizeof(input));
    return result;
}

#if defined(FORGEBOX_SE_COMBINED_TEST) || defined(FORGEBOX_GEN1_STORAGE)
enum { ACCOUNT_AUTH_SLOT = 10, ACCOUNT_ROLL_SLOT = 11, ACCOUNT_HOST_SLOT = 12 };
static uint8_t account_config[128], account_stage, account_status = 0xff;
static bool account_config_ready;
#ifdef FORGEBOX_SE_COMBINED_TEST
static uint32_t account_counter_before[2];
static bool account_counter_baseline;
static bool account_auth_rewrite_reproduced;
#endif

static bool atecc_account_policy(const uint8_t *cfg) {
    return cfg[18] == 0 && cfg[86] == 0 && cfg[87] == 0 &&
        (cfg[89] & 0x1c) == 0x1c && cfg[90] == 2 && cfg[91] == 4 &&
        cfg[24] == 0x80 && cfg[25] == 0x80 && cfg[100] == 0xfc && cfg[101] == 1 &&
        cfg[40] == 0xa0 && cfg[41] == 0x42 && cfg[116] == 0x5c && cfg[117] == 0 &&
        cfg[42] == 0xa0 && cfg[43] == 0x20 && cfg[118] == 0xdc && cfg[119] == 0x0a &&
        cfg[44] == 0xa0 && cfg[45] == 0x42 && cfg[120] == 0xdc && cfg[121] == 0x0a;
}

static void atecc_account_pin(const uint8_t *pin, unsigned length, const char *domain, uint8_t *out) {
    uint8_t salt[32], digest[32];
    OTP_PowerOn();
    memcpy(salt, (const void *)0x40009500u, sizeof(salt));
    // Factory OTP salt is read-only. The wallet backend supplies a separate
    // randomly salted, stretched PIN key even on boards with a blank OTP salt.
    hmac_sha256_oneline(salt, sizeof(salt), pin, length, digest);
    hmac_sha256_oneline((const uint8_t *)domain, strlen(domain), digest, sizeof(digest), out);
    clear_bytes(salt, sizeof(salt));
    clear_bytes(digest, sizeof(digest));
}

static bool atecc_account_write_once(uint8_t slot, const uint8_t *key) {
    if (!account_config_ready || (slot != ACCOUNT_AUTH_SLOT && slot != ACCOUNT_HOST_SLOT)) return false;
    const uint8_t address = slot << 3;
    const uint8_t mode = 0xc2;  // Data, 32 bytes, encrypted.
    I2CIO_Cfg_t bus;
    uint8_t payload[64], transcript[96] = {0};
    bool accepted = false;
    account_status = 0xff;
    if (!atecc_open(&bus)) return false;
    if (atecc_check_pairing(&bus, account_config, &account_status) != PAIRING_MATCH ||
        atecc_transport_mask(&bus, account_config, transcript) != PAIRING_MATCH) goto done;
    for (unsigned i = 0; i < 32; i++) payload[i] = key[i] ^ transcript[i];
    transcript[32] = 0x12;
    transcript[33] = mode;
    transcript[34] = address;
    transcript[36] = account_config[12];
    memcpy(transcript + 37, account_config, 2);
    memcpy(transcript + 64, key, 32);
    sha256_Raw(transcript, sizeof(transcript), payload + 32);
    account_status = 0xff;
    accepted = atecc_exchange(&bus, 0x12, mode, address, payload, sizeof(payload),
                              &account_status, 1, 100) && account_status == 0;
done:
    atecc_close(&bus);
    clear_bytes(payload, sizeof(payload));
    clear_bytes(transcript, sizeof(transcript));
    return accepted;
}

static bool atecc_account_write(uint8_t slot, const uint8_t *key) {
    if (atecc_account_write_once(slot, key)) return true;
    // A CRC-valid 0xee means the command was refused before execution.
    // Rebuild the complete session once. Never retry an ambiguous write reply.
    return account_status == 0xee && atecc_account_write_once(slot, key);
}

static bool atecc_account_roll(const uint8_t *auth) {
    I2CIO_Cfg_t bus;
    uint8_t tempkey[32];
    bool accepted = false;
    account_status = 0xff;
    if (!atecc_open(&bus)) return false;
    if (atecc_check_key(&bus, account_config, ACCOUNT_AUTH_SLOT, auth, &account_status) == PAIRING_MATCH &&
        atecc_tempkey(&bus, tempkey) == PAIRING_MATCH) {
        account_status = 0xff;
        accepted = atecc_exchange(&bus, 0x1c, 0, ACCOUNT_ROLL_SLOT, NULL, 0,
                                  &account_status, 1, 100) && account_status == 0;
    }
    atecc_close(&bus);
    clear_bytes(tempkey, sizeof(tempkey));
    return accepted;
}

static bool atecc_account_enroll_input(const uint8_t *pin, unsigned length) {
    uint8_t auth[32], host_key[32];
    bool enrolled = false;
    if (!account_config_ready || !board_random(host_key, sizeof(host_key))) goto done;
    atecc_account_pin(pin, length, "auth_key", auth);
    account_stage = 2;
    if (!atecc_account_write(ACCOUNT_AUTH_SLOT, auth)) goto done;
    account_stage = 3;
    if (!atecc_account_roll(auth)) goto done;
    account_stage = 4;
    enrolled = atecc_account_write(ACCOUNT_HOST_SLOT, host_key);
done:
    clear_bytes(auth, sizeof(auth));
    clear_bytes(host_key, sizeof(host_key));
    return enrolled;
}

static PairingStatus atecc_account_kdf_inputs(const uint8_t *auth_pin, const uint8_t *input_pin,
                                               unsigned length, uint8_t *out) {
    I2CIO_Cfg_t bus;
    uint8_t auth[32], input[32], intermediate[32];
    PairingStatus result = PAIRING_NO_CHIP;
    if (!account_config_ready) return PAIRING_POLICY;
    atecc_account_pin(auth_pin, length, "auth_key", auth);
    atecc_account_pin(input_pin, length, "password_atecc608b", input);
    account_status = 0xff;
    account_stage = 5;
    if (!atecc_open(&bus)) goto done;
    result = atecc_check_key(&bus, account_config, ACCOUNT_AUTH_SLOT, auth, &account_status);
    if (result != PAIRING_MATCH) goto close;
    account_stage = 6;
    if (!atecc_encrypted_kdf(&bus, ACCOUNT_ROLL_SLOT, input, intermediate)) {
        result = PAIRING_REPLY;
        goto close;
    }
    account_stage = 7;
    result = atecc_check_key(&bus, account_config, ACCOUNT_AUTH_SLOT, auth, &account_status);
    if (result != PAIRING_MATCH) {
        // The first authorization succeeded. A later failure is a transport/test error.
        result = PAIRING_REPLY;
        goto close;
    }
    account_stage = 8;
    if (!atecc_encrypted_kdf(&bus, ACCOUNT_HOST_SLOT, intermediate, out)) {
        result = PAIRING_REPLY;
        goto close;
    }
    for (unsigned i = 0; i < 3; i++) {
        sha256_Raw(out, 32, input);
        memcpy(out, input, 32);
    }
close:
    atecc_close(&bus);
done:
    clear_bytes(auth, sizeof(auth));
    clear_bytes(input, sizeof(input));
    clear_bytes(intermediate, sizeof(intermediate));
    return result;
}

#ifdef FORGEBOX_SE_COMBINED_TEST
static bool atecc_account_enroll(const char *pin) {
    return atecc_account_enroll_input((const uint8_t *)pin, 6);
}

static PairingStatus atecc_account_kdf(const char *pin, uint8_t *out) {
    return atecc_account_kdf_inputs((const uint8_t *)pin, (const uint8_t *)pin, 6, out);
}

static bool atecc_account_test_auth_rewrite(const char *pin) {
    const char replacement_pin[] = "749306";
    uint8_t original[32], after[32], auth[32];
    bool restored = false;
    if (atecc_account_kdf(pin, original) != PAIRING_MATCH) goto done;
    atecc_account_pin((const uint8_t *)replacement_pin, 6, "auth_key", auth);
    account_stage = 11;
    if (atecc_account_write(ACCOUNT_AUTH_SLOT, auth) &&
        atecc_account_kdf_inputs((const uint8_t *)replacement_pin, (const uint8_t *)pin, 6, after) == PAIRING_MATCH)
        account_auth_rewrite_reproduced = !memcmp(original, after, sizeof(original));
    // Restore the fixture credential even if the replacement write lost its ACK.
    atecc_account_pin((const uint8_t *)pin, 6, "auth_key", auth);
    account_stage = 12;
    restored = atecc_account_write(ACCOUNT_AUTH_SLOT, auth);
done:
    clear_bytes(original, sizeof(original));
    clear_bytes(after, sizeof(after));
    clear_bytes(auth, sizeof(auth));
    return restored;
}

static void atecc_account_checkpoint(uint8_t *out) {
    for (unsigned i = 0; i < 2; i++)
        for (unsigned byte = 0; byte < 3; byte++)
            out[i * 3 + byte] = account_counter_before[i] >> (byte * 8);
    out[6] = account_auth_rewrite_reproduced;
}

static void atecc_account_restore_checkpoint(const uint8_t *in) {
    for (unsigned i = 0; i < 2; i++)
        account_counter_before[i] = in[i * 3] | ((uint32_t)in[i * 3 + 1] << 8) |
                                    ((uint32_t)in[i * 3 + 2] << 16);
    account_counter_baseline = true;
    account_auth_rewrite_reproduced = in[6] == 1;
}
#endif
#endif
#endif

typedef struct {
    uint8_t key[32];
    uint8_t rom_id[8];
    uint8_t manufacturer[2];
} Ds28Identity;

static PairingStatus ds28s60_load_identity(Ds28Identity *identity, uint8_t *status) {
    const uint8_t rom_command[] = {0x44, 1, 116};
    uint8_t otp[96], rom[32], msg[76];
    PairingStatus result = PAIRING_OTP;
    OTP_PowerOn();
    memcpy(otp, (const void *)OTP_DS28_BINDING, sizeof(otp));
    if (!plausible_otp_key(otp) || !plausible_otp_key(otp + 32) ||
        !plausible_otp_key(otp + 64)) goto done;

    result = PAIRING_IDENTITY;
    if (!ds28s60_exchange(rom_command, sizeof(rom_command), rom, sizeof(rom), 20, status))
        goto done;
    memcpy(identity->rom_id, rom + 24, 8);
    memcpy(identity->manufacturer, rom + 22, 2);
    memcpy(msg, identity->rom_id, 8);
    memcpy(msg + 8, otp + 32, 64); // binding page and partial secret
    msg[72] = DS28_BINDING_PAGE;
    memcpy(msg + 73, identity->manufacturer, 2);
    msg[75] = DS28_COMPUTE_SECRET;
    hmac_sha256_oneline(otp, 32, msg, sizeof(msg), identity->key);
    result = PAIRING_MATCH;
done:
    clear_bytes(otp, sizeof(otp));
    clear_bytes(rom, sizeof(rom));
    clear_bytes(msg, sizeof(msg));
    return result;
}

static PairingStatus ds28s60_authenticated_read(const Ds28Identity *identity, uint8_t page,
                                                uint8_t *status, uint8_t *data) {
    const uint8_t read_command[] = {0x4b, 1, page};
    uint8_t auth_command[36] = {DS28_PAGE_AUTH, 34, page, 0};  // Secret A
    uint8_t msg[76], read_data[40], expected[32], actual[32];

    PairingStatus result = PAIRING_RNG;
    *status = 0xff;
    if (!board_random(auth_command + 4, 32)) goto done;
    result = PAIRING_ENCRYPTED_READ;
    if (!ds28s60_exchange(read_command, sizeof(read_command), read_data,
                          sizeof(read_data), 20, status)) goto done;
    uint8_t read_msg[20];
    memcpy(read_msg, read_data, 8);       // encrypted-read nonce
    memcpy(read_msg + 8, identity->rom_id, 8);
    read_msg[16] = page;
    memcpy(read_msg + 17, identity->manufacturer, 2);
    read_msg[19] = read_command[0];
    hmac_sha256_oneline(identity->key, sizeof(identity->key), read_msg, sizeof(read_msg), expected);
    memcpy(msg, identity->rom_id, 8);
    for (unsigned i = 0; i < 32; i++) msg[8 + i] = read_data[8 + i] ^ expected[i];
    memcpy(msg + 40, auth_command + 4, 32);
    msg[72] = page;
    memcpy(msg + 73, identity->manufacturer, 2);
    msg[75] = DS28_PAGE_AUTH;
    result = PAIRING_CHALLENGE;
    if (!ds28s60_exchange(auth_command, sizeof(auth_command), actual,
                          sizeof(actual), 20, status)) goto done;
    hmac_sha256_oneline(identity->key, sizeof(identity->key), msg, sizeof(msg), expected);
    unsigned difference = 0;
    for (unsigned i = 0; i < sizeof(expected); i++) difference |= actual[i] ^ expected[i];
    result = difference == 0 ? PAIRING_MATCH : PAIRING_REJECTED;
    if (result == PAIRING_MATCH) memcpy(data, msg + 8, 32);
done:
    clear_bytes(msg, sizeof(msg));
    clear_bytes(read_data, sizeof(read_data));
    clear_bytes(expected, sizeof(expected));
    clear_bytes(actual, sizeof(actual));
    clear_bytes(auth_command, sizeof(auth_command));
    return result;
}

static PairingStatus ds28s60_check_pairing(const uint8_t *protection, uint8_t page,
                                           uint8_t *status, uint8_t *blank_state) {
    if (protection && ((protection[0] & 0x7f) != 0x04 || protection[1] != 0x0c))
        return PAIRING_POLICY;
    Ds28Identity identity;
    uint8_t data[32];
    PairingStatus result = ds28s60_load_identity(&identity, status);
    if (result == PAIRING_MATCH)
        result = ds28s60_authenticated_read(&identity, page, status, data);
    if (result == PAIRING_MATCH && blank_state) *blank_state = classify_block(data);
    clear_bytes(&identity, sizeof(identity));
    clear_bytes(data, sizeof(data));
    return result;
}

#if defined(FORGEBOX_SE_STORAGE_WRITE) || defined(FORGEBOX_GEN1_STORAGE)
#define DS28_TEST_PAGE 5
#define DS28_RESET_SECRET_PAGE 11
#define DS28_PIN_WRAP_PAGE 44
#define DS28_FP_CREDENTIAL_PAGE 72
#define DS28_FP_METADATA_PAGE 84
#define DS28_AUTH_WRITE 0xb4

static bool ds28s60_attempt_test_write(const Ds28Identity *identity, uint8_t page, const uint8_t *old_data,
                                       const uint8_t *new_data) {
    // Keystone's Authenticated Write frame: page, ciphertext, MAC, nonce.
    if (page != DS28_TEST_PAGE
#if defined(FORGEBOX_SE_RESET_TEST) || defined(FORGEBOX_GEN1_STORAGE)
        && page != DS28_RESET_SECRET_PAGE
#endif
#ifdef FORGEBOX_GEN1_STORAGE
        && (page < DS28_PIN_WRAP_PAGE || page >= DS28_PIN_WRAP_PAGE + 4)
#endif
#ifdef FORGEBOX_FP_UNLOCK
        && page != DS28_FP_CREDENTIAL_PAGE && page != DS28_FP_METADATA_PAGE
#endif
        ) return false;
    uint8_t command[75] = {DS28_AUTH_WRITE, 73, page};
    uint8_t mask_input[20], msg[76], mask[32], status;
    if (!board_random(command + 67, 8)) return false;
    memcpy(mask_input, command + 67, 8);
    memcpy(mask_input + 8, identity->rom_id, 8);
    mask_input[16] = page;
    memcpy(mask_input + 17, identity->manufacturer, 2);
    mask_input[19] = DS28_AUTH_WRITE;
    hmac_sha256_oneline(identity->key, sizeof(identity->key), mask_input, sizeof(mask_input), mask);
    for (unsigned i = 0; i < 32; i++) command[3 + i] = new_data[i] ^ mask[i];
    memcpy(msg, identity->rom_id, 8);
    memcpy(msg + 8, old_data, 32);
    memcpy(msg + 40, new_data, 32);
    msg[72] = page;
    memcpy(msg + 73, identity->manufacturer, 2);
    msg[75] = DS28_AUTH_WRITE;
    hmac_sha256_oneline(identity->key, sizeof(identity->key), msg, sizeof(msg), command + 35);
    ds28s60_exchange(command, sizeof(command), NULL, 0, 20, &status);
    clear_bytes(mask, sizeof(mask));
    clear_bytes(msg, sizeof(msg));
    clear_bytes(command, sizeof(command));
    return true;
}

#ifdef FORGEBOX_SE_STORAGE_WRITE
static Page5TestStatus page5_test(Page5TestStatus requested) {
    static const uint8_t zeros[32] = {0};
    static const uint8_t marker_domain[] = "ForgeBox DS28S60 page 5 test v1";
    uint8_t policy[2], status, data[32], marker[32], readback[32];
    Ds28Identity identity;
    Page5TestStatus result = PAGE5_POLICY_FAILED;
    if (!ds28s60_read_protection(1, policy) || policy[0] != 0x04 || policy[1] != 0x0c)
        goto done;
    result = PAGE5_READ_FAILED;
    if (ds28s60_load_identity(&identity, &status) != PAIRING_MATCH ||
        ds28s60_authenticated_read(&identity, DS28_TEST_PAGE, &status, data) != PAIRING_MATCH)
        goto done;
    hmac_sha256_oneline(identity.key, sizeof(identity.key),
                        marker_domain, sizeof(marker_domain) - 1, marker);
    if (!memcmp(data, zeros, sizeof(data))) result = PAGE5_BLANK;
    else if (!memcmp(data, marker, sizeof(data))) result = PAGE5_MARKER_PRESENT;
    else result = PAGE5_OCCUPIED;
    if (requested == PAGE5_INSPECT) goto done;
    if (requested != result || (result != PAGE5_BLANK && result != PAGE5_MARKER_PRESENT)) {
        result = PAGE5_UNEXPECTED_CONTENTS;
        goto done;
    }
    const uint8_t *replacement = result == PAGE5_BLANK ? marker : zeros;
    Page5TestStatus verified = result == PAGE5_BLANK ? PAGE5_WRITE_VERIFIED : PAGE5_RESTORE_VERIFIED;
    if (!ds28s60_attempt_test_write(&identity, DS28_TEST_PAGE, data, replacement)) {
        result = PAGE5_RANDOM_FAILED;
        goto done;
    }
    // Read back even after an ambiguous SPI result. EEPROM may have committed.
    result = PAGE5_READBACK_FAILED;
    if (ds28s60_authenticated_read(&identity, DS28_TEST_PAGE, &status, readback) != PAIRING_MATCH)
        goto done;
    if (!memcmp(readback, replacement, sizeof(readback))) result = verified;
    else if (!memcmp(readback, data, sizeof(readback))) result = PAGE5_NO_CHANGE;
    else result = PAGE5_UNEXPECTED_CONTENTS;
done:
    clear_bytes(&identity, sizeof(identity));
    clear_bytes(data, sizeof(data));
    clear_bytes(marker, sizeof(marker));
    clear_bytes(readback, sizeof(readback));
    return result;
}
#ifdef FORGEBOX_SE_PIN_TEST
#define PIN_TEST_LIMIT 3
typedef struct {
    uint8_t marker[8], remaining, verifier[7], secret[16];
} PinTestRecord;

typedef struct {
    uint8_t retry_hint, nonce[16], ciphertext[16], tag[32];
} PinTestFlash;

_Static_assert(sizeof(PinTestRecord) == 32, "PIN test record must fit DS28S60 page 5");
static const char test_pin[] = "584201";
static const uint8_t test_plaintext[16] = "disposable test";
static const PinTestRecord empty_pin_record;

static bool pin_test_read(const Ds28Identity *id, PinTestRecord *record) {
    uint8_t status;
    return ds28s60_authenticated_read(id, DS28_TEST_PAGE, &status,
                                       (uint8_t *)record) == PAIRING_MATCH;
}

static bool pin_test_commit(const Ds28Identity *id, const PinTestRecord *old,
                             const PinTestRecord *next) {
    PinTestRecord readback;
    bool verified = ds28s60_attempt_test_write(id, DS28_TEST_PAGE, (const uint8_t *)old, (const uint8_t *)next) &&
        pin_test_read(id, &readback) && !memcmp(&readback, next, sizeof(readback));
    clear_bytes(&readback, sizeof(readback));
    return verified;
}

static void pin_test_verifier(const PinTestRecord *record, const char *pin, uint8_t *digest) {
    const uint8_t domain[] = "ForgeBox demo PIN";
    uint8_t input[sizeof(domain) + 6];
    memcpy(input, domain, sizeof(domain));
    memcpy(input + sizeof(domain), pin, 6);
    hmac_sha256_oneline(record->secret, sizeof(record->secret), input, sizeof(input), digest);
    clear_bytes(input, sizeof(input));
}

// A single disposable fixture. The SPI representation has no PIN verifier,
// secret, or authoritative counter. This is not the production wallet format.
static bool pin_test_flash_crypto(PinTestFlash *flash, const uint8_t *secret, bool seal) {
    uint8_t input[33] = {1}, mask[32], digest[32];
    memcpy(input + 1, flash->nonce, sizeof(flash->nonce));
    hmac_sha256_oneline(secret, 16, input, 17, mask);
    if (seal)
        for (unsigned i = 0; i < sizeof(flash->ciphertext); i++)
            flash->ciphertext[i] = test_plaintext[i] ^ mask[i];
    input[0] = 2;
    memcpy(input + 17, flash->ciphertext, sizeof(flash->ciphertext));
    hmac_sha256_oneline(secret, 16, input, sizeof(input), digest);
    unsigned differences = 0;
    if (seal) {
        memcpy(flash->tag, digest, sizeof(flash->tag));
    } else {
        for (unsigned i = 0; i < sizeof(flash->tag); i++) differences |= flash->tag[i] ^ digest[i];
        for (unsigned i = 0; i < sizeof(flash->ciphertext); i++)
            differences |= flash->ciphertext[i] ^ mask[i] ^ test_plaintext[i];
    }
    clear_bytes(mask, sizeof(mask));
    clear_bytes(digest, sizeof(digest));
    return differences == 0;
}

typedef enum { PIN_ATTEMPT_ERROR, PIN_ATTEMPT_OK, PIN_ATTEMPT_WRONG, PIN_ATTEMPT_BLOCKED } PinAttempt;

static PinAttempt pin_test_attempt(const Ds28Identity *id, const uint8_t *marker,
                                    const char *pin, PinTestFlash *flash) {
    PinTestRecord current, next;
    uint8_t digest[32];
    PinAttempt result = PIN_ATTEMPT_ERROR;
    if (!pin_test_read(id, &current) || memcmp(current.marker, marker, sizeof(current.marker)) ||
        current.remaining > PIN_TEST_LIMIT) goto done;
    if (current.remaining == 0) {
        result = PIN_ATTEMPT_BLOCKED;
        goto done;
    }
    next = current;
    next.remaining--;
    if (!pin_test_commit(id, &current, &next)) goto done;
    pin_test_verifier(&next, pin, digest);
    unsigned differences = 0;
    for (unsigned i = 0; i < sizeof(next.verifier); i++) differences |= next.verifier[i] ^ digest[i];
    if (differences) {
        result = PIN_ATTEMPT_WRONG;
        goto done;
    }
    if (!pin_test_flash_crypto(flash, next.secret, false)) goto done;
    current = next;
    next.remaining = PIN_TEST_LIMIT;
    if (pin_test_commit(id, &current, &next)) result = PIN_ATTEMPT_OK;
done:
    clear_bytes(&current, sizeof(current));
    clear_bytes(&next, sizeof(next));
    clear_bytes(digest, sizeof(digest));
    return result;
}

static bool pin_test_replay(const Ds28Identity *id, const uint8_t *marker) {
    PinTestRecord record = {.remaining = PIN_TEST_LIMIT};
    PinTestFlash original = {.retry_hint = PIN_TEST_LIMIT}, replay;
    uint8_t digest[32], wrong_secret[16];
    bool passed = false;
    if (!board_random(record.secret, sizeof(record.secret)) ||
        !board_random(original.nonce, sizeof(original.nonce))) goto done;
    memcpy(record.marker, marker, sizeof(record.marker));
    pin_test_verifier(&record, test_pin, digest);
    memcpy(record.verifier, digest, sizeof(record.verifier));
    pin_test_flash_crypto(&original, record.secret, true);
    // Negative control: the right PIN alone cannot open the copied fixture.
    sha256_Raw((const uint8_t *)test_pin, sizeof(test_pin) - 1, digest);
    memcpy(wrong_secret, digest, sizeof(wrong_secret));
    if (pin_test_flash_crypto(&original, wrong_secret, false) ||
        !pin_test_commit(id, &empty_pin_record, &record)) goto done;
    replay = original;
    if (pin_test_attempt(id, marker, test_pin, &replay) != PIN_ATTEMPT_OK) goto done;
    for (unsigned i = 0; i < PIN_TEST_LIMIT; i++) {
        replay = original;  // Replay the initial flash image, including its stale retry hint.
        if (pin_test_attempt(id, marker, "000000", &replay) != PIN_ATTEMPT_WRONG) goto done;
    }
    replay = original;
    passed = pin_test_attempt(id, marker, test_pin, &replay) == PIN_ATTEMPT_BLOCKED;
done:
    clear_bytes(&record, sizeof(record));
    clear_bytes(digest, sizeof(digest));
    clear_bytes(wrong_secret, sizeof(wrong_secret));
    return passed;
}

static PinTestStatus pin_test(PinTestStatus requested) {
    const uint8_t domain[] = "ForgeBox page 5 disposable PIN test v1";
    Ds28Identity identity;
    PinTestRecord current;
    uint8_t policy[2], status, marker[32];
    PinTestStatus result = PIN_TEST_ERROR;
    if (!ds28s60_read_protection(1, policy) || policy[0] != 0x04 || policy[1] != 0x0c ||
        ds28s60_load_identity(&identity, &status) != PAIRING_MATCH ||
        !pin_test_read(&identity, &current)) goto done;
    hmac_sha256_oneline(identity.key, sizeof(identity.key), domain, sizeof(domain) - 1, marker);
    if (!memcmp(&current, &empty_pin_record, sizeof(current))) result = PIN_TEST_READY;
    else if (memcmp(current.marker, marker, sizeof(current.marker)) ||
             current.remaining > PIN_TEST_LIMIT) result = PIN_TEST_OCCUPIED;
    else result = current.remaining == 0 ? PIN_TEST_EXHAUSTED : PIN_TEST_PARTIAL;
    if (requested == PIN_TEST_INSPECT) goto done;
    if (requested != result) {
        result = PIN_TEST_ERROR;
        goto done;
    }
    if (result == PIN_TEST_READY) {
        result = pin_test_replay(&identity, marker) ? PIN_TEST_REPLAY_BLOCKED : PIN_TEST_ERROR;
    } else if (result == PIN_TEST_EXHAUSTED || result == PIN_TEST_PARTIAL) {
        bool exhausted = result == PIN_TEST_EXHAUSTED;
        PinTestFlash fixture = {0};  // An exhausted counter must reject before examining SPI.
        if ((exhausted && pin_test_attempt(&identity, marker, test_pin, &fixture) != PIN_ATTEMPT_BLOCKED) ||
            !pin_test_commit(&identity, &current, &empty_pin_record)) result = PIN_TEST_ERROR;
        else result = exhausted ? PIN_TEST_REBOOT_VERIFIED : PIN_TEST_PARTIAL_CLEARED;
    }
done:
    clear_bytes(&identity, sizeof(identity));
    clear_bytes(&current, sizeof(current));
    clear_bytes(marker, sizeof(marker));
    return result;
}

#ifdef FORGEBOX_SE_RESET_TEST
#define RESET_REBOOT_CHECKPOINT 0x80
typedef struct {
    uint8_t owner[16], key[16];
} ResetSecret;
_Static_assert(sizeof(ResetSecret) == 32, "one protected page");
static const ResetSecret empty_reset_secret;

static bool reset_secret_read(const Ds28Identity *id, ResetSecret *secret) {
    uint8_t status;
    return ds28s60_authenticated_read(id, DS28_RESET_SECRET_PAGE, &status,
                                      (uint8_t *)secret) == PAIRING_MATCH;
}

static bool reset_secret_commit(const Ds28Identity *id, const ResetSecret *old,
                                const ResetSecret *next) {
    ResetSecret readback;
    bool verified = ds28s60_attempt_test_write(id, DS28_RESET_SECRET_PAGE,
        (const uint8_t *)old, (const uint8_t *)next) &&
        reset_secret_read(id, &readback) && !memcmp(&readback, next, sizeof(readback));
    clear_bytes(&readback, sizeof(readback));
    return verified;
}

static void reset_secret_owner(const PinTestRecord *record, uint8_t *out) {
    const uint8_t domain[] = "ForgeBox disposable wallet secret page";
    uint8_t digest[32];
    hmac_sha256_oneline(record->secret, sizeof(record->secret), domain, sizeof(domain) - 1, digest);
    memcpy(out, digest, 16);
    clear_bytes(digest, sizeof(digest));
}

static bool reset_wipe(const Ds28Identity *id, const PinTestRecord *record) {
    ResetSecret secret;
    uint8_t owner[16];
    reset_secret_owner(record, owner);
    bool verified = reset_secret_read(id, &secret);
    if (verified && memcmp(&secret, &empty_reset_secret, sizeof(secret))) {
        verified = !memcmp(secret.owner, owner, sizeof(owner)) &&
                   reset_secret_commit(id, &secret, &empty_reset_secret);
    }
    if (verified) verified = pin_test_commit(id, record, &empty_pin_record);
    clear_bytes(&secret, sizeof(secret));
    clear_bytes(owner, sizeof(owner));
    return verified;
}

static bool reset_enroll(const Ds28Identity *id, const uint8_t *marker, const char *pin,
                         PinTestFlash *flash) {
    PinTestRecord current, record = {.remaining = PIN_TEST_LIMIT};
    ResetSecret old_secret, secret;
    uint8_t digest[32];
#ifdef FORGEBOX_SE_COMBINED_TEST
    uint8_t contribution[32];
#endif
    bool enrolled = false;
    if (!pin_test_read(id, &current) || memcmp(&current, &empty_pin_record, sizeof(current)) ||
        !reset_secret_read(id, &old_secret) || memcmp(&old_secret, &empty_reset_secret, sizeof(old_secret)) ||
        !board_random(record.secret, sizeof(record.secret)) ||
        !board_random(secret.key, sizeof(secret.key)) ||
        !board_random(flash->nonce, sizeof(flash->nonce))) goto done;
    memcpy(record.marker, marker, sizeof(record.marker));
    pin_test_verifier(&record, pin, digest);
    memcpy(record.verifier, digest, sizeof(record.verifier));
    reset_secret_owner(&record, secret.owner);
    // Commit ownership before writing a secret so a reboot can clean up.
    if (!pin_test_commit(id, &current, &record)) goto done;
#ifdef FORGEBOX_SE_COMBINED_TEST
    if (!atecc_account_enroll(pin) || atecc_account_kdf(pin, contribution) != PAIRING_MATCH) goto done;
#endif
    if (!reset_secret_commit(id, &old_secret, &secret)) goto done;
    flash->retry_hint = PIN_TEST_LIMIT;
#ifdef FORGEBOX_SE_COMBINED_TEST
    hmac_sha256_oneline(contribution, sizeof(contribution), secret.key, sizeof(secret.key), digest);
    enrolled = pin_test_flash_crypto(flash, digest, true);
#else
    enrolled = pin_test_flash_crypto(flash, secret.key, true);
#endif
done:
#ifdef FORGEBOX_SE_COMBINED_TEST
    clear_bytes(contribution, sizeof(contribution));
#endif
    clear_bytes(&current, sizeof(current));
    clear_bytes(&record, sizeof(record));
    clear_bytes(&old_secret, sizeof(old_secret));
    clear_bytes(&secret, sizeof(secret));
    clear_bytes(digest, sizeof(digest));
    return enrolled;
}

typedef enum {
    RESET_ATTEMPT_ERROR, RESET_ATTEMPT_OK, RESET_ATTEMPT_WRONG, RESET_ATTEMPT_PENDING,
    RESET_ATTEMPT_WIPED, RESET_ATTEMPT_OLD_BLOB,
} ResetAttempt;

static ResetAttempt reset_attempt(const Ds28Identity *id, const uint8_t *marker, const char *pin,
                                  PinTestFlash *flash, bool interrupt_wipe) {
    PinTestRecord record, next;
    ResetSecret secret;
    uint8_t digest[32], owner[16];
#ifdef FORGEBOX_SE_COMBINED_TEST
    uint8_t contribution[32];
#endif
    ResetAttempt result = RESET_ATTEMPT_ERROR;
    if (!pin_test_read(id, &record) || memcmp(record.marker, marker, sizeof(record.marker)) ||
        !record.remaining || record.remaining > PIN_TEST_LIMIT) goto done;
    next = record;
    next.remaining--;
    if (!pin_test_commit(id, &record, &next)) goto done;
    pin_test_verifier(&next, pin, digest);
    unsigned difference = 0;
    for (unsigned i = 0; i < sizeof(next.verifier); i++) difference |= next.verifier[i] ^ digest[i];
#ifdef FORGEBOX_SE_COMBINED_TEST
    PairingStatus auth = atecc_account_kdf(pin, contribution);
    if (auth != (difference ? PAIRING_REJECTED : PAIRING_MATCH)) goto done;
#endif
    if (difference) {
        if (next.remaining) result = RESET_ATTEMPT_WRONG;
        else if (interrupt_wipe) {
            record = next;
            next.remaining = RESET_REBOOT_CHECKPOINT;
#ifdef FORGEBOX_SE_COMBINED_TEST
            atecc_account_checkpoint(next.verifier);
#endif
            if (pin_test_commit(id, &record, &next)) result = RESET_ATTEMPT_PENDING;
        }
        else if (reset_wipe(id, &next)) result = RESET_ATTEMPT_WIPED;
        goto done;
    }
    // The secret page is accessed only after the PIN check in the unlock path.
    reset_secret_owner(&next, owner);
    if (!reset_secret_read(id, &secret) || memcmp(owner, secret.owner, sizeof(owner))) goto done;
#ifdef FORGEBOX_SE_COMBINED_TEST
    hmac_sha256_oneline(contribution, sizeof(contribution), secret.key, sizeof(secret.key), digest);
    bool opened = pin_test_flash_crypto(flash, digest, false);
#else
    bool opened = pin_test_flash_crypto(flash, secret.key, false);
#endif
    if (!opened) {
        result = RESET_ATTEMPT_OLD_BLOB;
        goto done;
    }
    record = next;
    next.remaining = PIN_TEST_LIMIT;
    if (pin_test_commit(id, &record, &next)) result = RESET_ATTEMPT_OK;
done:
#ifdef FORGEBOX_SE_COMBINED_TEST
    clear_bytes(contribution, sizeof(contribution));
#endif
    clear_bytes(&record, sizeof(record));
    clear_bytes(&next, sizeof(next));
    clear_bytes(&secret, sizeof(secret));
    clear_bytes(digest, sizeof(digest));
    clear_bytes(owner, sizeof(owner));
    return result;
}

static bool reset_exhaust(const Ds28Identity *id, const uint8_t *marker,
                          PinTestFlash *flash, bool interrupt_wipe) {
    for (unsigned i = 1; i < PIN_TEST_LIMIT; i++)
        if (reset_attempt(id, marker, "000000", flash, false) != RESET_ATTEMPT_WRONG) return false;
    ResetAttempt expected = interrupt_wipe ? RESET_ATTEMPT_PENDING : RESET_ATTEMPT_WIPED;
    return reset_attempt(id, marker, "000000", flash, interrupt_wipe) == expected;
}

static ResetTestStatus reset_test(ResetTestStatus requested) {
#ifdef FORGEBOX_SE_COMBINED_TEST
    const uint8_t domain[] = "ForgeBox disposable combined account test v1";
#else
    const uint8_t domain[] = "ForgeBox disposable two-page reset test v1";
#endif
    const char new_pin[] = "382695";
    Ds28Identity identity;
    PinTestRecord record;
    ResetSecret secret;
    PinTestFlash old_flash, new_flash;
    uint8_t policy[2], status, marker[32];
    ResetTestStatus result = RESET_TEST_ERROR;
    if (!ds28s60_read_protection(1, policy) || policy[0] != 0x04 || policy[1] != 0x0c ||
        !ds28s60_read_protection(2, policy) || policy[0] != 0x04 || policy[1] != 0x0c ||
        ds28s60_load_identity(&identity, &status) != PAIRING_MATCH ||
        !pin_test_read(&identity, &record)) goto done;
    hmac_sha256_oneline(identity.key, sizeof(identity.key), domain, sizeof(domain) - 1, marker);
    if (memcmp(&record, &empty_pin_record, sizeof(record))) {
        if (requested != RESET_TEST_INSPECT || memcmp(record.marker, marker, sizeof(record.marker)) ||
            (record.remaining > PIN_TEST_LIMIT && record.remaining != RESET_REBOOT_CHECKPOINT))
            result = RESET_TEST_OCCUPIED;
        else {
#ifdef FORGEBOX_SE_COMBINED_TEST
            if (record.remaining == RESET_REBOOT_CHECKPOINT)
                atecc_account_restore_checkpoint(record.verifier);
#endif
            if (reset_wipe(&identity, &record))
                result = record.remaining == RESET_REBOOT_CHECKPOINT ? RESET_TEST_RECOVERED : RESET_TEST_PARTIAL_CLEARED;
        }
        goto done;
    }
    if (!reset_secret_read(&identity, &secret)) goto done;
    if (memcmp(&secret, &empty_reset_secret, sizeof(secret))) {
        result = RESET_TEST_OCCUPIED;
        goto done;
    }
#ifdef FORGEBOX_SE_COMBINED_TEST
    if (!account_config_ready) goto done;
#endif
    if (requested == RESET_TEST_INSPECT) {
        result = RESET_TEST_READY;
        goto done;
    }
    if (requested != RESET_TEST_READY ||
        !reset_enroll(&identity, marker, test_pin, &old_flash) ||
#ifdef FORGEBOX_SE_COMBINED_TEST
        !atecc_account_test_auth_rewrite(test_pin) ||
#endif
        reset_attempt(&identity, marker, test_pin, &old_flash, false) != RESET_ATTEMPT_OK ||
        !reset_exhaust(&identity, marker, &old_flash, false) ||
        !reset_enroll(&identity, marker, new_pin, &new_flash) ||
        reset_attempt(&identity, marker, test_pin, &old_flash, false) != RESET_ATTEMPT_WRONG ||
        reset_attempt(&identity, marker, new_pin, &old_flash, false) != RESET_ATTEMPT_OLD_BLOB ||
        reset_attempt(&identity, marker, new_pin, &new_flash, false) != RESET_ATTEMPT_OK ||
        !reset_exhaust(&identity, marker, &new_flash, true)) goto done;
    result = RESET_TEST_PENDING_REBOOT;
done:
    clear_bytes(&identity, sizeof(identity));
    clear_bytes(&record, sizeof(record));
    clear_bytes(&secret, sizeof(secret));
    clear_bytes(marker, sizeof(marker));
    return result;
}
#endif
#endif
#endif
#endif

#endif

#ifdef FORGEBOX_GEN1_STORAGE
static bool gen1_rewrite_auth(const uint8_t *pin) {
    uint8_t auth[32];
    atecc_account_pin(pin, 32, "auth_key", auth);
    bool written = atecc_account_write(ACCOUNT_AUTH_SLOT, auth);
    clear_bytes(auth, sizeof(auth));
    return written;
}
#ifdef FORGEBOX_FP_UNLOCK
static bool fingerprint_wallet_wipe(const Ds28Identity *identity);
static bool fingerprint_revoke_for_pin_change(const Ds28Identity *identity);
#endif
#include "gen1_storage.inc"
#include "seed_entropy.inc"
#else
bool board_gen1_enabled(void) { return false; }
int board_gen1_state(uint8_t *salt) { return -1; }
bool board_gen1_enroll(const uint8_t *salt, const uint8_t *pin_key, uint8_t *root) { return false; }
int board_gen1_unlock(const uint8_t *pin_key, uint8_t *root) { return -1; }
int board_gen1_change_pin(const uint8_t *old_pin, const uint8_t *new_pin) { return -1; }
bool board_gen1_wipe(void) { return false; }
int board_gen1_seed_entropy(const uint8_t *root, const uint8_t *pin_context, uint8_t *out) { return -1; }
#endif

bool board_cpu_debug_status(uint32_t *jtag_control, bool *halting_debug_enabled) {
    *jtag_control = TST->TST_JTAG;
    *halting_debug_enabled = (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0;
    return true;
}

void board_device_diagnostics(DeviceDiagnostics *result) {
    memset(result, 0, sizeof(*result));
    result->board_revision = GetHardwareVersionString();
    // Sample only the signal levels. Do not arm a sensor or invoke tamper
    // callbacks, which are destructive in the retail application.
    pin_mode(GPIOA, GPIO_Pin_2, GPIO_Mode_IN_FLOATING);
    pin_mode(GPIOF, GPIO_Pin_14, GPIO_Mode_IPU);
    result->tamper_high = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2) != Bit_RESET;
    result->fingerprint_irq_high = GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_14) != Bit_RESET;
    uint8_t protection[2] = {0};
    ds28s60_open();
    result->ds28s60_responded = ds28s60_read_protection(31, protection);
    result->ds28s60_protection = protection[1];
    I2CIO_Cfg_t bus;
    if (atecc_open(&bus)) {
        result->atecc_responded = atecc_query(&bus, 0x30, 0, 0, result->atecc_revision, 4);
        atecc_close(&bus);
    }
#ifdef FORGEBOX_USB_DEBUG
    board_capture_hardware_report();
#endif
}

#if defined(FORGEBOX_USB_DEBUG) || defined(FORGEBOX_FP_UNLOCK_TEST) || defined(FORGEBOX_FP_UNLOCK)
static uint8_t hardware_report[HARDWARE_REPORT_SIZE];
static uint32_t report_generation;

static uint32_t fingerprint_crc(const uint8_t *data, unsigned size) {
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < size; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}

enum { FP_REPLY = 1, FP_NO_BYTES, FP_BAD_REPLY, FP_TX_TIMEOUT };
// The retail protocol allows 240 payload bytes plus an 11-byte encrypted
// header, AES padding, a 3-byte frame header and a 4-byte CRC.
enum { FP_FRAME_MAX = 263 };

static bool fingerprint_send(const uint8_t *request, unsigned size) {
    uint32_t start = osKernelGetTickCount();
    for (unsigned i = 0; i < size; i++) {
        while (!UART_IsTXEmpty(UART2)) {
            if (osKernelGetTickCount() - start >= 100) return false;
        }
        UART_SendData(UART2, request[i]);
    }
    return true;
}

static unsigned fingerprint_receive(uint32_t start, uint32_t timeout, uint8_t *response,
                                    uint16_t *received) {
    unsigned length = 0, total = 0;
    while (osKernelGetTickCount() - start < timeout) {
        if (!UART_IsRXFIFONotEmpty(UART2)) {
            UserDelayUs(25);
            continue;
        }
        uint8_t byte = UART_ReceiveData(UART2);
        if (*received != UINT16_MAX) ++*received;
        if (!length && byte != 0xaa) continue;
        response[length++] = byte;
        if (length == 3) {
            total = 3 + response[1] + ((unsigned)response[2] << 8);
            if (total < 18 || total > FP_FRAME_MAX) length = 0;
        }
        if (!length || length != total) continue;
        uint32_t crc;
        memcpy(&crc, response + total - 4, 4);
        if (crc == fingerprint_crc(response, total - 4)) return total;
        length = 0;
        total = 0;
    }
    return 0;
}

static uint8_t fingerprint_query(uint8_t command, uint8_t *out, unsigned size,
                                 uint16_t *received) {
    uint8_t request[39] = {0xaa, 36, 0};
    request[23] = command;
    request[28] = 1;
    uint32_t crc = fingerprint_crc(request + 23, 12);
    memcpy(request + 19, &crc, 4);
    crc = fingerprint_crc(request, 35);
    memcpy(request + 35, &crc, 4);
    *received = 0;
    if (!fingerprint_send(request, sizeof(request))) return FP_TX_TIMEOUT;

    uint8_t response[FP_FRAME_MAX];
    uint32_t start = osKernelGetTickCount();
    unsigned total;
    while ((total = fingerprint_receive(start, 350, response, received)) != 0) {
        if (response[7] == command && response[8] == 0 &&
            response[12] == size && response[13] == 0 && total >= 18 + size) {
            memcpy(out, response + 14, size);
            return FP_REPLY;
        }
    }
    return *received ? FP_BAD_REPLY : FP_NO_BYTES;
}

static void fingerprint_uart_open(void) {
    SYSCTRL_APBPeriphClockCmd(SYSCTRL_APBPeriph_UART2, ENABLE);
    NVIC_DisableIRQ(UART2_IRQn);
    SYSCTRL_APBPeriphResetCmd(SYSCTRL_APBPeriph_UART2, ENABLE);
    GPIO_PinRemapConfig(GPIOD, GPIO_Pin_12 | GPIO_Pin_13, GPIO_Remap_0);
    UART_InitTypeDef uart = {
        .UART_BaudRate = 115200, .UART_WordLength = UART_WordLength_8b,
        .UART_StopBits = UART_StopBits_1, .UART_Parity = UART_Parity_No,
    };
    UART_Init(UART2, &uart);
    UART_FIFOInitTypeDef fifo;
    UART_FIFOStructInit(&fifo);
    fifo.FIFO_Enable = ENABLE;
    UART_FIFOInit(UART2, &fifo);
}

static void fingerprint_uart_close(void) {
    UART_DeInit(UART2);
    pin_mode(GPIOD, GPIO_Pin_12 | GPIO_Pin_13, GPIO_Mode_IN_FLOATING);
}

static void fingerprint_report(uint8_t *report) {
    uint16_t received;
    fingerprint_uart_open();
    if (fingerprint_query(0xb5, report + 12, 4, &received) == FP_REPLY) report[7] |= 2;
    if (fingerprint_query(0xd1, report + 16, 1, &received) == FP_REPLY && report[16] <= 1)
        report[7] |= 4;
    fingerprint_uart_close();
}

#if defined(FORGEBOX_BOOT_PROBE) || defined(FORGEBOX_FP_UNLOCK_TEST) || defined(FORGEBOX_FP_UNLOCK)
#include "fingerprint_count.inc"

static void fingerprint_restart(void) {
    // Match the stock FingerprintRestart sequence. Only module GPIO is driven.
    pin_mode(GPIOE, GPIO_Pin_11 | GPIO_Pin_12, GPIO_Mode_Out_PP);
    GPIO_ResetBits(GPIOE, GPIO_Pin_12);
    osDelay(10);
    GPIO_SetBits(GPIOE, GPIO_Pin_12);
    osDelay(10);
    GPIO_ResetBits(GPIOE, GPIO_Pin_11);
    osDelay(10);
    GPIO_SetBits(GPIOE, GPIO_Pin_11);
    osDelay(800);
    for (unsigned i = 0; i < 64 && UART_IsRXFIFONotEmpty(UART2); i++)
        (void)UART_ReceiveData(UART2);
}

#if defined(FORGEBOX_FP_ENROLL_TEST) || defined(FORGEBOX_FP_UNLOCK_TEST) || defined(FORGEBOX_FP_UNLOCK)
#include "fingerprint_enroll_test.inc"
#endif

#ifdef FORGEBOX_BOOT_PROBE
void board_probe_fingerprint(uint8_t *out) {
    uint8_t first[17], second[17];
    uint16_t counts[4];
    fingerprint_uart_open();
    fingerprint_restart();
    out[2] = fingerprint_query(0xd2, first, sizeof(first), &counts[2]);
    out[0] = fingerprint_query(0xb5, out + 4, 4, &counts[0]);
    out[1] = fingerprint_query(0xd1, out + 8, 1, &counts[1]);
    uint16_t init_received;
    out[18] = fingerprint_query(0xc2, out + 19, 1, &init_received);
    memcpy(out + 20, &init_received, sizeof(init_received));
    out[3] = fingerprint_query(0xd2, second, sizeof(second), &counts[3]);
    if (out[2] == FP_REPLY && first[0] != 0) out[2] = 5;
    if (out[3] == FP_REPLY && second[0] != 0) out[3] = 5;
    if (out[2] == FP_REPLY && out[3] == FP_REPLY)
        out[9] = memcmp(first + 1, second + 1, 16) ? 1 : 2;
    memcpy(out + 10, counts, sizeof(counts));
    fingerprint_count_probe(out + 22, out[0] == FP_REPLY, out[4]);
    volatile uint8_t *a = first, *b = second;
    for (unsigned i = 0; i < sizeof(first); i++) a[i] = b[i] = 0;
    fingerprint_uart_close();
}
#endif
#endif

void board_capture_hardware_report(void) {
    uint8_t report[HARDWARE_REPORT_SIZE] = {0};
    pin_mode(GPIOA, GPIO_Pin_2, GPIO_Mode_IN_FLOATING);
    pin_mode(GPIOF, GPIO_Pin_14, GPIO_Mode_IPU);
    report[4] = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2) != Bit_RESET) |
                ((GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_14) != Bit_RESET) << 1) |
                ((GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_11) != Bit_RESET) << 2) |
                ((GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_12) != Bit_RESET) << 3);
    report[214] = GetHardwareVersion();
#ifdef FORGEBOX_SE_PAIRING
    report[17] = report[18] = 0xff;  // No status byte received yet.
    report[19] = 3;                 // Storage probe with an additional Gen 1 slot.
    report[215] = report[216] = PAIRING_NO_CHIP;
    report[12] = report[13] = PAIRING_NO_CHIP;
    report[7] = PAIRING_NO_CHIP << 3;
#endif

    I2CIO_Cfg_t bus;
    if (atecc_open(&bus)) {
        if (atecc_query(&bus, 0x30, 0, 0, report + 8, 4)) report[7] |= 1;
        for (unsigned block = 0; block < 4; block++) {
            // Read, 32-byte CONFIG zone only, word address block * 8.
            if (atecc_query(&bus, 0x02, 0x80, block * 8, report + 28 + block * 32, 32))
                report[5] |= 1u << block;
        }
        for (unsigned counter = 0; counter < 2; counter++) {
            // Counter mode 0 is read; increment mode 1 is never sent.
            if (atecc_query(&bus, 0x24, 0, counter, report + 20 + counter * 4, 4))
                report[6] |= 1u << counter;
        }
#ifdef FORGEBOX_SE_PAIRING
        report[215] = report[5] == 15 ? atecc_check_pairing(&bus, report + 28, report + 17)
                                     : PAIRING_CONFIG;
#endif
        atecc_close(&bus);
#ifdef FORGEBOX_SE_PAIRING
        report[12] = report[215] == PAIRING_MATCH
            ? atecc_probe_scratch(&bus, report + 28, 8, report + 14) : report[215];
        uint8_t slot13_state = 0;
        PairingStatus slot13 = report[215] == PAIRING_MATCH
            ? atecc_probe_scratch(&bus, report + 28, 13, &slot13_state) : report[215];
        report[7] = (report[7] & 7) | (slot13 << 3);
        report[5] |= slot13_state << 4;
#ifdef FORGEBOX_SE_RESET_TEST
        if (report[12] == PAIRING_MATCH)
            report[6] |= atecc_check_encrypted_kdf(&bus, report + 28) << 2;
#endif
#ifdef FORGEBOX_SE_COMBINED_TEST
        account_stage = 1;
        account_counter_baseline = (report[6] & 3) == 3;
        if (account_counter_baseline)
            memcpy(account_counter_before, report + 20, sizeof(account_counter_before));
        account_config_ready = report[215] == PAIRING_MATCH && report[6] == 7 &&
            atecc_account_policy(report + 28) &&
            account_counter_before[0] < 0x1fff00u && account_counter_before[1] < 0x1fff00u;
        if (account_config_ready) memcpy(account_config, report + 28, sizeof(account_config));
#endif
        if (atecc_open(&bus)) {
            for (unsigned counter = 0; counter < 2; counter++) {
                uint8_t value[4];
                if ((report[6] & (1u << counter)) &&
                    atecc_query(&bus, 0x24, 0, counter, value, sizeof(value))) {
                    report[16] |= 1u << (counter + 2);
                    if (!memcmp(value, report + 20 + counter * 4, sizeof(value)))
                        report[16] |= 1u << counter;
                }
            }
            atecc_close(&bus);
        }
#endif
    }
    memset(report + 28, 0, 4);       // redact ATECC serial bytes
    memset(report + 28 + 8, 0, 5);
    ds28s60_open();
    uint32_t ds_valid = 0;
#ifdef FORGEBOX_SE_PAIRING
    // Probe these two policies first, before scanning the other blocks.
    for (unsigned attempt = 0; attempt < 3 && !(ds_valid & 2); attempt++)
        if (ds28s60_read_protection(1, report + 162)) ds_valid |= 2;
    if (ds28s60_read_protection(0, report + 160)) ds_valid |= 1;
#endif
    for (unsigned i = 0; i < 27; i++) {
        if (ds_valid & (1u << i)) continue;
        uint8_t block = i < 23 ? i : i + 8;  // user blocks 0–22, secret flags 31–34
        if (ds28s60_read_protection(block, report + 160 + 2 * i)) ds_valid |= 1u << i;
    }
    memcpy(report + 156, &ds_valid, 4);
#ifdef FORGEBOX_SE_PAIRING
    if (ds_valid & 1)
        report[216] = ds28s60_check_pairing(report + 160, 0, report + 18, NULL);
    else if (ds_valid)
        report[216] = PAIRING_CONFIG;
    uint8_t status;
    if (ds_valid)
        report[13] = ds28s60_check_pairing(
            ds_valid & 2 ? report + 162 : NULL, 5, &status, report + 15);
#else
    fingerprint_report(report);
#endif
    uint32_t generation = ++report_generation;
    memcpy(report, &generation, 4);
    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    memcpy(hardware_report, report, sizeof(report));
    __set_PRIMASK(irq_mask);
}

#ifdef FORGEBOX_SE_PAIRING
PairingStatus board_pairing_status(bool atecc) {
    return hardware_report[atecc ? 215 : 216];
}

PairingStatus board_storage_probe_status(bool atecc) {
    return hardware_report[atecc ? 12 : 13];
}

PairingStatus board_slot13_probe_status(void) {
    return (hardware_report[7] >> 3) & 0x0f;
}
#endif

#ifdef FORGEBOX_SE_STORAGE_WRITE
static void report_storage_test(uint8_t version, uint8_t result) {
    uint32_t generation = ++report_generation;
    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    memcpy(hardware_report, &generation, 4);
    hardware_report[6] = (hardware_report[6] & 0x0f) | (result << 4);
    hardware_report[19] = version;
    __set_PRIMASK(irq_mask);
}

#if defined(FORGEBOX_SE_RESET_TEST)
ResetTestStatus board_reset_test(ResetTestStatus requested) {
    ResetTestStatus result = reset_test(requested);
#ifdef FORGEBOX_SE_COMBINED_TEST
    uint32_t counters[2];
    uint8_t valid = 0, matches = 0;
    I2CIO_Cfg_t bus;
    if (atecc_open(&bus)) {
        for (unsigned i = 0; i < 2; i++) {
            if (atecc_query(&bus, 0x24, 0, i, (uint8_t *)&counters[i], sizeof(counters[i]))) {
                valid |= 1u << i;
                if (counters[i] == account_counter_before[i]) matches |= 1u << i;
            }
        }
        atecc_close(&bus);
    }
    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    hardware_report[6] = (hardware_report[6] & 0xfc) | valid;
    for (unsigned i = 0; i < 2; i++)
        if (valid & (1u << i)) memcpy(hardware_report + 20 + i * 4, &counters[i], sizeof(counters[i]));
    if (account_counter_baseline) hardware_report[16] = (hardware_report[16] & 0x0f) | (valid << 6) | (matches << 4);
    if (account_auth_rewrite_reproduced) hardware_report[7] |= 0x80;
    hardware_report[17] = result == RESET_TEST_RECOVERED ? 10 :
                          result == RESET_TEST_PENDING_REBOOT ? 9 : account_stage;
    hardware_report[18] = account_status;
    report_storage_test(7, result);
    __set_PRIMASK(irq_mask);
#else
    report_storage_test(6, result);
#endif
    return result;
}
#elif defined(FORGEBOX_SE_PIN_TEST)
PinTestStatus board_pin_test(PinTestStatus requested) {
    PinTestStatus result = pin_test(requested);
    report_storage_test(5, result);
    return result;
}
#else
Page5TestStatus board_page5_test(Page5TestStatus requested) {
    Page5TestStatus result = page5_test(requested);
    report_storage_test(4, result);
    return result;
}
#endif
#endif

void board_read_hardware_report(unsigned offset, uint8_t *out, unsigned size) {
    if (offset <= sizeof(hardware_report) && size <= sizeof(hardware_report) - offset)
        memcpy(out, hardware_report + offset, size);
}
#endif

#ifdef FORGEBOX_FP_UNLOCK_TEST
#include "fingerprint_unlock_test.inc"
#elif defined(FORGEBOX_FP_UNLOCK)
#include "fingerprint_unlock.inc"
#else
bool board_fp_unlock_test_enabled(void) { return false; }
int board_fp_unlock_test(unsigned action, const uint8_t *pin_key, uint8_t *result,
                         void (*progress)(unsigned, uint8_t)) { return -1; }
#endif

bool board_fp_unlock_enabled(void) {
#ifdef FORGEBOX_FP_UNLOCK
    return true;
#else
    return false;
#endif
}
