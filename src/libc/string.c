// PureC hosted libc: ANSI string + memory primitives.
// Self-contained, no syscalls; used by every ported program (TCC first).

#include "include/hosted/string.h"
#include "include/hosted/errno.h"
#include <stdbool.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *d = (uint8_t *)destination;
    const uint8_t *s = (const uint8_t *)source;
    for (size_t i = 0; i < count; i++) d[i] = s[i];
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    uint8_t *d = (uint8_t *)destination;
    const uint8_t *s = (const uint8_t *)source;
    if (d < s) {
        for (size_t i = 0; i < count; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = count; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    uint8_t *d = (uint8_t *)destination;
    for (size_t i = 0; i < count; i++) d[i] = (uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const uint8_t *l = (const uint8_t *)left;
    const uint8_t *r = (const uint8_t *)right;
    for (size_t i = 0; i < count; i++) {
        if (l[i] != r[i]) return l[i] < r[i] ? -1 : 1;
    }
    return 0;
}

void *memchr(const void *buffer, int value, size_t count) {
    const uint8_t *b = (const uint8_t *)buffer;
    for (size_t i = 0; i < count; i++) {
        if (b[i] == (uint8_t)value) return (void *)(b + i);
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) length++;
    return length;
}

size_t strnlen(const char *text, size_t capacity) {
    size_t length = 0;
    while (length < capacity && text[length]) length++;
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) { left++; right++; }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t count) {
    for (size_t i = 0; i < count; i++) {
        unsigned char l = (unsigned char)left[i];
        unsigned char r = (unsigned char)right[i];
        if (l != r || !l) return l - r;
    }
    return 0;
}

char *strcpy(char *destination, const char *source) {
    char *d = destination;
    while ((*d++ = *source++)) ;
    return destination;
}

char *strncpy(char *destination, const char *source, size_t count) {
    size_t i = 0;
    for (; i < count && source[i]; i++) destination[i] = source[i];
    for (; i < count; i++) destination[i] = '\0';
    return destination;
}

char *strcat(char *destination, const char *source) {
    strcpy(destination + strlen(destination), source);
    return destination;
}

char *strncat(char *destination, const char *source, size_t count) {
    size_t base = strlen(destination);
    size_t i = 0;
    for (; i < count && source[i]; i++) destination[base + i] = source[i];
    destination[base + i] = '\0';
    return destination;
}

char *strchr(const char *text, int value) {
    for (; *text; text++) {
        if (*text == (char)value) return (char *)text;
    }
    return value == 0 ? (char *)text : 0;
}

char *strrchr(const char *text, int value) {
    const char *found = 0;
    for (; *text; text++) {
        if (*text == (char)value) found = text;
    }
    if (value == 0) return (char *)text;
    return (char *)found;
}

char *strstr(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (strncmp(haystack, needle, nlen) == 0) return (char *)haystack;
    }
    return 0;
}

size_t strspn(const char *text, const char *accept) {
    size_t count = 0;
    for (; text[count]; count++) {
        bool ok = false;
        for (const char *a = accept; *a; a++) {
            if (text[count] == *a) { ok = true; break; }
        }
        if (!ok) break;
    }
    return count;
}

size_t strcspn(const char *text, const char *reject) {
    size_t count = 0;
    for (; text[count]; count++) {
        for (const char *r = reject; *r; r++) {
            if (text[count] == *r) return count;
        }
    }
    return count;
}

char *strpbrk(const char *text, const char *accept) {
    for (; *text; text++) {
        for (const char *a = accept; *a; a++) {
            if (*text == *a) return (char *)text;
        }
    }
    return 0;
}

char *strtok(char *text, const char *delimiters) {
    static char *saved;
    if (text) saved = text;
    if (!saved) return 0;
    saved += strspn(saved, delimiters);
    if (!*saved) { saved = 0; return 0; }
    char *token = saved;
    saved += strcspn(saved, delimiters);
    if (*saved) *saved++ = '\0';
    else saved = 0;
    return token;
}

// strdup/strndup need malloc;Allocated here to keep string.c
// syscall-free, the allocator lives in stdlib.c.
extern void *malloc(size_t size);

char *strdup(const char *text) {
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

char *strndup(const char *text, size_t count) {
    size_t length = strnlen(text, count);
    char *copy = (char *)malloc(length + 1);
    if (!copy) return 0;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

char *strerror(int code) {
    switch (code) {
        case 0: return "Success";
        case EPERM: return "Operation not permitted";
        case ENOENT: return "No such file or directory";
        case EINTR: return "Interrupted system call";
        case EIO: return "I/O error";
        case E2BIG: return "Argument list too long";
        case ENOEXEC: return "Exec format error";
        case EBADF: return "Bad file descriptor";
        case EAGAIN: return "Resource temporarily unavailable";
        case ENOMEM: return "Out of memory";
        case EACCES: return "Permission denied";
        case EFAULT: return "Bad address";
        case EBUSY: return "Device or resource busy";
        case EEXIST: return "File exists";
        case ENOTDIR: return "Not a directory";
        case EISDIR: return "Is a directory";
        case EINVAL: return "Invalid argument";
        case EMFILE: return "Too many open files";
        case ENOSPC: return "No space left on device";
        case ESPIPE: return "Illegal seek";
        case EROFS: return "Read-only file system";
        case EPIPE: return "Broken pipe";
        case EDOM: return "Math argument out of domain";
        case ERANGE: return "Math result not representable";
        case ENOSYS: return "Function not implemented";
        case ENOTEMPTY: return "Directory not empty";
        default: return "Unknown error";
    }
}
