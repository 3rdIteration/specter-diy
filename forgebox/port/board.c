#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "mhscpu.h"
#include "mhscpu_exti.h"
#include "mhscpu_rtc.h"
#include "mhscpu_sysctrl.h"
#include "mhscpu_wdt.h"
#include "mhscpu_trng.h"
#include "cmsis_os.h"
#include "drv_sys.h"
#include "drv_i2c.h"
#include "drv_gd25qxx.h"
#include "task.h"
#include "drv_power.h"
#include "drv_psram.h"
#include "drv_battery.h"
#include "drv_aw32001.h"
#include "drv_lcd_bright.h"
#include "hal_lcd.h"
#include "hal_touch.h"
#include "drv_sdcard.h"
#include "drv_sdio.h"
#include "low_power.h"
#include "user_memory.h"
#include "lvgl.h"
#include "board_api.h"
#include "camera.h"
#ifdef FORGEBOX_USB_DEBUG
#include "device_diagnostics.h"
#endif

#if defined(FORGEBOX_USB_DEBUG) || defined(FORGEBOX_USB_SERIAL)
void debug_usb_init(void);
#endif
#ifdef FORGEBOX_USB_DEBUG
#if defined(FORGEBOX_FP_ENROLL_TEST)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.1 Fingerprint enrollment test";
#elif defined(FORGEBOX_BOOT_PROBE)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.4 MAX32520 reply and touch probe";
#elif defined(FORGEBOX_SE_COMBINED_TEST)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.0 Gen 1 combined account test r2";
#elif defined(FORGEBOX_SE_RESET_TEST)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.0 SE wallet reset test";
#elif defined(FORGEBOX_SE_PIN_TEST)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.0 SE PIN retry test";
#elif defined(FORGEBOX_SE_STORAGE_WRITE)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.0 SE page 5 write test";
#elif defined(FORGEBOX_SE_PAIRING)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.0 SE storage probe r2";
#else
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.0 USB debug";
#endif
#elif defined(FORGEBOX_FP_UNLOCK_TEST)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.1 Specter fingerprint unlock test";
#elif defined(FORGEBOX_GEN1_STORAGE)
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v" FORGEBOX_VERSION;
#else
const char firmware_version[] __attribute__((section(".fixSection"))) = "Firmware v0.1.0 Specter MCU-only";
#endif
static lv_disp_buf_t draw_buf;
static lv_color_t pixels[480 * 16];
static uint16_t wire_pixels[480 * 16] __attribute__((aligned(4)));
static uint32_t sd_bounce[128];
static uint8_t *scratch;
static bool display_ready;
#if !defined(FORGEBOX_SE_PAIRING) && !defined(FORGEBOX_BOOT_PROBE) && !defined(FORGEBOX_USB_DEBUG)
#define FORGEBOX_IDLE_LOCK 1
#define IDLE_LOCK_MS (5u * 60u * 1000u)
static unsigned last_touch;
static volatile bool power_button_wake;
#endif
static lv_obj_t *splash_placeholder;
static bool sd_ready;
static bool rng_previous_valid;
static uint32_t rng_previous[4];
extern void forgebox_vm(void);
extern SDCardInfoStruct SDCardInfo;
extern void __fatal_error(const char *msg) __attribute__((noreturn));

bool board_usb_serial_available(void) {
#ifdef FORGEBOX_USB_SERIAL
    return true;
#else
    return false;
#endif
}
bool board_boot_flash_available(void) {
#ifdef FORGEBOX_BOOT_EXPORT
    return true;
#else
    return false;
#endif
}
#ifndef FORGEBOX_BOOT_EXPORT
bool board_boot_flash_read(unsigned offset, uint32_t *out, unsigned size) { return false; }
#endif
#ifndef FORGEBOX_USB_SERIAL
void board_usb_serial_enable(bool enable) {}
bool board_usb_serial_connected(void) { return false; }
unsigned board_usb_serial_read(uint8_t *out) { return 0; }
bool board_usb_serial_write(const uint8_t *data, unsigned size) { return false; }
#endif

