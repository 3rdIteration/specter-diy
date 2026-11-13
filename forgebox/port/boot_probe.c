#include <stdio.h>
#include <string.h>
#include "mhscpu.h"
#include "mhscpu_otp.h"
#include "hardware_version.h"
#include "device_diagnostics.h"
#include "sha2.h"

enum { ROM_AIR105 = 1, ROM_OTHER, ROM_NO_VECTORS, ROM_READ_FAULT, ROM_MH1903 };
enum { OTP_UNKNOWN, OTP_ERASED, OTP_PROGRAMMED, OTP_UNREADABLE };
enum { SIGNATURE_UNKNOWN, SIGNATURE_RSA, SIGNATURE_SM2 };
enum { ROM_SIZE = 0x20000, FINGERPRINT_OFFSET = 80, TOUCH_OFFSET = 140 };
static uint8_t probe_report[HARDWARE_REPORT_SIZE];
static volatile uint32_t touch_report;
#ifdef FORGEBOX_FP_ENROLL_TEST
// The status words and sequence fit in one HID chunk.
enum { FP_TEST_REPORT_OFFSET = 156 };
static volatile uint32_t fingerprint_test_report[FP_REPORT_FIELDS];
static volatile uint32_t fingerprint_test_sequence;

void board_fingerprint_test_report(FingerprintReportField field, int value) {
    fingerprint_test_report[field] = 0x10000u | (uint16_t)value;
    fingerprint_test_sequence++;
}
#endif

// Only a fault at this one data load is recoverable. No mapping, write
// protection, or debug registers are changed to make the load succeed.
extern const char boot_probe_load_instruction[], boot_probe_load_failed[];
static __attribute__((naked, noinline)) bool probe_read_word(uint32_t address, uint32_t *value) {
    __asm volatile (
        ".global boot_probe_load_instruction\n"
        "boot_probe_load_instruction:\n"
        "ldr r2, [r0]\n"
        "str r2, [r1]\n"
        "movs r0, #1\n"
        "bx lr\n"
        ".global boot_probe_load_failed\n"
        "boot_probe_load_failed:\n"
        "movs r0, #0\n"
        "bx lr\n");
}

static __attribute__((used, noinline)) void probe_recover_fault(uint32_t *frame) {
    const uint32_t load_fault = SCB_CFSR_PRECISERR_Msk | SCB_CFSR_DACCVIOL_Msk;
    uint32_t flags = SCB->CFSR;
    if (frame[6] != (uintptr_t)boot_probe_load_instruction || !(flags & load_fault))
        for (;;) {}
    SCB->CFSR = flags & (load_fault | SCB_CFSR_BFARVALID_Msk | SCB_CFSR_MMARVALID_Msk);
    SCB->HFSR = SCB_HFSR_FORCED_Msk;
    frame[6] = (uintptr_t)boot_probe_load_failed;
    __DSB();
    __ISB();
}

