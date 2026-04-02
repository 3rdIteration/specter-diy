/*
 * LVGL Hardware Abstraction Layer for STM32U5G9J-DK2
 *
 * Implements TFT display output via LTDC (parallel RGB 800x480)
 * and capacitive touch input via I2C.
 *
 * The DK2 board uses:
 *   - LTDC peripheral for 24-bit parallel RGB display (800x480)
 *   - I2C1 (PG14/PG13) for the capacitive touch controller
 *   - Internal SRAM for framebuffer storage (no external SDRAM)
 *
 * Framebuffer layout in SRAM:
 *   800 x 480 x 2 bytes (RGB565) = 768,000 bytes per buffer
 *   We use a single framebuffer + partial LVGL buffer to save RAM.
 */

#include "lv_stm_hal.h"
#include "lvgl.h"

#include "stm32u5xx_hal.h"
#include "stm32u5xx_hal_ltdc.h"
#include "stm32u5xx_hal_i2c.h"
#include "stm32u5xx_hal_gpio.h"
#include "stm32u5xx_hal_rcc.h"

/* Display resolution */
#define TFT_HOR_RES   800
#define TFT_VER_RES   480

/* LTDC timing for the 5" 800x480 LCD on DK2
 * These values are from the STM32CubeU5 BSP example */
#define HSYNC_WIDTH    4
#define HBP            8
#define HFP            8
#define VSYNC_WIDTH    4
#define VBP            8
#define VFP            8

/*
 * Framebuffer in SRAM
 * 800 * 480 * 2 (RGB565) = 768,000 bytes = 750 KB
 * Placed at the end of the 3 MB SRAM region
 */
#define FB_SRAM_BASE   0x20240000  /* ~2.25 MB into SRAM, leaving room for MicroPython heap */
static uint16_t *framebuffer = (uint16_t *)FB_SRAM_BASE;

static LTDC_HandleTypeDef hltdc;
static I2C_HandleTypeDef hi2c_touch;

static lv_disp_drv_t disp_drv;
static lv_disp_t *disp;

/* Forward declarations */
static void tft_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
static bool touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

/*
 * Initialize LTDC for 800x480 RGB565 output
 */
static void ltdc_init(void) {
    /* Enable LTDC and DMA2D clocks */
    __HAL_RCC_LTDC_CLK_ENABLE();
    __HAL_RCC_DMA2D_CLK_ENABLE();

    /* Enable GPIO clocks for LTDC pins */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    /* Configure LTDC GPIO pins (AF14 for most LTDC signals) */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF14_LTDC;

    /* Red pins: R0=PC6, R1=PC7, R2=PE15, R3=PD8, R4=PD9, R5=PD10, R6=PD11, R7=PD12 */
    gpio_init.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    gpio_init.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    gpio_init.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOD, &gpio_init);

    /* Green pins: G0=PC8, G1=PC9, G2=PE9, G3=PE10, G4=PE11, G5=PE12, G6=PE13, G7=PE14 */
    gpio_init.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    gpio_init.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    /* Blue pins: B0=PE5, B1=PE6, B2=PE7, B3=PE8, B4=PA3, B5=PB8, B6=PB9, B7=PE4 */
    gpio_init.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    gpio_init.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    gpio_init.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    /* Control pins: HSYNC=PA4, VSYNC=PA5, DE=PF10, CLK=PI14 */
    gpio_init.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    gpio_init.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &gpio_init);

    gpio_init.Pin = GPIO_PIN_14;
    HAL_GPIO_Init(GPIOI, &gpio_init);

    /* Configure LTDC */
    hltdc.Instance = LTDC;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
    hltdc.Init.HorizontalSync = HSYNC_WIDTH - 1;
    hltdc.Init.VerticalSync = VSYNC_WIDTH - 1;
    hltdc.Init.AccumulatedHBP = HSYNC_WIDTH + HBP - 1;
    hltdc.Init.AccumulatedVBP = VSYNC_WIDTH + VBP - 1;
    hltdc.Init.AccumulatedActiveW = HSYNC_WIDTH + HBP + TFT_HOR_RES - 1;
    hltdc.Init.AccumulatedActiveH = VSYNC_WIDTH + VBP + TFT_VER_RES - 1;
    hltdc.Init.TotalWidth = HSYNC_WIDTH + HBP + TFT_HOR_RES + HFP - 1;
    hltdc.Init.TotalHeigh = VSYNC_WIDTH + VBP + TFT_VER_RES + VFP - 1;
    hltdc.Init.Backcolor.Blue = 0xFF;
    hltdc.Init.Backcolor.Green = 0xFF;
    hltdc.Init.Backcolor.Red = 0xFF;

    HAL_LTDC_Init(&hltdc);

    /* Configure LTDC Layer 0 */
    LTDC_LayerCfgTypeDef layer_cfg = {0};
    layer_cfg.WindowX0 = 0;
    layer_cfg.WindowX1 = TFT_HOR_RES;
    layer_cfg.WindowY0 = 0;
    layer_cfg.WindowY1 = TFT_VER_RES;
    layer_cfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer_cfg.Alpha = 255;
    layer_cfg.Alpha0 = 0;
    layer_cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer_cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer_cfg.FBStartAdress = (uint32_t)framebuffer;
    layer_cfg.ImageWidth = TFT_HOR_RES;
    layer_cfg.ImageHeight = TFT_VER_RES;
    layer_cfg.Backcolor.Blue = 0xFF;
    layer_cfg.Backcolor.Green = 0xFF;
    layer_cfg.Backcolor.Red = 0xFF;

    HAL_LTDC_ConfigLayer(&hltdc, &layer_cfg, 0);

    /* Clear framebuffer to white */
    for (uint32_t i = 0; i < TFT_HOR_RES * TFT_VER_RES; i++) {
        framebuffer[i] = 0xFFFF; /* White in RGB565 */
    }
}

