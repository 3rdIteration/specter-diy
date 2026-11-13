#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/nlr.h"
#include "board_api.h"
#include "camera.h"
#include "pin_flash.h"
#include "device_diagnostics.h"
#include <string.h>
STATIC mp_obj_t fb_boot_flash_available(void) {
#if FORGEBOX_HOST
    return mp_const_false;
#else
    return mp_obj_new_bool(board_boot_flash_available());
#endif
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_boot_flash_available_obj, fb_boot_flash_available);
#if !FORGEBOX_HOST
STATIC mp_obj_t fb_boot_flash_read(mp_obj_t offset_arg, mp_obj_t data) {
    unsigned offset = mp_obj_get_int(offset_arg);
    mp_buffer_info_t buffer;
    mp_get_buffer_raise(data, &buffer, MP_BUFFER_WRITE);
    uint32_t words[256];
    if (!buffer.len || buffer.len > sizeof(words)) mp_raise_ValueError("invalid boot read length");
    if (!board_boot_flash_read(offset, words, buffer.len)) mp_raise_OSError(MP_EIO);
    memcpy(buffer.buf, words, buffer.len);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_boot_flash_read_obj, fb_boot_flash_read);
#endif
STATIC mp_obj_t fb_usb_serial_available(void) {
#if FORGEBOX_HOST
    return mp_const_false;
#else
    return mp_obj_new_bool(board_usb_serial_available());
#endif
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_usb_serial_available_obj, fb_usb_serial_available);
#if !FORGEBOX_HOST
STATIC mp_obj_t fb_usb_serial_enable(mp_obj_t enabled) {
    board_usb_serial_enable(mp_obj_is_true(enabled));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_usb_serial_enable_obj, fb_usb_serial_enable);
STATIC mp_obj_t fb_usb_serial_connected(void) {
    return mp_obj_new_bool(board_usb_serial_connected());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_usb_serial_connected_obj, fb_usb_serial_connected);
STATIC mp_obj_t fb_usb_serial_read(void) {
    uint8_t packet[64];
    unsigned size = board_usb_serial_read(packet);
    return size ? mp_obj_new_bytes(packet, size) : mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_usb_serial_read_obj, fb_usb_serial_read);
STATIC mp_obj_t fb_usb_serial_write(mp_obj_t data) {
    mp_buffer_info_t buffer;
    mp_get_buffer_raise(data, &buffer, MP_BUFFER_READ);
    if (!buffer.len || buffer.len >= 64) mp_raise_ValueError("invalid USB packet size");
    unsigned start = board_ticks();
    do {
        if (board_usb_serial_write(buffer.buf, buffer.len)) return mp_const_none;
        board_delay(1);
    } while (board_usb_serial_connected() && board_ticks() - start < 250);
    mp_raise_OSError(MP_EIO);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_usb_serial_write_obj, fb_usb_serial_write);
#endif
STATIC mp_obj_t fb_camera_start(void) {
    int status = board_camera_start();
    if (status) mp_raise_OSError(100 + status);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_camera_start_obj, fb_camera_start);
STATIC mp_obj_t fb_camera_stop(void) { board_camera_stop(); return mp_const_none; }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_camera_stop_obj, fb_camera_stop);
STATIC mp_obj_t fb_camera_read(void) {
    uint8_t result[CAMERA_MAX_QR_BYTES];
    int size = board_camera_read(result, sizeof(result));
    if (size < 0) mp_raise_OSError(200 - size);
    if (!size) return mp_const_none;
    mp_obj_t value = mp_obj_new_bytes(result, size);
    memset(result, 0, sizeof(result));
    return value;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_camera_read_obj, fb_camera_read);
STATIC mp_obj_t fb_camera_preview(mp_obj_t data) {
    mp_buffer_info_t b;
    mp_get_buffer_raise(data, &b, MP_BUFFER_WRITE);
    if (b.len != CAMERA_PREVIEW_BYTES && b.len != CAMERA_PREVIEW_FULL_BYTES)
        mp_raise_ValueError("invalid camera preview buffer size");
    return mp_obj_new_bool(board_camera_preview(b.buf, b.len));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_camera_preview_obj, fb_camera_preview);
