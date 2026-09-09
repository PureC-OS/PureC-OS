#pragma once
// PureC hosted libc: unistd (fd-level IO over the FILE layer, no kernel
// fd writes exist). fds 0/1/2 are the console; 3.. map onto FILE* slots.

#include <stddef.h>
#include <stdint.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

typedef long off_t;
typedef long ssize_t;

ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int origin);
int unlink(const char *path);
int access(const char *path, int mode);
int isatty(int fd);
unsigned int sleep(unsigned int seconds);
char *getcwd(char *buffer, size_t capacity);

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
