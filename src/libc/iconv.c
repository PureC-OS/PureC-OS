#include "include/hosted/iconv.h"
#include "include/hosted/errno.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/stdint.h"

enum charset {
    CS_UTF8,
    CS_ASCII,
    CS_LATIN1,
    CS_LATIN9,
    CS_CP1252,
    CS_UTF16LE,
    CS_UTF16BE,
    CS_UTF16,
    CS_UTF32LE,
    CS_UTF32BE,
    CS_UTF32,
    CS_UCS2LE,
    CS_UCS2BE,
    CS_COUNT
};

struct iconv_state {
    int from;
    int to;
    int translit;
    int ignore;
    int from_bom_seen;
    int to_bom_written;
};

static const uint16_t cp1252_high[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

static int iconv_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int charset_lookup(const char *name, size_t length)
{
    static const struct { const char *name; int id; } table[] = {
        {"UTF-8", CS_UTF8}, {"UTF8", CS_UTF8},
        {"ASCII", CS_ASCII}, {"US-ASCII", CS_ASCII}, {"ANSI_X3.4-1968", CS_ASCII},
        {"ISO-8859-1", CS_LATIN1}, {"ISO8859-1", CS_LATIN1}, {"ISO_8859-1", CS_LATIN1},
        {"LATIN1", CS_LATIN1}, {"L1", CS_LATIN1},
        {"ISO-8859-15", CS_LATIN9}, {"ISO8859-15", CS_LATIN9}, {"LATIN9", CS_LATIN9},
        {"WINDOWS-1252", CS_CP1252}, {"CP1252", CS_CP1252},
        {"UTF-16LE", CS_UTF16LE}, {"UTF-16BE", CS_UTF16BE}, {"UTF-16", CS_UTF16},
        {"UTF-32LE", CS_UTF32LE}, {"UTF-32BE", CS_UTF32BE}, {"UTF-32", CS_UTF32},
        {"UCS-4LE", CS_UTF32LE}, {"UCS-4BE", CS_UTF32BE}, {"UCS-4", CS_UTF32BE},
        {"UCS-2LE", CS_UCS2LE}, {"UCS-2BE", CS_UCS2BE}, {"UCS-2", CS_UCS2LE},
        {"UNICODELITTLE", CS_UTF16LE}, {"UNICODEBIG", CS_UTF16BE},
    };
    char buffer[40];
    if (length >= sizeof buffer) return -1;
    memcpy(buffer, name, length);
    buffer[length] = 0;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (iconv_eq(buffer, table[i].name)) return table[i].id;
    return -1;
}

static int parse_code(const char *code, int *translit, int *ignore)
{
    const char *slash = strchr(code, '/');
    size_t length = slash ? (size_t)(slash - code) : strlen(code);
    if (slash) {
        if (strstr(slash, "TRANSLIT") || strstr(slash, "translit")) *translit = 1;
        if (strstr(slash, "IGNORE") || strstr(slash, "ignore")) *ignore = 1;
    }
    return charset_lookup(code, length);
}

iconv_t iconv_open(const char *to_code, const char *from_code)
{
    if (!to_code || !from_code) { errno = EINVAL; return (iconv_t)-1; }
    struct iconv_state *state = calloc(1, sizeof *state);
    if (!state) { errno = ENOMEM; return (iconv_t)-1; }
    int translit = 0, ignore = 0, dummy_t = 0, dummy_i = 0;
    state->to = parse_code(to_code, &translit, &ignore);
    state->from = parse_code(from_code, &dummy_t, &dummy_i);
    if (state->to < 0 || state->from < 0) {
        free(state);
        errno = EINVAL;
        return (iconv_t)-1;
    }
    state->translit = translit;
    state->ignore = ignore;
    return state;
}

int iconv_close(iconv_t cd)
{
    if (cd == (iconv_t)-1 || !cd) { errno = EBADF; return -1; }
    free(cd);
    return 0;
}

static unsigned cs_endian_big(int cs)
{
    return cs == CS_UTF16BE || cs == CS_UTF32BE || cs == CS_UCS2BE || cs == CS_UTF16 || cs == CS_UTF32;
}

#define DEC_ILSEQ (-1)
#define DEC_SHORT (-2)

static int decode(struct iconv_state *st, const unsigned char *p, size_t n, uint32_t *cp, size_t *used)
{
    if (n == 0) return DEC_SHORT;
    switch (st->from) {
    case CS_ASCII:
        if (p[0] >= 0x80) return DEC_ILSEQ;
        *cp = p[0]; *used = 1; return 0;
    case CS_LATIN1:
        *cp = p[0]; *used = 1; return 0;
    case CS_LATIN9:
        *cp = p[0];
        switch (p[0]) {
        case 0xA4: *cp = 0x20AC; break;
        case 0xA6: *cp = 0x160; break;
        case 0xA8: *cp = 0x161; break;
        case 0xB4: *cp = 0x17D; break;
        case 0xB8: *cp = 0x17E; break;
        case 0xBC: *cp = 0x152; break;
        case 0xBD: *cp = 0x153; break;
        case 0xBE: *cp = 0x178; break;
        }
        *used = 1; return 0;
    case CS_CP1252:
        *cp = (p[0] >= 0x80 && p[0] < 0xA0) ? cp1252_high[p[0] - 0x80] : p[0];
        *used = 1; return 0;
    case CS_UTF8: {
        unsigned char c = p[0];
        size_t need;
        uint32_t value;
        if (c < 0x80) { *cp = c; *used = 1; return 0; }
        if (c >= 0xC2 && c <= 0xDF) { need = 2; value = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 3; value = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 4; value = c & 0x07; }
        else return DEC_ILSEQ;
        if (n < need) {
            for (size_t i = 1; i < n; i++) if ((p[i] & 0xC0) != 0x80) return DEC_ILSEQ;
            return DEC_SHORT;
        }
        for (size_t i = 1; i < need; i++) {
            if ((p[i] & 0xC0) != 0x80) return DEC_ILSEQ;
            value = (value << 6) | (p[i] & 0x3F);
        }
        if ((need == 3 && value < 0x800) || (need == 4 && (value < 0x10000 || value > 0x10FFFF))
            || (value >= 0xD800 && value <= 0xDFFF)) return DEC_ILSEQ;
        *cp = value; *used = need; return 0;
    }
    case CS_UTF16LE: case CS_UTF16BE: case CS_UTF16:
    case CS_UCS2LE: case CS_UCS2BE: {
        int big = (int)cs_endian_big(st->from);
        const unsigned char *q = p;
        size_t consumed = 0;
        if (st->from == CS_UTF16 && !st->from_bom_seen) {
            if (n < 2) return DEC_SHORT;
            if (q[0] == 0xFF && q[1] == 0xFE) { st->from = CS_UTF16LE; q += 2; consumed = 2; big = 0; }
            else if (q[0] == 0xFE && q[1] == 0xFF) { st->from = CS_UTF16BE; q += 2; consumed = 2; big = 1; }
            st->from_bom_seen = 1;
            if (consumed) { if (n - consumed < 2) { *cp = 0xFEFF; *used = consumed; return 1; } }
        }
        if (n - consumed < 2) return DEC_SHORT;
        uint32_t unit = big ? (uint32_t)(q[0] << 8 | q[1]) : (uint32_t)(q[1] << 8 | q[0]);
        if (unit >= 0xD800 && unit <= 0xDBFF && st->from != CS_UCS2LE && st->from != CS_UCS2BE) {
            if (n - consumed < 4) return DEC_SHORT;
            uint32_t low = big ? (uint32_t)(q[2] << 8 | q[3]) : (uint32_t)(q[3] << 8 | q[2]);
            if (low < 0xDC00 || low > 0xDFFF) return DEC_ILSEQ;
            *cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
            *used = consumed + 4; return 0;
        }
        if (unit >= 0xD800 && unit <= 0xDFFF) return DEC_ILSEQ;
        *cp = unit; *used = consumed + 2; return 0;
    }
    case CS_UTF32LE: case CS_UTF32BE: case CS_UTF32: {
        int big = (int)cs_endian_big(st->from);
        if (n < 4) return DEC_SHORT;
        uint32_t value = big ? ((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3])
                             : ((uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]);
        if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return DEC_ILSEQ;
        *cp = value; *used = 4; return 0;
    }
    }
    return DEC_ILSEQ;
}

static int encode_length(struct iconv_state *st, uint32_t cp)
{
    switch (st->to) {
    case CS_UTF8: return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    case CS_ASCII: return cp < 0x80 ? 1 : -1;
    case CS_LATIN1: return cp < 0x100 ? 1 : -1;
    case CS_LATIN9: case CS_CP1252: return 1;
    case CS_UTF16LE: case CS_UTF16BE: case CS_UTF16: return cp >= 0x10000 ? 4 : 2;
    case CS_UCS2LE: case CS_UCS2BE: return cp >= 0x10000 ? -1 : 2;
    default: return 4;
    }
}

static int encode(struct iconv_state *st, uint32_t cp, unsigned char *o)
{
    switch (st->to) {
    case CS_UTF8:
        if (cp < 0x80) { o[0] = (unsigned char)cp; return 1; }
        if (cp < 0x800) { o[0] = 0xC0 | (cp >> 6); o[1] = 0x80 | (cp & 0x3F); return 2; }
        if (cp < 0x10000) { o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 0x3F); o[2] = 0x80 | (cp & 0x3F); return 3; }
        o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 0x3F); o[2] = 0x80 | ((cp >> 6) & 0x3F); o[3] = 0x80 | (cp & 0x3F);
        return 4;
    case CS_ASCII:
        if (cp >= 0x80) return -1;
        o[0] = (unsigned char)cp; return 1;
    case CS_LATIN1:
        if (cp >= 0x100) return -1;
        o[0] = (unsigned char)cp; return 1;
    case CS_LATIN9:
        switch (cp) {
        case 0x20AC: o[0] = 0xA4; return 1;
        case 0x160: o[0] = 0xA6; return 1;
        case 0x161: o[0] = 0xA8; return 1;
        case 0x17D: o[0] = 0xB4; return 1;
        case 0x17E: o[0] = 0xB8; return 1;
        case 0x152: o[0] = 0xBC; return 1;
        case 0x153: o[0] = 0xBD; return 1;
        case 0x178: o[0] = 0xBE; return 1;
        case 0xA4: case 0xA6: case 0xA8: case 0xB4: case 0xB8: case 0xBC: case 0xBD: case 0xBE: return -1;
        }
        if (cp >= 0x100) return -1;
        o[0] = (unsigned char)cp; return 1;
    case CS_CP1252:
        if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) { o[0] = (unsigned char)cp; return 1; }
        for (int i = 0; i < 32; i++)
            if (cp1252_high[i] == cp) { o[0] = (unsigned char)(0x80 + i); return 1; }
        return -1;
    case CS_UTF16LE: case CS_UTF16BE: case CS_UTF16: case CS_UCS2LE: case CS_UCS2BE: {
        int big = (int)cs_endian_big(st->to);
        uint32_t u[2]; int count;
        if (cp >= 0x10000) {
            if (st->to == CS_UCS2LE || st->to == CS_UCS2BE) return -1;
            cp -= 0x10000; u[0] = 0xD800 + (cp >> 10); u[1] = 0xDC00 + (cp & 0x3FF); count = 2;
        } else { u[0] = cp; count = 1; }
        for (int i = 0; i < count; i++) {
            o[i * 2] = big ? (u[i] >> 8) : (u[i] & 0xFF);
            o[i * 2 + 1] = big ? (u[i] & 0xFF) : (u[i] >> 8);
        }
        return count * 2;
    }
    default: {
        int big = (int)cs_endian_big(st->to);
        o[big ? 0 : 3] = cp >> 24; o[big ? 1 : 2] = (cp >> 16) & 0xFF;
        o[big ? 2 : 1] = (cp >> 8) & 0xFF; o[big ? 3 : 0] = cp & 0xFF;
        return 4;
    }
    }
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft)
{
    struct iconv_state *st = cd;
    if (!cd || cd == (iconv_t)-1) { errno = EBADF; return (size_t)-1; }
    if (!inbuf || !*inbuf) {
        st->from_bom_seen = 0;
        st->to_bom_written = 0;
        return 0;
    }
    size_t irreversible = 0;
    while (*inbytesleft > 0) {
        uint32_t cp = 0;
        size_t used = 0;
        int status = decode(st, (const unsigned char *)*inbuf, *inbytesleft, &cp, &used);
        if (status == DEC_SHORT) { errno = EINVAL; return (size_t)-1; }
        if (status == DEC_ILSEQ) {
            if (st->ignore) { (*inbuf)++; (*inbytesleft)--; continue; }
            errno = EILSEQ; return (size_t)-1;
        }
        if (status == 1) { *inbuf += used; *inbytesleft -= used; continue; }
        unsigned char tmp[8];
        int length = encode_length(st, cp);
        if (length < 0) {
            if (st->translit) { cp = '?'; irreversible++; length = 1; }
            else if (st->ignore) { *inbuf += used; *inbytesleft -= used; continue; }
            else { errno = EILSEQ; return (size_t)-1; }
        }
        int bom = (st->to == CS_UTF16 || st->to == CS_UTF32) && !st->to_bom_written;
        size_t bom_len = bom ? (st->to == CS_UTF16 ? 2 : 4) : 0;
        if (*outbytesleft < (size_t)length + bom_len) { errno = E2BIG; return (size_t)-1; }
        if (bom) {
            unsigned char *o = (unsigned char *)*outbuf;
            if (st->to == CS_UTF16) { o[0] = 0xFE; o[1] = 0xFF; }
            else { o[0] = 0; o[1] = 0; o[2] = 0xFE; o[3] = 0xFF; }
            *outbuf += bom_len; *outbytesleft -= bom_len;
            st->to_bom_written = 1;
        }
        encode(st, cp, tmp);
        memcpy(*outbuf, tmp, (size_t)length);
        *outbuf += length; *outbytesleft -= (size_t)length;
        *inbuf += used; *inbytesleft -= used;
    }
    return irreversible;
}
