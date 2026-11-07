// PureC hosted libc: errno, conversions, qsort, rand (heap allocator: malloc.c).

#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/ctype.h"
#include "include/hosted/errno.h"
#include "include/purec.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int errno = 0;

// ---- integer conversions ----

static const char *convert_skip(const char *text) {
    while (isspace((unsigned char)*text)) text++;
    return text;
}

static int convert_digit(int c, int base) {
    if (c >= '0' && c <= '9' && c - '0' < base) return c - '0';
    if (c >= 'a' && c <= 'z' && c - 'a' + 10 < base) return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z' && c - 'A' + 10 < base) return c - 'A' + 10;
    return -1;
}

long strtol(const char *text, char **end, int base) {
    const char *p = convert_skip(text);
    bool negative = false;
    if (*p == '+' || *p == '-') { negative = *p == '-'; p++; }
    if (base == 0) {
        if (*p == '0') {
            if ((p[1] == 'x' || p[1] == 'X') && convert_digit(p[2], 16) >= 0) {
                base = 16; p += 2;
            } else { base = 8; }
        } else { base = 10; }
    } else if (base == 16) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
            && convert_digit(p[2], 16) >= 0) p += 2;
    }
    if (base < 2 || base > 36) { errno = EINVAL; if (end) *end = (char *)text; return 0; }
    unsigned long magnitude = 0;
    bool any = false;
    bool overflow = false;
    unsigned long limit = negative ? (unsigned long)LONG_MAX + 1UL : (unsigned long)LONG_MAX;
    while (*p) {
        int digit = convert_digit(*p, base);
        if (digit < 0) break;
        any = true;
        if (magnitude > (limit - (unsigned long)digit) / (unsigned long)base) {
            overflow = true;
            magnitude = limit;
            p++;
            while (convert_digit(*p, base) >= 0) p++;
            break;
        }
        magnitude = magnitude * (unsigned long)base + (unsigned long)digit;
        p++;
    }
    if (!any) p = text;
    if (end) *end = (char *)p;
    if (overflow) {
        errno = ERANGE;
        return negative ? LONG_MIN : LONG_MAX;
    }
    return negative ? -(long)magnitude : (long)magnitude;
}

unsigned long strtoul(const char *text, char **end, int base) {    const char *p = convert_skip(text);
    bool negative = false;
    if (*p == '+' || *p == '-') { negative = *p == '-'; p++; }
    if (base == 0) {
        if (*p == '0') {
            if ((p[1] == 'x' || p[1] == 'X') && convert_digit(p[2], 16) >= 0) {
                base = 16; p += 2;
            } else { base = 8; }
        } else { base = 10; }
    } else if (base == 16) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
            && convert_digit(p[2], 16) >= 0) p += 2;
    }
    if (base < 2 || base > 36) { errno = EINVAL; if (end) *end = (char *)text; return 0; }
    unsigned long magnitude = 0;
    bool any = false;
    bool overflow = false;
    while (*p) {
        int digit = convert_digit(*p, base);
        if (digit < 0) break;
        any = true;
        if (magnitude > (ULONG_MAX - (unsigned long)digit) / (unsigned long)base) {
            overflow = true;
            magnitude = ULONG_MAX;
            p++;
            while (convert_digit(*p, base) >= 0) p++;
            break;
        }
        magnitude = magnitude * (unsigned long)base + (unsigned long)digit;
        p++;
    }
    if (!any) p = text;
    if (end) *end = (char *)p;
    if (overflow) errno = ERANGE;
    return negative ? 0UL - magnitude : magnitude;
}

int atoi(const char *text) { return (int)strtol(text, 0, 10); }
long atol(const char *text) { return strtol(text, 0, 10); }
long long atoll(const char *text) { return (long long)strtol(text, 0, 10); }

// unsigned long is 64-bit here, same range as unsigned long long.
unsigned long long strtoull(const char *text, char **end, int base) {
    return (unsigned long long)strtoul(text, end, base);
}

// intmax_t is 64-bit here (see hosted/inttypes.h).
long long strtoimax(const char *text, char **end, int base) {
    return (long long)strtol(text, end, base);
}

