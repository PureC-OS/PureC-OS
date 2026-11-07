#pragma once
#include <stddef.h>
#include <stdint.h>

typedef unsigned int wint_t;
typedef struct { int state; } mbstate_t;
#define WEOF ((wint_t)-1)

#ifdef __cplusplus
extern "C" {
#endif

size_t wcslen(const wchar_t *text);
size_t mbrtowc(wchar_t *out, const char *text, size_t count, mbstate_t *state);
size_t wcrtomb(char *out, wchar_t value, mbstate_t *state);
size_t mbstowcs(wchar_t *out, const char *text, size_t count);
size_t wcstombs(char *out, const wchar_t *text, size_t count);
int mbsinit(const mbstate_t *state);

#ifdef __cplusplus
}
#endif
