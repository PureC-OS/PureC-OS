#pragma once
#define FNM_NOMATCH 1
#define FNM_PATHNAME 1
#define FNM_NOESCAPE 2
#define FNM_PERIOD 4
#define FNM_CASEFOLD 16
#ifdef __cplusplus
extern "C" {
#endif
int fnmatch(const char *pattern, const char *text, int flags);
#ifdef __cplusplus
}
#endif
