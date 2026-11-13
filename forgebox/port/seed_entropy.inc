// Runs inside the wallet VM task with the same transports used for unlock.
static bool seed_atecc_random(uint8_t *out) {
    I2CIO_Cfg_t bus;
    if (!atecc_open(&bus)) return false;
    // Match the Random command used by Keystone's CryptoAuthLib.
    bool ok = atecc_exchange(&bus, 0x1b, 0, 0, NULL, 0, out, 32, 50);
    atecc_close(&bus);
    return ok;
}

static bool seed_ds28_random(uint8_t *out) {
    const uint8_t command[] = {0xd2, 1, 32};
    uint8_t status;
    return ds28s60_exchange(command, sizeof(command), out, 32, 5, &status);
}

static bool seed_add_samples(SHA256_CTX *hash, bool (*read)(uint8_t *)) {
    uint8_t first[32], second[32];
    bool ok = read(first) && read(second) &&
              memcmp(first, first + 16, 16) && memcmp(second, second + 16, 16) &&
              memcmp(first, second, sizeof(first));
    if (ok) {
        sha256_Update(hash, first, sizeof(first));
        sha256_Update(hash, second, sizeof(second));
    }
    clear_bytes(first, sizeof(first));
    clear_bytes(second, sizeof(second));
    return ok;
}

static bool seed_mcu_random(uint8_t *out) {
    return board_random(out, 32);
}

int board_gen1_seed_entropy(const uint8_t *root, const uint8_t *pin_context, uint8_t *out) {
    static const uint8_t domain[] = "ForgeBox BIP39 entropy v1";
    SHA256_CTX hash;
    int failed = 1;
    sha256_Init(&hash);
    sha256_Update(&hash, domain, sizeof(domain));
    if (!seed_add_samples(&hash, seed_mcu_random)) goto done;
    failed = 2;
    if (!seed_add_samples(&hash, seed_atecc_random)) goto done;
    failed = 3;
    ds28s60_open();
    if (!seed_add_samples(&hash, seed_ds28_random)) goto done;
    sha256_Update(&hash, root, 32);
    sha256_Update(&hash, pin_context, 32);
    sha256_Final(&hash, out);
    failed = 0;
done:
    clear_bytes(&hash, sizeof(hash));
    return failed;
}
