#include "pkg.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define BLOCK 512
#define INBUF 16384

struct pkg_archive {
    FILE *file;
    int gzip;
    int stream_end;
    z_stream zs;
    unsigned char in[INBUF];
    uint64_t remaining;
    uint64_t padding;
    int finished;
};

static long raw_read(struct pkg_archive *a, void *buffer, size_t count)
{
    if (!a->gzip) {
        size_t got = fread(buffer, 1, count, a->file);
        if (got == 0 && ferror(a->file))
            return -1;
        return (long)got;
    }
    if (a->stream_end)
        return 0;
    a->zs.next_out = buffer;
    a->zs.avail_out = (uInt)count;
    while (a->zs.avail_out > 0) {
        if (a->zs.avail_in == 0) {
            size_t got = fread(a->in, 1, sizeof(a->in), a->file);
            if (got == 0) {
                if (ferror(a->file))
                    return -1;
                break;
            }
            a->zs.next_in = a->in;
            a->zs.avail_in = (uInt)got;
        }
        int rc = inflate(&a->zs, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) {
            a->stream_end = 1;
            break;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR)
            return -1;
    }
    return (long)(count - a->zs.avail_out);
}

static int read_exact(struct pkg_archive *a, void *buffer, size_t count)
{
    unsigned char *p = buffer;
    size_t done = 0;
    while (done < count) {
        long got = raw_read(a, p + done, count - done);
        if (got < 0)
            return -1;
        if (got == 0)
            return done == 0 ? 0 : -1;
        done += (size_t)got;
    }
    return 1;
}

static int skip_bytes(struct pkg_archive *a, uint64_t count)
{
    unsigned char scratch[BLOCK];
    while (count) {
        size_t n = count > sizeof(scratch) ? sizeof(scratch) : (size_t)count;
        int rc = read_exact(a, scratch, n);
        if (rc <= 0)
            return -1;
        count -= n;
    }
    return 0;
}

struct pkg_archive *pkg_archive_open(const char *path)
{
    struct pkg_archive *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->file = fopen(path, "rb");
    if (!a->file) {
        free(a);
        return NULL;
    }
    unsigned char magic[2];
    size_t got = fread(magic, 1, 2, a->file);
    rewind(a->file);
    if (got == 2 && magic[0] == 0x1f && magic[1] == 0x8b) {
        a->gzip = 1;
        if (inflateInit2(&a->zs, 15 + 16) != Z_OK) {
            fclose(a->file);
            free(a);
            return NULL;
        }
    }
    return a;
}

void pkg_archive_close(struct pkg_archive *a)
{
    if (!a)
        return;
    if (a->gzip)
        inflateEnd(&a->zs);
    if (a->file)
        fclose(a->file);
    free(a);
}

static uint64_t parse_number(const unsigned char *field, size_t length)
{
    if (field[0] & 0x80) {
        uint64_t v = field[0] & 0x7F;
        for (size_t i = 1; i < length; i++)
            v = (v << 8) | field[i];
        return v;
    }
    uint64_t v = 0;
    size_t i = 0;
    while (i < length && (field[i] == ' ' || field[i] == '\0'))
        i++;
    for (; i < length && field[i] >= '0' && field[i] <= '7'; i++)
        v = (v << 3) | (uint64_t)(field[i] - '0');
    return v;
}

static int header_checksum_ok(const unsigned char *h)
{
    unsigned long sum = 0;
    for (int i = 0; i < BLOCK; i++)
        sum += (i >= 148 && i < 156) ? ' ' : h[i];
    return sum == parse_number(h + 148, 8);
}

static int all_zero(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (p[i])
            return 0;
    return 1;
}

static void copy_field(char *out, size_t cap, const unsigned char *in, size_t length)
{
    size_t n = 0;
    while (n < length && in[n])
        n++;
    if (n >= cap)
        n = cap - 1;
    memcpy(out, in, n);
    out[n] = '\0';
}

static int path_safe(const char *path)
{
    if (path[0] == '/')
        return 0;
    const char *p = path;
    while (*p) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.')
            return 0;
        if (!end)
            break;
        p = end + 1;
    }
    return 1;
}

static void normalize_path(char *path)
{
    while (path[0] == '.' && path[1] == '/') {
        memmove(path, path + 2, strlen(path + 2) + 1);
    }
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = '\0';
}

static int read_data_text(struct pkg_archive *a, uint64_t size, char *out, size_t cap)
{
    if (size >= cap)
        return -1;
    if (size && read_exact(a, out, (size_t)size) <= 0)
        return -1;
    out[size] = '\0';
    uint64_t pad = (BLOCK - (size % BLOCK)) % BLOCK;
    return skip_bytes(a, pad);
}