STATIC mp_obj_t fb_camera_ui_phase(mp_obj_t phase) {
    board_camera_ui_phase(mp_obj_get_int(phase));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_camera_ui_phase_obj, fb_camera_ui_phase);
STATIC mp_obj_t fb_pin_read(mp_obj_t slot) {
    uint8_t data[PIN_SLOT_DATA_SIZE];
    if (!pin_flash_read(mp_obj_get_int(slot), data)) mp_raise_OSError(MP_EIO);
    return mp_obj_new_bytes(data, sizeof(data));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_pin_read_obj, fb_pin_read);
STATIC mp_obj_t fb_pin_write(mp_obj_t slot, mp_obj_t record) {
    mp_buffer_info_t data;
    mp_get_buffer_raise(record, &data, MP_BUFFER_READ);
    if (data.len != PIN_RECORD_SIZE) mp_raise_ValueError("invalid PIN record length");
    if (!pin_flash_write(mp_obj_get_int(slot), data.buf)) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_pin_write_obj, fb_pin_write);
STATIC mp_obj_t fb_pin_retire(mp_obj_t slot) {
    if (!pin_flash_retire(mp_obj_get_int(slot))) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_pin_retire_obj, fb_pin_retire);
STATIC mp_obj_t fb_storage_resume(void) {
    if (!wallet_flash_resume()) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_storage_resume_obj, fb_storage_resume);
STATIC mp_obj_t fb_storage_wipe(void) {
    if (!wallet_flash_wipe()) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_storage_wipe_obj, fb_storage_wipe);
STATIC mp_obj_t fb_gen1_enabled(void) {
#if FORGEBOX_HOST
    return mp_const_false;
#else
    return mp_obj_new_bool(board_gen1_enabled());
#endif
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_gen1_enabled_obj, fb_gen1_enabled);
STATIC mp_obj_t fb_fp_unlock_test_enabled(void) {
#if FORGEBOX_HOST
    return mp_const_false;
#else
    return mp_obj_new_bool(board_fp_unlock_test_enabled());
#endif
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_fp_unlock_test_enabled_obj, fb_fp_unlock_test_enabled);
STATIC mp_obj_t fb_fp_unlock_enabled(void) {
#if FORGEBOX_HOST
    return mp_const_false;
#else
    return mp_obj_new_bool(board_fp_unlock_enabled());
#endif
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_fp_unlock_enabled_obj, fb_fp_unlock_enabled);
#if !FORGEBOX_HOST
STATIC mp_obj_t fp_progress_callback;

STATIC void fb_fp_progress(unsigned steps, uint8_t status) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_call_function_2(fp_progress_callback, MP_OBJ_NEW_SMALL_INT(steps),
                           MP_OBJ_NEW_SMALL_INT(status));
        nlr_pop();
    }
}