void board_delay(unsigned ms) { osDelay(ms ? ms : 1); }
unsigned board_ticks(void) { return osKernelGetTickCount(); }
LowPowerState GetLowPowerState(void) { return LOW_POWER_STATE_WORKING; }
void board_reset(void) { NVIC_SystemReset(); }
bool board_power_off(void) {
    // The charger can keep the board powered over USB.
    if (Aw32001RefreshState() != 0 || GetUsbPowerState() == USB_POWER_STATE_CONNECT ||
        Aw32001PowerOff() != 0) return false;
    LcdBacklightOff();
    // Remain unavailable if the PMIC does not cut power immediately.
    for (;;) board_delay(100);
}
void board_init_scratch(void) {
    scratch = ExtMalloc(1024 * 1024);
    if (!scratch) __fatal_error("scratch allocation");
}
uintptr_t board_scratch(void) { return (uintptr_t)scratch; }
void board_backlight(bool on) { SetLcdBright(on ? 80 : 0); }

bool board_battery_read(uint32_t *millivolts, unsigned *percent, bool *charging) {
    // Keystone samples ADC4 ten times, discards the extremes and uses a
    // 6200 mV reference. Bound the wait rather than calling ADC_GetResult(),
    // whose SDK implementation spins indefinitely if conversion never finishes.
    const unsigned settle_samples = 7;
    const unsigned measurement_samples = 10;
    unsigned count = 0;
    uint32_t sum = 0, min = 0xfff, max = 0;
    unsigned started = board_ticks();
    ADC_StartCmd(ENABLE);
    ADC0->ADC_CR1 = (ADC0->ADC_CR1 & ~ADC_CR1_CHANNEL_MASK) | ADC_CHANNEL_4;
    while (count < settle_samples + measurement_samples &&
           (unsigned)(board_ticks() - started) < 50) {
        board_delay(1);
        if (!(ADC0->ADC_SR & ADC_SR_DONE_FLAG)) continue;
        uint32_t sample = ADC0->ADC_DATA & 0xfff;
        if (sample >= 0xf00) continue;
        if (count++ < settle_samples) continue;
        sum += sample;
        if (sample < min) min = sample;
        if (sample > max) max = sample;
    }
    ADC_StartCmd(DISABLE);
    if (count != settle_samples + measurement_samples) return false;
    uint32_t voltage = ADC_CalVoltage((sum - min - max) / (measurement_samples - 2), 6200);
    if (voltage < 2400 || voltage > 4500) return false;

    bool usb = Aw32001RefreshState() == 0 && GetUsbPowerState() == USB_POWER_STATE_CONNECT;
    ChargeState state = usb ? GetChargeState() : CHARGE_STATE_NOT_CHARGING;
    *charging = state == CHARGE_STATE_PRE_CHARGE || state == CHARGE_STATE_CHARGE;
    *millivolts = voltage;
    *percent = state == CHARGE_STATE_CHARGE_DONE ? 100 :
        GetBatteryPercentByMilliVolt(voltage, !usb);
    return true;
}

bool board_random(uint8_t *data, size_t len) {
    while (len) {
        uint32_t words[4];
        TRNG_Start(TRNG0);
        unsigned started = board_ticks();
        while (TRNG_Get(words, TRNG0) != 0) {
            if ((unsigned)(board_ticks() - started) > 1000) return false;
            board_delay(1);
        }
        bool all_zero = !(words[0] | words[1] | words[2] | words[3]);
        bool all_ones = (words[0] & words[1] & words[2] & words[3]) == UINT32_MAX;
        bool repeated = rng_previous_valid && !memcmp(words, rng_previous, sizeof(words));
        if (all_zero || all_ones || repeated) return false;
        memcpy(rng_previous, words, sizeof(words));
        rng_previous_valid = true;
        size_t n = len < sizeof(words) ? len : sizeof(words);
        memcpy(data, words, n);
        memset(words, 0, sizeof(words));
        data += n;
        len -= n;
    }
    return true;
}

bool board_sd_present(void) {
    if (!SdCardInsert()) { sd_ready = false; return false; }
    return true;
}
bool board_sd_init(void) {
    if (!board_sd_present()) return false;
    // FAT calls INIT for each new mount. A replacement card can be inserted
    // between mounts without the application ever observing an empty slot.
    sd_ready = SDCardSetup();
    return sd_ready;
}
uint32_t board_sd_blocks(void) { return sd_ready ? SdCardGetSectorCount() : 0; }
bool board_sd_io(bool write, uint32_t block, uint8_t *data, size_t len) {
    if (!board_sd_present() || !sd_ready || len % 512 || block >= board_sd_blocks()
        || len / 512 > board_sd_blocks() - block) return false;
    for (size_t offset = 0; offset < len; offset += 512, ++block) {
        // DMA uses a word-aligned internal-SRAM bounce buffer, never Python heap pointers.
        if (write) memcpy(sd_bounce, data + offset, 512);
        uint32_t address = SDCardInfo.Capacity > SDCardCapacityStandard ? block : block * 512;
        if (!SDIOTransferBlock(write ? SD_CMD_WRITE_SINGLE_BLOCK : SD_CMD_READ_SINGLE_BLOCK,
                address, write, (uint8_t *)sd_bounce, 512, 1)) {
            sd_ready = false;
            return false;
        }
        if (!write) memcpy(data + offset, sd_bounce, 512);
    }
    return true;
}