static void apply_pax(const char *text, char *path, size_t path_cap, char *link, size_t link_cap, uint64_t *size,
                      int *has_size)
{
    const char *p = text;
    while (*p) {
        char *end;
        unsigned long length = strtoul(p, &end, 10);
        if (end == p || *end != ' ' || length == 0)
            break;
        const char *record = end + 1;
        const char *stop = p + length;
        const char *eq = memchr(record, '=', (size_t)(stop - record));
        if (eq) {
            size_t klen = (size_t)(eq - record);
            size_t vlen = (size_t)(stop - eq - 1);
            if (vlen && eq[vlen] == '\n')
                vlen--;
            if (klen == 4 && !memcmp(record, "path", 4) && vlen < path_cap) {
                memcpy(path, eq + 1, vlen);
                path[vlen] = '\0';
            } else if (klen == 8 && !memcmp(record, "linkpath", 8) && vlen < link_cap) {
                memcpy(link, eq + 1, vlen);
                link[vlen] = '\0';
            } else if (klen == 4 && !memcmp(record, "size", 4)) {
                *size = strtoull(eq + 1, NULL, 10);
                *has_size = 1;
            }
        }
        p = stop;
    }
}

int pkg_archive_next(struct pkg_archive *a, struct pkg_entry *entry)
{
    if (a->finished)
        return 0;
    if (a->remaining + a->padding) {
        if (skip_bytes(a, a->remaining + a->padding) < 0) {
            errno = EIO;
            return -1;
        }
        a->remaining = 0;
        a->padding = 0;
    }
    char long_path[PKG_PATH_MAX] = "";
    char long_link[PKG_PATH_MAX] = "";
    char pax_path[PKG_PATH_MAX] = "";
    char pax_link[PKG_PATH_MAX] = "";
    uint64_t pax_size = 0;
    int pax_has_size = 0;
    for (;;) {
        unsigned char h[BLOCK];
        int rc = read_exact(a, h, BLOCK);
        if (rc == 0) {
            a->finished = 1;
            return 0;
        }
        if (rc < 0) {
            errno = EIO;
            return -1;
        }
        if (all_zero(h, BLOCK)) {
            a->finished = 1;
            return 0;
        }
        if (!header_checksum_ok(h)) {
            errno = EINVAL;
            return -1;
        }
        uint64_t size = parse_number(h + 124, 12);
        char type = (char)h[156];
        if (type == 'L' || type == 'K') {
            char *target = type == 'L' ? long_path : long_link;
            if (read_data_text(a, size, target, PKG_PATH_MAX) < 0) {
                errno = EINVAL;
                return -1;
            }
            continue;
        }
        if (type == 'x') {
            static char pax[8192];
            if (read_data_text(a, size, pax, sizeof(pax)) < 0) {
                errno = EINVAL;
                return -1;
            }
            apply_pax(pax, pax_path, sizeof(pax_path), pax_link, sizeof(pax_link), &pax_size, &pax_has_size);
            continue;
        }
        if (type == 'g') {
            if (skip_bytes(a, size + (BLOCK - (size % BLOCK)) % BLOCK) < 0) {
                errno = EIO;
                return -1;
            }
            continue;
        }
        memset(entry, 0, sizeof(*entry));
        if (pax_path[0]) {
            snprintf(entry->path, sizeof(entry->path), "%s", pax_path);
        } else if (long_path[0]) {
            snprintf(entry->path, sizeof(entry->path), "%s", long_path);
        } else {
            char name[101], prefix[156];
            copy_field(name, sizeof(name), h, 100);
            prefix[0] = '\0';
            if (!memcmp(h + 257, "ustar", 5))
                copy_field(prefix, sizeof(prefix), h + 345, 155);
            if (prefix[0])
                snprintf(entry->path, sizeof(entry->path), "%s/%s", prefix, name);
            else
                snprintf(entry->path, sizeof(entry->path), "%s", name);
        }
        if (pax_link[0])
            snprintf(entry->link, sizeof(entry->link), "%s", pax_link);
        else if (long_link[0])
            snprintf(entry->link, sizeof(entry->link), "%s", long_link);
        else
            copy_field(entry->link, sizeof(entry->link), h + 157, 100);
        normalize_path(entry->path);
        if (!entry->path[0] || !path_safe(entry->path)) {
            errno = EINVAL;
            return -1;
        }
        entry->mode = (uint32_t)parse_number(h + 100, 8) & 07777;
        entry->type = (type == '\0') ? '0' : type;
        entry->size = pax_has_size ? pax_size : size;
        if (entry->type == '0' || entry->type == '7') {
            a->remaining = entry->size;
            a->padding = (BLOCK - (entry->size % BLOCK)) % BLOCK;
        } else {
            a->remaining = 0;
            a->padding = 0;
            if (entry->type != '5' && entry->type != '2' && entry->type != '1')
                entry->size = 0;
        }
        if (entry->type == '7')
            entry->type = '0';
        return 1;
    }
}

long pkg_archive_read(struct pkg_archive *a, void *buffer, size_t count)
{
    if (a->remaining == 0)
        return 0;
    if (count > a->remaining)
        count = (size_t)a->remaining;
    long got = raw_read(a, buffer, count);
    if (got <= 0) {
        errno = EIO;
        return -1;
    }
    a->remaining -= (uint64_t)got;
    return got;
}
