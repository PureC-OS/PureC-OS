#pragma once
#include "pkg.h"
#include <stdarg.h>

struct options {
    char root[PKG_PATH_MAX];
    const char *config;
    int noconfirm;
    int needed;
    int force;
    int nodeps;
    int asdeps;
    int asexplicit;
    int verbose;
    int quiet;
};

extern struct options opt;

void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

const char *rp(const char *relative);
int mkdirs(const char *path, unsigned mode);
int confirm(const char *question);
void human_size(uint64_t bytes, char *out, size_t cap);
int hash_file(const char *path, char out[PKG_HASH_HEX]);
void hash_bytes(const void *data, size_t length, char out[PKG_HASH_HEX]);
int run_argv(char *const argv[]);
int copy_file(const char *from, const char *to);
char *read_text_file(const char *path, size_t *length);
int write_text_file(const char *path, const void *data, size_t length);
int path_exists(const char *path);
int is_directory(const char *path);
int remove_tree(const char *path);

struct localdb {
    struct pkg_info *pkgs;
    int count;
    int cap;
};

extern struct localdb ldb;

void localdb_load(void);
int localdb_find(const char *name);
char **localdb_files(const struct pkg_info *p, int *count);
void free_list(char **list, int count);
int localdb_write(const struct pkg_info *p, char **files, int count);
void localdb_remove(const struct pkg_info *p);
const char *localdb_owner(const char *path);
void owners_invalidate(void);
const char *localdb_dir(const struct pkg_info *p, char *out, size_t cap);

struct sync_meta {
    char repo[96];
    char tag[64];
    char url[PKG_PATH_MAX];
    char sig[PKG_PATH_MAX];
    char file[PKG_PATH_MAX];
    int complete;
    int local;
};

struct syncdb {
    struct pkg_info *pkgs;
    struct sync_meta *meta;
    int count;
    int cap;
};

extern struct syncdb sdb;

void config_load(void);
void syncdb_load(void);
int syncdb_find(const char *name);
int sync_refresh(void);
int fetch_package(int sync_index, char *out_path, size_t cap);
int repo_count(void);
const char *config_path(void);
const char *package_for_url(const char *url, char *why, size_t cap);
void list_sources(void);
int sync_complete(int sync_index);

int tx_install(char **targets, int count, int local_files);
int tx_remove(char **targets, int count, int recursive);
int tx_upgrade(int refresh_only_check);
int list_upgrades(int quiet);

#define SIG_OK 0
#define SIG_BAD (-1)
#define SIG_NOKEYS (-2)

int sig_verify_file(const char *file, const char *signature_file);