#if !defined(FORGEBOX_SE_PAIRING) && !defined(FORGEBOX_BOOT_PROBE)
static void show_splash(bool waking) {
    enum { WIDTH = 384, TITLE_HEIGHT = 54, LOGO_HEIGHT = 406, STATUS_HEIGHT = 44, ROWS = 16,
           LEFT = (480 - WIDTH) / 2, LOGO_TOP = (800 - LOGO_HEIGHT) / 2,
           TITLE_TOP = (LOGO_TOP - TITLE_HEIGHT) / 2,
           STATUS_TOP = (800 + LOGO_TOP + LOGO_HEIGHT - STATUS_HEIGHT) / 2 };
    extern const uint8_t specter_splash_pixels[];
    extern const uint8_t specter_wake_status_pixels[];
    const uint8_t *pixels = specter_splash_pixels;
    const struct { unsigned top, height; } strips[] = {
        {TITLE_TOP, TITLE_HEIGHT}, {LOGO_TOP, LOGO_HEIGHT}, {STATUS_TOP, STATUS_HEIGHT},
    };
    for (unsigned i = 0; i < sizeof(strips) / sizeof(strips[0]); i++) {
        if (waking && strips[i].top == STATUS_TOP) pixels = specter_wake_status_pixels;
        for (unsigned row = 0; row < strips[i].height; row += ROWS) {
            unsigned remaining = strips[i].height - row;
            unsigned rows = remaining < ROWS ? remaining : ROWS;
            unsigned bytes = rows * WIDTH * 2;
            unsigned y = strips[i].top + row;
            memcpy(wire_pixels, pixels, bytes);
            LcdDraw(LEFT, y, LEFT + WIDTH - 1, y + rows - 1, wire_pixels);
            while (LcdBusy()) {} // The DMA IRQ runs before the scheduler starts.
            pixels += bytes;
        }
    }
    board_backlight(true);
}
#endif

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    if (splash_placeholder && splash_placeholder == lv_scr_act()) {
        lv_disp_flush_ready(driver);
        return;
    }
    splash_placeholder = NULL;
    size_t count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    if (count > 480 * 16) __fatal_error("oversize display flush");
    for (size_t i = 0; i < count; ++i) {
        uint16_t c = lv_color_to16(colors[i]);
        wire_pixels[i] = (uint16_t)((c >> 8) | (c << 8)); // LCD's parallel bus is MSB first.
    }
    while (LcdBusy()) board_delay(1);
    LcdDraw(area->x1, area->y1, area->x2, area->y2, wire_pixels);
    while (LcdBusy()) board_delay(1);
    lv_disp_flush_ready(driver);
}
void board_clear_psram(void) {
    // PSRAM contents can survive a warm reset.
    volatile uint32_t *words = (volatile uint32_t *)GetPsramMappedAddr();
    for (size_t i = 0; i < GetPsramTotalSize() / sizeof(*words); i++) words[i] = 0;
}

#ifdef FORGEBOX_IDLE_LOCK
// PE14 is also the factory recovery button. Its EXTI records short
// presses that may have ended before the CPU finishes waking.
void EXTI4_IRQHandler(void) {
    power_button_wake = true;
    EXTI_ClearITPendingBit(EXTI_Line4);
    __DSB();
}

