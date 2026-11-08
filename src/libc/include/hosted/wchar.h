#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

typedef unsigned int wint_t;
typedef struct {
    int state;
    unsigned int partial;
    unsigned int needed;
} mbstate_t;

typedef struct file_impl FILE;
struct tm;

#define WEOF ((wint_t)0xFFFFFFFFu)
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647

#ifdef __cplusplus
extern "C" {
#endif

size_t wcslen(const wchar_t *text);
size_t wcsnlen(const wchar_t *text, size_t limit);
wchar_t *wcscpy(wchar_t *destination, const wchar_t *source);
wchar_t *wcsncpy(wchar_t *destination, const wchar_t *source, size_t count);
wchar_t *wcscat(wchar_t *destination, const wchar_t *source);
wchar_t *wcsncat(wchar_t *destination, const wchar_t *source, size_t count);
int wcscmp(const wchar_t *left, const wchar_t *right);
int wcsncmp(const wchar_t *left, const wchar_t *right, size_t count);
int wcscoll(const wchar_t *left, const wchar_t *right);
size_t wcsxfrm(wchar_t *destination, const wchar_t *source, size_t count);
wchar_t *wcschr(const wchar_t *text, wchar_t value);
wchar_t *wcsrchr(const wchar_t *text, wchar_t value);
wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle);
wchar_t *wcspbrk(const wchar_t *text, const wchar_t *accept);
size_t wcsspn(const wchar_t *text, const wchar_t *accept);
size_t wcscspn(const wchar_t *text, const wchar_t *reject);
wchar_t *wcstok(wchar_t *text, const wchar_t *delimiters, wchar_t **state);
wchar_t *wcsdup(const wchar_t *text);
wchar_t *wmemcpy(wchar_t *destination, const wchar_t *source, size_t count);
wchar_t *wmemmove(wchar_t *destination, const wchar_t *source, size_t count);
wchar_t *wmemset(wchar_t *destination, wchar_t value, size_t count);
int wmemcmp(const wchar_t *left, const wchar_t *right, size_t count);
wchar_t *wmemchr(const wchar_t *text, wchar_t value, size_t count);

int mbsinit(const mbstate_t *state);
size_t mbrlen(const char *text, size_t count, mbstate_t *state);
size_t mbrtowc(wchar_t *out, const char *text, size_t count, mbstate_t *state);
size_t wcrtomb(char *out, wchar_t value, mbstate_t *state);
size_t mbsrtowcs(wchar_t *out, const char **text, size_t count, mbstate_t *state);
size_t mbsnrtowcs(wchar_t *out, const char **text, size_t limit, size_t count, mbstate_t *state);
size_t wcsrtombs(char *out, const wchar_t **text, size_t count, mbstate_t *state);
size_t wcsnrtombs(char *out, const wchar_t **text, size_t limit, size_t count, mbstate_t *state);
size_t mbstowcs(wchar_t *out, const char *text, size_t count);
size_t wcstombs(char *out, const wchar_t *text, size_t count);
wint_t btowc(int value);
int wctob(wint_t value);
int mblen(const char *text, size_t count);
int mbtowc(wchar_t *out, const char *text, size_t count);
int wctomb(char *out, wchar_t value);

long wcstol(const wchar_t *text, wchar_t **end, int base);
unsigned long wcstoul(const wchar_t *text, wchar_t **end, int base);
long long wcstoll(const wchar_t *text, wchar_t **end, int base);
unsigned long long wcstoull(const wchar_t *text, wchar_t **end, int base);
double wcstod(const wchar_t *text, wchar_t **end);
float wcstof(const wchar_t *text, wchar_t **end);
long double wcstold(const wchar_t *text, wchar_t **end);

int fwide(FILE *stream, int mode);
wint_t fgetwc(FILE *stream);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t fputwc(wchar_t value, FILE *stream);
wint_t putwc(wchar_t value, FILE *stream);
wint_t putwchar(wchar_t value);
wint_t ungetwc(wint_t value, FILE *stream);
wchar_t *fgetws(wchar_t *buffer, int count, FILE *stream);
int fputws(const wchar_t *text, FILE *stream);
int vswprintf(wchar_t *buffer, size_t count, const wchar_t *format, va_list arguments);
int swprintf(wchar_t *buffer, size_t count, const wchar_t *format, ...);
int vfwprintf(FILE *stream, const wchar_t *format, va_list arguments);
int fwprintf(FILE *stream, const wchar_t *format, ...);
int vwprintf(const wchar_t *format, va_list arguments);
int wprintf(const wchar_t *format, ...);
size_t wcsftime(wchar_t *buffer, size_t count, const wchar_t *format, const struct tm *time);
int wcwidth(wchar_t value);
int wcswidth(const wchar_t *text, size_t count);

#ifdef __cplusplus
}
#endif
