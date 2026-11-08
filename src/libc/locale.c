// PureC hosted libc: locale. A single behaviour (C/POSIX rules, UTF-8
// multibyte encoding) is implemented; named locales are accepted and map onto it.

#include "include/hosted/langinfo.h"
#include "include/hosted/errno.h"
#include "include/hosted/locale.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include <limits.h>
#include <stddef.h>

struct purec_locale {
    char name[48];
    int mask;
};

static char locale_empty[] = "";
static char locale_decimal[] = ".";
static struct lconv locale_c = {
    locale_decimal, locale_empty, locale_empty, locale_empty, locale_empty,
    locale_empty, locale_empty, locale_empty, locale_empty, locale_empty,
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
};

static char global_name[48] = "C";
static locale_t active_locale = LC_GLOBAL_LOCALE;

static int locale_name_valid(const char *name)
{
    if (!name)
        return 0;
    if (!*name || !strcmp(name, "C") || !strcmp(name, "POSIX"))
        return 1;
    return strstr(name, "UTF-8") || strstr(name, "utf8") || strstr(name, "UTF8") ||
           strstr(name, "utf-8");
}

struct lconv *localeconv(void)
{
    return &locale_c;
}

char *setlocale(int category, const char *locale)
{
    if (category < LC_CTYPE || category > LC_ALL)
        return 0;
    if (!locale)
        return global_name;
    if (!locale_name_valid(locale))
        return 0;
    if (*locale)
    {
        strncpy(global_name, locale, sizeof(global_name) - 1);
        global_name[sizeof(global_name) - 1] = '\0';
    }
    else
    {
        strcpy(global_name, "C");
    }
    return global_name;
}

locale_t newlocale(int mask, const char *name, locale_t base)
{
    if (!name || (mask & ~LC_ALL_MASK))
    {
        errno = EINVAL;
        return 0;
    }
    if (!locale_name_valid(name))
    {
        errno = ENOENT;
        return 0;
    }
    locale_t result = base && base != LC_GLOBAL_LOCALE ? base : malloc(sizeof(*result));
    if (!result)
    {
        errno = ENOMEM;
        return 0;
    }
    strncpy(result->name, *name ? name : "C", sizeof(result->name) - 1);
    result->name[sizeof(result->name) - 1] = '\0';
    result->mask = mask;
    return result;
}

locale_t duplocale(locale_t locale)
{
    if (locale == LC_GLOBAL_LOCALE)
        return newlocale(LC_ALL_MASK, global_name, 0);
    if (!locale)
    {
        errno = EINVAL;
        return 0;
    }
    return newlocale(locale->mask, locale->name, 0);
}

void freelocale(locale_t locale)
{
    if (locale && locale != LC_GLOBAL_LOCALE)
        free(locale);
}

locale_t uselocale(locale_t locale)
{
    locale_t previous = active_locale;
    if (locale)
        active_locale = locale;
    return previous;
}

static const char *const day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                         "Thursday", "Friday", "Saturday"};
static const char *const day_abbreviations[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const month_names[] = {"January", "February", "March", "April", "May",
                                           "June", "July", "August", "September", "October",
                                           "November", "December"};
static const char *const month_abbreviations[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

char *nl_langinfo(nl_item item)
{
    if (item >= ABDAY_1 && item <= ABDAY_7)
        return (char *)day_abbreviations[item - ABDAY_1];
    if (item >= DAY_1 && item <= DAY_7)
        return (char *)day_names[item - DAY_1];
    if (item >= ABMON_1 && item <= ABMON_12)
        return (char *)month_abbreviations[item - ABMON_1];
    if (item >= MON_1 && item <= MON_12)
        return (char *)month_names[item - MON_1];
    switch (item)
    {
    case CODESET:
        return "UTF-8";
    case RADIXCHAR:
        return ".";
    case THOUSEP:
        return "";
    case YESEXPR:
        return "^[yY]";
    case NOEXPR:
        return "^[nN]";
    case CRNCYSTR:
        return "-";
    case D_T_FMT:
        return "%a %b %e %H:%M:%S %Y";
    case D_FMT:
        return "%m/%d/%y";
    case T_FMT:
        return "%H:%M:%S";
    case T_FMT_AMPM:
        return "%I:%M:%S %p";
    case AM_STR:
        return "AM";
    case PM_STR:
        return "PM";
    default:
        return "";
    }
}

char *nl_langinfo_l(nl_item item, locale_t locale)
{
    (void)locale;
    return nl_langinfo(item);
}
