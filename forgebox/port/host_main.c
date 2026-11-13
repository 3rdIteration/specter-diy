// Hosted test harness for the same VM, frozen application, FAT VFS and bindings.
// This does not emulate MH1903 peripherals or establish hardware correctness.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/objlist.h"
#include "lib/utils/pyexec.h"
#include "board_api.h"
#include "lvgl.h"
static uint8_t heap[16 * 1024 * 1024], scratch[1024 * 1024];
static lv_color_t pixels[480 * 16];
static lv_disp_buf_t draw;
static lv_color_t frame[480 * 800];
static bool initialized;
static bool card_inserted;
static unsigned sd_writes;
static uint8_t sd_image[4 * 1024 * 1024];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) frame[y * 480 + x] = *colors++;
    lv_disp_flush_ready(driver);
}
void board_display_init(void) {
    if (initialized) return;
    lv_init();
    lv_disp_buf_init(&draw, pixels, NULL, 480 * 16);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.buffer = &draw; driver.hor_res = 480; driver.ver_res = 800; driver.flush_cb = flush;
    lv_disp_drv_register(&driver);
    initialized = true;
}
void board_display_update(unsigned dt) { lv_tick_inc(dt); lv_task_handler(); }
void board_delay(unsigned ms) { usleep(ms * 1000); }
unsigned board_ticks(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
void board_reset(void) { exit(0); }
bool board_power_off(void) { return false; }
void board_backlight(bool on) { (void)on; }
uintptr_t board_scratch(void) { return (uintptr_t)scratch; }
bool board_random(uint8_t *data, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, data, len);
    close(fd);
    return n == (ssize_t)len;
}
// Host-only removable-card fixture; never compiled into the ARM firmware.
#include "camera.h"
#include "device_diagnostics.h"
bool board_battery_read(uint32_t *millivolts, unsigned *percent, bool *charging) {
    *millivolts = 3800;
    *percent = 52;
    *charging = false;
    return true;
}
bool board_cpu_debug_status(uint32_t *jtag_control, bool *halting_debug_enabled) {
    (void)jtag_control;
    (void)halting_debug_enabled;
    return false;
}

void board_device_diagnostics(DeviceDiagnostics *result) {
    *result = (DeviceDiagnostics){.board_revision = "Host fixture", .tamper_high = true};
}
static uint8_t camera_frame[CAMERA_MAX_QR_BYTES];
static size_t camera_size;
static bool camera_active;
void board_test_camera_frame(const uint8_t *data, size_t size) {
    memcpy(camera_frame, data, size); camera_size = size;
}
bool board_test_camera_active(void) { return camera_active; }
void board_camera_ui_phase(unsigned phase) { (void)phase; }
int board_camera_start(void) { camera_active = true; return 0; }
void board_camera_stop(void) { camera_active = false; }
int board_camera_read(uint8_t *out, size_t capacity) {
    if (!camera_active) return -11;
    if (camera_size > capacity) return -12;
    size_t size = camera_size;
    memcpy(out, camera_frame, size); memset(camera_frame, 0, sizeof(camera_frame));
    camera_size = 0; return size;
}
bool board_camera_preview(uint8_t *out, size_t capacity) {
    if (!camera_active) return false;
    static uint8_t gray[640 * 480];
    for (size_t i = 0; i < sizeof(gray); ++i) gray[i] = i % 251;
    if (capacity == CAMERA_PREVIEW_FULL_BYTES) {
        static uint8_t preview[CAMERA_PREVIEW_BYTES];
        camera_convert_preview(gray, preview);
        camera_expand_preview(preview, out);
    } else if (capacity == CAMERA_PREVIEW_BYTES) {
        camera_convert_preview(gray, out);
    } else return false;
    return true;
}
bool board_sd_present(void) { return card_inserted; }
static bool card_initialized;
bool board_sd_init(void) { return card_initialized = card_inserted; }
uint32_t board_sd_blocks(void) { return card_initialized ? sizeof(sd_image) / 512 : 0; }
void board_test_sd_insert(bool present) {
    if (card_inserted != present) card_initialized = false;
    card_inserted = present;
}
unsigned board_test_sd_writes(void) { return sd_writes; }
void board_startup_failed(void) { exit(2); }
bool board_sd_io(bool write, uint32_t block, uint8_t *data, size_t len) {
    if (!card_inserted || block >= board_sd_blocks() || len % 512 ||
        len / 512 > board_sd_blocks() - block) return false;
    if (write) { memcpy(sd_image + block * 512, data, len); sd_writes++; }
    else memcpy(data, sd_image + block * 512, len);
    return true;
}
void gc_collect(void) {
    jmp_buf regs;
    setjmp(regs);
    uintptr_t sp = (uintptr_t)&regs;
    gc_collect_start();
    gc_collect_root((void **)sp, ((uintptr_t)MP_STATE_THREAD(stack_top) - sp) / sizeof(uintptr_t));
    gc_collect_end();
}
void nlr_jump_fail(void *v) { fprintf(stderr, "uncaught NLR\n"); exit(2); }
void __fatal_error(const char *msg) { fprintf(stderr, "fatal: %s\n", msg); exit(2); }
int main(void) {
    mp_stack_ctrl_init(); mp_stack_set_limit(1024 * 1024);
    gc_init(heap, heap + sizeof(heap));
    mp_init(); mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_path), 0);
    int ok = pyexec_frozen_module("native_smoke.py");
    // Useful for checking the real Specter UI without access to a ForgeBox.
    FILE *image = fopen("build/host-screen.ppm", "wb");
    if (image) {
        fprintf(image, "P6\n480 800\n255\n");
        for (size_t i = 0; i < 480 * 800; ++i) {
            uint8_t rgb[] = {frame[i].ch.red, frame[i].ch.green, frame[i].ch.blue};
            fwrite(rgb, 1, 3, image);
        }
        fclose(image);
    }
    mp_deinit();
    return ok == 1 ? 0 : 1;
}
