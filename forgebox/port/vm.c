#include <stdint.h>
#include <string.h>
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/objlist.h"
#include "lib/utils/gchelper.h"
#include "lib/utils/pyexec.h"
#include "user_memory.h"
#include "board_api.h"

void forgebox_vm(void) {
    mp_stack_ctrl_init();
    mp_stack_set_limit(88 * 1024);
    uint8_t *heap = ExtMalloc(5 * 1024 * 1024);
    if (!heap) for (;;) board_delay(1000);
    gc_init(heap, heap + 5 * 1024 * 1024);
    mp_init();
    // Never import executable Python/MPY files from removable media.
    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_path), 0);
    pyexec_frozen_module("forgebox_main.py");
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