STATIC mp_obj_t fb_fp_unlock_test(mp_obj_t action_arg, mp_obj_t pin_arg, mp_obj_t progress_arg) {
    unsigned action = mp_obj_get_int(action_arg);
    mp_buffer_info_t pin = {0};
    if (action == 1 || action == 5) {
        mp_get_buffer_raise(pin_arg, &pin, MP_BUFFER_READ);
        if (pin.len != 32) mp_raise_ValueError("invalid fingerprint test PIN key");
    }
    uint8_t result[64];
    fp_progress_callback = progress_arg;
    int status = board_fp_unlock_test(action, pin.buf, result,
                                    progress_arg == mp_const_none ? NULL : fb_fp_progress);
    fp_progress_callback = MP_OBJ_NULL;
    mp_obj_t out = action == 2 && !status ? mp_obj_new_bytearray(sizeof(result), result)
                                         : MP_OBJ_NEW_SMALL_INT(status);
    volatile uint8_t *bytes = result;
    for (unsigned i = 0; i < sizeof(result); i++) bytes[i] = 0;
    return out;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(fb_fp_unlock_test_obj, fb_fp_unlock_test);

STATIC mp_obj_t fb_gen1_state(void) {
    uint8_t salt[GEN1_SALT_SIZE];
    int attempts = board_gen1_state(salt);
    if (attempts < 0) mp_raise_OSError(MP_EIO);
    if (!attempts) return mp_const_none;
    mp_obj_t state[] = {mp_obj_new_bytes(salt, sizeof(salt)), MP_OBJ_NEW_SMALL_INT(attempts)};
    return mp_obj_new_tuple(2, state);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_gen1_state_obj, fb_gen1_state);

STATIC mp_obj_t fb_gen1_root_bytes(uint8_t *root) {
    mp_obj_t result = mp_obj_new_bytes(root, 32);
    volatile uint8_t *bytes = root;
    for (unsigned i = 0; i < 32; i++) bytes[i] = 0;
    return result;
}

STATIC mp_obj_t fb_gen1_enroll(mp_obj_t salt_arg, mp_obj_t pin_arg) {
    mp_buffer_info_t salt, pin;
    uint8_t root[32];
    mp_get_buffer_raise(salt_arg, &salt, MP_BUFFER_READ);
    mp_get_buffer_raise(pin_arg, &pin, MP_BUFFER_READ);
    if (salt.len != GEN1_SALT_SIZE || pin.len != 32) mp_raise_ValueError("invalid Gen 1 input");
    if (!board_gen1_enroll(salt.buf, pin.buf, root)) mp_raise_OSError(MP_EIO);
    return fb_gen1_root_bytes(root);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_gen1_enroll_obj, fb_gen1_enroll);

STATIC mp_obj_t fb_gen1_unlock(mp_obj_t pin_arg) {
    mp_buffer_info_t pin;
    uint8_t root[32];
    mp_get_buffer_raise(pin_arg, &pin, MP_BUFFER_READ);
    if (pin.len != 32) mp_raise_ValueError("invalid Gen 1 PIN key");
    int attempts = board_gen1_unlock(pin.buf, root);
    if (attempts == -2) return fb_gen1_root_bytes(root);
    if (attempts < 0) mp_raise_OSError(MP_EIO);
    return MP_OBJ_NEW_SMALL_INT(attempts);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_gen1_unlock_obj, fb_gen1_unlock);
STATIC mp_obj_t fb_gen1_change_pin(mp_obj_t old_arg, mp_obj_t new_arg) {
    mp_buffer_info_t old, new;
    mp_get_buffer_raise(old_arg, &old, MP_BUFFER_READ);
    mp_get_buffer_raise(new_arg, &new, MP_BUFFER_READ);
    if (old.len != 32 || new.len != 32) mp_raise_ValueError("invalid PIN key");
    return MP_OBJ_NEW_SMALL_INT(board_gen1_change_pin(old.buf, new.buf));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_gen1_change_pin_obj, fb_gen1_change_pin);

STATIC mp_obj_t fb_gen1_wipe(void) {
    if (!board_gen1_wipe()) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_gen1_wipe_obj, fb_gen1_wipe);
STATIC mp_obj_t fb_gen1_seed_entropy(mp_obj_t root_arg, mp_obj_t pin_arg, mp_obj_t output_arg) {
    mp_buffer_info_t root, pin, output;
    mp_get_buffer_raise(root_arg, &root, MP_BUFFER_READ);
    mp_get_buffer_raise(pin_arg, &pin, MP_BUFFER_READ);
    mp_get_buffer_raise(output_arg, &output, MP_BUFFER_WRITE);
    if (root.len != 32 || pin.len != 32 || output.len != 32)
        mp_raise_ValueError("invalid seed entropy buffers");
    return MP_OBJ_NEW_SMALL_INT(board_gen1_seed_entropy(root.buf, pin.buf, output.buf));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(fb_gen1_seed_entropy_obj, fb_gen1_seed_entropy);
#endif
STATIC mp_obj_t fb_pin_failure(mp_obj_t slot, mp_obj_t index) {
    if (!pin_flash_record_failure(mp_obj_get_int(slot), mp_obj_get_int(index))) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_pin_failure_obj, fb_pin_failure);
STATIC mp_obj_t fb_seed_read(mp_obj_t slot) {
    uint8_t data[SEED_SLOT_DATA_SIZE];
    if (!seed_flash_read(mp_obj_get_int(slot), data)) mp_raise_OSError(MP_EIO);
    return mp_obj_new_bytes(data, sizeof(data));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_seed_read_obj, fb_seed_read);
STATIC mp_obj_t fb_seed_write(mp_obj_t slot, mp_obj_t record) {
    mp_buffer_info_t data;
    mp_get_buffer_raise(record, &data, MP_BUFFER_READ);
    if (data.len != SEED_RECORD_SIZE) mp_raise_ValueError("invalid seed record length");
    if (!seed_flash_write(mp_obj_get_int(slot), data.buf)) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_seed_write_obj, fb_seed_write);
STATIC mp_obj_t fb_seed_backup_read(mp_obj_t slot) {
    uint8_t data[SEED_CATALOG_SIZE];
    unsigned size;
    if (!seed_backup_read(mp_obj_get_int(slot), data, &size)) mp_raise_OSError(MP_EIO);
    return size ? mp_obj_new_bytes(data, size) : mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_seed_backup_read_obj, fb_seed_backup_read);
STATIC mp_obj_t fb_seed_catalog_write(mp_obj_t slot, mp_obj_t record) {
    mp_buffer_info_t data;
    mp_get_buffer_raise(record, &data, MP_BUFFER_READ);
    if (data.len != SEED_CATALOG_SIZE) mp_raise_ValueError("invalid seed catalog length");
    if (!seed_catalog_write(mp_obj_get_int(slot), data.buf)) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_seed_catalog_write_obj, fb_seed_catalog_write);
STATIC mp_obj_t fb_wallet_spi_capacity(void) {
    return mp_obj_new_int_from_uint(wallet_spi_capacity());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_wallet_spi_capacity_obj, fb_wallet_spi_capacity);
STATIC mp_obj_t fb_wallet_spi_jedec_id(void) {
    return mp_obj_new_int_from_uint(wallet_spi_jedec_id());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_wallet_spi_jedec_id_obj, fb_wallet_spi_jedec_id);
STATIC mp_obj_t fb_wallet_spi_erased_sectors(mp_obj_t first, mp_obj_t count) {
    uint32_t mask;
    if (!wallet_spi_erased_sectors(mp_obj_get_int(first), mp_obj_get_int(count), &mask))
        mp_raise_OSError(MP_EIO);
    return mp_obj_new_int_from_uint(mask);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_wallet_spi_erased_sectors_obj, fb_wallet_spi_erased_sectors);
STATIC mp_obj_t fb_descriptor_flash_read(mp_obj_t bank_arg) {
    unsigned bank = mp_obj_get_int(bank_arg);
    int size = descriptor_flash_read(bank, NULL);
    if (size < 0) mp_raise_OSError(MP_EIO);
    if (!size) return mp_const_none;
    vstr_t data;
    vstr_init_len(&data, size);
    if (descriptor_flash_read(bank, (uint8_t *)data.buf) != size) mp_raise_OSError(MP_EIO);
    return mp_obj_new_str_from_vstr(&mp_type_bytes, &data);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_descriptor_flash_read_obj, fb_descriptor_flash_read);
STATIC mp_obj_t fb_descriptor_flash_write(mp_obj_t bank, mp_obj_t record) {
    mp_buffer_info_t data;
    mp_get_buffer_raise(record, &data, MP_BUFFER_READ);
    if (!descriptor_flash_write(mp_obj_get_int(bank), data.buf, data.len)) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(fb_descriptor_flash_write_obj, fb_descriptor_flash_write);
STATIC mp_obj_t fb_device_diagnostics(void) {
    DeviceDiagnostics result;
    board_device_diagnostics(&result);
    mp_obj_t fields[] = {
        mp_obj_new_str(result.board_revision, strlen(result.board_revision)),
        mp_obj_new_bool(result.tamper_high),
        mp_obj_new_bool(result.fingerprint_irq_high),
        result.ds28s60_responded ? mp_obj_new_int(result.ds28s60_protection) : mp_const_none,
        result.atecc_responded ? mp_obj_new_bytes(result.atecc_revision, sizeof(result.atecc_revision)) : mp_const_none,
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(fields), fields);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_device_diagnostics_obj, fb_device_diagnostics);
STATIC mp_obj_t fb_cpu_debug_status(void) {
    uint32_t jtag_control;
    bool halting_debug_enabled;
    if (!board_cpu_debug_status(&jtag_control, &halting_debug_enabled)) return mp_const_none;
    mp_obj_t fields[] = {
        mp_obj_new_int_from_uint(jtag_control), mp_obj_new_bool(halting_debug_enabled),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(fields), fields);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_cpu_debug_status_obj, fb_cpu_debug_status);
STATIC mp_obj_t fb_battery_status(void) {
    uint32_t millivolts;
    unsigned percent;
    bool charging;
    if (!board_battery_read(&millivolts, &percent, &charging)) return mp_const_none;
    mp_obj_t fields[] = {
        mp_obj_new_int_from_uint(percent), mp_obj_new_bool(charging),
        mp_obj_new_int_from_uint(millivolts),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(fields), fields);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_battery_status_obj, fb_battery_status);
STATIC mp_obj_t fb_reset(void) { board_reset(); return mp_const_none; }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_reset_obj, fb_reset);
STATIC mp_obj_t fb_power_off(void) {
    if (!board_power_off()) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_power_off_obj, fb_power_off);
STATIC mp_obj_t fb_present(void) { return mp_obj_new_bool(board_sd_present()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_present_obj, fb_present);
STATIC mp_obj_t fb_init(void) { return mp_obj_new_bool(board_sd_init()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_init_obj, fb_init);
STATIC mp_obj_t fb_blocks(void) { return mp_obj_new_int_from_uint(board_sd_blocks()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_blocks_obj, fb_blocks);
STATIC mp_obj_t fb_scratch(void) { return mp_obj_new_int_from_uint(board_scratch()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_scratch_obj, fb_scratch);
STATIC mp_obj_t fb_io(mp_obj_t wr, mp_obj_t block, mp_obj_t data) {
    mp_buffer_info_t b;
    bool write = mp_obj_is_true(wr);
    mp_get_buffer_raise(data, &b, write ? MP_BUFFER_READ : MP_BUFFER_WRITE);
    mp_int_t n = mp_obj_get_int(block);
    if (n < 0 || b.len % 512) mp_raise_ValueError("unaligned SD block request");
    if (!board_sd_io(write, n, b.buf, b.len)) mp_raise_OSError(MP_EIO);
    return MP_OBJ_NEW_SMALL_INT(0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(fb_io_obj, fb_io);
#if FORGEBOX_HOST
extern void board_test_camera_frame(const uint8_t *, size_t);
extern bool board_test_camera_active(void);
STATIC mp_obj_t fb_fixture(mp_obj_t data) {
    mp_buffer_info_t b;
    mp_get_buffer_raise(data, &b, MP_BUFFER_READ);
    if (b.len > CAMERA_MAX_QR_BYTES) mp_raise_ValueError("fixture too large");
    board_test_camera_frame(b.buf, b.len); return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_fixture_obj, fb_fixture);
STATIC mp_obj_t fb_active(void) { return mp_obj_new_bool(board_test_camera_active()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_active_obj, fb_active);
STATIC mp_obj_t fb_insert(mp_obj_t present) { board_test_sd_insert(mp_obj_is_true(present)); return mp_const_none; }
STATIC MP_DEFINE_CONST_FUN_OBJ_1(fb_insert_obj, fb_insert);
STATIC mp_obj_t fb_writes(void) { return mp_obj_new_int(board_test_sd_writes()); }
STATIC MP_DEFINE_CONST_FUN_OBJ_0(fb_writes_obj, fb_writes);
#endif
STATIC const mp_rom_map_elem_t fb_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_forgebox)},
    {MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&fb_reset_obj)},
    {MP_ROM_QSTR(MP_QSTR_power_off), MP_ROM_PTR(&fb_power_off_obj)},
    {MP_ROM_QSTR(MP_QSTR_sd_present), MP_ROM_PTR(&fb_present_obj)},
    {MP_ROM_QSTR(MP_QSTR_sd_init), MP_ROM_PTR(&fb_init_obj)},
    {MP_ROM_QSTR(MP_QSTR_sd_blocks), MP_ROM_PTR(&fb_blocks_obj)},
    {MP_ROM_QSTR(MP_QSTR_sd_io), MP_ROM_PTR(&fb_io_obj)},
    {MP_ROM_QSTR(MP_QSTR_scratch), MP_ROM_PTR(&fb_scratch_obj)},
    {MP_ROM_QSTR(MP_QSTR_camera_start), MP_ROM_PTR(&fb_camera_start_obj)},
    {MP_ROM_QSTR(MP_QSTR_camera_stop), MP_ROM_PTR(&fb_camera_stop_obj)},
    {MP_ROM_QSTR(MP_QSTR_camera_read), MP_ROM_PTR(&fb_camera_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_camera_preview), MP_ROM_PTR(&fb_camera_preview_obj)},
    {MP_ROM_QSTR(MP_QSTR_camera_ui_phase), MP_ROM_PTR(&fb_camera_ui_phase_obj)},
    {MP_ROM_QSTR(MP_QSTR_pin_flash_read), MP_ROM_PTR(&fb_pin_read_obj)},
#ifndef FORGEBOX_PRODUCTION
    {MP_ROM_QSTR(MP_QSTR_pin_flash_write), MP_ROM_PTR(&fb_pin_write_obj)},
#endif
#ifndef FORGEBOX_PRODUCTION
    {MP_ROM_QSTR(MP_QSTR_pin_flash_retire), MP_ROM_PTR(&fb_pin_retire_obj)},
#endif
    {MP_ROM_QSTR(MP_QSTR_wallet_flash_resume), MP_ROM_PTR(&fb_storage_resume_obj)},
    {MP_ROM_QSTR(MP_QSTR_wallet_flash_wipe), MP_ROM_PTR(&fb_storage_wipe_obj)},
    {MP_ROM_QSTR(MP_QSTR_gen1_enabled), MP_ROM_PTR(&fb_gen1_enabled_obj)},
    {MP_ROM_QSTR(MP_QSTR_fp_unlock_test_enabled), MP_ROM_PTR(&fb_fp_unlock_test_enabled_obj)},
    {MP_ROM_QSTR(MP_QSTR_fp_unlock_enabled), MP_ROM_PTR(&fb_fp_unlock_enabled_obj)},
#if !FORGEBOX_HOST
    {MP_ROM_QSTR(MP_QSTR_fp_unlock_test), MP_ROM_PTR(&fb_fp_unlock_test_obj)},
    {MP_ROM_QSTR(MP_QSTR_gen1_state), MP_ROM_PTR(&fb_gen1_state_obj)},
    {MP_ROM_QSTR(MP_QSTR_gen1_enroll), MP_ROM_PTR(&fb_gen1_enroll_obj)},
    {MP_ROM_QSTR(MP_QSTR_gen1_unlock), MP_ROM_PTR(&fb_gen1_unlock_obj)},
    {MP_ROM_QSTR(MP_QSTR_gen1_change_pin), MP_ROM_PTR(&fb_gen1_change_pin_obj)},
    {MP_ROM_QSTR(MP_QSTR_gen1_wipe), MP_ROM_PTR(&fb_gen1_wipe_obj)},
    {MP_ROM_QSTR(MP_QSTR_gen1_seed_entropy), MP_ROM_PTR(&fb_gen1_seed_entropy_obj)},
#endif
#ifndef FORGEBOX_PRODUCTION
    {MP_ROM_QSTR(MP_QSTR_pin_flash_failure), MP_ROM_PTR(&fb_pin_failure_obj)},
#endif
    {MP_ROM_QSTR(MP_QSTR_seed_flash_read), MP_ROM_PTR(&fb_seed_read_obj)},
#ifndef FORGEBOX_PRODUCTION
    {MP_ROM_QSTR(MP_QSTR_seed_flash_write), MP_ROM_PTR(&fb_seed_write_obj)},
#endif
    {MP_ROM_QSTR(MP_QSTR_seed_backup_read), MP_ROM_PTR(&fb_seed_backup_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_seed_catalog_write), MP_ROM_PTR(&fb_seed_catalog_write_obj)},
    {MP_ROM_QSTR(MP_QSTR_wallet_spi_capacity), MP_ROM_PTR(&fb_wallet_spi_capacity_obj)},
    {MP_ROM_QSTR(MP_QSTR_usb_serial_available), MP_ROM_PTR(&fb_usb_serial_available_obj)},
#ifndef FORGEBOX_PRODUCTION
    {MP_ROM_QSTR(MP_QSTR_boot_flash_available), MP_ROM_PTR(&fb_boot_flash_available_obj)},
#endif
#if !FORGEBOX_HOST
#ifndef FORGEBOX_PRODUCTION
    {MP_ROM_QSTR(MP_QSTR_boot_flash_read), MP_ROM_PTR(&fb_boot_flash_read_obj)},
#endif
    {MP_ROM_QSTR(MP_QSTR_usb_serial_enable), MP_ROM_PTR(&fb_usb_serial_enable_obj)},
    {MP_ROM_QSTR(MP_QSTR_usb_serial_connected), MP_ROM_PTR(&fb_usb_serial_connected_obj)},
    {MP_ROM_QSTR(MP_QSTR_usb_serial_read), MP_ROM_PTR(&fb_usb_serial_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_usb_serial_write), MP_ROM_PTR(&fb_usb_serial_write_obj)},
#endif
    {MP_ROM_QSTR(MP_QSTR_wallet_spi_jedec_id), MP_ROM_PTR(&fb_wallet_spi_jedec_id_obj)},
#ifndef FORGEBOX_PRODUCTION
    {MP_ROM_QSTR(MP_QSTR_wallet_spi_erased_sectors), MP_ROM_PTR(&fb_wallet_spi_erased_sectors_obj)},
#endif
    {MP_ROM_QSTR(MP_QSTR_descriptor_flash_read), MP_ROM_PTR(&fb_descriptor_flash_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_descriptor_flash_write), MP_ROM_PTR(&fb_descriptor_flash_write_obj)},
    {MP_ROM_QSTR(MP_QSTR_device_diagnostics), MP_ROM_PTR(&fb_device_diagnostics_obj)},
    {MP_ROM_QSTR(MP_QSTR_cpu_debug_status), MP_ROM_PTR(&fb_cpu_debug_status_obj)},
    {MP_ROM_QSTR(MP_QSTR_battery_status), MP_ROM_PTR(&fb_battery_status_obj)},
#if FORGEBOX_HOST
    {MP_ROM_QSTR(MP_QSTR__test_camera_frame), MP_ROM_PTR(&fb_fixture_obj)},
    {MP_ROM_QSTR(MP_QSTR__test_camera_active), MP_ROM_PTR(&fb_active_obj)},
    {MP_ROM_QSTR(MP_QSTR__test_sd_insert), MP_ROM_PTR(&fb_insert_obj)},
    {MP_ROM_QSTR(MP_QSTR__test_sd_writes), MP_ROM_PTR(&fb_writes_obj)},
#endif
};
STATIC MP_DEFINE_CONST_DICT(fb_globals, fb_globals_table);
const mp_obj_module_t fb_module = {{&mp_type_module}, (mp_obj_dict_t *)&fb_globals};
MP_REGISTER_MODULE(MP_QSTR_forgebox, fb_module, 1);