void board_deep_sleep(void) {
    // All wallet coroutines have exited. Clear the unused portion of the
    // downward-growing VM stack before putting the CPU to sleep.
    TaskStatus_t task;
    vTaskGetInfo(NULL, &task, pdFALSE, eInvalid);
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uintptr_t active_stack = __get_PSP();
    volatile uint32_t *stack = task.pxStackBase;
    while ((uintptr_t)(stack + 1) <= active_stack) *stack++ = 0;
    __set_PRIMASK(primask);
    board_backlight(false);
    board_usb_serial_enable(false);
    TouchClose();

    ClosePower(POWER_TYPE_VCC33);
    GPIO_ResetBits(GPIOG, GPIO_Pin_0);  // DS28S60 power

    // Shut down the scheduler's wake sources until the power button is pressed.
    // The wallet VM is recreated after wake.
    __disable_irq();
    uint32_t systick = SysTick->CTRL;
    SysTick->CTRL = 0;
    power_button_wake = false;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    unsigned interrupt_banks = (SCnSCB->ICTR & 0xfu) + 1u;
    uint32_t enabled[8];
    for (unsigned bank = 0; bank < interrupt_banks; bank++) {
        enabled[bank] = NVIC->ISER[bank];
        NVIC->ICER[bank] = UINT32_MAX;
        NVIC->ICPR[bank] = UINT32_MAX;
    }
    uint32_t watchdog = WDT->WDT_CR;
    WDT->WDT_CR &= ~WDT_CR_WDT_EN;
    WDT_ReloadCounter();

    RTC_ITConfig(DISABLE);
    GPIO->WAKE_P0_EN = 0;
    GPIO->WAKE_P1_EN = 0;
    GPIO->WAKE_P2_EN = 0;
    GPIO->WAKE_P3_EN = 0;
    GPIO->WAKE_TYPE_EN &= ~BIT(12);  // RTC wake enable
    GPIO_WakeEvenConfig(GPIO_PortSourceGPIOE, GPIO_Pin_14, ENABLE);
    GPIO_WakeModeConfig(GPIO_WakeMode_Now);
    EXTI_LineConfig(EXTI_Line4, EXTI_PinSource14, EXTI_Trigger_Falling);
    EXTI_ClearITPendingBit(EXTI_Line4);
    NVIC_ClearPendingIRQ(EXTI4_IRQn);
    NVIC_EnableIRQ(EXTI4_IRQn);

    // Use the same deep-sleep power gates and USB register values as
    // Keystone. Preserve the original settings for wake.
    const uint32_t msr = SYSCTRL->MSR_CR1;
    const uint32_t analog = SYSCTRL->ANA_CTRL;
    const uint32_t ldo = SYSCTRL->LDO25_CR;
    const uint32_t clock = SYSCTRL->FREQ_SEL;
    *(volatile uint32_t *)(USB_BASE + 0x60u) = 0x01;
    *(volatile uint32_t *)USB_BASE = 0x4100;
    UserDelayUs(1000);
    while (!power_button_wake &&
           !NVIC_GetPendingIRQ(EXTI4_IRQn) &&
           GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_14) != Bit_RESET) {
        SYSCTRL->MSR_CR1 = msr | BIT(27);
        SYSCTRL->ANA_CTRL = analog | BIT(7);
        SYSCTRL->LDO25_CR = ldo | BIT(4) | BIT(5);
        __DSB();
        SYSCTRL_EnterSleep(SleepMode_DeepSleep);
        __disable_irq();
        SYSCTRL->LDO25_CR = ldo;
        SYSCTRL->ANA_CTRL = analog;
        SYSCTRL->MSR_CR1 = msr;
        SYSCTRL->FREQ_SEL = clock;
        __DSB();
    }
    GPIO_WakeEvenConfig(GPIO_PortSourceGPIOE, GPIO_Pin_14, DISABLE);
    EXTI_LineConfig(EXTI_Line4, EXTI_PinSource14, EXTI_Trigger_Off);
    EXTI_ClearITPendingBit(EXTI_Line4);
    NVIC_DisableIRQ(EXTI4_IRQn);
    NVIC_ClearPendingIRQ(EXTI4_IRQn);
    WDT_ReloadCounter();
    WDT->WDT_CR = watchdog;
    for (unsigned bank = 0; bank < interrupt_banks; bank++) {
        NVIC->ICPR[bank] = UINT32_MAX;
        NVIC->ISER[bank] = enabled[bank];
    }
    SysTick->VAL = 0;
    SysTick->CTRL = systick;
    __enable_irq();
    OpenPower(POWER_TYPE_VCC33);
    board_delay(20);
    LcdInit();
    show_splash(true);
    while (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_14) == Bit_RESET) board_delay(20);
    PsramOpen();
    sd_ready = false;
    I2cInit();
    Gd25FlashOpen();
#if defined(FORGEBOX_USB_SERIAL)
    debug_usb_init();
#endif
    display_ready = false;
    splash_placeholder = NULL;
}
#else
void board_deep_sleep(void) { board_reset(); }
#endif

bool board_idle_timeout(void) {
#ifdef FORGEBOX_IDLE_LOCK
    return display_ready && board_ticks() - last_touch >= IDLE_LOCK_MS;
#else
    return false;
#endif
}

