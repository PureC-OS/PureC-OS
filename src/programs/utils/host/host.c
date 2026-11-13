#include "../lib/common.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

int main(int argc, char **argv)
{
    util_init(argc, argv);
    if (argc != 2 || argv[1][0] == '-') {
        printf("usage: host NAME\n");
        return argc == 2 ? 0 : 1;
    }
    struct addrinfo hints, *list = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    int rc = getaddrinfo(argv[1], 0, &hints, &list);
    if (rc != 0) {
        printf("Host %s not found: %s\n", argv[1], gai_strerror(rc));
        return 1;
    }
    for (struct addrinfo *a = list; a; a = a->ai_next)
        printf("%s has address %s\n", argv[1],
               inet_ntoa(((struct sockaddr_in *)a->ai_addr)->sin_addr));
    freeaddrinfo(list);
    return 0;
}
