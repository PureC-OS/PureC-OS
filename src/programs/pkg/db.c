#include "pkgd.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct localdb ldb;

struct owner {
    char *path;
    int pkg;
};

static struct owner *owners;
static int owner_count;
static int owners_ready;

static const char *local_root(void)
{
    return rp("var/lib/pkg/local");
}

const char *localdb_dir(const struct pkg_info *p, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s-%s", local_root(), p->name, p->version);
    return out;
}

void owners_invalidate(void)
{
    for (int i = 0; i < owner_count; i++)
        free(owners[i].path);
    free(owners);
    owners = NULL;
    owner_count = 0;
    owners_ready = 0;
}

void localdb_load(void)
{
    free(ldb.pkgs);
    ldb.pkgs = NULL;
    ldb.count = ldb.cap = 0;
    owners_invalidate();
    DIR *d = opendir(local_root());
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char path[PKG_PATH_MAX + 300];
        snprintf(path, sizeof(path), "%s/%s/desc", local_root(), e->d_name);
        char *text = read_text_file(path, NULL);
        if (!text)
            continue;
        struct pkg_info info;
        int rc = pkg_info_parse(text, &info);
        free(text);
        if (rc < 0)
            continue;
        if (ldb.count == ldb.cap) {
            int cap = ldb.cap ? ldb.cap * 2 : 64;
            struct pkg_info *grown = realloc(ldb.pkgs, (size_t)cap * sizeof(*grown));
            if (!grown)
                die("out of memory");
            ldb.pkgs = grown;
            ldb.cap = cap;
        }
        ldb.pkgs[ldb.count++] = info;
    }
    closedir(d);
}

int localdb_find(const char *name)
{
    for (int i = 0; i < ldb.count; i++)
        if (!strcmp(ldb.pkgs[i].name, name))
            return i;
    return -1;
}

void free_list(char **list, int count)
{
    for (int i = 0; i < count; i++)
        free(list[i]);
    free(list);
}

char **localdb_files(const struct pkg_info *p, int *count)
{
    char dir[PKG_PATH_MAX + 128], path[PKG_PATH_MAX + 160];
    localdb_dir(p, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/files", dir);
    *count = 0;
    char *text = read_text_file(path, NULL);
    if (!text)
        return NULL;
    int cap = 64, n = 0;
    char **list = malloc((size_t)cap * sizeof(char *));
    char *line = text;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol)
            *eol = '\0';
        if (*line) {
            if (n == cap) {
                cap *= 2;
                list = realloc(list, (size_t)cap * sizeof(char *));
            }
            list[n++] = strdup(line);
        }
        if (!eol)
            break;
        line = eol + 1;
    }
    free(text);
    *count = n;
    return list;
}

int localdb_write(const struct pkg_info *p, char **files, int count)
{
    char dir[PKG_PATH_MAX + 128], path[PKG_PATH_MAX + 160];
    localdb_dir(p, dir, sizeof(dir));
    if (mkdirs(dir, 0755) < 0)
        return -1;
    static char desc[32768];
    if (pkg_info_format(p, desc, sizeof(desc)) < 0)
        return -1;
    snprintf(path, sizeof(path), "%s/desc", dir);
    if (write_text_file(path, desc, strlen(desc)) < 0)
        return -1;
    size_t total = 0;
    for (int i = 0; i < count; i++)
        total += strlen(files[i]) + 1;
    char *list = malloc(total + 1);
    if (!list)
        return -1;
    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        size_t n = strlen(files[i]);
        memcpy(list + pos, files[i], n);
        pos += n;
        list[pos++] = '\n';
    }
    snprintf(path, sizeof(path), "%s/files", dir);
    int rc = write_text_file(path, list, pos);
    free(list);
    return rc;
}

void localdb_remove(const struct pkg_info *p)
{
    char dir[PKG_PATH_MAX + 128];
    localdb_dir(p, dir, sizeof(dir));
    remove_tree(dir);
}

static int owner_compare(const void *a, const void *b)
{
    return strcmp(((const struct owner *)a)->path, ((const struct owner *)b)->path);
}

static void owners_build(void)
{
    owners_invalidate();
    int cap = 1024;
    owners = malloc((size_t)cap * sizeof(*owners));
    for (int i = 0; i < ldb.count; i++) {
        int count;
        char **files = localdb_files(&ldb.pkgs[i], &count);
        for (int j = 0; j < count; j++) {
            size_t n = strlen(files[j]);
            if (n && files[j][n - 1] == '/')
                continue;
            if (owner_count == cap) {
                cap *= 2;
                owners = realloc(owners, (size_t)cap * sizeof(*owners));
            }
            owners[owner_count].path = files[j];
            owners[owner_count].pkg = i;
            owner_count++;
            files[j] = NULL;
        }
        for (int j = 0; j < count; j++)
            free(files[j]);
        free(files);
    }
    qsort(owners, (size_t)owner_count, sizeof(*owners), owner_compare);
    owners_ready = 1;
}

const char *localdb_owner(const char *path)
{
    if (!owners_ready)
        owners_build();
    struct owner key = {(char *)path, 0};
    struct owner *hit = bsearch(&key, owners, (size_t)owner_count, sizeof(*owners), owner_compare);
    return hit ? ldb.pkgs[hit->pkg].name : NULL;
}
