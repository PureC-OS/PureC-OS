#include "pkgd.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "sha512.h"

struct options opt;

void die(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void warn(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "warning: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void msg(const char *fmt, ...)
{
    va_list ap;
    printf(":: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

void note(const char *fmt, ...)
{
    va_list ap;
    if (opt.quiet)
        return;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

const char *rp(const char *relative)
{
    static char ring[4][PKG_PATH_MAX + 64];
    static int next;
    char *out = ring[next++ & 3];
    while (*relative == '/')
        relative++;
    size_t root_len = strlen(opt.root);
    if (root_len == 1 && opt.root[0] == '/')
        snprintf(out, PKG_PATH_MAX + 64, "/%s", relative);
    else
        snprintf(out, PKG_PATH_MAX + 64, "%s/%s", opt.root, relative);
    return out;
}

int mkdirs(const char *path, unsigned mode)
{
    char tmp[PKG_PATH_MAX + 64];
    size_t n = strlen(path);
    if (n >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] != '/')
            continue;
        tmp[i] = '\0';
        if (mkdir(tmp, mode) < 0 && errno != EEXIST)
            return -1;
        tmp[i] = '/';
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST)
        return -1;
    return 0;
}

int confirm(const char *question)
{
    if (opt.noconfirm) {
        printf("%s [Y/n] y\n", question);
        return 1;
    }
    printf("%s [Y/n] ", question);
    fflush(stdout);
    char line[32];
    if (!fgets(line, sizeof(line), stdin))
        return 0;
    return line[0] == '\n' || line[0] == 'y' || line[0] == 'Y';
}

void human_size(uint64_t bytes, char *out, size_t cap)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        u++;
    }
    if (u == 0)
        snprintf(out, cap, "%llu B", (unsigned long long)bytes);
    else
        snprintf(out, cap, "%.2f %s", v, units[u]);
}

static void to_hex(const uint8_t *digest, char out[PKG_HASH_HEX])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 64; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[128] = '\0';
}

void hash_bytes(const void *data, size_t length, char out[PKG_HASH_HEX])
{
    uint8_t digest[PURE_SHA512_DIGEST_LEN];
    pure_sha512(data, length, digest);
    to_hex(digest, out);
}

int hash_file(const char *path, char out[PKG_HASH_HEX])
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    pure_sha512_ctx ctx;
    pure_sha512_init(&ctx);
    unsigned char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
        pure_sha512_update(&ctx, buf, got);
    int bad = ferror(f);
    fclose(f);
    if (bad)
        return -1;
    uint8_t digest[PURE_SHA512_DIGEST_LEN];
    pure_sha512_final(&ctx, digest);
    to_hex(digest, out);
    return 0;
}

int run_argv(char *const argv[])
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in)
        return -1;
    char tmp[PKG_PATH_MAX + 80];
    snprintf(tmp, sizeof(tmp), "%s.part", to);
    FILE *out = fopen(tmp, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    char buf[8192];
    size_t got;
    int bad = 0;
    while ((got = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, got, out) != got) {
            bad = 1;
            break;
        }
    if (ferror(in))
        bad = 1;
    fclose(in);
    if (fclose(out) != 0)
        bad = 1;
    if (bad || rename(tmp, to) < 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

char *read_text_file(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got;
    while ((got = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += got;
        if (len + 1 >= cap) {
            cap *= 2;
            char *grown = realloc(buf, cap);
            if (!grown) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = grown;
        }
    }
    fclose(f);
    buf[len] = '\0';
    if (length)
        *length = len;
    return buf;
}

int write_text_file(const char *path, const void *data, size_t length)
{
    char tmp[PKG_PATH_MAX + 80];
    snprintf(tmp, sizeof(tmp), "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    int bad = length && fwrite(data, 1, length, f) != length;
    if (fclose(f) != 0)
        bad = 1;
    if (bad || rename(tmp, path) < 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int path_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

int is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode))
        return unlink(path);
    DIR *d = opendir(path);
    if (!d)
        return -1;
    struct dirent *e;
    int rc = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char child[PKG_PATH_MAX + 300];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        if (remove_tree(child) < 0)
            rc = -1;
    }
    closedir(d);
    if (rmdir(path) < 0)
        rc = -1;
    return rc;
}
