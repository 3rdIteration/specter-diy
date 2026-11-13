#include <assert.h>
#include "lvgl.h"

static unsigned flushes;

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    (void)area;
    (void)pixels;
    flushes++;
    lv_disp_flush_ready(driver);
}

int main(void) {
    _Static_assert(LV_MEM_CUSTOM == 0 && LV_ENABLE_GC == 0,
                   "standalone LVGL must have its own memory pool");
    static lv_color_t pixels[480 * 16];
    static lv_disp_buf_t buffer;
    lv_init();
    lv_disp_buf_init(&buffer, pixels, NULL, 480 * 16);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.buffer = &buffer;
    driver.hor_res = 480;
    driver.ver_res = 800;
    driver.flush_cb = flush;
    lv_disp_t *display = lv_disp_drv_register(&driver);
    assert(display);
    lv_obj_t *label = lv_label_create(lv_scr_act(), NULL);
    assert(label);
    lv_label_set_text(label, "Secure-element pairing");
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_refr_now(display);
    assert(flushes > 0);
}
