#include "json.h"
#include <stdlib.h>
#include <string.h>

const char *json_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        p++;
    return p;
}

static const char *skip_string(const char *p)
{
    if (*p != '"')
        return NULL;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p)
                return NULL;
        }
        p++;
    }
    return *p == '"' ? p + 1 : NULL;
}

const char *json_skip(const char *p)
{
    p = json_ws(p);
    if (!p || !*p)
        return NULL;
    if (*p == '"')
        return skip_string(p);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        p = json_ws(p + 1);
        if (p && *p == close)
            return p + 1;
        for (;;) {
            if (close == '}') {
                p = skip_string(json_ws(p));
                if (!p)
                    return NULL;
                p = json_ws(p);
                if (*p != ':')
                    return NULL;
                p++;
            }
            p = json_ws(json_skip(p));
            if (!p)
                return NULL;
            if (*p == ',') {
                p++;
                continue;
            }
            return *p == close ? p + 1 : NULL;
        }
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t')
        p++;
    return p;
}

int json_string(const char *p, char *out, size_t cap)
{
    p = json_ws(p);
    if (!p || *p != '"' || !cap)
        return -1;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            c = *p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                unsigned v = 0;
                for (int i = 0; i < 4 && *p; i++, p++) {
                    char h = *p;
                    v = v * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
                }
                c = v < 128 ? (char)v : '?';
                break;
            }
            case '\0': return -1;
            default: break;
            }
        }
        if (n + 1 < cap)
            out[n++] = c;
    }
    out[n] = '\0';
    return *p == '"' ? 0 : -1;
}

long long json_number(const char *p)
{
    p = json_ws(p);
    return p ? strtoll(p, NULL, 10) : 0;
}

int json_object_next(const char **cursor, char *key, size_t key_cap, const char **value)
{
    const char *p = json_ws(*cursor);
    if (!p)
        return -1;
    if (*p == '{' || *p == ',')
        p = json_ws(p + 1);
    if (*p == '}')
        return 0;
    if (json_string(p, key, key_cap) < 0)
        return -1;
    p = json_ws(skip_string(p));
    if (!p || *p != ':')
        return -1;
    p = json_ws(p + 1);
    *value = p;
    p = json_skip(p);
    if (!p)
        return -1;
    *cursor = json_ws(p);
    return 1;
}

int json_array_next(const char **cursor, const char **value)
{
    const char *p = json_ws(*cursor);
    if (!p)
        return -1;
    if (*p == '[' || *p == ',')
        p = json_ws(p + 1);
    if (*p == ']')
        return 0;
    *value = p;
    p = json_skip(p);
    if (!p)
        return -1;
    *cursor = json_ws(p);
    return 1;
}

const char *json_get(const char *object, const char *name)
{
    const char *cursor = json_ws(object);
    if (!cursor || *cursor != '{')
        return NULL;
    char key[64];
    const char *value;
    while (json_object_next(&cursor, key, sizeof(key), &value) == 1)
        if (!strcmp(key, name))
            return value;
    return NULL;
}
