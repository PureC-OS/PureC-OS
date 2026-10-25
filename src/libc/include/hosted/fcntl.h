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

int open(const char *path, int flags, ...);
int creat(const char *path, int mode);
