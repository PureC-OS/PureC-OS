#ifndef PUREC_HOSTED_ICONV_H
#define PUREC_HOSTED_ICONV_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef void *iconv_t;

iconv_t iconv_open(const char *to_code, const char *from_code);
size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);
int iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif
#endif