unsigned long long strtoumax(const char *text, char **end, int base) {
    return (unsigned long long)strtoul(text, end, base);
}

long long imaxabs(long long value) {
    return value < 0 ? -value : value;
}

double strtod(const char *text, char **end) {
    const char *p = convert_skip(text);
    bool negative = false;
    if (*p == '+' || *p == '-') { negative = *p == '-'; p++; }
    // Minimal inf/nan (case-insensitive) for compiler sources.
    if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N')
        && (p[2] == 'f' || p[2] == 'F')) {
        p += 3;
        if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N')
            && (p[2] == 'i' || p[2] == 'I') && (p[3] == 'n' || p[3] == 'N')
            && (p[4] == 'i' || p[4] == 'I') && (p[5] == 't' || p[5] == 'T')
            && (p[6] == 'y' || p[6] == 'Y')) p += 5;
        if (end) *end = (char *)p;
        double inf = 1e308 * 10.0;
        return negative ? -inf : inf;
    }
    if ((p[0] == 'n' || p[0] == 'N') && (p[1] == 'a' || p[1] == 'A')
        && (p[2] == 'n' || p[2] == 'N')) {
        p += 3;
        if (*p == '(') { p++; while (*p && *p != ')') p++; if (*p) p++; }
        if (end) *end = (char *)p;
        return 0.0 / 0.0 * (negative ? -1.0 : 1.0);
    }
    double value = 0.0;
    bool any = false;
    while (isdigit((unsigned char)*p)) { any = true; value = value * 10.0 + (*p - '0'); p++; }
    if (*p == '.') {
        p++;
        double place = 0.1;
        while (isdigit((unsigned char)*p)) {
            any = true; value += (*p - '0') * place; place *= 0.1; p++;
        }
    }
    if (!any) { if (end) *end = (char *)text; return 0.0; }
    if (*p == 'e' || *p == 'E') {
        const char *e = p + 1;
        bool eneg = false;
        if (*e == '+' || *e == '-') { eneg = *e == '-'; e++; }
        if (isdigit((unsigned char)*e)) {
            int exp = 0;
            while (isdigit((unsigned char)*e)) { exp = exp * 10 + (*e - '0'); e++; }
            double factor = 1.0;
            while (exp > 0) {
                if (exp >= 8) { factor *= 1e8; exp -= 8; }
                else { factor *= 10.0; exp--; }
                if (factor > 1e308) break;
            }
            value = eneg ? value / factor : value * factor;
            p = e;
        }
    }
    if (end) *end = (char *)p;
    if (value > 1e308) { errno = ERANGE; value = 1e308 * 10.0; }
    return negative ? -value : value;
}

float strtof(const char *text, char **end) {
    // Clamp through double; beyond float range becomes inf (ERANGE).
    errno = 0;
    double value = strtod(text, end);
    if (value > 3.402823466e38 || value < -3.402823466e38) {
        errno = ERANGE;
        return value < 0 ? -(3.402823466e38f * 2.0f) : 3.402823466e38f * 2.0f;
    }
    return (float)value;
}

long double strtold(const char *text, char **end) {
    // Long-double range exceeds double; literals outside double range
    // clamp to infinity (documented TCC-port limitation).
    errno = 0;
    double value = strtod(text, end);
    return (long double)value;
}

// ---- misc ----

int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
    const uint8_t *items = (const uint8_t *)base;
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        const void *item = items + middle * size;
        int order = compare(key, item);
        if (order == 0) return (void *)item;
        if (order < 0) high = middle;
        else low = middle + 1;
    }
    return 0;
}

