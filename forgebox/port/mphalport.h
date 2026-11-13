#include <stdint.h>
void mp_hal_delay_ms(uintptr_t ms);
void mp_hal_delay_us(uintptr_t us);
uintptr_t mp_hal_ticks_ms(void);
uintptr_t mp_hal_ticks_us(void);
uintptr_t mp_hal_ticks_cpu(void);
int mp_hal_stdin_rx_chr(void);
void mp_hal_stdout_tx_strn(const char *str, uintptr_t len);
void mp_hal_stdout_tx_strn_cooked(const char *str, uintptr_t len);
