// Camera pin/clock configuration adapted from the official ForgeBox
// drv_qrdecode.c (YANSSIE HK LIMITED; see THIRD_PARTY_NOTICES.md).
#include <string.h>
#include "mhscpu.h"
#include "cmsis_os.h"
#include "decodelib.h"
#include "drv_i2c.h"
#include "user_memory.h"
#include "board_api.h"
#include "camera.h"

#define DECODE_POOL_BYTES (410 * 1024)
#define CAPTURE_TIMEOUT_MS 2000
// Match Keystone's internal-SRAM camera pool. The decoder uses it for
// frame DMA and DMA descriptors as well as CPU-side decoding.
static uint8_t pool[DECODE_POOL_BYTES] __attribute__((aligned(8)));
static uint8_t *preview;
static bool active, hardware_started, capturing, fresh_preview;
static volatile bool capture_error;
static unsigned capture_started;
static DecodeConfigTypeDef config;
static volatile CameraDiagnostic diagnostic;

void board_camera_diagnostic(CameraDiagnostic *snapshot) { *snapshot = diagnostic; }
void board_camera_ui_phase(unsigned phase) { diagnostic.ui_phase = phase; }

static void camera_clock(void) {
    SYSCTRL_ClocksTypeDef clocks;
    TIM_PWMInitTypeDef pwm = {0};
    SYSCTRL_APBPeriphClockCmd(SYSCTRL_APBPeriph_TIMM0, ENABLE);
    SYSCTRL_GetClocksFreq(&clocks);
    // Exactly the official driver's PCLK/2 camera clock (nominal comment: 24 MHz).
    uint32_t period = clocks.PCLK_Frequency / (clocks.PCLK_Frequency / 2);
    pwm.TIM_LowLevelPeriod = period / 2 - 1;
    pwm.TIM_HighLevelPeriod = period - pwm.TIM_LowLevelPeriod - 2;
    pwm.TIMx = TIM_5;
    TIM_PWMInit(TIMM0, &pwm);
    GPIO_PinRemapConfig(GPIOA, GPIO_Pin_5, GPIO_Remap_2);
    TIM_Cmd(TIMM0, TIM_5, ENABLE);
}

static void stop_hardware(void) {
    if (!hardware_started) return;
    NVIC_DisableIRQ(DCMI_IRQn);
    DCMI_CaptureCmd(DISABLE);
    if (active) CloseDecode();
    active = false;
    capturing = false;
    fresh_preview = false;
    TIM_Cmd(TIMM0, TIM_5, DISABLE);
    // Restore I2C0 for touchscreen polling after the shared camera SCCB bus.
    I2cInit();
    hardware_started = false;
}

static void wipe(uint8_t *data, size_t size) {
    volatile uint8_t *p = data;
    while (size--) *p++ = 0;
}

void board_camera_stop(void) {
    bool pool_in_use = hardware_started;
    stop_hardware();
    if (pool_in_use) wipe(pool, sizeof(pool));
    if (preview) { wipe(preview, CAMERA_PREVIEW_BYTES); ExtFree(preview); preview = NULL; }
}

