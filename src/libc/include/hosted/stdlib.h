#pragma once
// PureC hosted libc: stdlib, phase 1 of the TCC migration foundation.
// Heap is a free-list allocator over pc_heap_grow arenas (see
// src/libc/stdlib.c); there is no sbrk shrink, freed blocks coalesce.

#include <stddef.h>

#define EXIT_SUCCESS 0
#define MB_CUR_MAX ((size_t)4)
#define EXIT_FAILURE 1
#define RAND_MAX 32767

#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
void *aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void **memptr, size_t alignment, size_t size);
void *memalign(size_t alignment, size_t size);

int atoi(const char *text);
long atol(const char *text);
long long atoll(const char *text);
double atof(const char *text);
long strtol(const char *text, char **end, int base);
long long strtoll(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);
// Available: userspace may use SSE (kernel switches FPU per thread).
double strtod(const char *text, char **end);
float strtof(const char *text, char **end);
long double strtold(const char *text, char **end);
int abs(int value);
long labs(long value);
void *bsearch(const void *key, const void *base, size_t count, size_t size,
               int (*compare)(const void *, const void *));
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
int rand(void);
void srand(unsigned int seed);
long random(void);
void srandom(unsigned int seed);
int rand_r(unsigned int *seed);
long lrand48(void);
long mrand48(void);
double drand48(void);
void srand48(long seed);
long long llabs(long long value);
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
lldiv_t lldiv(long long numerator, long long denominator);
void qsort_r(void *base, size_t count, size_t size,
             int (*compare)(const void *, const void *, void *), void *context);

char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int clearenv(void);
int mkstemp(char *path);
int getloadavg(double loadavg[], int count);
char *mktemp(char *path);
char *mkdtemp(char *path);
int putenv(char *entry);
int putenv(char *entry);
char *realpath(const char *path, char *resolved);
int system(const char *cmd);
void exit(int status) __attribute__((noreturn));
int atexit(void (*function)(void));
int __cxa_atexit(void (*function)(void *), void *argument, void *dso);
void __cxa_finalize(void *dso);
void _exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int mblen(const char *text, size_t count);
int mbtowc(wchar_t *out, const char *text, size_t count);
int wctomb(char *out, wchar_t value);
#ifdef __cplusplus
}
#endif
