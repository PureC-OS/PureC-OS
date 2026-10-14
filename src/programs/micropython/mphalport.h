#include "py/mpconfig.h"
#include "py/misc.h"
#include <time.h>

static inline mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)clock();
}

static inline void mp_hal_set_interrupt_char(char c) {
    (void)c;
}

int mp_hal_stdin_rx_chr(void);
mp_uint_t mp_hal_stdout_tx_strn(const char *str, mp_uint_t len);