int board_camera_start(void) {
    if (active) return 0;
    diagnostic.attempt++;
    diagnostic.phase = 1;
    diagnostic.status = 0;
    diagnostic.frames = 0;
    diagnostic.dcmi_errors = 0;
    preview = ExtMalloc(CAMERA_PREVIEW_BYTES);
    if (!preview) { diagnostic.status = -10; return -10; }
    memset(pool, 0, sizeof(pool));
    memset(preview, 0, CAMERA_PREVIEW_BYTES);
    // The decoder checks its MCU license in OTP. These operations enable/reset
    // the read peripheral, exactly as upstream; this code does not program OTP.
    SYSCTRL_AHBPeriphClockCmd(SYSCTRL_AHBPeriph_OTP, ENABLE);
    SYSCTRL_AHBPeriphResetCmd(SYSCTRL_AHBPeriph_OTP, ENABLE);
    SYSCTRL_APBPeriphClockCmd(SYSCTRL_APBPeriph_I2C0, ENABLE);
    I2C_DeInit(I2C0);
    GPIO_PinRemapConfig(GPIOB, GPIO_Pin_0, GPIO_Remap_0);
    GPIO_PinRemapConfig(GPIOB, GPIO_Pin_1, GPIO_Remap_0);
    camera_clock();
    hardware_started = true;
    DecodeInitTypeDef init = {0};
    init.pool = pool; init.size = sizeof(pool);
    init.CAM_PWDN_GPIOx = GPIOH; init.CAM_PWDN_GPIO_Pin = GPIO_Pin_9;
    init.CAM_RST_GPIOx = GPIOH; init.CAM_RST_GPIO_Pin = GPIO_Pin_8;
    init.CAM_I2Cx = I2C0; init.CAM_I2CClockSpeed = I2C_ClockSpeed_400KHz;
    diagnostic.phase = 2;
    int status = DecodeInit(&init);
    diagnostic.status = status;
    if (status != DecodeInitSuccess) { board_camera_stop(); return status; }
    active = true;
    DecodeConfigInit(&config);
    // Decode only QR codes. Preserve the vendor QR enhancement flags.
    config.cfgCODE128 = config.cfgCODE39 = config.cfgCODE93 = 0;
    config.cfgEAN13 = config.cfgEAN8 = config.cfgUPC_A = config.cfgUPC_E0 = 0;
    config.cfgUPC_E1 = config.cfgISBN13 = config.cfgInterleaved2of5 = 0;
    config.cfgPDF417 = config.cfgDataMatrix = 0;
    config.cfgQRCode |= 1U << DEQR_STATUS;
    DCMI_ClearITPendingBit(DCMI_IT_VSYNC | DCMI_IT_OVF | DCMI_IT_LINE | DCMI_IT_FRAME | DCMI_IT_ERR);
    DCMI_ITConfig(DCMI_IT_OVF | DCMI_IT_FRAME | DCMI_IT_ERR, ENABLE);
    NVIC_ClearPendingIRQ(DCMI_IRQn);
    NVIC_SetPriority(DCMI_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 1, 0));
    NVIC_EnableIRQ(DCMI_IRQn);
    capture_error = false;
    I2cInit();
    diagnostic.phase = 3;
    return 0;
}

int board_camera_read(uint8_t *result, size_t capacity) {
    if (!active) return -11;
    if (capacity < 256 || capacity > CAMERA_MAX_QR_BYTES) return -12;
    if (!capturing) {
        capture_error = false;
        diagnostic.phase = 4;
        DecodeDcmiStart();
        capturing = true;
        capture_started = board_ticks();
        return 0;
    }
    if (capture_error) { diagnostic.status = -13; board_camera_stop(); return -13; }
    if (!DecodeDcmiFinish()) {
        if ((unsigned)(board_ticks() - capture_started) >= CAPTURE_TIMEOUT_MS) {
            diagnostic.status = -14; board_camera_stop(); return -14;
        }
        return 0;
    }
    diagnostic.phase = 5;
    diagnostic.frames++;
    const uint8_t *gray = (const uint8_t *)GetImageBuffAddr();
    if (gray) { camera_convert_preview(gray, preview); fresh_preview = true; }
    DecodeResultTypeDef decoded = {.result = result, .maxn = capacity};
    // DecodeStart is the vendor's synchronous decoder. Frame waiting above is
    // cooperative/bounded; execution inside this binary routine is not timed out.
    int size = DecodeStart(&config, &decoded);
    diagnostic.phase = 6;
    diagnostic.status = size < 0 ? size : 0;
    capturing = false;
    if (size > 0) CleanDecodeBuffFlag();
    if (size > (int)capacity || decoded.resn > capacity) {
        diagnostic.status = -12;
        return -12;
    }
    if (size > 0 && decoded.id != ID_QRCODE) return 0;
    return size;
}

bool board_camera_preview(uint8_t *out, size_t capacity) {
    if (!active || !fresh_preview) return false;
    if (capacity == CAMERA_PREVIEW_FULL_BYTES) camera_expand_preview(preview, out);
    else if (capacity == CAMERA_PREVIEW_BYTES) memcpy(out, preview, CAMERA_PREVIEW_BYTES);
    else return false;
    fresh_preview = false;
    return true;
}

void DCMI_IRQHandler(void) {
    if (DCMI_GetITStatus(DCMI_IT_FRAME) != RESET) DCMI_CallBackFrame();
    if (DCMI_GetITStatus(DCMI_IT_OVF) != RESET || DCMI_GetITStatus(DCMI_IT_ERR) != RESET) {
        diagnostic.dcmi_errors++;
        capture_error = true;
    }
    DCMI_ClearITPendingBit(DCMI_IT_VSYNC | DCMI_IT_OVF | DCMI_IT_LINE | DCMI_IT_FRAME | DCMI_IT_ERR);
}
