#pragma once
// PureC hosted libc: string + memory, phase 1 of the TCC migration
// foundation. Implemented in src/libc/string.c, no syscalls needed.

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
void *memchr(const void *buffer, int value, size_t count);

size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t count);
char *strcat(char *destination, const char *source);
char *strncat(char *destination, const char *source, size_t count);
char *strchr(const char *text, int value);
char *strrchr(const char *text, int value);
char *strstr(const char *haystack, const char *needle);
char *strdup(const char *text);
char *strndup(const char *text, size_t count);
char *strerror(int code);
char *strtok(char *text, const char *delimiters);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
char *strpbrk(const char *text, const char *accept);
size_t strnlen(const char *text, size_t capacity);