static bool touch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver;
    static TouchStatus_t status;
    TouchStatus_t next = {0};
    if (TouchGetStatus(&next) == 0) {
        status = next;
#ifdef FORGEBOX_IDLE_LOCK
        if (status.touch) last_touch = board_ticks();
#endif
    }
    data->point.x = status.x < 480 ? status.x : 479;
    data->point.y = status.y < 800 ? status.y : 799;
    data->state = status.touch ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    return false;
}
void board_display_init(void) {
    if (display_ready) return;
    lv_init();
    lv_disp_buf_init(&draw_buf, pixels, NULL, 480 * 16);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.buffer = &draw_buf;
    driver.hor_res = 480;
    driver.ver_res = 800;
    driver.flush_cb = flush;
    lv_disp_drv_register(&driver);
#if !defined(FORGEBOX_SE_PAIRING) && !defined(FORGEBOX_BOOT_PROBE)
    // Leave the hardware splash visible until the application loads a real screen.
    splash_placeholder = lv_scr_act();
#endif
#if (!defined(FORGEBOX_SE_PAIRING) && !defined(FORGEBOX_BOOT_PROBE)) || defined(FORGEBOX_SE_STORAGE_WRITE) || defined(FORGEBOX_FP_ENROLL_TEST)
    TouchInit(NULL);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = touch;
#ifdef FORGEBOX_SE_STORAGE_WRITE
    input.long_press_time = 1500;
#endif
    lv_indev_drv_register(&input);
#endif
    display_ready = true;
#ifdef FORGEBOX_IDLE_LOCK
    last_touch = board_ticks();
#endif
    board_backlight(true);
}
void board_display_update(unsigned dt) {
    if (!display_ready) return;
    (void)dt;
    static unsigned last;
    unsigned now = board_ticks();
    lv_tick_inc(now - last);
    last = now;
    lv_task_handler();
}
void board_startup_failed(void) {
    board_display_init();
    lv_obj_t *screen = lv_obj_create(NULL, NULL);
    lv_obj_t *label = lv_label_create(screen, NULL);
    lv_label_set_text(label, "Specter failed to start.\n\nExperimental ForgeBox port\n\nHold power to restart, then keep\nholding for factory recovery.");
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_scr_load(screen);
    board_display_update(50);
}

static void recovery_task(void *arg) {
    (void)arg;
    unsigned press = 0;
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Pin = GPIO_Pin_14;
    gpio.GPIO_Remap = GPIO_Remap_1;
    GPIO_Init(GPIOE, &gpio);
    for (;;) {
        WDT_ReloadCounter();
        if (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_14) == Bit_RESET) {
            if (!press) press = board_ticks();
            if ((unsigned)(board_ticks() - press) >= 3000) board_reset();
        } else press = 0;
        board_delay(20);
    }
}
#ifdef FORGEBOX_SE_PAIRING
static const char *pairing_summary(PairingStatus status) {
    switch (status) {
        case PAIRING_MATCH: return "Authenticated";
        case PAIRING_NO_CHIP: return "No device reply";
        case PAIRING_CONFIG: return "Could not read configuration";
        case PAIRING_POLICY: return "Unexpected chip policy";
        case PAIRING_OTP: return "OTP material unavailable";
        case PAIRING_RNG: return "Random source error";
        case PAIRING_IDENTITY: return "Could not read chip identity";
        case PAIRING_ENCRYPTED_READ: return "Encrypted read failed";
        case PAIRING_CHALLENGE: return "Challenge command failed";
        case PAIRING_REPLY: return "Invalid authentication reply";
        case PAIRING_REJECTED: return "Authentication did not match";
        case PAIRING_GENDIG: return "Transport key GenDig failed";
        case PAIRING_INCONSISTENT: return "Independent reads disagree";
        default: return "Unknown result";
    }
}
#endif

#ifdef FORGEBOX_SE_STORAGE_WRITE
static bool page5_confirmed, page5_power_off;

static void page5_confirm(lv_obj_t *button, lv_event_t event) {
    (void)button;
    if (event == LV_EVENT_LONG_PRESSED) page5_confirmed = true;
}

static void page5_request_power_off(lv_obj_t *button, lv_event_t event) {
    (void)button;
    if (event == LV_EVENT_CLICKED) page5_power_off = true;
}