static void qsort_swap(uint8_t *a, uint8_t *b, size_t size) {
    while (size--) { uint8_t t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *)) {
    if (count < 2 || !size || !compare) return;
    uint8_t *items = (uint8_t *)base;
    // Iterative quicksort with median-of-three pivot; insertion sort for
    // tiny partitions keeps stack use flat (matters on small stacks).
    size_t stack[64];
    int depth = 0;
    stack[depth++] = 0;
    stack[depth++] = count - 1;
    while (depth > 0) {
        size_t high = stack[--depth];
        size_t low = stack[--depth];
        if (high - low < 16) {
            for (size_t i = low + 1; i <= high; i++) {
                size_t j = i;
                while (j > low && compare(items + (j - 1) * size, items + j * size) > 0) {
                    qsort_swap(items + (j - 1) * size, items + j * size, size);
                    j--;
                }
            }
            continue;
        }
        size_t mid = low + (high - low) / 2;
        if (compare(items + low * size, items + mid * size) > 0)
            qsort_swap(items + low * size, items + mid * size, size);
        if (compare(items + mid * size, items + high * size) > 0)
            qsort_swap(items + mid * size, items + high * size, size);
        if (compare(items + low * size, items + mid * size) > 0)
            qsort_swap(items + low * size, items + mid * size, size);
        // Median now at mid; move it to high and run Lomuto.
        qsort_swap(items + mid * size, items + high * size, size);
        size_t store = low;
        for (size_t k = low; k < high; k++) {
            if (compare(items + k * size, items + high * size) <= 0) {
                qsort_swap(items + store * size, items + k * size, size);
                store++;
            }
        }
        qsort_swap(items + store * size, items + high * size, size);
        // Push larger partition first to bound explicit stack depth.
        bool has_left = store > low;
        bool has_right = store < high;
        size_t left_lo = low, left_hi = has_left ? store - 1 : 0;
        size_t right_lo = has_right ? store + 1 : 0, right_hi = high;
        size_t left_size = has_left ? left_hi - left_lo + 1 : 0;
        size_t right_size = has_right ? right_hi - right_lo + 1 : 0;
        if (left_size > right_size) {
            if (has_left && depth + 2 <= 64) { stack[depth++] = left_lo; stack[depth++] = left_hi; }
            if (has_right && depth + 2 <= 64) { stack[depth++] = right_lo; stack[depth++] = right_hi; }
        } else {
            if (has_right && depth + 2 <= 64) { stack[depth++] = right_lo; stack[depth++] = right_hi; }
            if (has_left && depth + 2 <= 64) { stack[depth++] = left_lo; stack[depth++] = left_hi; }
        }
        if (depth + 2 > 64) {
            // Degenerate fallback: finish with insertion sort over all.
            for (size_t k = 1; k < count; k++) {
                size_t m = k;
                while (m > 0 && compare(items + (m - 1) * size, items + m * size) > 0) {
                    qsort_swap(items + (m - 1) * size, items + m * size, size);
                    m--;
                }
            }
            return;
        }
    }
}

static unsigned long rand_state = 1;

int rand(void) {
    rand_state = rand_state * 1103515245UL + 12345UL;
    return (int)((rand_state >> 16) & 0x7FFF);
}

void srand(unsigned int seed) { rand_state = seed ? seed : 1; }

char **environ;
static bool environ_owned;

static size_t environ_count(void) {
    size_t count = 0;
    if (environ) while (environ[count]) count++;
    return count;
}

static bool environ_match(const char *entry, const char *name, size_t length) {
    return strncmp(entry, name, length) == 0 && entry[length] == '=';
}

static bool environ_make_owned(size_t extra) {
    size_t count = environ_count();
    char **fresh = (char **)malloc((count + extra + 1) * sizeof(char *));
    if (!fresh) return false;
    for (size_t i = 0; i < count; i++) {
        if (environ_owned) { fresh[i] = environ[i]; continue; }
        size_t length = strlen(environ[i]) + 1;
        fresh[i] = (char *)malloc(length);
        if (!fresh[i]) return false;
        memcpy(fresh[i], environ[i], length);
    }
    fresh[count] = 0;
    if (environ_owned) free(environ);
    environ = fresh;
    environ_owned = true;
    return true;
}

