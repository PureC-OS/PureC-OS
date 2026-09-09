#pragma once
// PureC hosted libc: stdlib, phase 1 of the TCC migration foundation.
// Heap is a free-list allocator over pc_heap_grow arenas (see
// src/libc/stdlib.c); there is no sbrk shrink, freed blocks coalesce.

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);

int atoi(const char *text);
long atol(const char *text);
long long atoll(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
// Available: userspace may use SSE (kernel switches FPU per thread).
double strtod(const char *text, char **end);
int abs(int value);
long labs(long value);
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
int rand(void);
void srand(unsigned int seed);
char *getenv(const char *name);
void exit(int status);
void abort(void);