static lv_obj_t *page5_button(const char *text, lv_event_cb_t callback) {
    lv_obj_t *button = lv_btn_create(lv_scr_act(), NULL);
    lv_obj_set_size(button, 400, 72);
    lv_obj_align(button, NULL, LV_ALIGN_CENTER, 0, 125);
    lv_obj_set_event_cb(button, callback);
    lv_label_set_text(lv_label_create(button, NULL), text);
    return button;
}

static void page5_show(lv_obj_t *label, const char *text) {
    lv_label_set_text(label, text);
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, -45);
    lv_refr_now(NULL);
}

#if defined(FORGEBOX_SE_RESET_TEST)
static void reset_test_ui(ResetTestStatus state) {
    lv_obj_t *label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
    lv_obj_t *confirm = NULL;
    switch (state) {
        case RESET_TEST_READY:
#ifdef FORGEBOX_SE_COMBINED_TEST
            page5_show(label, "Disposable Gen 1 account test\n\n"
                              "Hold to replace ATECC keys in slots 10-12\n"
                              "and use blank DS28S60 pages 5 and 11.");
#else
            page5_show(label, "Disposable wallet reset test\n\n"
                              "Pages 5 and 11 are blank.\n"
                              "Hold to test automatic wipe and reboot recovery.");
#endif
            confirm = page5_button("Hold to run reset test", page5_confirm);
            break;
        case RESET_TEST_RECOVERED:
            page5_show(label, "Reset and re-enrollment verified.\n"
                              "The old wallet record stayed locked.\n\n"
                              "Boot finished the interrupted wipe.\n"
                              "Pages 5 and 11 verified all zero.");
            break;
        case RESET_TEST_PARTIAL_CLEARED:
            page5_show(label, "Interrupted test data cleared.\n"
                              "Pages 5 and 11 verified all zero.");
            break;
        default:
            page5_show(label, "Cannot use pages 5 and 11.\n"
                              "Connect USB for the diagnostic report.");
            break;
    }
    for (;;) {
        board_display_update(30);
        if (confirm && page5_confirmed) {
            page5_confirmed = false;
            lv_obj_del(confirm);
            confirm = NULL;
            page5_show(label, "Testing automatic wipe and fresh enrollment...");
            if (board_reset_test(RESET_TEST_READY) == RESET_TEST_PENDING_REBOOT) {
                page5_show(label, "Reset tests passed.\nRestarting with wipe pending...");
                board_delay(1200);
                board_reset();
            }
            page5_show(label, "Test stopped. Check USB for the result.\n"
                              "Restart this image to check for test data.");
        }
        board_delay(30);
    }
}
#elif defined(FORGEBOX_SE_PIN_TEST)
static void pin_test_ui(PinTestStatus state) {
    lv_obj_t *label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
    lv_obj_t *confirm_button = NULL;
    if (state == PIN_TEST_READY) {
        page5_show(label, "Disposable PIN retry test\n\n"
                          "Hold to run the test using DS28S60 page 5.\n"
                          "The SPI flash image is simulated in RAM.");
        confirm_button = page5_button("Hold to run PIN test", page5_confirm);
    } else if (state == PIN_TEST_EXHAUSTED) {
        page5_show(label, "The exhausted retry count survived restart.\n\n"
                          "Hold to verify lockout and restore page 5.");
        confirm_button = page5_button("Hold to verify and clear", page5_confirm);
    } else if (state == PIN_TEST_PARTIAL) {
        page5_show(label, "An interrupted test record was found.\n\n"
                          "Hold to clear it and restore page 5.");
        confirm_button = page5_button("Hold to clear test record", page5_confirm);
    } else {
        page5_show(label, "Page 5 is unavailable or contains other data.\n\n"
                          "Connect USB for the report.");
    }
    for (;;) {
        board_display_update(30);
        if (confirm_button && page5_confirmed) {
            page5_confirmed = false;
            lv_obj_del(confirm_button);
            confirm_button = NULL;
            page5_show(label, "Running DS28S60 PIN checks...");
            state = board_pin_test(state);
            if (state == PIN_TEST_REPLAY_BLOCKED) {
                page5_show(label, "Three failed guesses exhausted the counter.\n"
                                  "Replaying the flash record did not unlock it.\n\n"
                                  "Unplug USB, power off, then start this image again.");
                page5_button("Power off", page5_request_power_off);
            } else if (state == PIN_TEST_REBOOT_VERIFIED) {
                page5_show(label, "Lockout verified after restart.\n\n"
                                  "Page 5 restored and verified all zero.");
            } else if (state == PIN_TEST_PARTIAL_CLEARED) {
                page5_show(label, "Interrupted test record cleared.\n\n"
                                  "Page 5 restored and verified all zero.");
            } else {
                page5_show(label, "The PIN test could not be verified.\n\n"
                                  "Read the USB report and restart this image.");
            }
        }
        if (page5_power_off) {
            page5_power_off = false;
            board_power_off();
            page5_show(label, "Disconnect USB before powering off.");
        }
        board_delay(30);
    }
}
#else
static void page5_ui(Page5TestStatus state) {
    lv_obj_t *label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
    lv_obj_t *confirm_button = NULL;
    if (state == PAGE5_BLANK) {
        page5_show(label, "DS28S60 page 5 is all zero.\n\n"
                          "Hold the button to write a test marker.");
        confirm_button = page5_button("Hold to write test marker", page5_confirm);
    } else if (state == PAGE5_MARKER_PRESENT) {
        page5_show(label, "The test marker survived restart.\n\n"
                          "Hold the button to restore page 5 to zero.");
        confirm_button = page5_button("Hold to restore zeroes", page5_confirm);
    } else {
        page5_show(label, "Page 5 is unavailable or contains other data.\n\n"
                          "Connect USB for the detailed report.");
    }
    for (;;) {
        board_display_update(30);
        if (confirm_button && page5_confirmed) {
            page5_confirmed = false;
            lv_obj_del(confirm_button);
            confirm_button = NULL;
            page5_show(label, "Checking and updating DS28S60 page 5...");
            state = board_page5_test(state);
            if (state == PAGE5_WRITE_VERIFIED) {
                page5_show(label, "Test marker written and verified.\n\n"
                                  "Unplug USB and power off, then restart\n"
                                  "this firmware to restore the page.");
                page5_button("Power off", page5_request_power_off);
            } else if (state == PAGE5_RESTORE_VERIFIED) {
                page5_show(label, "Page 5 restored and verified all zero.");
            } else {
                page5_show(label, "Page 5 update could not be verified.\n\n"
                                  "Connect USB for the detailed report.");
            }
        }
        if (page5_power_off) {
            page5_power_off = false;
            board_power_off();
            page5_show(label, "Disconnect USB before powering off.");
        }
        board_delay(30);
    }
}
#endif
#endif

