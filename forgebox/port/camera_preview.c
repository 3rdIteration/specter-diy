#include "camera.h"
#include <string.h>
// The vendor interface documents a 640x480 Y-only frame. Match the official
// driver's portrait rotation/center crop, with no LCD writes outside LVGL.
void camera_convert_preview(const uint8_t *gray, uint8_t *out) {
    for (unsigned y = 0; y < CAMERA_PREVIEW_SIDE; ++y) {
        for (unsigned x = 0; x < CAMERA_PREVIEW_SIDE; ++x) {
            unsigned source_x = 80 + (CAMERA_PREVIEW_SIDE - 1 - y) * 2;
            unsigned source_y = x * 2;
            uint8_t value = gray[source_y * 640 + source_x];
            unsigned i = (y * CAMERA_PREVIEW_SIDE + x) * 4;
            out[i] = value; out[i + 1] = value; out[i + 2] = value; out[i + 3] = 0xff;
        }
    }
}

void camera_expand_preview(const uint8_t *preview, uint8_t *out) {
    const unsigned row_size = CAMERA_PREVIEW_SIDE * 4 * 2;
    for (unsigned y = 0; y < CAMERA_PREVIEW_SIDE; y++) {
        uint8_t *row = out + 2 * y * row_size;
        for (unsigned x = 0; x < CAMERA_PREVIEW_SIDE; x++) {
            const uint8_t *pixel = preview + (y * CAMERA_PREVIEW_SIDE + x) * 4;
            memcpy(row + x * 8, pixel, 4);
            memcpy(row + x * 8 + 4, pixel, 4);
        }
        memcpy(row + row_size, row, row_size);
    }
}
