#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sha2.h"

typedef int I2CIO_Cfg_t;
static unsigned counts[3], changed_source, failed_source, repeated_source;

static void clear_bytes(void *out, unsigned size) { memset(out, 0, size); }

static bool sample(unsigned source, uint8_t *out) {
    if (failed_source == source + 1) return false;
    unsigned call = counts[source]++;
    if (repeated_source == source + 1) call = 0;
    for (unsigned i = 0; i < 32; i++)
        out[i] = 64 * source + 32 * call + i + (changed_source == source + 1);
    return true;
}

static bool board_random(uint8_t *out, size_t len) {
    assert(len == 32);
    return sample(0, out);
}

static bool atecc_open(I2CIO_Cfg_t *bus) { *bus = 0; return true; }
static void atecc_close(const I2CIO_Cfg_t *bus) { assert(*bus == 0); }
static bool atecc_exchange(const I2CIO_Cfg_t *bus, uint8_t opcode, uint8_t mode,
                           uint8_t address, const uint8_t *input, unsigned input_size,
                           uint8_t *out, unsigned size, unsigned wait_ms) {
    assert(!*bus && opcode == 0x1b && !mode && !address && !input && !input_size);
    assert(size == 32 && wait_ms >= 23);
    return sample(1, out);
}

static void ds28s60_open(void) {}
static bool ds28s60_exchange(const uint8_t *command, unsigned command_size,
                             uint8_t *out, unsigned size, unsigned polls, uint8_t *status) {
    const uint8_t expected[] = {0xd2, 1, 32};
    assert(command_size == sizeof(expected) && !memcmp(command, expected, command_size));
    assert(size == 32 && polls > 0);
    *status = 0xaa;
    return sample(2, out);
}

#include "../port/seed_entropy.inc"

static int generate(const uint8_t *root, const uint8_t *pin, uint8_t *out) {
    memset(counts, 0, sizeof(counts));
    return board_gen1_seed_entropy(root, pin, out);
}

int main(void) {
    uint8_t root[32] = {1}, pin[32] = {2}, output[32], expected[32];
    uint8_t transcript[sizeof("ForgeBox BIP39 entropy v1") + 256] = "ForgeBox BIP39 entropy v1";
    uint8_t *inputs = transcript + sizeof("ForgeBox BIP39 entropy v1");
    for (unsigned i = 0; i < 192; i++) inputs[i] = i;
    memcpy(inputs + 192, root, sizeof(root));
    memcpy(inputs + 224, pin, sizeof(pin));
    sha256_Raw(transcript, sizeof(transcript), expected);
    assert(!generate(root, pin, output) && !memcmp(output, expected, sizeof(output)));
    for (unsigned source = 1; source <= 3; source++) {
        changed_source = source;
        assert(!generate(root, pin, output) && memcmp(output, expected, sizeof(output)));
        memcpy(output, expected, sizeof(output));
        failed_source = source;
        assert(generate(root, pin, output) == (int)source && !memcmp(output, expected, sizeof(output)));
        failed_source = 0;
        repeated_source = source;
        assert(generate(root, pin, output) == (int)source && !memcmp(output, expected, sizeof(output)));
        repeated_source = 0;
    }
    changed_source = 0;
    root[0]++;
    assert(!generate(root, pin, output) && memcmp(output, expected, sizeof(output)));
    root[0]--;
    pin[0]++;
    assert(!generate(root, pin, output) && memcmp(output, expected, sizeof(output)));
    puts("PASS: SHA-256 transcript includes all five inputs and rejects failed or repeated samples");
}
