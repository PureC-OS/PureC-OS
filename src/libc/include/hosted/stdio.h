#pragma once
// PureC hosted libc: stdio, phase 1 of the TCC migration foundation.
// Model notes (dictated by the kernel ABI, see docs below):
// - There is no fd-based write syscall, only path-based full-file
//   replace (pc_file_write). So every writable FILE preloads the file
//   into a heap buffer and flushes the whole buffer on fflush/fclose.
// - Reads are served from the same buffer; the kernel fd is closed
//   right after preload, keeping fd usage at zero during parsing.
// - stdout/stderr go through raw SYS_WRITE (console); stdin through
//   blocking SYS_GETCHAR. Float formats %f/%F/%e/%E are supported
//   (userspace may use SSE: the kernel switches FPU per thread).

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)
#define BUFSIZ 4096
#define FILENAME_MAX 256
#define FOPEN_MAX 16
#define TMP_MAX 10000
#define L_tmpnam 64

typedef struct file_impl FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
char *fgets(char *buffer, int count, FILE *stream);
int fputc(int value, FILE *stream);
int putc(int value, FILE *stream);
int putchar(int value);
int fputs(const char *text, FILE *stream);
int puts(const char *text);
int ungetc(int value, FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
void perror(const char *prefix);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int snprintf(char *buffer, size_t capacity, const char *format, ...);
int vsnprintf(char *buffer, size_t capacity, const char *format, va_list args);
int vfprintf(FILE *stream, const char *format, va_list args);
int sprintf(char *buffer, const char *format, ...);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
void setbuf(FILE *stream, char *buffer);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
