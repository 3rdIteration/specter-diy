/*
 * SRAM-based RAM device for STM32U5G9J-DK2
 *
 * Replaces the external SDRAM module used on F469-DISCO.
 * The DK2 has 3 MB internal SRAM instead of 16 MB external SDRAM.
 *
 * Memory layout for the DK2 (3 MB total SRAM at 0x20000000):
 *   0x20000000 - 0x2023FFFF: MicroPython heap + stack (~2.25 MB)
 *   0x20240000 - 0x20300000: LTDC framebuffer (800*480*2 = 768 KB)
 *   0x20300000 - 0x20380000: RAMDevice filesystem (512 KB)
 *   0x20380000 - 0x20400000: Preallocated memory (512 KB)
 *
 * NOTE: The module is named 'sdram' in Python for compatibility with
 * the existing Specter-DIY codebase (platform.py imports sdram).
 */

#include <assert.h>
#include <string.h>
#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"

/* Preallocated memory region (512 KB) */
#define PREALLOCATED_SRAM_PTR   0x20380000
#define PREALLOCATED_SRAM_SIZE  0x80000   /* 512 KB */

/* RAMDevice filesystem region (512 KB) */
#define SRAM_START_ADDRESS  ((size_t)0x20300000)
#define SRAM_END_ADDRESS    ((size_t)0x20380000)

typedef struct _mp_obj_sdram_ramdevice_t {
    mp_obj_base_t base;
    size_t start;
    size_t len;
    size_t block_size;
} mp_obj_sdram_ramdevice_t;

/****************************** RAMDevice class ******************************/

STATIC mp_obj_t sdram_ramdevice_make_new(const mp_obj_type_t *type,
                                         size_t n_args, size_t n_kw,
                                         const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 3, false);
    mp_obj_sdram_ramdevice_t *o = m_new_obj(mp_obj_sdram_ramdevice_t);
    o->base.type = type;
    o->start = SRAM_START_ADDRESS;
    o->len = SRAM_END_ADDRESS - SRAM_START_ADDRESS;
    if (n_args + n_kw > 0) {
        o->block_size = mp_obj_get_int(args[0]);
    } else {
        o->block_size = 512;
    }
    return MP_OBJ_FROM_PTR(o);
}

STATIC mp_obj_t sdram_ramdevice_copy(mp_obj_t self_in) {
    mp_obj_sdram_ramdevice_t *o = m_new_obj(mp_obj_sdram_ramdevice_t);
    mp_obj_sdram_ramdevice_t *self = MP_OBJ_TO_PTR(self_in);
    o->base.type = self->base.type;
    o->start = self->start;
    o->len = self->len;
    o->block_size = self->block_size;
    return MP_OBJ_FROM_PTR(o);
}

STATIC mp_obj_t sdram_ramdevice_readblocks(mp_obj_t self_in,
                                            mp_obj_t block_num,
                                            mp_obj_t buf) {
    mp_obj_sdram_ramdevice_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t buffer;
    mp_get_buffer_raise(buf, &buffer, MP_BUFFER_WRITE);
    size_t start = self->start + mp_obj_get_int(block_num) * self->block_size;
    if (start + buffer.len > SRAM_END_ADDRESS) {
        mp_raise_ValueError("SRAM read out of bounds");
        return mp_const_none;
    }
    memcpy(buffer.buf, (uint8_t *)start, buffer.len);
    return mp_const_none;
}

STATIC mp_obj_t sdram_ramdevice_writeblocks(mp_obj_t self_in,
                                             mp_obj_t block_num,
                                             mp_obj_t buf) {
    mp_obj_sdram_ramdevice_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t buffer;
    mp_get_buffer_raise(buf, &buffer, MP_BUFFER_READ);
    size_t start = self->start + mp_obj_get_int(block_num) * self->block_size;
    if (start + buffer.len > SRAM_END_ADDRESS) {
        mp_raise_ValueError("SRAM write out of bounds");
        return mp_const_none;
    }
    memcpy((uint8_t *)start, buffer.buf, buffer.len);
    return mp_const_none;
}

STATIC mp_obj_t sdram_ramdevice_ioctl(mp_obj_t self_in,
                                       mp_obj_t op,
                                       mp_obj_t arg) {
    mp_obj_sdram_ramdevice_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t op_int = mp_obj_get_int(op);
    if (op_int == 4) {
        return mp_obj_new_int(self->len / self->block_size);
    } else if (op_int == 5) {
        return mp_obj_new_int(self->block_size);
    }
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(sdram_ramdevice_readblocks_obj, sdram_ramdevice_readblocks);
STATIC MP_DEFINE_CONST_FUN_OBJ_3(sdram_ramdevice_writeblocks_obj, sdram_ramdevice_writeblocks);
STATIC MP_DEFINE_CONST_FUN_OBJ_3(sdram_ramdevice_ioctl_obj, sdram_ramdevice_ioctl);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sdram_ramdevice_copy_obj, sdram_ramdevice_copy);

STATIC const mp_rom_map_elem_t sdram_ramdevice_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readblocks), MP_ROM_PTR(&sdram_ramdevice_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&sdram_ramdevice_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl), MP_ROM_PTR(&sdram_ramdevice_ioctl_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy), MP_ROM_PTR(&sdram_ramdevice_copy_obj) },
};

STATIC MP_DEFINE_CONST_DICT(sdram_ramdevice_dict, sdram_ramdevice_dict_table);

STATIC const mp_obj_type_t sdram_ramdevice_type = {
    { &mp_type_type },
    .name = MP_QSTR_ramdevice,
    .make_new = sdram_ramdevice_make_new,
    .locals_dict = (void *)&sdram_ramdevice_dict,
};

/* init() is a no-op on DK2 since we use internal SRAM (always available) */
STATIC mp_obj_t sdram_init(void) {
    /* No initialization needed - internal SRAM is always available */
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(sdram_init_obj, sdram_init);

/***************** Preallocated memory (512 KB) ***************/
STATIC mp_obj_t sdram_preallocated_ptr(void) {
    return mp_obj_new_int_from_ull(PREALLOCATED_SRAM_PTR);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(sdram_preallocated_ptr_obj, sdram_preallocated_ptr);

STATIC mp_obj_t sdram_preallocated_size(void) {
    return mp_obj_new_int_from_ull(PREALLOCATED_SRAM_SIZE);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(sdram_preallocated_size_obj, sdram_preallocated_size);

/****************************** MODULE ******************************/

STATIC const mp_rom_map_elem_t sdram_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sdram) },
    { MP_ROM_QSTR(MP_QSTR_RAMDevice), MP_ROM_PTR(&sdram_ramdevice_type) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&sdram_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_preallocated_ptr), MP_ROM_PTR(&sdram_preallocated_ptr_obj) },
    { MP_ROM_QSTR(MP_QSTR_preallocated_size), MP_ROM_PTR(&sdram_preallocated_size_obj) },
};

STATIC MP_DEFINE_CONST_DICT(sdram_module_globals, sdram_module_globals_table);

const mp_obj_module_t sdram_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sdram_module_globals,
};

/* Register as 'sdram' for compatibility with existing Python code */
MP_REGISTER_MODULE(MP_QSTR_sdram, sdram_user_cmodule, MODULE_SDRAM_ENABLED);
