#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CAMERA_PREVIEW_SIDE 240
#define CAMERA_PREVIEW_BYTES (CAMERA_PREVIEW_SIDE * CAMERA_PREVIEW_SIDE * 4)
#define CAMERA_PREVIEW_FULL_BYTES (4 * CAMERA_PREVIEW_BYTES)
#define CAMERA_MAX_QR_BYTES 4096
int board_camera_start(void);
void board_camera_stop(void);
int board_camera_read(uint8_t *result, size_t capacity);
bool board_camera_preview(uint8_t *out, size_t capacity);
void camera_convert_preview(const uint8_t *gray, uint8_t *out);
void camera_expand_preview(const uint8_t *preview, uint8_t *out);
typedef struct {
    uint32_t attempt;
    uint32_t phase;
    int32_t status;
    uint32_t frames;
    uint32_t dcmi_errors;
    uint32_t ui_phase;
} CameraDiagnostic;
void board_camera_diagnostic(CameraDiagnostic *snapshot);
void board_camera_ui_phase(unsigned phase);
