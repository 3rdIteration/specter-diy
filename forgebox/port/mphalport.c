#include "mphalport.h"
#include "board_api.h"
void mp_hal_delay_ms(uintptr_t ms) { board_delay(ms); }
void mp_hal_delay_us(uintptr_t us) { board_delay((us + 999) / 1000); }
uintptr_t mp_hal_ticks_ms(void) { return board_ticks(); }
uintptr_t mp_hal_ticks_us(void) { return board_ticks() * 1000; }
uintptr_t mp_hal_ticks_cpu(void) { return board_ticks() * 204000; }
int mp_hal_stdin_rx_chr(void) { for (;;) board_delay(100); }
void mp_hal_set_interrupt_char(int c) { (void)c; }
// No UART/USB REPL or Python exception/secret logging in device builds.
void mp_hal_stdout_tx_strn(const char *str, uintptr_t len) {
#if FORGEBOX_HOST
    extern int write(int, const void *, unsigned long);
    write(1, str, len);
#else
    (void)str; (void)len;
#endif
}
