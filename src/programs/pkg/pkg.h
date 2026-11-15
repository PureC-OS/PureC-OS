#pragma once
#include <stddef.h>
#include <stdint.h>

#define PKG_NAME_MAX 64
#define PKG_VERSION_MAX 48
#define PKG_DESC_MAX 256
#define PKG_PATH_MAX 512
#define PKG_DEPENDS_MAX 16
#define PKG_DEP_MAX 96

struct pkg_info {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char desc[PKG_DESC_MAX];
    char arch[16];
    char depends[PKG_DEPENDS_MAX][PKG_DEP_MAX];
    int depend_count;
    uint64_t size;
};

struct pkg_entry {
    char path[PKG_PATH_MAX];
    char link[PKG_PATH_MAX];
    uint64_t size;
    uint32_t mode;
    char type;
};

struct pkg_archive;

int pkg_vercmp(const char *a, const char *b);

struct pkg_archive *pkg_archive_open(const char *path);
int pkg_archive_next(struct pkg_archive *a, struct pkg_entry *entry);
long pkg_archive_read(struct pkg_archive *a, void *buffer, size_t count);
void pkg_archive_close(struct pkg_archive *a);

int pkg_info_parse(const char *text, struct pkg_info *out);
int pkg_dep_split(const char *dep, char *name, size_t name_cap, char *op, size_t op_cap, char *version,
                  size_t version_cap);
int pkg_dep_satisfied(const char *op, const char *wanted, const char *have);
