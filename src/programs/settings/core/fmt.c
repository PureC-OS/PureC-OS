#include "settings/fmt.h"

void fmt_init(struct fmt *f, char *buf, int cap)
{
    f->buf = buf;
    f->cap = cap;
    f->len = 0;
    if (cap > 0)
        buf[0] = 0;
}

struct fmt *fc(struct fmt *f, char c)
{
    if (f->len < f->cap - 1) {
        f->buf[f->len++] = c;
        f->buf[f->len] = 0;
    }
    return f;
}

struct fmt *fs(struct fmt *f, const char *s)
{
    while (s && *s)
        fc(f, *s++);
    return f;
}

struct fmt *fu(struct fmt *f, uint64_t v)
{
    char rev[24];
    int n = 0;
    do {
        rev[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < (int)sizeof(rev));
    while (n)
        fc(f, rev[--n]);
    return f;
}

struct fmt *fi(struct fmt *f, int64_t v)
{
    if (v < 0) {
        fc(f, '-');
        return fu(f, (uint64_t)(-v));
    }
    return fu(f, (uint64_t)v);
}

struct fmt *fu2(struct fmt *f, uint32_t v)
{
    fc(f, (char)('0' + (v / 10) % 10));
    return fc(f, (char)('0' + v % 10));
}

struct fmt *fip(struct fmt *f, uint32_t ip)
{
    fu(f, (ip >> 24) & 255);
    fc(f, '.');
    fu(f, (ip >> 16) & 255);
    fc(f, '.');
    fu(f, (ip >> 8) & 255);
    fc(f, '.');
    return fu(f, ip & 255);
}

struct fmt *fmac(struct fmt *f, const uint8_t *mac)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        if (i)
            fc(f, ':');
        fc(f, hex[mac[i] >> 4]);
        fc(f, hex[mac[i] & 15]);
    }
    return f;
}

struct fmt *fdec(struct fmt *f, uint64_t value, uint64_t div, int digits)
{
    fu(f, value / div);
    if (digits > 0) {
        fc(f, '.');
        uint64_t rem = value % div;
        for (int i = 0; i < digits; i++) {
            rem *= 10;
            fc(f, (char)('0' + rem / div));
            rem %= div;
        }
    }
    return f;
}

struct fmt *fbytes(struct fmt *f, uint64_t bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    uint64_t div = 1;
    while (u < 4 && bytes >= div * 1024) {
        div *= 1024;
        u++;
    }
    if (u == 0)
        fu(f, bytes);
    else
        fdec(f, bytes, div, bytes / div >= 100 ? 0 : 1);
    fc(f, ' ');
    return fs(f, units[u]);
}

struct fmt *fdur(struct fmt *f, uint64_t ms)
{
    uint64_t s = ms / 1000;
    uint64_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
    if (d) {
        fu(f, d);
        fs(f, "d ");
    }
    if (d || h) {
        fu(f, h);
        fs(f, "h ");
    }
    fu(f, m);
    fs(f, "m");
    if (!d && !h) {
        fc(f, ' ');
        fu(f, s % 60);
        fc(f, 's');
    }
    return f;
}

int fmt_streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void fmt_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    for (; src && src[i] && i < cap - 1; i++)
        dst[i] = src[i];
    if (cap > 0)
        dst[i] = 0;
}

void civil_from_unix(int64_t t, int *year, int *month, int *day, int *hour, int *min, int *sec,
                     int *wday)
{
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    *hour = (int)(rem / 3600);
    *min = (int)(rem / 60 % 60);
    *sec = (int)(rem % 60);
    *wday = (int)((days % 7 + 11) % 7);
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *year = (int)(y + (*month <= 2));
}