/*
 * Initialize I2C for touch controller
 */
static void touch_i2c_init(void) {
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* Configure I2C1 GPIO: SCL=PG14, SDA=PG13 */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = GPIO_PIN_14 | GPIO_PIN_13;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOG, &gpio_init);

    hi2c_touch.Instance = I2C1;
    hi2c_touch.Init.Timing = 0x10707DBC; /* 100 kHz I2C timing */
    hi2c_touch.Init.OwnAddress1 = 0;
    hi2c_touch.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c_touch.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c_touch.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c_touch.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c_touch);
}

/* Touch controller I2C address (7-bit) - typical for GT911 or similar */
#define TOUCH_I2C_ADDR  (0x5D << 1)

void tft_init(void) {
    ltdc_init();
}

void touchpad_init(void) {
    touch_i2c_init();

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = touchpad_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv);

    /* Set up LVGL display driver */
    static lv_color_t disp_buf1[TFT_HOR_RES * 30];
    static lv_disp_buf_t buf;
    lv_disp_buf_init(&buf, disp_buf1, NULL, TFT_HOR_RES * 30);
    lv_disp_drv_init(&disp_drv);

    disp_drv.buffer = &buf;
    disp_drv.flush_cb = tft_flush;
    disp_drv.hor_res = TFT_HOR_RES;
    disp_drv.ver_res = TFT_VER_RES;
    disp = lv_disp_drv_register(&disp_drv);
}

/*
 * Flush LVGL buffer to the LTDC framebuffer
 */
static void tft_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    int32_t x, y;
    for (y = area->y1; y <= area->y2; y++) {
        for (x = area->x1; x <= area->x2; x++) {
            framebuffer[y * TFT_HOR_RES + x] = lv_color_to16(*color_p);
            color_p++;
        }
    }
    lv_disp_flush_ready(drv);
}

/*
 * Read touch input from the capacitive touch controller via I2C
 */
static bool touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static int16_t last_x = 0;
    static int16_t last_y = 0;

    uint8_t touch_data[7] = {0};
    uint8_t reg_addr[2] = {0x81, 0x4E}; /* GT911 touch status register */

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(&hi2c_touch, TOUCH_I2C_ADDR,
                                                        reg_addr, 2, 10);
    if (status == HAL_OK) {
        status = HAL_I2C_Master_Receive(&hi2c_touch, TOUCH_I2C_ADDR,
                                         touch_data, 7, 10);
    }

    if (status == HAL_OK && (touch_data[0] & 0x80) && (touch_data[0] & 0x0F) > 0) {
        /* Touch detected - read X/Y coordinates (little-endian 16-bit) */
        data->point.x = (int16_t)(touch_data[2] | (touch_data[3] << 8));
        data->point.y = (int16_t)(touch_data[4] | (touch_data[5] << 8));

        /* Clamp to display bounds */
        if (data->point.x >= TFT_HOR_RES) data->point.x = TFT_HOR_RES - 1;
        if (data->point.y >= TFT_VER_RES) data->point.y = TFT_VER_RES - 1;

        last_x = data->point.x;
        last_y = data->point.y;
        data->state = LV_INDEV_STATE_PR;

        /* Clear touch status flag */
        uint8_t clear_cmd[3] = {0x81, 0x4E, 0x00};
        HAL_I2C_Master_Transmit(&hi2c_touch, TOUCH_I2C_ADDR, clear_cmd, 3, 10);
    } else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_REL;
    }

    return false;
}
