// PureC hosted libc: C locale only (no tables, no zones).
// Enough for ported programs: Lua lua_getlocaledecpoint() and os.setlocale.

#include "include/hosted/locale.h"
#include <stddef.h>

static char locale_decimal[] = ".";
static struct lconv locale_c = { locale_decimal, "", "" };

struct lconv *localeconv(void) {
    return &locale_c;
}

char *setlocale(int category, const char *locale) {
    static char name[] = "C";
    (void)category;
    (void)locale;
    return name;
}
