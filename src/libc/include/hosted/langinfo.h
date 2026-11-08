#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef int nl_item;
#define CODESET 14
char *nl_langinfo(nl_item item);
#ifdef __cplusplus
}
#endif