char *getenv(const char *name) {
    static char value[128];
    if (!name || !name[0]) return 0;
    if (environ) {
        size_t length = strlen(name);
        for (size_t i = 0; environ[i]; i++) {
            if (environ_match(environ[i], name, length))
                return environ[i] + length + 1;
        }
        return 0;
    }
    if (pc_getenv(name, value, sizeof(value)) < 0) return 0;
    return value;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !name[0] || !value || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!environ) {
        if (!overwrite && getenv(name)) return 0;
        return pc_setenv(name, value) < 0 ? -1 : 0;
    }
    size_t length = strlen(name);
    for (size_t i = 0; environ[i]; i++) {
        if (!environ_match(environ[i], name, length)) continue;
        if (!overwrite) return 0;
        if (!environ_make_owned(0)) { errno = ENOMEM; return -1; }
        char *entry = (char *)malloc(length + strlen(value) + 2);
        if (!entry) { errno = ENOMEM; return -1; }
        memcpy(entry, name, length);
        entry[length] = '=';
        strcpy(entry + length + 1, value);
        environ[i] = entry;
        (void)pc_setenv(name, value);
        return 0;
    }
    if (!environ_make_owned(1)) { errno = ENOMEM; return -1; }
    char *entry = (char *)malloc(length + strlen(value) + 2);
    if (!entry) { errno = ENOMEM; return -1; }
    memcpy(entry, name, length);
    entry[length] = '=';
    strcpy(entry + length + 1, value);
    size_t count = environ_count();
    environ[count] = entry;
    environ[count + 1] = 0;
    (void)pc_setenv(name, value);
    return 0;
}

int unsetenv(const char *name) {
    if (!name || !name[0] || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!environ) return pc_unsetenv(name) < 0 ? -1 : 0;
    size_t length = strlen(name);
    for (size_t i = 0; environ[i]; i++) {
        if (!environ_match(environ[i], name, length)) continue;
        if (!environ_make_owned(0)) { errno = ENOMEM; return -1; }
        size_t count = environ_count();
        for (size_t j = i; j < count; j++) environ[j] = environ[j + 1];
        (void)pc_unsetenv(name);
        return 0;
    }
    return 0;
}

int clearenv(void) {
    if (environ_owned) free(environ);
    environ = (char **)malloc(sizeof(char *));
    if (!environ) { errno = ENOMEM; return -1; }
    environ[0] = 0;
    environ_owned = true;
    return 0;
}

struct exit_handler {
    struct exit_handler *next;
    void (*function)(void *);
    void *argument;
    void *dso;
};

extern void __pc_dl_fini(void) __attribute__((weak));

static struct exit_handler *exit_handlers;
static int exit_running;

int __cxa_atexit(void (*function)(void *), void *argument, void *dso) {
    struct exit_handler *entry = (struct exit_handler *)malloc(sizeof(*entry));
    if (!entry) return -1;
    entry->function = function;
    entry->argument = argument;
    entry->dso = dso;
    entry->next = exit_handlers;
    exit_handlers = entry;
    return 0;
}

static void atexit_thunk(void *function) {
    ((void (*)(void))function)();
}

int atexit(void (*function)(void)) {
    if (!function) return -1;
    return __cxa_atexit(atexit_thunk, (void *)function, 0);
}

void __cxa_finalize(void *dso) {
    struct exit_handler **link = &exit_handlers;
    while (*link) {
        struct exit_handler *entry = *link;
        if (dso && entry->dso != dso) {
            link = &entry->next;
            continue;
        }
        *link = entry->next;
        entry->function(entry->argument);
        free(entry);
        link = &exit_handlers;
    }
}

void exit(int status) {
    extern int fflush(void *stream);
    if (!exit_running) {
        exit_running = 1;
        __cxa_finalize(0);
        if (__pc_dl_fini) __pc_dl_fini();
    }
    fflush(0);
    pc_exit(status);
}
int putenv(char *entry) {
    if (!entry) return -1;
    char name[128];
    size_t length = 0;
    while (entry[length] && entry[length] != '=') length++;
    if (!entry[length] || length == 0 || length >= sizeof(name)) return -1;
    memcpy(name, entry, length);
    name[length] = '\0';
    return setenv(name, entry + length + 1, 1);
}

double atof(const char *text) { return strtod(text, 0); }

void _exit(int status) { pc_exit(status); for (;;) { } }
void abort(void) { pc_exit(134); }
