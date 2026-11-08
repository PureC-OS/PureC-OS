// PureC hosted libc: wide characters. UTF-8 is the multibyte encoding, wchar_t
// is a 32-bit Unicode code point. Classification covers ASCII, Latin-1,
// Latin Extended, Greek, Cyrillic and the common CJK/Hangul ranges.

#include "include/hosted/errno.h"
#include "include/hosted/stdio.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/time.h"
#include "include/hosted/wchar.h"
#include "include/hosted/wctype.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define SIZE_ERROR ((size_t)-1)
#define SIZE_PARTIAL ((size_t)-2)

static mbstate_t internal_state;

size_t wcslen(const wchar_t *text)
{
    size_t length = 0;
    while (text[length])
        length++;
    return length;
}

size_t wcsnlen(const wchar_t *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length])
        length++;
    return length;
}

wchar_t *wcscpy(wchar_t *destination, const wchar_t *source)
{
    wchar_t *start = destination;
    while ((*destination++ = *source++))
        ;
    return start;
}

wchar_t *wcsncpy(wchar_t *destination, const wchar_t *source, size_t count)
{
    size_t index = 0;
    for (; index < count && source[index]; index++)
        destination[index] = source[index];
    for (; index < count; index++)
        destination[index] = 0;
    return destination;
}

wchar_t *wcscat(wchar_t *destination, const wchar_t *source)
{
    wcscpy(destination + wcslen(destination), source);
    return destination;
}

wchar_t *wcsncat(wchar_t *destination, const wchar_t *source, size_t count)
{
    wchar_t *end = destination + wcslen(destination);
    size_t index = 0;
    for (; index < count && source[index]; index++)
        end[index] = source[index];
    end[index] = 0;
    return destination;
}

int wcscmp(const wchar_t *left, const wchar_t *right)
{
    while (*left && *left == *right)
    {
        left++;
        right++;
    }
    return (*left > *right) - (*left < *right);
}

int wcsncmp(const wchar_t *left, const wchar_t *right, size_t count)
{
    for (size_t index = 0; index < count; index++)
    {
        if (left[index] != right[index])
            return left[index] > right[index] ? 1 : -1;
        if (!left[index])
            return 0;
    }
    return 0;
}

int wcscoll(const wchar_t *left, const wchar_t *right) { return wcscmp(left, right); }

size_t wcsxfrm(wchar_t *destination, const wchar_t *source, size_t count)
{
    size_t length = wcslen(source);
    if (count)
    {
        size_t copy = length < count - 1 ? length : count - 1;
        wmemcpy(destination, source, copy);
        destination[copy] = 0;
    }
    return length;
}

wchar_t *wcschr(const wchar_t *text, wchar_t value)
{
    for (;; text++)
    {
        if (*text == value)
            return (wchar_t *)text;
        if (!*text)
            return 0;
    }
}

wchar_t *wcsrchr(const wchar_t *text, wchar_t value)
{
    const wchar_t *found = 0;
    for (;; text++)
    {
        if (*text == value)
            found = text;
        if (!*text)
            return (wchar_t *)found;
    }
}

wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle)
{
    size_t length = wcslen(needle);
    if (!length)
        return (wchar_t *)haystack;
    for (; *haystack; haystack++)
        if (*haystack == *needle && !wcsncmp(haystack, needle, length))
            return (wchar_t *)haystack;
    return 0;
}

wchar_t *wcspbrk(const wchar_t *text, const wchar_t *accept)
{
    for (; *text; text++)
        if (wcschr(accept, *text))
            return (wchar_t *)text;
    return 0;
}

size_t wcsspn(const wchar_t *text, const wchar_t *accept)
{
    size_t length = 0;
    while (text[length] && wcschr(accept, text[length]))
        length++;
    return length;
}

size_t wcscspn(const wchar_t *text, const wchar_t *reject)
{
    size_t length = 0;
    while (text[length] && !wcschr(reject, text[length]))
        length++;
    return length;
}

