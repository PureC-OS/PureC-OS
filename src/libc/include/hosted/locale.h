#pragma once
// PureC hosted libc: locale (C locale only).
// No locale tables: single "C" locale, decimal_point ".".
// Enough for ported programs (Lua lua_getlocaledecpoint, os.setlocale).

#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
};

struct lconv *localeconv(void);
char *setlocale(int category, const char *locale);