#ifdef FORGEBOX_FP_ENROLL_TEST
#include "fingerprint_test_ui.inc"
#endif

static void vm_task(void *arg) {
    (void)arg;
#if defined(FORGEBOX_FP_ENROLL_TEST)
    board_capture_boot_probe();
    board_display_init();
    fingerprint_test_ui();
#elif defined(FORGEBOX_BOOT_PROBE)
    GPIO_InitTypeDef finger_irq = {
        .GPIO_Pin = GPIO_Pin_14, .GPIO_Mode = GPIO_Mode_IPU, .GPIO_Remap = GPIO_Remap_1,
    };
    GPIO_Init(GPIOF, &finger_irq);
    board_capture_boot_probe();
    board_display_init();
    lv_obj_t *label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
    char summary[240], text[400];
    board_boot_probe_summary(summary, sizeof(summary));
    bool previous = GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_14) == Bit_RESET;
    unsigned transitions = 0;
    bool redraw = true;
    for (;;) {
        bool pressed = GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_14) == Bit_RESET;
        if (pressed != previous) {
            transitions++;
            previous = pressed;
            redraw = true;
        }
        if (redraw) {
            board_boot_probe_update_touch(pressed, transitions);
            snprintf(text, sizeof(text), "%s\n\nTouch and lift your finger.\n"
                     "Sensor IRQ: %s\nTransitions: %u", summary,
                     pressed ? "Low (active)" : "High (inactive)", transitions);
            lv_label_set_text(label, text);
            lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0);
            redraw = false;
        }
        board_display_update(10);
        board_delay(10);
    }
#elif defined(FORGEBOX_SE_PAIRING)
    // Publish results even if the LCD cannot be initialized.
    board_capture_hardware_report();
#if defined(FORGEBOX_SE_RESET_TEST)
    ResetTestStatus state = board_reset_test(RESET_TEST_INSPECT);
    board_display_init();
    reset_test_ui(state);
#elif defined(FORGEBOX_SE_PIN_TEST)
    PinTestStatus state = board_pin_test(PIN_TEST_INSPECT);
    board_display_init();
    pin_test_ui(state);