wchar_t *wcstok(wchar_t *text, const wchar_t *delimiters, wchar_t **state)
{
    if (!text)
        text = *state;
    if (!text)
        return 0;
    text += wcsspn(text, delimiters);
    if (!*text)
    {
        *state = 0;
        return 0;
    }
    wchar_t *end = text + wcscspn(text, delimiters);
    if (*end)
    {
        *end = 0;
        *state = end + 1;
    }
    else
    {
        *state = 0;
    }
    return text;
}

wchar_t *wcsdup(const wchar_t *text)
{
    size_t length = wcslen(text) + 1;
    wchar_t *copy = malloc(length * sizeof(wchar_t));
    if (copy)
        memcpy(copy, text, length * sizeof(wchar_t));
    return copy;
}

wchar_t *wmemcpy(wchar_t *destination, const wchar_t *source, size_t count)
{
    return memcpy(destination, source, count * sizeof(wchar_t));
}

wchar_t *wmemmove(wchar_t *destination, const wchar_t *source, size_t count)
{
    return memmove(destination, source, count * sizeof(wchar_t));
}

wchar_t *wmemset(wchar_t *destination, wchar_t value, size_t count)
{
    for (size_t index = 0; index < count; index++)
        destination[index] = value;
    return destination;
}

int wmemcmp(const wchar_t *left, const wchar_t *right, size_t count)
{
    for (size_t index = 0; index < count; index++)
        if (left[index] != right[index])
            return left[index] > right[index] ? 1 : -1;
    return 0;
}

wchar_t *wmemchr(const wchar_t *text, wchar_t value, size_t count)
{
    for (size_t index = 0; index < count; index++)
        if (text[index] == value)
            return (wchar_t *)(text + index);
    return 0;
}

int mbsinit(const mbstate_t *state)
{
    return !state || !state->needed;
}

size_t mbrtowc(wchar_t *out, const char *text, size_t count, mbstate_t *state)
{
    if (!state)
        state = &internal_state;
    if (!text)
    {
        state->needed = 0;
        state->partial = 0;
        state->state = 0;
        return 0;
    }
    for (size_t index = 0; index < count; index++)
    {
        unsigned char byte = (unsigned char)text[index];
        if (!state->needed)
        {
            if (byte < 0x80)
            {
                if (out)
                    *out = byte;
                return byte ? index + 1 : 0;
            }
            if ((byte & 0xE0) == 0xC0 && byte >= 0xC2)
            {
                state->partial = byte & 0x1F;
                state->needed = 1;
            }
            else if ((byte & 0xF0) == 0xE0)
            {
                state->partial = byte & 0x0F;
                state->needed = 2;
            }
            else if ((byte & 0xF8) == 0xF0 && byte <= 0xF4)
            {
                state->partial = byte & 0x07;
                state->needed = 3;
            }
            else
            {
                errno = EILSEQ;
                return SIZE_ERROR;
            }
            state->state = 1;
            continue;
        }
        if ((byte & 0xC0) != 0x80)
        {
            state->needed = 0;
            state->state = 0;
            errno = EILSEQ;
            return SIZE_ERROR;
        }
        state->partial = (state->partial << 6) | (byte & 0x3F);
        if (--state->needed)
            continue;
        unsigned int value = state->partial;
        state->state = 0;
        if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) || value < 0x80)
        {
            errno = EILSEQ;
            return SIZE_ERROR;
        }
        if (out)
            *out = (wchar_t)value;
        return index + 1;
    }
    return SIZE_PARTIAL;
}

size_t mbrlen(const char *text, size_t count, mbstate_t *state)
{
    return mbrtowc(0, text, count, state ? state : &internal_state);
}

