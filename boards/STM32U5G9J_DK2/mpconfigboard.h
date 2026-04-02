/*
 * Board configuration for STM32U5G9J-DK2
 * MCU: STM32U5G9ZJT6Q (Cortex-M33, 160 MHz, 4 MB Flash, 3 MB SRAM)
 * Display: 5" 800x480 RGB TFT LCD with capacitive touch
 * External memory: 1 Gbit Octo-SPI NOR flash (MX25LM51245G)
 * USB: Type-C (USB 2.0 High-Speed)
 */

#define MICROPY_BOARD_EARLY_INIT    STM32U5G9DK2_board_early_init
void STM32U5G9DK2_board_early_init(void);

#define MICROPY_HW_BOARD_NAME       "STM32U5G9J-DK2"
#define MICROPY_HW_MCU_NAME         "STM32U5G9ZJ"

#define MICROPY_HW_ENABLE_INTERNAL_FLASH_STORAGE (1)
#define MICROPY_HW_HAS_SWITCH       (1)
#define MICROPY_HW_HAS_FLASH        (1)
#define MICROPY_HW_ENABLE_RNG       (1)
#define MICROPY_HW_ENABLE_RTC       (1)
#define MICROPY_HW_ENABLE_USB       (1)
#define MICROPY_HW_ENABLE_SDCARD    (1)
#define MICROPY_HW_ENABLE_ADC       (1)
#define MICROPY_PY_UCRYPTOLIB_CONSTS (1)

/* Use Octo-SPI flash for secondary storage (/qspi mount point) */
/* MX25LM51245G: 512 Mbit = 64 MByte */
#define MICROPY_HW_SPIFLASH_SIZE_BITS (512 * 1024 * 1024)
#define MICROPY_HW_OSPIFLASH_SIZE_BITS_LOG2 (26)  /* 64 MB */

/* Custom module enables - same as F469 for Specter compatibility */
#ifndef MODULE_SECP256K1_ENABLED
#define MODULE_SECP256K1_ENABLED    (1)
#endif
#ifndef MODULE_HASHLIB_ENABLED
#define MODULE_HASHLIB_ENABLED      (1)
#endif
#ifndef MODULE_DISPLAY_ENABLED
#define MODULE_DISPLAY_ENABLED      (1)
#endif
#ifndef MODULE_QRCODE_ENABLED
#define MODULE_QRCODE_ENABLED       (1)
#endif
#ifndef MODULE_SCARD_ENABLED
#define MODULE_SCARD_ENABLED        (1)
#endif
/* DK2 uses internal SRAM instead of external SDRAM, but we keep the
   module name 'sdram' for Python-level compatibility */
#ifndef MODULE_SDRAM_ENABLED
#define MODULE_SDRAM_ENABLED        (1)
#endif

/*
 * Clock configuration
 * The DK2 has a 16 MHz HSE crystal oscillator.
 * PLL: 16 MHz / 1 * 10 / 1 = 160 MHz SYSCLK
 */
#define MICROPY_HW_CLK_PLLM         (1)
#define MICROPY_HW_CLK_PLLN         (10)
#define MICROPY_HW_CLK_PLLP         (10)
#define MICROPY_HW_CLK_PLLQ         (2)
#define MICROPY_HW_CLK_PLLR         (1)
#define MICROPY_HW_CLK_PLLVCI       (RCC_PLLVCIRANGE_1)
#define MICROPY_HW_CLK_PLLFRAC      (0)

/* 4 wait states for 160 MHz at 3.3V (RM0481 Table 37) */
#define MICROPY_HW_FLASH_LATENCY    FLASH_LATENCY_4

/* 32 kHz LSE crystal for RTC */
#define MICROPY_HW_RTC_USE_LSE      (1)
#define MICROPY_HW_RCC_RTC_CLKSOURCE (RCC_RTCCLKSOURCE_LSE)

/*
 * UART configuration
 * UART1: STLINK VCP (REPL) on PA9/PA10
 * UART2: Arduino connector / QR scanner (mapped to "YA")
 * UART3: Available on expansion connector (mapped to "YB")
 */
#define MICROPY_HW_UART1_TX         (pin_A9)
#define MICROPY_HW_UART1_RX         (pin_A10)

#define MICROPY_HW_UART2_NAME       "YA"
#define MICROPY_HW_UART2_TX         (pin_D5)
#define MICROPY_HW_UART2_RX         (pin_D6)
#define MICROPY_HW_UART2_RTS        (pin_D4)
#define MICROPY_HW_UART2_CTS        (pin_D3)

#define MICROPY_HW_UART3_NAME       "YB"
#define MICROPY_HW_UART3_TX         (pin_D8)
#define MICROPY_HW_UART3_RX         (pin_D9)

/* Connect REPL to UART1 (STLINK VCP) */
#define MICROPY_HW_UART_REPL        PYB_UART_1
#define MICROPY_HW_UART_REPL_BAUD   115200

/*
 * I2C configuration
 * I2C1: Touch controller (PG14/PG13 on DK2 board)
 * I2C2: Arduino connector / Specter Shield battery monitor
 */
#define MICROPY_HW_I2C1_SCL         (pin_G14)
#define MICROPY_HW_I2C1_SDA         (pin_G13)
#define MICROPY_HW_I2C2_SCL         (pin_F1)
#define MICROPY_HW_I2C2_SDA         (pin_F0)

/*
 * SPI configuration
 * SPI1: Arduino connector (D10-D13)
 */
#define MICROPY_HW_SPI1_NSS         (pin_D14)
#define MICROPY_HW_SPI1_SCK         (pin_A5)
#define MICROPY_HW_SPI1_MISO        (pin_A6)
#define MICROPY_HW_SPI1_MOSI        (pin_A7)

/*
 * User button (directly active high, directly active high when pressed)
 */
#define MICROPY_HW_USRSW_PIN        (pin_C13)
#define MICROPY_HW_USRSW_PULL       (GPIO_NOPULL)
#define MICROPY_HW_USRSW_EXTI_MODE  (GPIO_MODE_IT_RISING)
#define MICROPY_HW_USRSW_PRESSED    (1)

/*
 * LEDs - DK2 has 2 user LEDs
 * LED1 (green): PD2
 * LED2 (red):   PD4
 * We define LED3/LED4 as aliases so code referencing 4 LEDs still compiles
 */
#define MICROPY_HW_LED1             (pin_D2)   /* Green LED */
#define MICROPY_HW_LED2             (pin_D4)   /* Red LED */
#define MICROPY_HW_LED3             (pin_D2)   /* Alias to LED1 */
#define MICROPY_HW_LED4             (pin_D4)   /* Alias to LED2 */
#define MICROPY_HW_LED_ON(pin)      (mp_hal_pin_high(pin))
#define MICROPY_HW_LED_OFF(pin)     (mp_hal_pin_low(pin))

/*
 * SD card (SDMMC1)
 */
#define MICROPY_HW_SDMMC_CK         (pin_C12)
#define MICROPY_HW_SDMMC_CMD        (pin_D2)
#define MICROPY_HW_SDMMC_D0         (pin_C8)
#define MICROPY_HW_SDMMC_D1         (pin_C9)
#define MICROPY_HW_SDMMC_D2         (pin_C10)
#define MICROPY_HW_SDMMC_D3         (pin_C11)

/*
 * USB configuration (USB HS in FS mode, Type-C connector)
 */
#define MICROPY_HW_USB_HS           (1)
#define MICROPY_HW_USB_HS_IN_FS     (1)
#define MICROPY_HW_USB_VBUS_DETECT_PIN (pin_A9)
