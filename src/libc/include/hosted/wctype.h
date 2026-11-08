#pragma once
#include <wchar.h>

typedef unsigned long wctype_t;
typedef const int *wctrans_t;

#ifdef __cplusplus
extern "C" {
#endif

int iswalnum(wint_t value);
int iswalpha(wint_t value);
int iswblank(wint_t value);
int iswcntrl(wint_t value);
int iswdigit(wint_t value);
int iswgraph(wint_t value);
int iswlower(wint_t value);
int iswprint(wint_t value);
int iswpunct(wint_t value);
int iswspace(wint_t value);
int iswupper(wint_t value);
int iswxdigit(wint_t value);
wctype_t wctype(const char *name);
int iswctype(wint_t value, wctype_t type);
wint_t towlower(wint_t value);
wint_t towupper(wint_t value);
wctrans_t wctrans(const char *name);
wint_t towctrans(wint_t value, wctrans_t transform);

#ifdef __cplusplus
}
#endif
