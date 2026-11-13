#include "mhscpu.h"
#include "board_api.h"

enum { BOOT_FLASH_BASE = 0x01000000, BOOT_FLASH_SIZE = 0x81000 };
extern const char boot_flash_load[], boot_flash_fault[];

static __attribute__((naked, noinline)) bool read_word(uint32_t address, uint32_t *out) {
    __asm volatile (
        ".global boot_flash_load\n"
        "boot_flash_load:\n"
        "ldr r2, [r0]\n"
        "str r2, [r1]\n"
        "movs r0, #1\n"
        "bx lr\n"
        ".global boot_flash_fault\n"
        "boot_flash_fault:\n"
        "movs r0, #0\n"
        "bx lr\n");
}

static __attribute__((used, noinline)) void recover_read_fault(uint32_t *frame) {
    const uint32_t faults = SCB_CFSR_PRECISERR_Msk | SCB_CFSR_DACCVIOL_Msk;
    uint32_t flags = SCB->CFSR;
    if (frame[6] != (uintptr_t)boot_flash_load || !(flags & faults))
        for (;;) {}
    SCB->CFSR = flags & (faults | SCB_CFSR_BFARVALID_Msk | SCB_CFSR_MMARVALID_Msk);
    SCB->HFSR = SCB_HFSR_FORCED_Msk;
    frame[6] = (uintptr_t)boot_flash_fault;
    __DSB();
    __ISB();
}

void __attribute__((naked)) HardFault_Handler(void) {
    __asm volatile ("tst lr, #4\n"
                    "ite eq\n"
                    "mrseq r0, msp\n"
                    "mrsne r0, psp\n"
                    "b recover_read_fault\n");
}
void BusFault_Handler(void) __attribute__((alias("HardFault_Handler")));
void MemManage_Handler(void) __attribute__((alias("HardFault_Handler")));

bool board_boot_flash_read(unsigned offset, uint32_t *out, unsigned size) {
    if (offset > BOOT_FLASH_SIZE || size > BOOT_FLASH_SIZE - offset || (offset | size) & 3)
        return false;
    for (unsigned i = 0; i < size; i += 4)
        if (!read_word(BOOT_FLASH_BASE + offset + i, &out[i / 4])) return false;
    return true;
}
