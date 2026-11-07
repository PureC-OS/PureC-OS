#pragma once
#include <netinet/in.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

in_addr_t inet_addr(const char *text);
int inet_aton(const char *text, struct in_addr *address);
char *inet_ntoa(struct in_addr address);
int inet_pton(int family, const char *text, void *destination);
const char *inet_ntop(int family, const void *source, char *destination,
                      socklen_t size);

#ifdef __cplusplus
}
#endif