#elif defined(FORGEBOX_SE_STORAGE_WRITE)
    Page5TestStatus state = board_page5_test(PAGE5_INSPECT);
    board_display_init();
    page5_ui(state);
#else
    board_display_init();
    lv_obj_t *label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
    char text[384];
    snprintf(text, sizeof(text),
             "Secure-element storage probe\n\n"
             "ATECC608B pairing: %s\nSlot 8 read: %s\nSlot 13 read: %s\n\n"
             "DS28S60 pairing: %s\nPage 5 read: %s\n\n"
             "Connect USB for the detailed report.",
             pairing_summary(board_pairing_status(true)),
             pairing_summary(board_storage_probe_status(true)),
             pairing_summary(board_slot13_probe_status()),
             pairing_summary(board_pairing_status(false)),
             pairing_summary(board_storage_probe_status(false)));
    lv_label_set_text(label, text);
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_refr_now(NULL);
    for (;;) {
        board_display_update(30);
        board_delay(30);
    }
#endif
#else
    board_clear_psram();
    board_init_scratch();
#ifdef FORGEBOX_USB_DEBUG
    board_capture_hardware_report();
#endif
    forgebox_vm();
#endif
}
// PF1 is the touchscreen IRQ. This port polls the controller from LVGL.
void EXTI5_IRQHandler(void) {
    EXTI_ClearITPendingBit(EXTI_Line5);
    __DSB();
}

int main(void) {
    __disable_irq();
    // Official startup sets VTOR; the factory bootloader already initializes FPU.
    SCB->CPACR |= 0x00F00000;
    __DSB(); __ISB();
    SystemClockInit();
    // The factory bootloader can leave this GPIO interrupt armed.
    SYSCTRL_APBPeriphClockCmd(SYSCTRL_APBPeriph_GPIO, ENABLE);
    NVIC_DisableIRQ(EXTI5_IRQn);
    EXTI_LineConfig(EXTI_Line5, EXTI_PinSource1, EXTI_Trigger_Off);
    EXTI_ClearITPendingBit(EXTI_Line5);
    NVIC_ClearPendingIRQ(EXTI5_IRQn);
#if defined(FORGEBOX_USB_DEBUG) || defined(FORGEBOX_USB_SERIAL)
    NVIC_DisableIRQ(USB_IRQn);
    NVIC_ClearPendingIRQ(USB_IRQn);
#endif
    __enable_irq();
    PowerInit();
    LcdBacklightOff(); LcdBrightInit(); LcdCheck(); LcdInit();
#if !defined(FORGEBOX_SE_PAIRING) && !defined(FORGEBOX_BOOT_PROBE)
    show_splash(false);
#endif
    NvicInit(); PsramInit(); BatteryInit(); Aw32001Init();
    SYSCTRL_APBPeriphClockCmd(SYSCTRL_APBPeriph_TRNG, ENABLE);
    SYSCTRL_APBPeriphResetCmd(SYSCTRL_APBPeriph_TRNG, ENABLE);
    GPIO_InitTypeDef detect = {0};
    detect.GPIO_Mode = GPIO_Mode_IPU;
    detect.GPIO_Pin = GPIO_Pin_7;
    detect.GPIO_Remap = GPIO_Remap_1;
    GPIO_Init(GPIOD, &detect);
#if defined(FORGEBOX_USB_DEBUG) || defined(FORGEBOX_USB_SERIAL)
    debug_usb_init();
#endif
    osKernelInitialize();
    const osThreadAttr_t recovery = {.name="recovery", .stack_size=2048, .priority=osPriorityHigh};
    const osThreadAttr_t vm = {.name="specter", .stack_size=96 * 1024, .priority=osPriorityNormal};
    if (!osThreadNew(recovery_task, NULL, &recovery) || !osThreadNew(vm_task, NULL, &vm))
        for (;;) {}
    WDT_ModeConfig(WDT_Mode_CPUReset);
    WDT_SetReload(0xC300000U);
    WDT_ReloadCounter(); WDT_Enable();
    osKernelStart();
    for (;;) {}
}

// No logging channel is exposed while handling seeds or signing transactions.
int _write(int fd, const char *buf, int n) { (void)fd; (void)buf; return n; }
void *_sbrk(ptrdiff_t n) { (void)n; errno = ENOMEM; return (void *)-1; }
void ShowAssert(const char *file, uint32_t line) { (void)file; (void)line; for (;;) {} }
void assert_failed(uint8_t *file, uint32_t line) { ShowAssert((char *)file, line); }
