#include <stdint.h>
#include <string.h>
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/objlist.h"
#include "lib/utils/gchelper.h"
#include "lib/utils/pyexec.h"
#include "user_memory.h"
#include "FreeRTOS.h"
#include "board_api.h"
#include "camera.h"

// Retain only an authenticated ciphertext across sleep. Power loss drops it.
static uint8_t *sleep_session;
static size_t sleep_session_size;
static bool sleep_requested;

void vm_request_sleep(const uint8_t *data, size_t size) {
    vm_clear_sleep_session();
    if (size) {
        // The RTOS heap is in retained internal SRAM.
        sleep_session = pvPortMalloc(size);
        if (sleep_session) {
            memcpy(sleep_session, data, size);
            sleep_session_size = size;
        }
    }
    sleep_requested = true;
}

const uint8_t *vm_sleep_session(size_t *size) {
    *size = sleep_session_size;
    return sleep_session;
}

void vm_clear_sleep_session(void) {
    volatile uint8_t *p = sleep_session;
    for (size_t i = 0; i < sleep_session_size; i++) p[i] = 0;
    vPortFree(sleep_session);
    sleep_session = NULL;
    sleep_session_size = 0;
}

void forgebox_lv_reinit(void);
void forgebox_psram_heap_reset(void);

void forgebox_vm(void) {
    mp_stack_ctrl_init();
    mp_stack_set_limit(88 * 1024);
    uint8_t *heap = ExtMalloc(5 * 1024 * 1024);
    if (!heap) for (;;) board_delay(1000);
    for (;;) {
        gc_init(heap, heap + 5 * 1024 * 1024);
        mp_init();
        // Never import executable Python/MPY files from removable media.
        mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_path), 0);
        sleep_requested = false;
        pyexec_frozen_module("forgebox_main.py");
        if (!sleep_requested) break;
        board_backlight(false);
        board_usb_serial_enable(false);
        board_camera_stop();
        mp_deinit();
        memset(&mp_state_ctx, 0, sizeof(mp_state_ctx));
        mp_stack_ctrl_init();
        mp_stack_set_limit(88 * 1024);
        board_clear_psram();
        board_deep_sleep();
        board_clear_psram();
        forgebox_psram_heap_reset();
        board_init_scratch();
        heap = ExtMalloc(5 * 1024 * 1024);
        if (!heap) board_reset();
        forgebox_lv_reinit();
    }
    vm_clear_sleep_session();
    board_startup_failed();
    // Fail closed: retain factory recovery through the independent button task.
    for (;;) board_delay(1000);
}

void gc_collect(void) {
    gc_collect_start();
    uintptr_t regs[10];
    uintptr_t sp = gc_helper_get_regs_and_sp(regs);
    gc_collect_root((void **)sp, ((uintptr_t)MP_STATE_THREAD(stack_top) - sp) / sizeof(uintptr_t));
    gc_collect_end();
}
void nlr_jump_fail(void *value) { (void)value; for (;;) board_delay(1000); }
void NORETURN __fatal_error(const char *msg) { (void)msg; for (;;) board_delay(1000); }
