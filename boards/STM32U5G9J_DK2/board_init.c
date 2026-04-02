/*
 * Board early initialization for STM32U5G9J-DK2
 *
 * This runs before MicroPython starts. It configures essential
 * hardware such as LTDC GPIO pins for the display.
 */
#include "py/mphal.h"

void STM32U5G9DK2_board_early_init(void) {
    /* Nothing needed at early init for DK2.
     * LTDC and display initialization is handled by the display usermod.
     * GPIO clocks are enabled by the HAL as needed. */
}
