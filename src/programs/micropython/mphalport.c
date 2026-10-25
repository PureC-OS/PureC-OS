#include <stdio.h>
#include <unistd.h>
#include "py/mpconfig.h"
#include "py/misc.h"

int mp_hal_stdin_rx_chr(void) {
    unsigned char c = 0;
    int r = (int)read(STDIN_FILENO, &c, 1);
    if (r <= 0) {
        return 4;
    }
    return (int)c;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    size_t n = fwrite(str, 1, (size_t)len, stdout);
    fflush(stdout);
    return (mp_uint_t)n;
}
