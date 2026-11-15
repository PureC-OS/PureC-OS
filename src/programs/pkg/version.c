#include "pkg.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int compare_segments(const char *a, const char *b)
{
    if (!strcmp(a, b))
        return 0;
    const char *one = a;
    const char *two = b;
    const char *prev1 = a;
    const char *prev2 = b;
    while (*one && *two) {
        while (*one && !isalnum((unsigned char)*one))
            one++;
        while (*two && !isalnum((unsigned char)*two))
            two++;
        if (!*one || !*two)
            break;
        if ((one - prev1) != (two - prev2))
            return (one - prev1) < (two - prev2) ? -1 : 1;
        const char *s1 = one;
        const char *s2 = two;
        int numeric;
        if (isdigit((unsigned char)*s1)) {
            while (isdigit((unsigned char)*s1))
                s1++;
            while (isdigit((unsigned char)*s2))
                s2++;
            numeric = 1;
        } else {
            while (isalpha((unsigned char)*s1))
                s1++;
            while (isalpha((unsigned char)*s2))
                s2++;
            numeric = 0;
        }
        if (s2 == two)
            return numeric ? 1 : -1;
        size_t n1 = (size_t)(s1 - one);
        size_t n2 = (size_t)(s2 - two);
        if (numeric) {
            while (n1 && *one == '0') {
                one++;
                n1--;
            }
            while (n2 && *two == '0') {
                two++;
                n2--;
            }
            if (n1 != n2)
                return n1 > n2 ? 1 : -1;
        }
        size_t common = n1 < n2 ? n1 : n2;
        int c = strncmp(one, two, common);
        if (c)
            return c > 0 ? 1 : -1;
        if (n1 != n2)
            return n1 > n2 ? 1 : -1;
        one = s1;
        two = s2;
        prev1 = one;
        prev2 = two;
    }
    if (!*one && !*two)
        return 0;
    if ((!*one && !isalpha((unsigned char)*two)) || isalpha((unsigned char)*one))
        return -1;
    return 1;
}

static void split_version(const char *text, long *epoch, char *ver, size_t ver_cap, char *rel, size_t rel_cap)
{
    const char *p = text;
    *epoch = 0;
    const char *colon = strchr(text, ':');
    if (colon) {
        const char *q = text;
        while (q < colon && isdigit((unsigned char)*q))
            q++;
        if (q == colon && colon != text) {
            *epoch = strtol(text, NULL, 10);
            p = colon + 1;
        }
    }
    const char *dash = strrchr(p, '-');
    size_t vlen = dash ? (size_t)(dash - p) : strlen(p);
    if (vlen >= ver_cap)
        vlen = ver_cap - 1;
    memcpy(ver, p, vlen);
    ver[vlen] = '\0';
    if (dash) {
        strncpy(rel, dash + 1, rel_cap - 1);
        rel[rel_cap - 1] = '\0';
    } else {
        rel[0] = '\0';
    }
}

int pkg_vercmp(const char *a, const char *b)
{
    if (!strcmp(a, b))
        return 0;
    long ea, eb;
    char va[PKG_VERSION_MAX], vb[PKG_VERSION_MAX], ra[PKG_VERSION_MAX], rb[PKG_VERSION_MAX];
    split_version(a, &ea, va, sizeof(va), ra, sizeof(ra));
    split_version(b, &eb, vb, sizeof(vb), rb, sizeof(rb));
    if (ea != eb)
        return ea > eb ? 1 : -1;
    int c = compare_segments(va, vb);
    if (c)
        return c;
    if (ra[0] && rb[0])
        return compare_segments(ra, rb);
    return 0;
}