size_t wcrtomb(char *out, wchar_t value, mbstate_t *state)
{
    (void)state;
    unsigned int code = (unsigned int)value;
    if (!out)
        return 1;
    if (code < 0x80)
    {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800)
    {
        out[0] = (char)(0xC0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    }
    if (code >= 0xD800 && code <= 0xDFFF)
    {
        errno = EILSEQ;
        return SIZE_ERROR;
    }
    if (code < 0x10000)
    {
        out[0] = (char)(0xE0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        out[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    }
    if (code < 0x110000)
    {
        out[0] = (char)(0xF0 | (code >> 18));
        out[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        out[3] = (char)(0x80 | (code & 0x3F));
        return 4;
    }
    errno = EILSEQ;
    return SIZE_ERROR;
}

size_t mbsnrtowcs(wchar_t *out, const char **text, size_t limit, size_t count, mbstate_t *state)
{
    if (!state)
        state = &internal_state;
    const char *cursor = *text;
    size_t produced = 0;
    size_t used = 0;
    while (used < limit && (!out || produced < count))
    {
        wchar_t value;
        size_t step = mbrtowc(&value, cursor, limit - used, state);
        if (step == SIZE_ERROR)
        {
            *text = cursor;
            return SIZE_ERROR;
        }
        if (step == SIZE_PARTIAL)
        {
            cursor += limit - used;
            used = limit;
            break;
        }
        if (step == 0)
        {
            if (out)
                out[produced] = 0;
            *text = 0;
            return produced;
        }
        if (out)
            out[produced] = value;
        produced++;
        cursor += step;
        used += step;
    }
    *text = cursor;
    return produced;
}

size_t mbsrtowcs(wchar_t *out, const char **text, size_t count, mbstate_t *state)
{
    return mbsnrtowcs(out, text, SIZE_MAX, count, state);
}

size_t wcsnrtombs(char *out, const wchar_t **text, size_t limit, size_t count, mbstate_t *state)
{
    const wchar_t *cursor = *text;
    size_t produced = 0;
    char piece[4];
    for (size_t index = 0; index < limit; index++)
    {
        wchar_t value = cursor[index];
        if (!value)
        {
            if (out && produced < count)
                out[produced] = 0;
            *text = 0;
            return produced;
        }
        size_t used = wcrtomb(piece, value, state);
        if (used == SIZE_ERROR)
        {
            *text = cursor + index;
            return SIZE_ERROR;
        }
        if (out)
        {
            if (produced + used > count)
            {
                *text = cursor + index;
                return produced;
            }
            memcpy(out + produced, piece, used);
        }
        produced += used;
    }
    *text = cursor + limit;
    return produced;
}

size_t wcsrtombs(char *out, const wchar_t **text, size_t count, mbstate_t *state)
{
    return wcsnrtombs(out, text, SIZE_MAX, count, state);
}

size_t mbstowcs(wchar_t *out, const char *text, size_t count)
{
    const char *cursor = text;
    mbstate_t state = {0, 0, 0};
    size_t produced = mbsrtowcs(out, &cursor, out ? count : SIZE_MAX, &state);
    return produced;
}

size_t wcstombs(char *out, const wchar_t *text, size_t count)
{
    const wchar_t *cursor = text;
    mbstate_t state = {0, 0, 0};
    return wcsrtombs(out, &cursor, out ? count : SIZE_MAX, &state);
}

wint_t btowc(int value)
{
    return value >= 0 && value < 0x80 ? (wint_t)value : WEOF;
}

int wctob(wint_t value)
{
    return value < 0x80 ? (int)value : -1;
}

int mblen(const char *text, size_t count)
{
    if (!text)
        return 0;
    mbstate_t state = {0, 0, 0};
    size_t result = mbrtowc(0, text, count, &state);
    return result == SIZE_ERROR || result == SIZE_PARTIAL ? -1 : (int)result;
}

int mbtowc(wchar_t *out, const char *text, size_t count)
{
    if (!text)
        return 0;
    mbstate_t state = {0, 0, 0};
    size_t result = mbrtowc(out, text, count, &state);
    return result == SIZE_ERROR || result == SIZE_PARTIAL ? -1 : (int)result;
}

int wctomb(char *out, wchar_t value)
{
    if (!out)
        return 0;
    size_t result = wcrtomb(out, value, 0);
    return result == SIZE_ERROR ? -1 : (int)result;
}

static int in_range(wint_t value, wint_t low, wint_t high)
{
    return value >= low && value <= high;
}

int iswupper(wint_t value)
{
    if (value < 0x80)
        return value >= 'A' && value <= 'Z';
    if (in_range(value, 0xC0, 0xDE))
        return value != 0xD7;
    if (in_range(value, 0x100, 0x17F))
    {
        if (value == 0x138 || value == 0x149 || value == 0x17F)
            return 0;
        if (in_range(value, 0x139, 0x148) || in_range(value, 0x179, 0x17E))
            return value & 1;
        return !(value & 1);
    }
    return in_range(value, 0x391, 0x3A9) || in_range(value, 0x400, 0x42F);
}

int iswlower(wint_t value)
{
    if (value < 0x80)
        return value >= 'a' && value <= 'z';
    if (value == 0xB5 || in_range(value, 0xDF, 0xFF))
        return value != 0xF7;
    if (in_range(value, 0x100, 0x17F))
        return !iswupper(value);
    return in_range(value, 0x3B1, 0x3C9) || in_range(value, 0x430, 0x45F);
}

int iswalpha(wint_t value)
{
    if (value < 0x80)
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    return value == 0xAA || value == 0xB5 || value == 0xBA || in_range(value, 0xC0, 0xD6) ||
           in_range(value, 0xD8, 0xF6) || in_range(value, 0xF8, 0x2C1) ||
           in_range(value, 0x370, 0x3FF) || in_range(value, 0x400, 0x52F) ||
           in_range(value, 0x531, 0x556) || in_range(value, 0x561, 0x587) ||
           in_range(value, 0x5D0, 0x5EA) || in_range(value, 0x620, 0x64A) ||
           in_range(value, 0x904, 0x939) || in_range(value, 0xE01, 0xE30) ||
           in_range(value, 0x1E00, 0x1FFF) || in_range(value, 0x3041, 0x30FF) ||
           in_range(value, 0x3400, 0x4DBF) || in_range(value, 0x4E00, 0x9FFF) ||
           in_range(value, 0xAC00, 0xD7A3);
}

int iswdigit(wint_t value) { return value >= '0' && value <= '9'; }

int iswxdigit(wint_t value)
{
    return iswdigit(value) || in_range(value, 'a', 'f') || in_range(value, 'A', 'F');
}

int iswalnum(wint_t value) { return iswalpha(value) || iswdigit(value); }

int iswspace(wint_t value)
{
    return value == ' ' || in_range(value, 9, 13) || value == 0x85 || value == 0x1680 ||
           in_range(value, 0x2000, 0x2006) || in_range(value, 0x2008, 0x200A) ||
           value == 0x2028 || value == 0x2029 || value == 0x205F || value == 0x3000;
}

int iswblank(wint_t value)
{
    return value == ' ' || value == '\t' || value == 0x1680 ||
           in_range(value, 0x2000, 0x2006) || in_range(value, 0x2008, 0x200A) ||
           value == 0x205F || value == 0x3000;
}

int iswcntrl(wint_t value)
{
    return value < 0x20 || in_range(value, 0x7F, 0x9F) || value == 0x2028 || value == 0x2029;
}

int iswprint(wint_t value)
{
    if (value < 0x20 || in_range(value, 0x7F, 0x9F) || value == 0x2028 || value == 0x2029)
        return 0;
    if (in_range(value, 0xD800, 0xDFFF) || value > 0x10FFFF)
        return 0;
    return 1;
}

int iswgraph(wint_t value) { return iswprint(value) && !iswspace(value); }

int iswpunct(wint_t value) { return iswgraph(value) && !iswalnum(value); }

wint_t towlower(wint_t value)
{
    if (value < 0x80)
        return value >= 'A' && value <= 'Z' ? value + 32 : value;
    if (in_range(value, 0xC0, 0xDE) && value != 0xD7)
        return value + 32;
    if (in_range(value, 0x100, 0x17F) && iswupper(value))
        return value + 1;
    if (in_range(value, 0x391, 0x3A9))
        return value + 32;
    if (in_range(value, 0x410, 0x42F))
        return value + 32;
    if (in_range(value, 0x400, 0x40F))
        return value + 80;
    return value;
}

wint_t towupper(wint_t value)
{
    if (value < 0x80)
        return value >= 'a' && value <= 'z' ? value - 32 : value;
    if (value == 0xB5)
        return 0x39C;
    if (in_range(value, 0xE0, 0xFE) && value != 0xF7)
        return value - 32;
    if (value == 0xFF)
        return 0x178;
    if (in_range(value, 0x100, 0x17F) && iswlower(value) && value != 0x138 && value != 0x149 &&
        value != 0x17F)
        return value - 1;
    if (in_range(value, 0x3B1, 0x3C9) && value != 0x3C2)
        return value - 32;
    if (in_range(value, 0x430, 0x44F))
        return value - 32;
    if (in_range(value, 0x450, 0x45F))
        return value - 80;
    return value;
}

static const char *const class_names[] = {"alnum", "alpha", "blank", "cntrl", "digit", "graph",
                                           "lower", "print", "punct", "space", "upper", "xdigit"};

wctype_t wctype(const char *name)
{
    for (unsigned index = 0; index < sizeof(class_names) / sizeof(class_names[0]); index++)
        if (!strcmp(name, class_names[index]))
            return index + 1;
    return 0;
}

int iswctype(wint_t value, wctype_t type)
{
    switch (type)
    {
    case 1: return iswalnum(value);
    case 2: return iswalpha(value);
    case 3: return iswblank(value);
    case 4: return iswcntrl(value);
    case 5: return iswdigit(value);
    case 6: return iswgraph(value);
    case 7: return iswlower(value);
    case 8: return iswprint(value);
    case 9: return iswpunct(value);
    case 10: return iswspace(value);
    case 11: return iswupper(value);
    case 12: return iswxdigit(value);
    default: return 0;
    }
}

static const int transform_lower = 1;
static const int transform_upper = 2;

wctrans_t wctrans(const char *name)
{
    if (!strcmp(name, "tolower"))
        return &transform_lower;
    if (!strcmp(name, "toupper"))
        return &transform_upper;
    return 0;
}

wint_t towctrans(wint_t value, wctrans_t transform)
{
    if (transform == &transform_lower)
        return towlower(value);
    if (transform == &transform_upper)
        return towupper(value);
    return value;
}

int wcwidth(wchar_t value)
{
    if (value == 0)
        return 0;
    if (value < 0x20 || in_range((wint_t)value, 0x7F, 0x9F))
        return -1;
    if (in_range((wint_t)value, 0x300, 0x36F) || in_range((wint_t)value, 0x200B, 0x200F))
        return 0;
    if (in_range((wint_t)value, 0x1100, 0x115F) || in_range((wint_t)value, 0x2E80, 0xA4CF) ||
        in_range((wint_t)value, 0xAC00, 0xD7A3) || in_range((wint_t)value, 0xF900, 0xFAFF) ||
        in_range((wint_t)value, 0xFE30, 0xFE6F) || in_range((wint_t)value, 0xFF00, 0xFF60) ||
        in_range((wint_t)value, 0xFFE0, 0xFFE6) || in_range((wint_t)value, 0x1F300, 0x1F64F) ||
        in_range((wint_t)value, 0x20000, 0x3FFFD))
        return 2;
    return 1;
}

int wcswidth(const wchar_t *text, size_t count)
{
    int total = 0;
    for (size_t index = 0; index < count && text[index]; index++)
    {
        int width = wcwidth(text[index]);
        if (width < 0)
            return -1;
        total += width;
    }
    return total;
}

#define NUMBER_BUFFER 512

static size_t narrow_number(const wchar_t *text, char *buffer, const wchar_t **start)
{
    while (iswspace((wint_t)*text))
        text++;
    *start = text;
    size_t length = 0;
    while (text[length] && text[length] < 0x80 && length + 1 < NUMBER_BUFFER)
    {
        buffer[length] = (char)text[length];
        length++;
    }
    buffer[length] = 0;
    return length;
}

#define WIDE_INTEGER(name, type, narrow)                                       \
    type name(const wchar_t *text, wchar_t **end, int base)                    \
    {                                                                          \
        char buffer[NUMBER_BUFFER];                                            \
        const wchar_t *start;                                                  \
        narrow_number(text, buffer, &start);                                   \
        char *stop;                                                            \
        type value = narrow(buffer, &stop, base);                              \
        if (end)                                                               \
            *end = (wchar_t *)(stop == buffer ? text : start + (stop - buffer)); \
        return value;                                                          \
    }

WIDE_INTEGER(wcstol, long, strtol)
WIDE_INTEGER(wcstoul, unsigned long, strtoul)
WIDE_INTEGER(wcstoll, long long, strtoll)
WIDE_INTEGER(wcstoull, unsigned long long, strtoull)

#define WIDE_FLOAT(name, type, narrow)                                         \
    type name(const wchar_t *text, wchar_t **end)                              \
    {                                                                          \
        char buffer[NUMBER_BUFFER];                                            \
        const wchar_t *start;                                                  \
        narrow_number(text, buffer, &start);                                   \
        char *stop;                                                            \
        type value = narrow(buffer, &stop);                                    \
        if (end)                                                               \
            *end = (wchar_t *)(stop == buffer ? text : start + (stop - buffer)); \
        return value;                                                          \
    }

WIDE_FLOAT(wcstod, double, strtod)
WIDE_FLOAT(wcstof, float, strtof)
WIDE_FLOAT(wcstold, long double, strtold)

static int stream_orientation(FILE *stream)
{
    (void)stream;
    return 0;
}

int fwide(FILE *stream, int mode)
{
    (void)mode;
    return stream_orientation(stream);
}

wint_t fgetwc(FILE *stream)
{
    int first = fgetc(stream);
    if (first == EOF)
        return WEOF;
    if (first < 0x80)
        return (wint_t)first;
    char bytes[4];
    bytes[0] = (char)first;
    size_t length = 1;
    mbstate_t state = {0, 0, 0};
    for (;;)
    {
        wchar_t value;
        size_t result = mbrtowc(&value, bytes + length - 1, 1, &state);
        if (result == SIZE_PARTIAL)
        {
            int next = fgetc(stream);
            if (next == EOF || length >= 4)
            {
                errno = EILSEQ;
                return WEOF;
            }
            bytes[length++] = (char)next;
            continue;
        }
        if (result == SIZE_ERROR)
        {
            errno = EILSEQ;
            return WEOF;
        }
        return (wint_t)value;
    }
}

wint_t getwc(FILE *stream) { return fgetwc(stream); }

wint_t getwchar(void) { return fgetwc(stdin); }

wint_t fputwc(wchar_t value, FILE *stream)
{
    char bytes[4];
    size_t length = wcrtomb(bytes, value, 0);
    if (length == SIZE_ERROR)
        return WEOF;
    for (size_t index = 0; index < length; index++)
        if (fputc((unsigned char)bytes[index], stream) == EOF)
            return WEOF;
    return (wint_t)value;
}

wint_t putwc(wchar_t value, FILE *stream) { return fputwc(value, stream); }

wint_t putwchar(wchar_t value) { return fputwc(value, stdout); }

wint_t ungetwc(wint_t value, FILE *stream)
{
    if (value == WEOF)
        return WEOF;
    char bytes[4];
    size_t length = wcrtomb(bytes, (wchar_t)value, 0);
    if (length == SIZE_ERROR)
        return WEOF;
    for (size_t index = length; index > 0; index--)
        if (ungetc((unsigned char)bytes[index - 1], stream) == EOF)
            return WEOF;
    return value;
}

wchar_t *fgetws(wchar_t *buffer, int count, FILE *stream)
{
    if (count <= 0)
        return 0;
    int index = 0;
    while (index + 1 < count)
    {
        wint_t value = fgetwc(stream);
        if (value == WEOF)
            break;
        buffer[index++] = (wchar_t)value;
        if (value == L'\n')
            break;
    }
    if (!index)
        return 0;
    buffer[index] = 0;
    return buffer;
}

int fputws(const wchar_t *text, FILE *stream)
{
    for (; *text; text++)
        if (fputwc(*text, stream) == WEOF)
            return -1;
    return 1;
}

static char *narrow_format(const wchar_t *format)
{
    size_t bytes = wcstombs(0, format, 0);
    if (bytes == SIZE_ERROR)
        return 0;
    char *copy = malloc(bytes + 1);
    if (copy)
        wcstombs(copy, format, bytes + 1);
    return copy;
}

int vswprintf(wchar_t *buffer, size_t count, const wchar_t *format, va_list arguments)
{
    char *narrow = narrow_format(format);
    if (!narrow)
        return -1;
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(0, 0, narrow, copy);
    va_end(copy);
    if (length < 0)
    {
        free(narrow);
        return -1;
    }
    char *text = malloc((size_t)length + 1);
    if (!text)
    {
        free(narrow);
        return -1;
    }
    vsnprintf(text, (size_t)length + 1, narrow, arguments);
    free(narrow);
    size_t produced = 0;
    const char *cursor = text;
    mbstate_t state = {0, 0, 0};
    while (*cursor)
    {
        wchar_t value;
        size_t used = mbrtowc(&value, cursor, 4, &state);
        if (used == SIZE_ERROR || used == SIZE_PARTIAL || used == 0)
        {
            free(text);
            return -1;
        }
        if (produced + 1 >= count)
        {
            free(text);
            if (count)
                buffer[count - 1] = 0;
            return -1;
        }
        buffer[produced++] = value;
        cursor += used;
    }
    free(text);
    if (count)
        buffer[produced] = 0;
    return (int)produced;
}

int swprintf(wchar_t *buffer, size_t count, const wchar_t *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vswprintf(buffer, count, format, arguments);
    va_end(arguments);
    return result;
}

int vfwprintf(FILE *stream, const wchar_t *format, va_list arguments)
{
    char *narrow = narrow_format(format);
    if (!narrow)
        return -1;
    int result = vfprintf(stream, narrow, arguments);
    free(narrow);
    return result;
}

int fwprintf(FILE *stream, const wchar_t *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfwprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int vwprintf(const wchar_t *format, va_list arguments)
{
    return vfwprintf(stdout, format, arguments);
}

int wprintf(const wchar_t *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfwprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}

size_t wcsftime(wchar_t *buffer, size_t count, const wchar_t *format, const struct tm *time)
{
    char *narrow = narrow_format(format);
    if (!narrow)
        return 0;
    char text[512];
    size_t length = strftime(text, sizeof(text), narrow, time);
    free(narrow);
    if (!length)
        return 0;
    const char *cursor = text;
    size_t produced = mbsrtowcs(buffer, &cursor, count, 0);
    if (produced == SIZE_ERROR || produced >= count)
        return 0;
    return produced;
}
