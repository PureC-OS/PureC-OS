#include "pkg.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

static void set_text(char *out, size_t cap, const char *value)
{
    strncpy(out, value, cap - 1);
    out[cap - 1] = '\0';
}

int pkg_info_parse(const char *text, struct pkg_info *out)
{
    memset(out, 0, sizeof(*out));
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[512];
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = eol ? eol + 1 : p + len;
        char *s = trim(line);
        if (!*s || *s == '#')
            continue;
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = trim(s);
        char *value = trim(eq + 1);
        if (!strcmp(key, "pkgname"))
            set_text(out->name, sizeof(out->name), value);
        else if (!strcmp(key, "pkgver"))
            set_text(out->version, sizeof(out->version), value);
        else if (!strcmp(key, "pkgdesc"))
            set_text(out->desc, sizeof(out->desc), value);
        else if (!strcmp(key, "arch"))
            set_text(out->arch, sizeof(out->arch), value);
        else if (!strcmp(key, "size"))
            out->size = strtoull(value, NULL, 10);
        else if (!strcmp(key, "depend") && *value && out->depend_count < PKG_DEPENDS_MAX)
            set_text(out->depends[out->depend_count++], PKG_DEP_MAX, value);
    }
    return out->name[0] && out->version[0] ? 0 : -1;
}

int pkg_dep_split(const char *dep, char *name, size_t name_cap, char *op, size_t op_cap, char *version,
                  size_t version_cap)
{
    size_t n = 0;
    while (dep[n] && dep[n] != '<' && dep[n] != '>' && dep[n] != '=')
        n++;
    if (n == 0 || n >= name_cap)
        return -1;
    memcpy(name, dep, n);
    name[n] = '\0';
    const char *rest = dep + n;
    size_t o = 0;
    while (rest[o] == '<' || rest[o] == '>' || rest[o] == '=')
        o++;
    if (o >= op_cap || strlen(rest + o) >= version_cap)
        return -1;
    memcpy(op, rest, o);
    op[o] = '\0';
    strcpy(version, rest + o);
    if (o && !version[0])
        return -1;
    return 0;
}

int pkg_dep_satisfied(const char *op, const char *wanted, const char *have)
{
    if (!op[0])
        return 1;
    int c = pkg_vercmp(have, wanted);
    if (!strcmp(op, "="))
        return c == 0;
    if (!strcmp(op, ">="))
        return c >= 0;
    if (!strcmp(op, "<="))
        return c <= 0;
    if (!strcmp(op, ">"))
        return c > 0;
    if (!strcmp(op, "<"))
        return c < 0;
    return 0;
}