void __attribute__((naked)) HardFault_Handler(void) {
    __asm volatile (
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b probe_recover_fault\n");
}
void BusFault_Handler(void) __attribute__((alias("HardFault_Handler")));
void MemManage_Handler(void) __attribute__((alias("HardFault_Handler")));

static uint8_t rom_hash(uint8_t *out) {
    // SHA-256 of AIR105_BOOTROM.bin in racerxdl/megahunt-bootroms.
    static const uint8_t air105[32] = {
        0xc5, 0xad, 0x3c, 0xa0, 0xea, 0x91, 0x7c, 0xca,
        0x24, 0xd6, 0x3e, 0x27, 0x22, 0x85, 0x47, 0x0a,
        0x1a, 0x2d, 0xe4, 0xd9, 0x6c, 0xff, 0x91, 0xa3,
        0x77, 0xa9, 0x29, 0xb8, 0xf8, 0xd0, 0xd4, 0x52,
    };
    // Public MH1903 dump. This is the exact hash reported by ForgeBox v3.1.
    static const uint8_t mh1903[32] = {
        0x96, 0xfc, 0x24, 0xb3, 0xd8, 0x78, 0x9a, 0x50,
        0x00, 0xd9, 0xef, 0xcc, 0x82, 0x45, 0xd4, 0xf1,
        0x2c, 0x34, 0x3f, 0xaa, 0x09, 0x47, 0x92, 0x57,
        0x44, 0x07, 0xc5, 0x74, 0x5b, 0x03, 0xfa, 0x3e,
    };
    uint32_t vectors[2];
    if (!probe_read_word(0, &vectors[0]) || !probe_read_word(4, &vectors[1]))
        return ROM_READ_FAULT;
    if (vectors[0] <= 0x20000000 || vectors[0] > 0x20100000 ||
        (vectors[0] & 3) || vectors[1] < 0x101 || vectors[1] >= ROM_SIZE || !(vectors[1] & 1))
        return ROM_NO_VECTORS;

    SHA256_CTX hash;
    sha256_Init(&hash);
    for (uint32_t addr = 0; addr < ROM_SIZE; addr += 64) {
        uint32_t chunk[16];
        for (unsigned i = 0; i < 16; i++) {
            if (!probe_read_word(addr + 4 * i, &chunk[i])) return ROM_READ_FAULT;
        }
        sha256_Update(&hash, (const uint8_t *)chunk, sizeof(chunk));
    }
    sha256_Final(&hash, out);
    if (!memcmp(out, air105, 32)) return ROM_AIR105;
    if (!memcmp(out, mh1903, 32)) return ROM_MH1903;
    return ROM_OTHER;
}

static void put_u32(uint8_t *out, uint32_t value) {
    memcpy(out, &value, sizeof(value));
}

void board_capture_boot_probe(void) {
    uint8_t report[HARDWARE_REPORT_SIZE] = {0};
    memcpy(report + 4, "FBB1", 4);
    report[8] = GetHardwareVersion();
    put_u32(report + 20, TST->TST_JTAG);
    put_u32(report + 24, TST->TST_ROM);
    report[28] = (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0;
    put_u32(report + 32, SCB->CPUID);
    put_u32(report + 36, SCB->VTOR);

    // Run this first so a missing ROM mapping never prevents the UART probe.
    board_probe_fingerprint(report + FINGERPRINT_OFFSET);
    report[9] = rom_hash(report + 40);
    OTP_PowerOn();  // Powers/clocks the read interface. No programming operation.
    uint32_t value;
    if (probe_read_word((uintptr_t)&OTP->RO, &value)) {
        report[73] |= 1;
        put_u32(report + 12, value);
    }
    if (probe_read_word((uintptr_t)&OTP->ROL, &value)) {
        report[73] |= 2;
        put_u32(report + 16, value);
    }
    report[72] = 0xff;
    bool mh1903 = report[9] == ROM_MH1903;
    if (report[9] == ROM_AIR105 || mh1903) {
        // AIR105 literal at 0x729c, MH1903 literal at 0x728c.
        if (!probe_read_word(mh1903 ? 0x40008e28 : 0x40008a28, &value) || !value)
            report[10] = OTP_UNREADABLE;
        else
            report[10] = value == UINT32_MAX ? OTP_ERASED : OTP_PROGRAMMED;
        if (mh1903) {
            // MH1903 TryBoot calls the 2048-bit verification path at 0x4d6e.
            if (report[10] == OTP_PROGRAMMED) report[11] = SIGNATURE_RSA;
            // The lockout checks at 0x50bc and 0x50de use this field.
            if (probe_read_word(0x4001f204, &value)) report[72] = (value >> 8) & 0x0f;
        } else {
            // AIR105 TryBoot at 0x4df0 checks the low 20 bits of this word.
            if (report[10] == OTP_PROGRAMMED && probe_read_word(0x40008c18, &value) && value)
                report[11] = (value & 0xfffff) == 0xa501 ? SIGNATURE_SM2 : SIGNATURE_RSA;
            report[72] = (SYSCTRL->RSVD_POR >> 16) & 0x0f;
        }
    }

    memcpy(probe_report + 4, report + 4, sizeof(report) - 4);
    __DMB();
    put_u32(probe_report, 1);
}

void board_read_boot_probe(unsigned offset, uint8_t *out, unsigned size) {
    if (offset <= sizeof(probe_report) && size <= sizeof(probe_report) - offset) {
        memcpy(out, probe_report + offset, size);
        if (offset <= TOUCH_OFFSET && offset + size >= TOUCH_OFFSET + 4)
            put_u32(out + TOUCH_OFFSET - offset, touch_report);
#ifdef FORGEBOX_FP_ENROLL_TEST
        if (offset <= FP_TEST_REPORT_OFFSET &&
            offset + size >= FP_TEST_REPORT_OFFSET + sizeof(fingerprint_test_report) + 4) {
            for (unsigned i = 0; i < FP_REPORT_FIELDS; i++)
                put_u32(out + FP_TEST_REPORT_OFFSET - offset + 4 * i, fingerprint_test_report[i]);
            put_u32(out + FP_TEST_REPORT_OFFSET - offset + sizeof(fingerprint_test_report),
                    fingerprint_test_sequence);
        }
#endif
    }
}

void board_boot_probe_update_touch(bool active, unsigned transitions) {
    touch_report = (transitions << 2) | (active ? 2u : 0u) | 1u;
}

void board_boot_probe_summary(char *out, unsigned size) {
    const char *rom = probe_report[9] == ROM_AIR105 ? "AIR105 reference matched" :
        probe_report[9] == ROM_MH1903 ? "MH1903 reference matched" :
        probe_report[9] == ROM_OTHER ? "Different ROM fingerprint" : "ROM unavailable";
    const char *rng = probe_report[FINGERPRINT_OFFSET + 9] == 1 ? "Two distinct replies" :
        probe_report[FINGERPRINT_OFFSET + 9] == 2 ? "Repeated reply" : "No valid sample pair";
    const uint8_t *count = probe_report + FINGERPRINT_OFFSET + 22;
    char count_text[40];
    if (count[0] == 1)
        snprintf(count_text, sizeof(count_text), "%u enrolled templates", count[1]);
    else
        snprintf(count_text, sizeof(count_text), "Count unavailable (status %u)", count[0]);
    snprintf(out, size, "Boot ROM and MAX32520 probe\n\n%s\nMAX32520 RNG: %s\n%s\n\n"
             "Connect USB for the full report.", rom, rng, count_text);
}
