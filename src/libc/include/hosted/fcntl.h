#pragma once
// PureC hosted libc: fcntl (TCC needs open() with O_* for objects).
// O_* numbers follow Linux; O_BINARY is 0 (tcc.h defaults it anyway).

#define O_ACCMODE 0003
#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_BINARY 0
#define O_NONBLOCK 04000
#define O_CLOEXEC 02000000

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD_CLOEXEC 1030
#define FD_CLOEXEC 1

int open(const char *path, int flags, ...);
int creat(const char *path, int mode);
int fcntl(int fd, int command, ...);
