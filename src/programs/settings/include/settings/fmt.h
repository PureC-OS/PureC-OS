#pragma once
#include <stdint.h>

struct fmt {
    char *buf;
    int cap;
    int len;
};

void fmt_init(struct fmt *f, char *buf, int cap);
struct fmt *fs(struct fmt *f, const char *s);
struct fmt *fu(struct fmt *f, uint64_t v);
struct fmt *fi(struct fmt *f, int64_t v);
struct fmt *fu2(struct fmt *f, uint32_t v);
struct fmt *fc(struct fmt *f, char c);
struct fmt *fip(struct fmt *f, uint32_t ip);
struct fmt *fmac(struct fmt *f, const uint8_t *mac);
struct fmt *fbytes(struct fmt *f, uint64_t bytes);
struct fmt *fdur(struct fmt *f, uint64_t ms);
struct fmt *fdec(struct fmt *f, uint64_t value, uint64_t div, int digits);
int fmt_streq(const char *a, const char *b);
void fmt_copy(char *dst, const char *src, int cap);
void civil_from_unix(int64_t t, int *year, int *month, int *day, int *hour, int *min, int *sec,
                     int *wday);
