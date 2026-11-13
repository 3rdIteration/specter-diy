#include <stdint.h>
#include <alloca.h>
#ifndef FORGEBOX_HOST
#define FORGEBOX_HOST (0)
#endif
#define MICROPY_HW_BOARD_NAME "ForgeBox"
#define MICROPY_HW_MCU_NAME "MH1903"
#define MICROPY_PY_SYS_PLATFORM "forgebox"
#define MICROPY_ENABLE_COMPILER (FORGEBOX_HOST)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_GC_STACK_ENTRY_TYPE uint32_t
#define MICROPY_ENABLE_FINALISER (1)
#define MICROPY_STACK_CHECK (1)
#define MICROPY_ENABLE_SCHEDULER (1)
#define MICROPY_MODULE_FROZEN_MPY (1)
#define MICROPY_QSTR_EXTRA_POOL mp_qstr_frozen_const_pool
#define MICROPY_MODULE_WEAK_LINKS (1)
#define MICROPY_LONGINT_IMPL MICROPY_LONGINT_IMPL_MPZ
#define MPZ_DIG_SIZE (16)
#define MICROPY_FLOAT_IMPL MICROPY_FLOAT_IMPL_FLOAT
#define MICROPY_ERROR_REPORTING MICROPY_ERROR_REPORTING_DETAILED
#define MICROPY_PY_BUILTINS_FROZENSET (1)
#define MICROPY_PY_ASYNC_AWAIT (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_PY_BUILTINS_STR_CENTER (1)
#define MICROPY_PY_BUILTINS_STR_PARTITION (1)
#define MICROPY_PY_BUILTINS_STR_SPLITLINES (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)
#define MICROPY_PY_ARRAY_SLICE_ASSIGN (1)
#define MICROPY_PY_BUILTINS_SLICE_ATTRS (1)
#define MICROPY_PY_BUILTINS_SLICE_INDICES (1)
#define MICROPY_PY_ALL_SPECIAL_METHODS (1)
#define MICROPY_PY_FUNCTION_ATTRS (1)
#define MICROPY_PY_DESCRIPTORS (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT (1)
#define MICROPY_PY_COLLECTIONS_DEQUE (1)
#define MICROPY_PY_IO_IOBASE (1)
#define MICROPY_PY_IO_FILEIO (1)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_PY_UJSON (1)
#define MICROPY_PY_URE (1)
#define MICROPY_PY_UBINASCII (1)
#define MICROPY_PY_UBINASCII_CRC32 (1)
#define MICROPY_PY_UCTYPES (1)
#define MICROPY_PY_UHEAPQ (1)
#define MICROPY_PY_USELECT (1)
#define MICROPY_EVENT_POLL_HOOK do { extern void board_delay(unsigned); board_delay(1); } while (0);
#define MICROPY_PY_UTIMEQ (1)
#define MICROPY_PY_UTIME_MP_HAL (1)
#define MICROPY_PY_UCRYPTOLIB (1)
#define MICROPY_SSL_MBEDTLS (1)
// Specter's user module provides sha256/sha512/ripemd160/pbkdf2/hmac.
#define MICROPY_PY_UHASHLIB (0)
#define MODULE_HASHLIB_ENABLED (1)
#define MODULE_QRCODE_ENABLED (1)
#define MICROPY_VFS (1)
#define MICROPY_READER_VFS (1)
#define MICROPY_FATFS_NORTC (1)
#define MICROPY_FATFS_RPATH (2)
#define MICROPY_FATFS_ENABLE_LFN (2)
#define MICROPY_FATFS_LFN_CODE_PAGE 437
#define MICROPY_ALLOC_PATH_MAX (256)
#define MICROPY_QSTR_BYTES_IN_HASH (2)
#define mp_type_fileio mp_type_vfs_fat_fileio
#define mp_type_textio mp_type_vfs_fat_textio
#define mp_import_stat mp_vfs_import_stat
#define mp_builtin_open mp_vfs_open
#define mp_builtin_open_obj mp_vfs_open_obj
#define MICROPY_PORT_BUILTINS {MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mp_builtin_open_obj)},
#define MP_STATE_PORT MP_STATE_VM
#include "lvgl/src/lv_misc/lv_gc.h"
#define MICROPY_PORT_ROOT_POINTERS LV_ROOTS void *mp_lv_user_data; const char *readline_hist[8];
#define MICROPY_MAKE_POINTER_CALLABLE(p) ((void *)((uintptr_t)(p) | 1))
#if FORGEBOX_HOST
#define UINT_FMT "%lu"
#define INT_FMT "%ld"
#else
#define UINT_FMT "%u"
#define INT_FMT "%d"
#endif
typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;
#define MP_PLAT_PRINT_STRN(str, len) mp_hal_stdout_tx_strn_cooked(str, len)
