#pragma once
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define UTIL_PATH 1024

#define C_RESET "\x1b[0m"
#define C_BOLD "\x1b[1m"
#define C_DIM "\x1b[2m"
#define C_UNDER "\x1b[4m"
#define C_RED "\x1b[31m"
#define C_GREEN "\x1b[32m"
#define C_YELLOW "\x1b[33m"
#define C_BLUE "\x1b[34m"
#define C_MAGENTA "\x1b[35m"
#define C_CYAN "\x1b[36m"
#define C_GRAY "\x1b[90m"

extern const char *prog;

struct opts {
    int argc;
    char **argv;
    int index;
    int sub;
    bool stop;
    const char *arg;
    const char *name;
    const char *value;
    char **operands;
    int count;
};

void util_init(int argc, char **argv);
int util_err(const char *fmt, ...);
int util_perr(const char *what);
void util_usage(const char *text);

void opts_begin(struct opts *o, int argc, char **argv);
int opts_next(struct opts *o, const char *spec);
bool opts_long(const struct opts *o, const char *name);
int util_bad_option(const struct opts *o, int c);

bool util_color(int fd, const char *when);
int util_term_width(void);
int util_term_height(void);

void util_human(char *out, size_t cap, unsigned long long bytes);
void util_join(char *out, size_t cap, const char *dir, const char *name);
const char *util_base(const char *path);
bool util_is_dir(const char *path);
bool util_exists(const char *path);
void util_mode_string(mode_t mode, char out[11]);
bool util_parse_mode(const char *text, unsigned int *mode, bool is_dir);
const char *util_name_color(const char *name, mode_t mode);
const char *util_user(uid_t uid, char *buf, size_t cap);
const char *util_group(gid_t gid, char *buf, size_t cap);

FILE *util_fopen(const char *path);
void util_fclose(FILE *file);
ssize_t util_getline(char **line, size_t *cap, FILE *file);
int util_copy_stream(FILE *in, FILE *out);
bool util_parse_long(const char *text, long *out);
int util_exec_wait(char **argv);
