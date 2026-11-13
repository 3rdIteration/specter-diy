#include "py/runtime.h"
#include "extmod/utime_mphal.h"
#include "lib/timeutils/timeutils.h"
STATIC mp_obj_t time_localtime(mp_obj_t seconds) {
    mp_int_t value = mp_obj_get_int(seconds);
    if (value < 0) mp_raise_ValueError("dates before 2000 are unsupported");
    timeutils_struct_time_t tm;
    timeutils_seconds_since_2000_to_struct_time(value, &tm);
    mp_obj_t tuple[] = {
        mp_obj_new_int(tm.tm_year), mp_obj_new_int(tm.tm_mon), mp_obj_new_int(tm.tm_mday),
        mp_obj_new_int(tm.tm_hour), mp_obj_new_int(tm.tm_min), mp_obj_new_int(tm.tm_sec),
        mp_obj_new_int(tm.tm_wday), mp_obj_new_int(tm.tm_yday),
    };
    return mp_obj_new_tuple(8, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(time_localtime_obj, time_localtime);
STATIC const mp_rom_map_elem_t time_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_utime)},
    {MP_ROM_QSTR(MP_QSTR_localtime), MP_ROM_PTR(&time_localtime_obj)},
    {MP_ROM_QSTR(MP_QSTR_gmtime), MP_ROM_PTR(&time_localtime_obj)},
    {MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&mp_utime_sleep_obj)},
    {MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&mp_utime_sleep_ms_obj)},
    {MP_ROM_QSTR(MP_QSTR_sleep_us), MP_ROM_PTR(&mp_utime_sleep_us_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&mp_utime_ticks_ms_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_us), MP_ROM_PTR(&mp_utime_ticks_us_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_cpu), MP_ROM_PTR(&mp_utime_ticks_cpu_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_add), MP_ROM_PTR(&mp_utime_ticks_add_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_diff), MP_ROM_PTR(&mp_utime_ticks_diff_obj)},
};
STATIC MP_DEFINE_CONST_DICT(time_globals, time_globals_table);
const mp_obj_module_t time_module = {{&mp_type_module}, (mp_obj_dict_t *)&time_globals};
MP_REGISTER_MODULE(MP_QSTR_utime, time_module, 1);
