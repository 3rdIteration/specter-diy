/*
 * Display driver module for STM32U5G9J-DK2
 *
 * This is the MicroPython C module that provides the 'display' module
 * used by Specter-DIY's GUI layer. It initializes the LTDC-based
 * 800x480 RGB TFT LCD and capacitive touchscreen on the DK2 board.
 *
 * The Python-level API is identical to the F469 display module:
 *   display.init()       - Initialize display and touch
 *   display.update(dt)   - Process LVGL tasks (called in main loop)
 *   display.on()         - Turn display on
 *   display.off()        - Turn display off
 */

#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "lvgl.h"
#include "lv_stm_hal.h"

STATIC mp_obj_t display_init(void) {
    lv_init();
    tft_init();
    touchpad_init();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_init_obj, display_init);

STATIC mp_obj_t display_update(mp_obj_t dt_obj) {
    uint32_t dt = mp_obj_get_int(dt_obj);
    lv_tick_inc(dt);
    lv_task_handler();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_update_obj, display_update);

STATIC mp_obj_t display_on(void) {
    /* DK2: Enable LCD backlight via LTDC or GPIO */
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_on_obj, display_on);

STATIC mp_obj_t display_off(void) {
    /* DK2: Disable LCD backlight */
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_off_obj, display_off);

/****************************** MODULE ******************************/

STATIC const mp_rom_map_elem_t display_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&display_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&display_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&display_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&display_off_obj) },
};
STATIC MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

const mp_obj_module_t display_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_module_globals,
};

#include "lv_mpy_example.c"

MP_REGISTER_MODULE(MP_QSTR_udisplay, display_user_cmodule, MODULE_DISPLAY_ENABLED);
MP_REGISTER_MODULE(MP_QSTR_lvgl, mp_module_lvgl, MODULE_DISPLAY_ENABLED);
