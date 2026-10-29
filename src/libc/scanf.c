// PureC hosted libc: sscanf/vsscanf. Supports %d %i %u %o %x %X %c %s
// %[set] %f %e %g %p %n %%, the * flag, field width and the
// hh/h/l/ll/z/j/t/L length modifiers.

#include "include/hosted/stdio.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/ctype.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

enum length { LEN_INT, LEN_CHAR, LEN_SHORT, LEN_LONG, LEN_LLONG, LEN_SIZE, LEN_LDOUBLE };

static void store_integer(va_list *args, enum length length, bool is_signed,
                          unsigned long long value) {
    switch (length) {
    case LEN_CHAR:
        if (is_signed) *va_arg(*args, signed char *) = (signed char)value;
        else *va_arg(*args, unsigned char *) = (unsigned char)value;
        break;
    case LEN_SHORT:
        if (is_signed) *va_arg(*args, short *) = (short)value;
        else *va_arg(*args, unsigned short *) = (unsigned short)value;
        break;
    case LEN_LONG:
    case LEN_LLONG:
    case LEN_SIZE:
        if (is_signed) *va_arg(*args, long long *) = (long long)value;
        else *va_arg(*args, unsigned long long *) = value;
        break;
    default:
        if (is_signed) *va_arg(*args, int *) = (int)value;
        else *va_arg(*args, unsigned int *) = (unsigned int)value;
        break;
    }
}

static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 99;
}

static bool in_set(const char *set, size_t set_length, bool negate, int c) {
    bool found = false;
    for (size_t i = 0; i < set_length; i++) {
        if (i + 2 < set_length && set[i + 1] == '-') {
            if (c >= (unsigned char)set[i] && c <= (unsigned char)set[i + 2]) found = true;
            i += 2;
        } else if ((unsigned char)set[i] == c) {
            found = true;
        }
    }
    return found != negate;
}

int vsscanf(const char *input, const char *format, va_list args_in) {
    va_list args;
    va_copy(args, args_in);
    const char *p = input;
    int assigned = 0;
    bool any_input = false;

    while (*format) {
        if (isspace((unsigned char)*format)) {
            while (isspace((unsigned char)*p)) p++;
            format++;
            continue;
        }
        if (*format != '%') {
            if (*p != *format) goto done;
            p++;
            format++;
            continue;
        }
        format++;
        if (*format == '%') {
            if (*p != '%') goto done;
            p++;
            format++;
            continue;
        }

        bool suppress = false;
        if (*format == '*') { suppress = true; format++; }
        size_t width = 0;
        while (*format >= '0' && *format <= '9') width = width * 10 + (size_t)(*format++ - '0');
        enum length length = LEN_INT;
        if (*format == 'h') {
            format++;
            if (*format == 'h') { format++; length = LEN_CHAR; } else length = LEN_SHORT;
        } else if (*format == 'l') {
            format++;
            if (*format == 'l') { format++; length = LEN_LLONG; } else length = LEN_LONG;
        } else if (*format == 'z' || *format == 'j' || *format == 't') {
            format++;
            length = LEN_SIZE;
        } else if (*format == 'L') {
            format++;
            length = LEN_LDOUBLE;
        }

        char conv = *format;
        if (!conv) goto done;
        format++;

        if (conv == 'n') {
            if (!suppress) store_integer(&args, length, true, (unsigned long long)(p - input));
            continue;
        }

        if (conv != 'c' && conv != '[') {
            while (isspace((unsigned char)*p)) p++;
        }
        if (!*p) {
            if (!any_input && assigned == 0) assigned = -1;
            goto done;
        }
        size_t limit = width ? width : (size_t)-1;

        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x'
            || conv == 'X' || conv == 'p') {
            int base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X' || conv == 'p') ? 16
                     : conv == 'i' ? 0 : 10;
            bool negative = false;
            const char *start = p;
            size_t used = 0;
            if (limit && (*p == '-' || *p == '+')) { negative = *p == '-'; p++; used++; }
            if ((base == 0 || base == 16) && used + 1 < limit && p[0] == '0'
                && (p[1] == 'x' || p[1] == 'X') && digit_value((unsigned char)p[2]) < 16) {
                p += 2;
                used += 2;
                base = 16;
            } else if (base == 0) {
                base = p[0] == '0' ? 8 : 10;
            }
            unsigned long long value = 0;
            size_t digits = 0;
            while (used < limit && digit_value((unsigned char)*p) < base) {
                value = value * (unsigned)base + (unsigned)digit_value((unsigned char)*p);
                p++;
                used++;
                digits++;
            }
            if (!digits) { p = start; goto done; }
            if (negative) value = 0ULL - value;
            any_input = true;
            if (!suppress) {
                if (conv == 'p') *va_arg(args, void **) = (void *)(uintptr_t)value;
                else store_integer(&args, length, conv == 'd' || conv == 'i', value);
                assigned++;
            }
        } else if (conv == 'f' || conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G'
                   || conv == 'a') {
            char buffer[128];
            size_t count = 0;
            const char *q = p;
            if (count < limit && (*q == '-' || *q == '+')) buffer[count++] = *q++;
            while (count < limit && count + 1 < sizeof(buffer)
                   && (isdigit((unsigned char)*q) || *q == '.' || *q == 'e' || *q == 'E'
                       || ((*q == '-' || *q == '+') && (q[-1] == 'e' || q[-1] == 'E')))) {
                buffer[count++] = *q++;
            }
            buffer[count] = '\0';
            char *end;
            double value = strtod(buffer, &end);
            if (end == buffer) goto done;
            p += end - buffer;
            any_input = true;
            if (!suppress) {
                if (length == LEN_LONG) *va_arg(args, double *) = value;
                else if (length == LEN_LDOUBLE) *va_arg(args, long double *) = (long double)value;
                else *va_arg(args, float *) = (float)value;
                assigned++;
            }
        } else if (conv == 's') {
            char *out = suppress ? 0 : va_arg(args, char *);
            size_t used = 0;
            while (*p && !isspace((unsigned char)*p) && used < limit) {
                if (out) out[used] = *p;
                p++;
                used++;
            }
            if (!used) goto done;
            if (out) { out[used] = '\0'; assigned++; }
            any_input = true;
        } else if (conv == 'c') {
            size_t count = width ? width : 1;
            char *out = suppress ? 0 : va_arg(args, char *);
            for (size_t i = 0; i < count; i++) {
                if (!*p) goto done;
                if (out) out[i] = *p;
                p++;
            }
            if (out) assigned++;
            any_input = true;
        } else if (conv == '[') {
            bool negate = false;
            if (*format == '^') { negate = true; format++; }
            const char *set = format;
            if (*format == ']') format++;
            while (*format && *format != ']') format++;
            size_t set_length = (size_t)(format - set);
            if (*format == ']') format++;
            char *out = suppress ? 0 : va_arg(args, char *);
            size_t used = 0;
            while (*p && used < limit && in_set(set, set_length, negate, (unsigned char)*p)) {
                if (out) out[used] = *p;
                p++;
                used++;
            }
            if (!used) goto done;
            if (out) { out[used] = '\0'; assigned++; }
            any_input = true;
        } else {
            goto done;
        }
    }

done:
    va_end(args);
    return assigned;
}

int sscanf(const char *input, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsscanf(input, format, args);
    va_end(args);
    return result;
}
