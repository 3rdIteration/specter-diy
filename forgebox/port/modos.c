#include "py/runtime.h"
#include "py/mperrno.h"
#include "extmod/vfs.h"
#include "extmod/vfs_fat.h"
#include "board_api.h"
STATIC mp_obj_t os_urandom(mp_obj_t size) {
    mp_int_t n = mp_obj_get_int(size);
    if (n < 0 || n > 4096) mp_raise_ValueError("invalid random request");
    vstr_t data;
    vstr_init_len(&data, n);
    if (!board_random((uint8_t *)data.buf, n)) {
        vstr_clear(&data);
        mp_raise_OSError(MP_EIO);
    }
    return mp_obj_new_str_from_vstr(&mp_type_bytes, &data);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(os_urandom_obj, os_urandom);
STATIC mp_obj_t os_sync(void) { return mp_const_none; } // SD writes are synchronous; files close before unmount.
STATIC MP_DEFINE_CONST_FUN_OBJ_0(os_sync_obj, os_sync);
STATIC const mp_rom_map_elem_t os_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_uos)},
    {MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&mp_vfs_mount_obj)},
    {MP_ROM_QSTR(MP_QSTR_umount), MP_ROM_PTR(&mp_vfs_umount_obj)},
    {MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&mp_vfs_chdir_obj)},
    {MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&mp_vfs_getcwd_obj)},
    {MP_ROM_QSTR(MP_QSTR_ilistdir), MP_ROM_PTR(&mp_vfs_ilistdir_obj)},
    {MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&mp_vfs_listdir_obj)},
    {MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&mp_vfs_mkdir_obj)},
    {MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&mp_vfs_remove_obj)},
    {MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&mp_vfs_rename_obj)},
    {MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&mp_vfs_rmdir_obj)},
    {MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&mp_vfs_stat_obj)},
    {MP_ROM_QSTR(MP_QSTR_statvfs), MP_ROM_PTR(&mp_vfs_statvfs_obj)},
    {MP_ROM_QSTR(MP_QSTR_VfsFat), MP_ROM_PTR(&mp_fat_vfs_type)},
    {MP_ROM_QSTR(MP_QSTR_urandom), MP_ROM_PTR(&os_urandom_obj)},
    {MP_ROM_QSTR(MP_QSTR_sync), MP_ROM_PTR(&os_sync_obj)},
};
STATIC MP_DEFINE_CONST_DICT(os_globals, os_globals_table);
const mp_obj_module_t os_module = {{&mp_type_module}, (mp_obj_dict_t *)&os_globals};
MP_REGISTER_MODULE(MP_QSTR_uos, os_module, 1);
