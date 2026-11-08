#pragma once
// PureC hosted libc: unistd (fd-level IO over the FILE layer, no kernel
// fd writes exist). fds 0/1/2 are the console; 3.. map onto FILE* slots.

#include <stddef.h>
#include <stdint.h>
#include "sys/types.h"

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define _POSIX_VERSION 200809L
#define _POSIX_THREADS 200809L
#define _POSIX_TIMERS 200809L
#define _POSIX_MONOTONIC_CLOCK 200809L
#define _POSIX_SEMAPHORES 200809L

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#ifdef __cplusplus
extern "C" {
#endif
typedef long off_t;
typedef long ssize_t;

extern char **environ;

ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
ssize_t pread(int fd, void *buffer, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset);
int close(int fd);
off_t lseek(int fd, off_t offset, int origin);
int unlink(const char *path);
int unlinkat(int dirfd, const char *path, int flags);
int rmdir(const char *path);
int access(const char *path, int mode);
int isatty(int fd);
unsigned int sleep(unsigned int seconds);
char *getcwd(char *buffer, size_t capacity);
#define _CS_PATH 0
size_t confstr(int name, char *buffer, size_t length);
char *getlogin(void);
pid_t fork(void);
pid_t vfork(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setpgid(pid_t pid, pid_t group);
pid_t getpgrp(void);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t group);
pid_t getpid(void);
pid_t getppid(void);
int dup(int fd);
int ftruncate(int fd, off_t length);
int truncate(const char *path, off_t length);
int fsync(int fd);
int fdatasync(int fd);
int gethostname(char *name, size_t length);
long sysconf(int name);
int usleep(unsigned int microseconds);
ssize_t readlink(const char *path, char *buffer, size_t size);
int symlink(const char *target, const char *path);
int chown(const char *path, uid_t owner, gid_t group);
char *ttyname(int fd);
#define _SC_CLK_TCK 2
#define _SC_OPEN_MAX 4
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE 30
#define _SC_NPROCESSORS_CONF 83
#define _SC_NPROCESSORS_ONLN 84

int dup2(int old_fd, int new_fd);
int pipe(int fds[2]);
int pipe2(int fds[2], int flags);
int chdir(const char *path);
int execve(const char *path, char *const *argv, char *const *envp);
int execv(const char *path, char *const *argv);
int execvp(const char *file, char *const *argv);
int execvpe(const char *file, char *const *argv, char *const *envp);
int execl(const char *path, const char *argument, ...);
int execlp(const char *file, const char *argument, ...);
int pause(void);
unsigned int alarm(unsigned int seconds);

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#ifdef __cplusplus
}
#endif
