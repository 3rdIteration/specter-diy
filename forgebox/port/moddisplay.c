#include "py/runtime.h"
#include "board_api.h"
STATIC mp_obj_t display_init(void) { board_display_init(); return mp_const_none; }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_init_obj, display_init);
STATIC mp_obj_t display_update(mp_obj_t dt) { board_display_update(mp_obj_get_int(dt)); return mp_const_none; }
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_update_obj, display_update);
STATIC mp_obj_t display_on(void) { board_backlight(true); return mp_const_none; }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_on_obj, display_on);
STATIC mp_obj_t display_off(void) { board_backlight(false); return mp_const_none; }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(display_off_obj, display_off);
STATIC mp_obj_t display_rotation(mp_obj_t value) {
    if (mp_obj_get_int(value) != 0) mp_raise_ValueError("rotation is fixed at portrait");
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_rotation_obj, display_rotation);
STATIC const mp_rom_map_elem_t display_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_udisplay)},
    {MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&display_init_obj)},
    {MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&display_update_obj)},
    {MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&display_on_obj)},
    {MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&display_off_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_rotation), MP_ROM_PTR(&display_rotation_obj)},
};
STATIC MP_DEFINE_CONST_DICT(display_globals, display_globals_table);
const mp_obj_module_t display_module = {{&mp_type_module}, (mp_obj_dict_t *)&display_globals};
#include "lv_mpy_example.c"
MP_REGISTER_MODULE(MP_QSTR_udisplay, display_module, 1);
MP_REGISTER_MODULE(MP_QSTR_lvgl, mp_module_lvgl, 1);
