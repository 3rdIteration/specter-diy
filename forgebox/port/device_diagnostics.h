#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *board_revision;
    bool tamper_high;
    bool fingerprint_irq_high;
    bool ds28s60_responded;
    uint8_t ds28s60_protection;
    bool atecc_responded;
    uint8_t atecc_revision[4];
} DeviceDiagnostics;

void board_device_diagnostics(DeviceDiagnostics *result);
bool board_cpu_debug_status(uint32_t *jtag_control, bool *halting_debug_enabled);

#define HARDWARE_REPORT_SIZE 217
void board_capture_hardware_report(void);
void board_read_hardware_report(unsigned offset, uint8_t *out, unsigned size);
#ifdef FORGEBOX_BOOT_PROBE
void board_capture_boot_probe(void);
void board_read_boot_probe(unsigned offset, uint8_t *out, unsigned size);
void board_boot_probe_update_touch(bool active, unsigned transitions);
void board_probe_fingerprint(uint8_t *out);
void board_boot_probe_summary(char *out, unsigned size);
#endif
#if defined(FORGEBOX_FP_ENROLL_TEST) || defined(FORGEBOX_FP_UNLOCK_TEST) || defined(FORGEBOX_FP_UNLOCK)
typedef enum {
    FP_TEST_COUNT, FP_TEST_ENROLL, FP_TEST_MATCH, FP_TEST_REMOVE,
} FingerprintTestAction;
// Count for FP_TEST_COUNT. Otherwise module status, or a negative transport error.
int board_fingerprint_test(FingerprintTestAction action, void (*progress)(unsigned, uint8_t));
#endif
#ifdef FORGEBOX_FP_ENROLL_TEST
typedef enum {
    FP_REPORT_ENROLLMENT, FP_REPORT_ENROLLED_SCAN, FP_REPORT_OTHER_SCAN,
    FP_REPORT_DELETE, FP_REPORT_COUNT, FP_REPORT_FIELDS,
} FingerprintReportField;
void board_fingerprint_test_report(FingerprintReportField field, int value);
#endif

bool board_fp_unlock_test_enabled(void);
bool board_fp_unlock_enabled(void);
int board_fp_unlock_test(unsigned action, const uint8_t *pin_key, uint8_t *result,
                         void (*progress)(unsigned, uint8_t));

#if defined(FORGEBOX_SE_PAIRING) || defined(FORGEBOX_GEN1_STORAGE) || defined(FORGEBOX_BOOT_PROBE)
typedef enum {
    PAIRING_MATCH = 1,
    PAIRING_NO_CHIP,
    PAIRING_CONFIG,
    PAIRING_POLICY,
    PAIRING_OTP,
    PAIRING_RNG,
    PAIRING_IDENTITY,
    PAIRING_ENCRYPTED_READ,
    PAIRING_CHALLENGE,
    PAIRING_REPLY,
    PAIRING_REJECTED,
    PAIRING_GENDIG,
    PAIRING_INCONSISTENT,
} PairingStatus;
#endif

#ifdef FORGEBOX_SE_PAIRING
PairingStatus board_pairing_status(bool atecc);
PairingStatus board_storage_probe_status(bool atecc);
PairingStatus board_slot13_probe_status(void);
#endif

#define GEN1_SALT_SIZE 22
bool board_gen1_enabled(void);
// State: 0 blank, positive attempts remaining, negative storage error.
int board_gen1_state(uint8_t *salt);
bool board_gen1_enroll(const uint8_t *salt, const uint8_t *pin_key, uint8_t *root);
// Unlock: -2 success, -1 storage error, 0 wiped, positive wrong PIN.
int board_gen1_unlock(const uint8_t *pin_key, uint8_t *root);
int board_gen1_change_pin(const uint8_t *old_pin, const uint8_t *new_pin);
bool board_gen1_wipe(void);
// Return zero on success, or the index of the failed entropy source.
int board_gen1_seed_entropy(const uint8_t *root, const uint8_t *pin_context, uint8_t *out);

#ifdef FORGEBOX_SE_STORAGE_WRITE
typedef enum {
    PAGE5_INSPECT = 0,
    PAGE5_BLANK,
    PAGE5_MARKER_PRESENT,
    PAGE5_OCCUPIED,
    PAGE5_WRITE_VERIFIED,
    PAGE5_RESTORE_VERIFIED,
    PAGE5_POLICY_FAILED,
    PAGE5_READ_FAILED,
    PAGE5_NO_CHANGE,
    PAGE5_READBACK_FAILED,
    PAGE5_UNEXPECTED_CONTENTS,
    PAGE5_RANDOM_FAILED,
} Page5TestStatus;

Page5TestStatus board_page5_test(Page5TestStatus requested);
#endif

#ifdef FORGEBOX_SE_PIN_TEST
typedef enum {
    PIN_TEST_INSPECT = 0,
    PIN_TEST_READY,
    PIN_TEST_EXHAUSTED,
    PIN_TEST_PARTIAL,
    PIN_TEST_REPLAY_BLOCKED,
    PIN_TEST_REBOOT_VERIFIED,
    PIN_TEST_PARTIAL_CLEARED,
    PIN_TEST_OCCUPIED,
    PIN_TEST_ERROR,
} PinTestStatus;

PinTestStatus board_pin_test(PinTestStatus requested);
#endif

#ifdef FORGEBOX_SE_RESET_TEST
typedef enum {
    RESET_TEST_INSPECT = 0,
    RESET_TEST_READY,
    RESET_TEST_PENDING_REBOOT,
    RESET_TEST_RECOVERED,
    RESET_TEST_PARTIAL_CLEARED,
    RESET_TEST_OCCUPIED,
    RESET_TEST_ERROR,
} ResetTestStatus;

ResetTestStatus board_reset_test(ResetTestStatus requested);
#endif
